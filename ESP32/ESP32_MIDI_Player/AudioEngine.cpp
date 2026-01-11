#include "AudioEngine.h"
#include "Config.h"
#include "PlayMode.h"

#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>
#include <string.h>

// =======================
// GLOBAL STATE
// =======================

volatile PlayMode currentMode = MODE_FREE;
volatile bool stopRequested = false;

static volatile bool audioMuted = false;

// =======================
// SYNTH TUNABLES
// =======================

static constexpr int OUT_FRAMES = 256;

// voice count is from Config.h (MAX_VOICES)
static_assert(MAX_VOICES >= 1, "MAX_VOICES must be >= 1");

static float masterVolume = 1.5f;         // raise if needed (start safe)
static volatile uint32_t metroSamples = 0;

// Envelope times (milliseconds)
static constexpr float ATTACK_MS  = 2.0f;
static constexpr float DECAY_MS   = 80.0f;
static constexpr float TAIL_MS    = 1800.0f;   // natural piano fade
static constexpr float RELEASE_MS = 250.0f;

static constexpr float METRO_DECAY_MS = 180.0f;

// Levels
static constexpr float PEAK_LEVEL = 1.0f;
static constexpr float DECAY_LEVEL = 0.35f;    // after hammer
static constexpr float TAIL_LEVEL  = 0.08f;    // very soft sustain

static constexpr uint32_t ATTACK_S  = SAMPLE_RATE * ATTACK_MS  / 1000.0f;
static constexpr uint32_t DECAY_S   = SAMPLE_RATE * DECAY_MS   / 1000.0f;
static constexpr uint32_t TAIL_S    = SAMPLE_RATE * TAIL_MS    / 1000.0f;
static constexpr uint32_t RELEASE_S = SAMPLE_RATE * RELEASE_MS / 1000.0f;
static constexpr uint32_t METRO_DECAY_S = SAMPLE_RATE * METRO_DECAY_MS / 1000.0f;


// =======================
// FAST SINE: wavetable
// =======================

static constexpr int SINE_TABLE_BITS = 10;
static constexpr int SINE_TABLE_SIZE = (1 << SINE_TABLE_BITS); // 1024
static int16_t sineTable[SINE_TABLE_SIZE]; // int16 in RAM (2KB) — OK on ESP32
static bool sineInited = false;

static void initSineTable() {
  if (sineInited) return;
  for (int i = 0; i < SINE_TABLE_SIZE; i++) {
    float x = (2.0f * (float)M_PI * (float)i) / (float)SINE_TABLE_SIZE;
    sineTable[i] = (int16_t)lrintf(sinf(x) * 32767.0f);
  }
  sineInited = true;
}

// phase is uint32, index uses top bits
static inline float fastSine(uint32_t phase) {
  uint32_t idx = phase >> (32 - SINE_TABLE_BITS);
  return (float)sineTable[idx] / 32768.0f;
}

// =======================
// UTILS
// =======================

static inline float midiNoteToHz(uint8_t note) {
  // A4 = 69 -> 440Hz
  return 440.0f * powf(2.0f, ((int)note - 69) / 12.0f);
}

// simple deterministic noise (no rand() in audio loop)
static inline float lcgNoise(uint32_t &state) {
  state = state * 1664525u + 1013904223u;
  // [-1,1]
  return ((int32_t)(state >> 9) / 8388608.0f);
}

// =======================
// VOICE
// =======================

enum EnvStage : uint8_t {
  ENV_OFF = 0,
  ENV_ATTACK,
  ENV_DECAY,
  ENV_TAIL,
  ENV_RELEASE,
  ENV_METRO_DECAY
};


struct Voice {
  bool active = false;
  uint8_t note = 0;
  float velocity = 0.0f;     // 0..1

  uint32_t phase = 0;
  uint32_t phase2 = 0;
  uint32_t phase3 = 0;

