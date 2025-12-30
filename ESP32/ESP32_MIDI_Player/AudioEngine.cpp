#include "AudioEngine.h"
#include "Config.h"
#include "PlayMode.h"
#include "PianoSamples.h"
#include "SdLock.h"

#include <Arduino.h>
#include <SD.h>
#include <driver/i2s.h>
#include <math.h>

// =======================
// GLOBAL STATE
// =======================

volatile PlayMode currentMode = MODE_FREE;
volatile bool stopRequested = false;
static volatile bool audioMuted = false;

static portMUX_TYPE voicesMux = portMUX_INITIALIZER_UNLOCKED;

// =======================
// VOICE
// =======================

struct Voice {
  bool active = false;
  File file;

  float playbackRate = 1.0f;   // pitch multiplier
  float phase = 0.0f;          // fractional phase accumulator

  uint8_t midiNote = 0;
  float velocity = 1.0f;

  int16_t buffer[256];
  int bufferSamples = 0;
  int bufferIndex = 0;
};

static Voice voices[MAX_VOICES];
static float masterVolume = 1.2f;   // 🔊 louder
static volatile uint32_t metroSamples = 0;

// =======================
// WAV PARSER (PCM 16-bit)
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
// AUDIO TASK (FIXED PITCH ENGINE)
// =======================

static void audioTask(void*) {
  int16_t outBuf[256 * 2];

  while (true) {
    for (int i = 0; i < 256; i++) {

      float mix = 0.0f;

      if (!audioMuted) {

        // metronome
        if (metroSamples > 0) {
          mix = (metroSamples & 1) ? 0.9f : -0.9f;
          metroSamples--;
        }

        // piano samples
        else if (currentMode == MODE_SONG_AUDIO) {

          for (int v = 0; v < MAX_VOICES; v++) {

            portENTER_CRITICAL(&voicesMux);
            bool active = voices[v].active;
            portEXIT_CRITICAL(&voicesMux);
            if (!active) continue;

            // refill buffer if needed
            portENTER_CRITICAL(&voicesMux);
            bool needFill = (voices[v].bufferIndex + 2 >= voices[v].bufferSamples);
            portEXIT_CRITICAL(&voicesMux);

            if (needFill) {
              int bytes;
              SdLock_take();
              bytes = voices[v].file.read(
                (uint8_t*)voices[v].buffer,
                sizeof(voices[v].buffer)
              );
              SdLock_give();

              portENTER_CRITICAL(&voicesMux);
              voices[v].bufferSamples = bytes / 2;
              voices[v].bufferIndex = 0;
              portEXIT_CRITICAL(&voicesMux);

              if (bytes <= 0) {
                SdLock_take();
                voices[v].file.close();
                SdLock_give();

                portENTER_CRITICAL(&voicesMux);
                voices[v].active = false;
                portEXIT_CRITICAL(&voicesMux);
                continue;
              }
            }

            // ---- FIXED PITCH + INTERPOLATION ----
            portENTER_CRITICAL(&voicesMux);

            int i0 = voices[v].bufferIndex;
            int i1 = i0 + 1;
            if (i1 >= voices[v].bufferSamples) i1 = i0;

            float frac = voices[v].phase;
            float s0 = voices[v].buffer[i0];
            float s1 = voices[v].buffer[i1];

            float sample = s0 + (s1 - s0) * frac;

            voices[v].phase += voices[v].playbackRate;
            int advance = (int)voices[v].phase;
            voices[v].phase -= advance;
            voices[v].bufferIndex += advance;

            float vel = voices[v].velocity;
            portEXIT_CRITICAL(&voicesMux);

            mix += (sample / 32768.0f) * vel;
          }
        }
      }

      mix *= masterVolume;
      if (mix > 1.0f) mix = 1.0f;
      if (mix < -1.0f) mix = -1.0f;

      int16_t s16 = (int16_t)(mix * 28000); // 🔊 louder DAC drive
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
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 256;
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

  xTaskCreatePinnedToCore(audioTask, "audio", 8192, nullptr, 3, nullptr, 1);
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
  portENTER_CRITICAL(&voicesMux);
  for (int i = 0; i < MAX_VOICES; i++) {
    bool wasActive = voices[i].active;
    voices[i].active = false;
    voices[i].bufferIndex = 0;
    voices[i].bufferSamples = 0;
    voices[i].phase = 0.0f;
    portEXIT_CRITICAL(&voicesMux);

    if (wasActive) {
      SdLock_take();
      voices[i].file.close();
      SdLock_give();
    }

    portENTER_CRITICAL(&voicesMux);
  }
  portEXIT_CRITICAL(&voicesMux);

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

  SdLock_take();
  File f = SD.open(smp->filename, FILE_READ);
  SdLock_give();
  if (!f) return;

  uint32_t dataStart;
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

  portENTER_CRITICAL(&voicesMux);
  voices[slot].file = f;
  voices[slot].active = true;
  voices[slot].midiNote = note;
  float v = velocity / 127.0f;
  voices[slot].velocity = powf(v, 0.7f) * 1.4f;
  voices[slot].bufferIndex = 0;
  voices[slot].bufferSamples = 0;
  voices[slot].phase = 0.0f;

  int semitone = (int)note - (int)smp->midiRoot;
  voices[slot].playbackRate = semitoneRate(semitone);
  portEXIT_CRITICAL(&voicesMux);
}

void Audio_noteOff(uint8_t note) {
  for (int i = 0; i < MAX_VOICES; i++) {
    portENTER_CRITICAL(&voicesMux);
    bool match = (voices[i].active && voices[i].midiNote == note);
    if (match) voices[i].active = false;
    portEXIT_CRITICAL(&voicesMux);

    if (match) {
      SdLock_take();
      voices[i].file.close();
      SdLock_give();
    }
  }
}
