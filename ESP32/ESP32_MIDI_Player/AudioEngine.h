#pragma once
#include <Arduino.h>

// =======================
// AUDIO ENGINE API
// =======================

void Audio_init();

void Audio_noteOn(uint8_t note, uint8_t velocity);
void Audio_noteOff(uint8_t note);

// Stop all voices immediately
void Audio_allNotesOff();

// Metronome click
void Audio_triggerMetronome();
