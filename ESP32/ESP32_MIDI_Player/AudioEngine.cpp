#include "AudioEngine.h"
#include "Config.h"
#include "PlayMode.h"
#include "PianoSamples.h"
#include "SdLock.h"

#include <Arduino.h>
#include <SD.h>
#include <driver/i2s.h>
#include <math.h>
#include <string.h>

// =======================
// GLOBAL STATE
// =======================

volatile PlayMode currentMode = MODE_FREE;
volatile bool stopRequested = false;
static volatile bool audioMuted = false;

static portMUX_TYPE voicesMux = portMUX_INITIALIZER_UNLOCKED;

// =======================
// TUNABLES
// =======================

static constexpr int OUT_FRAMES = 256;          // frames per i2s_write
static constexpr int VOICE_BUFS = 2;            // ping/pong
static constexpr int BUF_SAMPLES = 1024;        // per buffer, mono int16 samples

static float masterVolume = 2.2f;               // overall gain
static volatile uint32_t metroSamples = 0;

// =======================
// WAV PARSER (PCM 16-bit mono expected)
// =======================

static bool wavSeekToData(File& f, uint32_t& dataStart) {
  f.seek(0);

  char riff[4], wave[4];
  if (f.read((uint8_t*)riff, 4) != 4) return false;
  f.seek(8);
  if (f.read((uint8_t*)wave, 4) != 4) return false;

  if (memcmp(riff, "RIFF", 4) != 0) return false;
  if (memcmp(wave, "WAVE", 4) != 0) return false;

  while (f.available()) {
    char id[4];
    uint32_t size;
    if (f.read((uint8_t*)id, 4) != 4) return false;
    if (f.read((uint8_t*)&size, 4) != 4) return false;

    if (memcmp(id, "data", 4) == 0) {
      dataStart = f.position();
      return true;
    }
    f.seek(f.position() + size);
  }
  return false;
}

static inline float semitoneRate(int semitone) {
  return powf(2.0f, (float)semitone / 12.0f);
}

// =======================
// VOICE (double-buffered, SD-loaded in background task)
// =======================

struct Voice {
  bool active = false;
  bool eof = false;

  File file;

  uint8_t midiNote = 0;
  float velocity = 1.0f;

  float playbackRate = 1.0f;
  float phase = 0.0f;           // fractional phase

  // ping/pong buffers
  int16_t buf[VOICE_BUFS][BUF_SAMPLES];
  int bufSamples[VOICE_BUFS] = {0, 0};   // valid samples in each buffer
  bool bufReady[VOICE_BUFS] = {false, false};
  bool bufLoading[VOICE_BUFS] = {false, false};

  uint8_t curBuf = 0;           // which buffer we are reading from
  int bufIndex = 0;             // integer index into curBuf
};

static Voice voices[MAX_VOICES];

// =======================
// BACKGROUND LOADER TASK
// =======================

static void loaderFillBuffer(int v, int b) {
  // Mark loading (short critical)
  portENTER_CRITICAL(&voicesMux);
  if (!voices[v].active || voices[v].eof || voices[v].bufReady[b] || voices[v].bufLoading[b]) {
    portEXIT_CRITICAL(&voicesMux);
    return;
  }
  voices[v].bufLoading[b] = true;
  portEXIT_CRITICAL(&voicesMux);

  // Do SD read (can block) - NOT in critical, but under SdLock.
  int bytes = 0;
  SdLock_take();
  if (voices[v].file) {
    bytes = voices[v].file.read((uint8_t*)voices[v].buf[b], sizeof(voices[v].buf[b]));
  }
  SdLock_give();

  // Commit results
  portENTER_CRITICAL(&voicesMux);
  voices[v].bufLoading[b] = false;

  if (!voices[v].active) {
    // voice got turned off while we were reading
    voices[v].bufReady[b] = false;
    voices[v].bufSamples[b] = 0;
    portEXIT_CRITICAL(&voicesMux);
    return;
  }

  if (bytes <= 0) {
    voices[v].eof = true;
    voices[v].bufReady[b] = false;
    voices[v].bufSamples[b] = 0;
    portEXIT_CRITICAL(&voicesMux);
    return;
  }

  voices[v].bufSamples[b] = bytes / 2;
  voices[v].bufReady[b] = true;
  portEXIT_CRITICAL(&voicesMux);
}

