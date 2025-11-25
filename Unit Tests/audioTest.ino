#include <Arduino.h>
#include "driver/i2s.h"

#define I2S_SAMPLE_RATE   44100          // קצב דגימה 44.1kHz
#define I2S_PORT          I2S_NUM_0

// פינים ל-I2S
#define I2S_BCK_PIN       26            // מחובר ל-BCK במודול
#define I2S_LRCK_PIN      25            // מחובר ל-LRCK / LCK
#define I2S_DATA_PIN      22            // מחובר ל-DIN במודול

// תדר הצליל (440Hz = לה)
#define TONE_FREQ         440.0

void setupI2S()
{
  i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = I2S_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,   // סטריאו
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCK_PIN,
    .ws_io_num = I2S_LRCK_PIN,
    .data_out_num = I2S_DATA_PIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  // התקנת הדרייבר והגדרת הפינים
  i2s_driver_install(I2S_PORT, &config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);
  i2s_zero_dma_buffer(I2S_PORT);
}

void setup() {
  Serial.begin(115200);
  Serial.println("I2S + PCM5102A tone test");

  setupI2S();
}

void loop() {
  // נייצר סינוס "און־ליין"
  static double phase = 0.0;
  const double phaseIncrement = 2.0 * PI * TONE_FREQ / I2S_SAMPLE_RATE;

  // נשלח בלוק קטן כל פעם
  const int samplesCount = 256;
  uint32_t buffer[samplesCount];  // כל אלמנט = 2×16bit (ימין+שמאל)

  for (int i = 0; i < samplesCount; i++) {
    // ערך סינוס -1..1
    float s = sin(phase);
    phase += phaseIncrement;
    if (phase >= 2.0 * PI) {
      phase -= 2.0 * PI;
    }

    // המרה ל-16bit signed
    int16_t sample = (int16_t)(s * 32767);

    // אותו ערוץ גם לימין וגם לשמאל
    uint32_t stereoSample = ((uint16_t)sample << 16) | ((uint16_t)sample);

    buffer[i] = stereoSample;
  }

  size_t bytesWritten;
  // כתיבה ל-I2S (חסימתי – מחכה עד שנשלח)
  i2s_write(I2S_PORT, buffer, sizeof(buffer), &bytesWritten, portMAX_DELAY);
}