  uint32_t inc = 0;
  uint32_t inc2 = 0;
  uint32_t inc3 = 0;

  // envelope
  EnvStage stage = ENV_OFF;
  uint32_t stagePos = 0;
  float env = 0.0f;          // 0..1
  float releaseStart = 0.0f;

  // “hammer noise” burst at note start
  uint32_t noiseState = 0;
  uint32_t hammerLeft = 0;   // samples remaining
};

static Voice voices[MAX_VOICES];
static portMUX_TYPE voicesMux = portMUX_INITIALIZER_UNLOCKED;

// =======================
// VOICE ALLOCATION
// =======================

// Find a free voice; if none, steal the quietest/releasing one.
static int allocVoice() {
  int freeIdx = -1;
  int stealIdx = 0;
  float stealScore = 9999.0f;

  portENTER_CRITICAL(&voicesMux);
  for (int i = 0; i < MAX_VOICES; i++) {
    if (!voices[i].active) {
      freeIdx = i;
      break;
    }
    // score: lower env means better to steal
    float score = voices[i].env;
    // prefer stealing releasing voices
    if (voices[i].stage == ENV_RELEASE) score *= 0.5f;
    if (score < stealScore) {
      stealScore = score;
      stealIdx = i;
    }
  }
  portEXIT_CRITICAL(&voicesMux);

  return (freeIdx >= 0) ? freeIdx : stealIdx;
}

// =======================
// ENVELOPE STEP
// =======================

static inline void envStep(Voice &v) {
  switch (v.stage) {

    case ENV_ATTACK:
      v.stagePos++;
      v.env = (float)v.stagePos / ATTACK_S;
      if (v.stagePos >= ATTACK_S) {
        v.env = PEAK_LEVEL;
        v.stage = ENV_DECAY;
        v.stagePos = 0;
      }
      break;

    case ENV_DECAY: {
      v.stagePos++;
      float t = (float)v.stagePos / DECAY_S;
      if (t >= 1.0f) {
        v.env = DECAY_LEVEL;
        v.stage = ENV_TAIL;
        v.stagePos = 0;
      } else {
        v.env = PEAK_LEVEL + (DECAY_LEVEL - PEAK_LEVEL) * t;
      }
    } break;

    case ENV_TAIL: {
      v.stagePos++;
      float t = (float)v.stagePos / TAIL_S;
      if (t >= 1.0f) {
        v.env = TAIL_LEVEL;
      } else {
        // exponential decay (key realism)
        v.env = DECAY_LEVEL * expf(-4.0f * t);
        if (v.env < TAIL_LEVEL) v.env = TAIL_LEVEL;
      }
    } break;

    case ENV_METRO_DECAY: {
      v.stagePos++;
      float t = (float)v.stagePos / METRO_DECAY_S;
      if (t >= 1.0f) { v.env = 0.0f; v.active = false; v.stage = ENV_OFF; }
      else { v.env = PEAK_LEVEL * (1.0f - t); }
    } break;

    case ENV_RELEASE: {
      v.stagePos++;
      float t = (float)v.stagePos / RELEASE_S;
      if (t >= 1.0f) {
        v.env = 0.0f;
        v.active = false;
        v.stage = ENV_OFF;
      } else {
        v.env = v.releaseStart * (1.0f - t);
      }
    } break;

    default:
      v.env = 0.0f;
      v.active = false;
      v.stage = ENV_OFF;
      break;
  }
}


// =======================
// AUDIO TASK
// =======================

