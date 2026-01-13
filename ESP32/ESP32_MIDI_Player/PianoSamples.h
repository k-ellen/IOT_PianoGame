#pragma once
#include <stdint.h>

// Velocity layers
enum PianoDyn : uint8_t {
  DYN1 = 1,
  DYN2 = 2,
  DYN3 = 3
};

struct PianoSample {
  uint8_t midiRoot;       // root MIDI note
  PianoDyn dyn;           // velocity layer
  const char* filename;  // WAV path on SD
};

// Public API
const PianoSample* PianoSamples_pick(uint8_t midiNote, uint8_t velocity);
bool PianoSamples_checkAllOnSD();
