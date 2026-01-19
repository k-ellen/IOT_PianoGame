#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>

// ==========================================
// 1. PIN CONFIG (MATCH YOUR HARDWARE!)
// ==========================================
#define I2S_BCK  26  // BCLK / Bit Clock
#define I2S_WS   35  // LRC / Word Select / Left-Right Clock
#define I2S_DOUT 25  // DIN / Data Out

#define SAMPLE_RATE 48000

// ==========================================
// 2. AUDIO ENGINE (Simplified for Test)
// ==========================================

// Sine Table for fast synthesis
static int16_t sineTable[1024];
void initSine() {
  for (int i = 0; i < 1024; i++) {
    sineTable[i] = (int16_t)(sinf(i * 2.0f * PI / 1024.0f) * 32767.0f);
  }
}
inline float fastSine(uint32_t phase) {
  return (float)sineTable[(phase >> 22) & 1023] / 32768.0f;
}

// Global "Voice" State
struct Voice {
  bool active = false;
  uint32_t phase1 = 0;
  uint32_t inc1 = 0;
  uint32_t phase2 = 0;
  uint32_t inc2 = 0;
  
  float env = 0.0f;
  float decay = 0.0f;
  
  int noiseSamples = 0; // "Click" transient
};

volatile Voice voice;
portMUX_TYPE audioMux = portMUX_INITIALIZER_UNLOCKED;

// ==========================================
// 3. I2S TASK
// ==========================================
void audioTask(void *pv) {
  int16_t outBuf[256 * 2]; // Stereo buffer
  uint32_t noiseState = 12345;

  while (true) {
    for (int i = 0; i < 256; i++) {
      float sample = 0.0f;

      portENTER_CRITICAL(&audioMux);
      if (voice.active) {
        // A. Primary Tone (Woodblock Body)
        float osc = fastSine(voice.phase1);
        voice.phase1 += voice.inc1;

        // B. Secondary Tone (Hollow Ring - Detuned)
        float osc2 = fastSine(voice.phase2);
        voice.phase2 += voice.inc2;

        // C. Noise Burst (The "Click" Attack)
        float noise = 0.0f;
        if (voice.noiseSamples > 0) {
          noiseState = noiseState * 1664525 + 1013904223;
          noise = ((int32_t)noiseState / 2147483648.0f) * 0.5f; 
          voice.noiseSamples--;
        }

        // Mix: 70% Body + 30% Ring + Click
        float signal = (osc * 0.7f) + (osc2 * 0.3f) + noise;
        
        // Envelope
        sample = signal * voice.env;
        voice.env *= voice.decay; // Exponential fade

        if (voice.env < 0.001f) voice.active = false;
      }
      portEXIT_CRITICAL(&audioMux);

      // Master Volume
      sample *= 0.5f; 

      // Output to I2S (Stereo Copy)
      int16_t s16 = (int16_t)(sample * 16000.0f);
      outBuf[i*2] = s16;
      outBuf[i*2+1] = s16;
    }
    
    size_t w;
    i2s_write(I2S_NUM_0, outBuf, sizeof(outBuf), &w, portMAX_DELAY);
  }
}

// ==========================================
// 4. TRIGGER FUNCTION
// ==========================================
void triggerMetronome() {
  Serial.println("TOCK!");
  
  // High "Woodblock" Pitch (Approx 880Hz / A5)
  float hz = 880.0f; 
  
  portENTER_CRITICAL(&audioMux);
  voice.active = true;
  voice.env = 1.0f;
  voice.decay = 0.9992f; // Fast decay
  
  voice.phase1 = 0;
  voice.inc1 = (uint32_t)((hz * 4294967296.0f) / SAMPLE_RATE);
  
  voice.phase2 = 0;
  voice.inc2 = (uint32_t)((hz * 1.45f * 4294967296.0f) / SAMPLE_RATE); // Detuned harmonic
  
  voice.noiseSamples = 400; // ~8ms of click noise
  portEXIT_CRITICAL(&audioMux);
}

// ==========================================
// 5. MAIN SETUP & LOOP
// ==========================================
void setup() {
  Serial.begin(115200);
  initSine();

  // I2S Setup
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = 128,
    .use_apll = false
  };
  
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  i2s_zero_dma_buffer(I2S_NUM_0);

  // Start Audio Task
  xTaskCreatePinnedToCore(audioTask, "I2S", 4096, NULL, 5, NULL, 1);
}

void loop() {
  triggerMetronome();
  delay(500); // 120 BPM

}
