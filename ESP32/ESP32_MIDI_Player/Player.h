#pragma once
#include <Arduino.h>

// Global flag to control metronome
extern bool g_metronomeEnabled; 

// Call this from your main.ino when Firebase updates the setting
void Player_setMetronome(bool enabled);

// Plays a song twice:
// 1) LEDs + synth audio
// 2) LEDs + metronome
// Then returns to FREE PLAY
void Player_playSong(const String &localPath);

// Called by main MIDI callback when in MODE_LEARN
void Player_onNoteOn(uint8_t note);
void Player_onNoteOff(uint8_t note);

void checkMidi();