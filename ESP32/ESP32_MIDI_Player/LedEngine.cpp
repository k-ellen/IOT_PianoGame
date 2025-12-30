#include "LedEngine.h"
#include "Config.h"
#include <Adafruit_NeoPixel.h>

static Adafruit_NeoPixel pixels(NUMPIXELS, NEO_PIN, NEO_GRB + NEO_KHZ800);

static int leds[][3] = {
  {2,3,-1},{4,5,-1},{6,7,-1},{8,9,-1},{10,11,-1},{12,13,-1},
  {14,15,-1},{16,17,-1},{18,19,-1},{20,21,-1},{22,23,-1},{24,25,-1},
  {26,27,-1},{28,29,-1},{30,31,-1},{32,33,-1},{34,35,-1},{36,37,-1},
  {38,39,-1},{40,41,-1},{42,-1,-1},{43,44,45},{46,-1,-1},{47,48,-1},
  {49,50,-1},{51,52,-1},{53,54,-1},{55,56,-1},{57,58,-1},{59,60,-1},
  {61,62,-1},{63,64,-1},{65,66,-1},{67,68,-1},{69,70,-1},{71,-1,-1},
  {72,73,-1},{74,75,-1},{76,77,-1},{78,79,-1},{80,81,-1},{82,83,-1},
  {84,85,-1},{86,87,-1},{88,89,-1},{90,91,-1},{92,93,-1},{94,95,-1},
  {96,97,-1},{98,99,-1},{100,101,-1},{102,103,-1},{104,105,-1},
  {106,107,-1},{108,109,-1},{110,111,-1},{112,113,-1},{114,115,-1},
  {116,117,-1},{118,119,-1},{120,121,122},{123,124,125}
};

void Led_init() {
  pixels.begin();
  pixels.clear();
  pixels.setBrightness(128);
  pixels.show();
}

void Led_clear() {
  pixels.clear();
  pixels.show();
}

void Led_noteOn(uint8_t note, uint32_t color) {
  int i = (int)note - KEY_SHIFT;
  if (i < 0) return;

  for (int j = 0; j < 3; j++) {
    if (leds[i][j] != -1) {
      pixels.setPixelColor(leds[i][j], color);
    }
  }
  pixels.show();
}

void Led_noteOff(uint8_t note) {
  Led_noteOn(note, 0);
}

void setLedBuffer(int note, uint32_t color) {
  // Adjust 'note' if your strip index is offset (e.g., note - 21)
  int pixelIndex = note - KEY_SHIFT; 
  if (pixelIndex < 0 || pixelIndex >= (int)(sizeof(leds) / sizeof(leds[0]))) return;
  for (int i = 0; i < 3; i++) {
    int led = leds[pixelIndex][i];
    if (led != -1) pixels.setPixelColor(led, color);
  }
  pixels.show();
}

void Led_animateRainbow() {
  static uint16_t firstPixelHue = 0;
  static unsigned long lastFrame = 0;

  // 1. Limit Framerate (e.g., 20ms = 50 FPS) to save CPU
  if (millis() - lastFrame < 20) return;
  lastFrame = millis();

  // 2. Fill strip with rainbow
  for(int i=0; i<NUMPIXELS; i++) {
    // Hue varies slightly per pixel to create the wave
    int pixelHue = firstPixelHue + (i * 65536L / NUMPIXELS);
    // ColorHSV creates the rainbow color
    pixels.setPixelColor(i, pixels.gamma32(pixels.ColorHSV(pixelHue)));
  }

  // 3. Advance the rainbow for next time
  firstPixelHue += 256; 
  
  // 4. Mark dirty so Led_update() knows to draw it
  ledDirty = true; 
}

// ✅ NEW: immediate clear of entire strip
void Led_clearAll() {
  pixels.clear();
  pixels.show();
}