static void loaderTask(void*) {
  while (true) {
    // Scan voices and fill any missing buffer
    for (int v = 0; v < MAX_VOICES; v++) {
      bool act, eof;
      uint8_t cur;
      bool r0, r1;

      portENTER_CRITICAL(&voicesMux);
      act = voices[v].active;
      eof = voices[v].eof;
      cur = voices[v].curBuf;
      r0 = voices[v].bufReady[0];
      r1 = voices[v].bufReady[1];
      portEXIT_CRITICAL(&voicesMux);

      if (!act || eof) continue;

      // Prefer filling the "other" buffer first (so swap is seamless)
      int other = (cur ^ 1);
      if (!(other ? r1 : r0)) loaderFillBuffer(v, other);
      // Then fill current if somehow empty
      if (!((cur == 0) ? r0 : r1)) loaderFillBuffer(v, cur);
    }

    // Small sleep - loader doesn't need to spin hard
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

// =======================
// AUDIO TASK (REAL-TIME SAFE: NO SD ACCESS)
// =======================

static void audioTask(void*) {
  int16_t outBuf[OUT_FRAMES * 2];

  while (true) {
    for (int i = 0; i < OUT_FRAMES; i++) {
      float mix = 0.0f;

      if (!audioMuted) {
        // metronome takes priority
        if (metroSamples > 0) {
          mix = (metroSamples & 1) ? 0.9f : -0.9f;
          metroSamples--;
        } else if (currentMode == MODE_SONG_AUDIO) {

          // Optional: simple auto-gain vs polyphony
          int activeCount = 0;
          for (int v = 0; v < MAX_VOICES; v++) {
            portENTER_CRITICAL(&voicesMux);
            bool a = voices[v].active;
            portEXIT_CRITICAL(&voicesMux);
            if (a) activeCount++;
          }
          // float polyGain = (activeCount <= 1) ? 1.0f : (1.0f / (0.7f * activeCount));
          float polyGain = 0.6f;   // fixed, cheap, stable


          for (int v = 0; v < MAX_VOICES; v++) {

            // Snapshot minimal state
            portENTER_CRITICAL(&voicesMux);
            bool active = voices[v].active;
            uint8_t curBuf = voices[v].curBuf;
            bool ready = voices[v].bufReady[curBuf];
            int nSamp = voices[v].bufSamples[curBuf];
            int idx = voices[v].bufIndex;
            float phase = voices[v].phase;
            float rate = voices[v].playbackRate;
            float vel = voices[v].velocity;
            portEXIT_CRITICAL(&voicesMux);

            if (!active || !ready || nSamp <= 1) continue;

            // Bound check (if we ran out, mark buffer consumed and try swap)
            if (idx >= (nSamp - 1)) {
              portENTER_CRITICAL(&voicesMux);
              // consume current buffer
              voices[v].bufReady[curBuf] = false;
              voices[v].bufSamples[curBuf] = 0;
              voices[v].bufIndex = 0;
              voices[v].phase = 0.0f;

              // swap if other ready
              uint8_t other = curBuf ^ 1;
              if (voices[v].bufReady[other] && voices[v].bufSamples[other] > 1) {
                voices[v].curBuf = other;
              } else {
                // no data ready yet -> silence this voice until loader fills (don’t kill it)
                // If you prefer, you can stop it here, but silence is smoother.
              }
              portEXIT_CRITICAL(&voicesMux);
              continue;
            }

            // Fetch samples (linear interpolation)
            int i0 = idx;
            int i1 = i0 + 1;
            if (i1 >= nSamp) i1 = i0;

            // We must read the buffer contents WITHOUT critical; but buffer memory is stable
            // while bufReady[curBuf]==true. We only set bufReady false when consumed.
            float s0 = (float)voices[v].buf[curBuf][i0];
            float s1 = (float)voices[v].buf[curBuf][i1];
            float sample = s0 + (s1 - s0) * phase;

            // Advance phase/index
            phase += rate;
            int advance = (int)phase;
            phase -= advance;
            idx += advance;

            // Commit updated indices (short critical)
            portENTER_CRITICAL(&voicesMux);
            if (voices[v].active && voices[v].curBuf == curBuf) {
              voices[v].phase = phase;
              voices[v].bufIndex = idx;
            }
            portEXIT_CRITICAL(&voicesMux);

            // Mix (scale to [-1..1] domain)
            mix += ((sample * 0.000045f) * vel) * polyGain;
          }
        }
      }

      // master gain + soft saturation
      mix *= masterVolume;

      if (mix > 1.2f)  mix = 1.2f;
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
  i2s_config_t cfg{};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_I2S;
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

  // Audio must be higher priority than loader
  xTaskCreatePinnedToCore(audioTask, "audio", 8192, nullptr, 4, nullptr, 1);
  xTaskCreatePinnedToCore(loaderTask, "wav_loader", 4096, nullptr, 2, nullptr, 0);
}

// =======================
// CONTROL API
// =======================

void Audio_setMuted(bool muted) {
  audioMuted = muted;
  if (muted) Audio_allNotesOff();
}

void Audio_triggerMetronome() {
  if (!audioMuted) metroSamples = SAMPLE_RATE / 120;
}

void Audio_allNotesOff() {
  // Turn off voices first
  for (int i = 0; i < MAX_VOICES; i++) {
    bool closeMe = false;

    portENTER_CRITICAL(&voicesMux);
    closeMe = voices[i].active;
    voices[i].active = false;
    voices[i].eof = false;
    voices[i].bufReady[0] = voices[i].bufReady[1] = false;
    voices[i].bufLoading[0] = voices[i].bufLoading[1] = false;
    voices[i].bufSamples[0] = voices[i].bufSamples[1] = 0;
    voices[i].bufIndex = 0;
    voices[i].curBuf = 0;
    voices[i].phase = 0.0f;
    portEXIT_CRITICAL(&voicesMux);

    if (closeMe) {
      SdLock_take();
      if (voices[i].file) voices[i].file.close();
      SdLock_give();
    }
  }

  metroSamples = 0;
}

void Audio_noteOn(uint8_t note, uint8_t velocity) {
  if (audioMuted) return;

  const PianoSample* smp = PianoSamples_pick(note, velocity);
  if (!smp) return;

  int slot = -1;
  portENTER_CRITICAL(&voicesMux);
  for (int i = 0; i < MAX_VOICES; i++) {
    if (!voices[i].active) { slot = i; break; }
  }
  portEXIT_CRITICAL(&voicesMux);
  if (slot < 0) return;

  // Open file + seek data (SD can block, do it outside critical)
  SdLock_take();
  File f = SD.open(smp->filename, FILE_READ);
  SdLock_give();
  if (!f) return;

  uint32_t dataStart = 0;
  SdLock_take();
  bool ok = wavSeekToData(f, dataStart);
  if (ok) f.seek(dataStart);
  SdLock_give();

  if (!ok) {
    SdLock_take();
    f.close();
    SdLock_give();
    return;
  }

  // Initialize voice state
  float v = velocity / 127.0f;
  float vel = powf(v, 0.7f) * 1.4f;
  int semitone = (int)note - (int)smp->midiRoot;
  float rate = semitoneRate(semitone);

  portENTER_CRITICAL(&voicesMux);
  voices[slot] = Voice{};           // reset
  voices[slot].file = f;
  voices[slot].active = true;
  voices[slot].eof = false;
  voices[slot].midiNote = note;
  voices[slot].velocity = vel;
  voices[slot].playbackRate = rate;
  voices[slot].phase = 0.0f;
  voices[slot].curBuf = 0;
  voices[slot].bufIndex = 0;
  portEXIT_CRITICAL(&voicesMux);
  // Prefill BOTH buffers synchronously so chords start cleanly
  loaderFillBuffer(slot, 0);
  loaderFillBuffer(slot, 1);
}

void Audio_noteOff(uint8_t note) {
  for (int i = 0; i < MAX_VOICES; i++) {
    bool match = false;

    portENTER_CRITICAL(&voicesMux);
    match = (voices[i].active && voices[i].midiNote == note);
    if (match) voices[i].active = false;
    portEXIT_CRITICAL(&voicesMux);

    if (match) {
      SdLock_take();
      if (voices[i].file) voices[i].file.close();
      SdLock_give();
    }
  }
}

void Audio_playEffect(const char* filename) {
  // if (audioMuted) return;
  audioMuted = false;
  Serial.printf("🔊 Attempting to play effect: %s\n", filename);

  // 1. Find a free voice slot OR steal one
  int slot = -1;
  
  portENTER_CRITICAL(&voicesMux);
  // Try to find an empty slot first
  for (int i = 0; i < MAX_VOICES; i++) {
    if (!voices[i].active) { 
      slot = i; 
      break; 
    }
  }
  
  // If full, STEAL slot 0 (Priority override!)
  if (slot < 0) {
     slot = 0;
     voices[0].active = false; // Kill the previous sound
  }
  portEXIT_CRITICAL(&voicesMux);

  // 2. Safely close any previous file in this slot (Critical for stealing!)
  SdLock_take();
  if (voices[slot].file) {
      voices[slot].file.close();
  }
  
  // 3. Open the new file
  File f = SD.open(filename, FILE_READ);
  SdLock_give();
  
  if (!f) {
    Serial.printf("❌ Error: File not found on SD: %s\n", filename);
    return;
  }

  // 4. Find WAV data chunk
  uint32_t dataStart = 0;
  SdLock_take();
  bool ok = wavSeekToData(f, dataStart);
  if (ok) f.seek(dataStart);
  SdLock_give();

  if (!ok) {
    Serial.printf("❌ Error: Invalid WAV format: %s\n", filename);
    SdLock_take(); 
    f.close(); 
    SdLock_give();
    return;
  }

  // 5. Configure the voice
  portENTER_CRITICAL(&voicesMux);
  voices[slot] = Voice{}; 
  voices[slot].file = f;
  voices[slot].active = true;
  voices[slot].eof = false;
  voices[slot].midiNote = 0;    
  voices[slot].velocity = 10.0f; // BOOST VOLUME (2.0x) for feedback
  voices[slot].playbackRate = 1.0f; 
  voices[slot].phase = 0.0f;
  voices[slot].curBuf = 0;
  voices[slot].bufIndex = 0;
  portEXIT_CRITICAL(&voicesMux);

  // 6. Pre-load buffers immediately
  loaderFillBuffer(slot, 0);
  loaderFillBuffer(slot, 1);
  
  Serial.printf("✅ Effect playing in voice slot %d\n", slot);
}
