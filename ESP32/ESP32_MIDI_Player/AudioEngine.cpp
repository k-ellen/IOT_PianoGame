#include "AudioEngine.h"
#include "Config.h"
#include "PlayMode.h"

#include <driver/i2s.h>
#include <math.h>
#include <string.h>

// =======================
// GLOBAL STATE
// =======================

volatile PlayMode currentMode = MODE_FREE;
volatile bool stopRequested = false;

// =======================
// SYNTH
// =======================

struct Voice {
  bool active = false;
  float phase = 0.0f;
  float freq = 0.0f;
  float vel = 0.0f;
};

static Voice voices[MAX_VOICES];
static float masterVolume = 1.00f;
static volatile uint32_t metroSamples = 0;

// =======================
// HELPERS
// =======================

static float noteToFreq(uint8_t note) {
  return 440.0f * powf(2.0f, (note - 69) / 12.0f);
}

// =======================
// AUDIO TASK
// =======================

static void audioTask(void *) {
  int16_t buffer[256 * 2];

  while (true) {
    for (int i = 0; i < 256; i++) {
      float sample = 0.0f;

      // Metronome priority
      if (metroSamples > 0) {
        sample = (metroSamples & 1) ? 0.9f : -0.9f;
        metroSamples--;
      }
      else if (currentMode == MODE_SONG_AUDIO) {
        for (int v = 0; v < MAX_VOICES; v++) {
          if (!voices[v].active) continue;

          sample += sinf(voices[v].phase) * voices[v].vel;
          voices[v].phase +=
            2.0f * M_PI * voices[v].freq / SAMPLE_RATE;

          if (voices[v].phase > 2.0f * M_PI)
            voices[v].phase -= 2.0f * M_PI;
        }
      }

      sample *= masterVolume;
      sample = constrain(sample, -1.0f, 1.0f);

      int16_t s = (int16_t)(sample * 12000);
      buffer[i * 2]     = s;
      buffer[i * 2 + 1] = s;
    }

    size_t written;
    i2s_write(I2S_NUM_0, buffer, sizeof(buffer),
              &written, portMAX_DELAY);
  }
}

// =======================
// PUBLIC API
// =======================

void Audio_init() {
  // ---- I2S CONFIG ----
  i2s_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));

  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  cfg.fixed_mclk = 0;  // 🔴 Disable MCLK

  // ---- PIN CONFIG ----
  i2s_pin_config_t pins;
  memset(&pins, 0, sizeof(pins));

  pins.bck_io_num = I2S_BCK;
  pins.ws_io_num = I2S_WS;
  pins.data_out_num = I2S_DOUT;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  pins.mck_io_num = I2S_PIN_NO_CHANGE;  // 🔴 Disable MCLK

  // ---- START I2S ----
  i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
  i2s_set_pin(I2S_NUM_0, &pins);
  i2s_start(I2S_NUM_0);

  xTaskCreatePinnedToCore(
    audioTask,
    "audio",
    4096,
    nullptr,
    3,
    nullptr,
    1
  );
}

// =======================
// SYNTH CONTROL
// =======================

void Audio_noteOn(uint8_t note, uint8_t velocity) {
  for (int i = 0; i < MAX_VOICES; i++) {
    if (!voices[i].active) {
      voices[i].active = true;
      voices[i].freq = noteToFreq(note);
      voices[i].vel = (velocity / 127.0f) * 1.0f;
      voices[i].phase = 0;
      return;
    }
  }
}

void Audio_noteOff(uint8_t note) {
  float f = noteToFreq(note);
  for (int i = 0; i < MAX_VOICES; i++) {
    if (voices[i].active &&
        fabs(voices[i].freq - f) < 1.0f) {
      voices[i].active = false;
    }
  }
}

void Audio_allNotesOff() {
  for (int i = 0; i < MAX_VOICES; i++) {
    voices[i].active = false;
    voices[i].phase = 0;
  }
}

void Audio_triggerMetronome() {
  metroSamples = SAMPLE_RATE / 100; // ~10 ms click
}