static void audioTask(void*) {
  int16_t outBuf[OUT_FRAMES * 2];
  uint32_t globalNoise = 0x12345678u;

  while (true) {
    for (int i = 0; i < OUT_FRAMES; i++) {
      float mix = 0.0f;

      if (!audioMuted) {
          float polyGain = 0.50f;

          for (int v = 0; v < MAX_VOICES; v++) {
            Voice local;
            bool act;

            portENTER_CRITICAL(&voicesMux);
            act = voices[v].active;
            if (act) {
               envStep(voices[v]);
               local = voices[v]; // Copy
               voices[v].phase  += voices[v].inc;
               voices[v].phase2 += voices[v].inc2;
               voices[v].phase3 += voices[v].inc3;
               if (voices[v].hammerLeft > 0) voices[v].hammerLeft--;
            }
            portEXIT_CRITICAL(&voicesMux);

            if (!act || local.stage == ENV_OFF) continue;

            // --- OSCILLATORS ---
            float s1 = fastSine(local.phase);
            float s2 = fastSine(local.phase2);
            
            // Mix: 70% Fundamental + 30% Ring (Matched to standalone)
            float osc = (0.7f * s1) + (0.3f * s2); 
            
            // --- NOISE ---
            float noise = 0.0f;
            if (local.hammerLeft > 0) {
                 float n = lcgNoise(globalNoise);
                 // 0.5 volume for click
                 noise = n * 0.5f * (local.hammerLeft / (float)(SAMPLE_RATE * 0.006f));
            }

            mix += (osc + noise) * local.env * local.velocity * polyGain;
          }
      }

      mix *= masterVolume;
      if (mix > 1.2f) mix = 1.2f;
      if (mix < -1.2f) mix = -1.2f;
      mix = mix / (1.0f + fabsf(mix));

      int16_t s16 = (int16_t)(mix * 32760);
      outBuf[i * 2]     = s16;
      outBuf[i * 2 + 1] = s16;
    }

    size_t written;
    i2s_write(I2S_NUM_0, outBuf, sizeof(outBuf), &written, portMAX_DELAY);
  }
}

// =======================
// INIT
// =======================

void Audio_init() {
  initSineTable();

  i2s_config_t cfg{};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_I2S;

  // these are good, stable defaults
  cfg.dma_buf_len = 512;
  cfg.dma_buf_count = 10;
  cfg.tx_desc_auto_clear = true;

  i2s_pin_config_t pins{};
  pins.bck_io_num = I2S_BCK;
  pins.ws_io_num = I2S_WS;
  pins.data_out_num = I2S_DOUT;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  pins.mck_io_num = I2S_PIN_NO_CHANGE;

  i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
  i2s_set_pin(I2S_NUM_0, &pins);
  i2s_zero_dma_buffer(I2S_NUM_0);
  i2s_start(I2S_NUM_0);

  xTaskCreatePinnedToCore(audioTask, "audio", 8192, nullptr, 4, nullptr, 1);
}

// =======================
// CONTROL API
// =======================

void Audio_setMuted(bool muted) {
  audioMuted = muted;
  if (muted) Audio_allNotesOff();
}


void Audio_triggerMetronome() {
  audioMuted = false; // Optional: force unmute

  float hz = 880.0f; // High Woodblock Pitch
  float vel = 1.0f; 

  int slot = allocVoice();
  portENTER_CRITICAL(&voicesMux);
  
  voices[slot].active = true;
  voices[slot].note = 0; // Not used for metro
  voices[slot].velocity = vel;

  // Oscillators (Matched to test code)
  voices[slot].inc  = (uint32_t)((hz * 4294967296.0f) / (float)SAMPLE_RATE);
  voices[slot].inc2 = (uint32_t)((hz * 1.45f * 4294967296.0f) / (float)SAMPLE_RATE); // Detuned harmonic
  voices[slot].inc3 = 0;

  // Envelope & Noise
  voices[slot].stage = ENV_METRO_DECAY; // Special short decay
  voices[slot].stagePos = 0;
  voices[slot].env = 1.0f;
  
  // 400 samples noise (approx 8ms) for the "Click"
  voices[slot].hammerLeft = 400; 

  portEXIT_CRITICAL(&voicesMux);
  // Serial.println("🔊 TOCK!"); 
}
// void Audio_triggerMetronome() {
//   audioMuted = false; // Force sound

