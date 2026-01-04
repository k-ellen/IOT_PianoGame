#pragma once
#include <stdint.h>

// ===== PINS =====
#define SD_CS_PIN      5
#define NEO_PIN        14
#define MIDI_RX_PIN    15

#define I2S_BCK   26
#define I2S_DOUT  25
#define I2S_WS    33

// ===== AUDIO =====
#define SAMPLE_RATE 22050
#define MAX_VOICES 6

// ===== LED =====
#define NUMPIXELS 126
#define KEY_SHIFT 36
#define FIRST_KEY 36
#define LAST_KEY 96
// ===== LEARNING SEGMENTS =====
#define SEGMENT_CHORDS 10   // number of chord-steps per segment
#define MAX_SEGMENTS   256