//   float hz = 880.0f; // High Woodblock Pitch
//   float vel = 1.0f; 

//   int slot = allocVoice();
//   portENTER_CRITICAL(&voicesMux);
  
//   voices[slot].active = true;
//   voices[slot].note = 0; 
//   voices[slot].velocity = vel;

//   // Exact ratio from your test code (1.45)
//   voices[slot].inc  = (uint32_t)((hz * 4294967296.0f) / (float)SAMPLE_RATE);
//   voices[slot].inc2 = (uint32_t)((hz * 1.45f * 4294967296.0f) / (float)SAMPLE_RATE); 
//   voices[slot].inc3 = 0;

//   // Envelope
//   voices[slot].stage = ENV_METRO_DECAY; 
//   voices[slot].stagePos = 0;
//   voices[slot].env = 1.0f;
  
//   // 400 samples noise (~8ms)
//   voices[slot].hammerLeft = 400; 

//   portEXIT_CRITICAL(&voicesMux);
//   Serial.println("🔊 TOCK!"); 
// }

void Audio_allNotesOff() {
  portENTER_CRITICAL(&voicesMux);
  for (int i = 0; i < MAX_VOICES; i++) {
    voices[i] = Voice{};
  }
  portEXIT_CRITICAL(&voicesMux);
  metroSamples = 0;
}

void Audio_noteOn(uint8_t note, uint8_t velocity) {
  if (currentMode == MODE_FREE || currentMode == MODE_LEARN) {
    return;
  }

  if (audioMuted) return;

  // velocity curve: make soft notes audible but keep loud notes strong
  float vel = (velocity / 127.0f);
  vel = powf(vel, 0.70f);
  vel = 0.20f + vel * 0.90f; // keep a floor

  float hz = midiNoteToHz(note);

  // phase increment: inc = hz * 2^32 / Fs
  uint32_t inc  = (uint32_t)((hz * 4294967296.0f) / (float)SAMPLE_RATE);
  uint32_t inc2 = (uint32_t)(((hz * 2.0f) * 4294967296.0f) / (float)SAMPLE_RATE);
  uint32_t inc3 = (uint32_t)(((hz * 3.0f) * 4294967296.0f) / (float)SAMPLE_RATE);

  int slot = allocVoice();

  portENTER_CRITICAL(&voicesMux);

  // If same note already active, retrigger that voice (better feel in free-play)
  for (int i = 0; i < MAX_VOICES; i++) {
    if (voices[i].active && voices[i].note == note) {
      slot = i;
      break;
    }
  }

  voices[slot].active = true;
  voices[slot].note = note;
  voices[slot].velocity = vel;

  voices[slot].inc = inc;
  voices[slot].inc2 = inc2;
  voices[slot].inc3 = inc3;

  // randomize starting phase so stacked notes don't phase-lock
  voices[slot].phase  = (uint32_t)esp_random();
  voices[slot].phase2 = (uint32_t)esp_random();
  voices[slot].phase3 = (uint32_t)esp_random();

  voices[slot].stage = ENV_ATTACK;
  voices[slot].stagePos = 0;
  voices[slot].env = 0.0f;

  // hammer noise ~6ms
  voices[slot].hammerLeft = (uint32_t)(SAMPLE_RATE * 0.006f);
  voices[slot].noiseState = (uint32_t)esp_random();

  portEXIT_CRITICAL(&voicesMux);
}

void Audio_noteOff(uint8_t note) {
  portENTER_CRITICAL(&voicesMux);
  for (int i = 0; i < MAX_VOICES; i++) {
    if (voices[i].active && voices[i].note == note) {
      // start release
      if (voices[i].stage != ENV_RELEASE && voices[i].stage != ENV_OFF) {
        voices[i].stage = ENV_RELEASE;
        voices[i].stagePos = 0;
        voices[i].releaseStart = voices[i].env;
      }
    }
  }
  portEXIT_CRITICAL(&voicesMux);
}
