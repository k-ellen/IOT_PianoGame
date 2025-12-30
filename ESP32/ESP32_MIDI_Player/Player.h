#pragma once
#include <Arduino.h>

// Plays a song twice:
// 1) LEDs + synth audio
// 2) LEDs + metronome
// Then returns to FREE PLAY
void Player_playSong(const String &localPath);

// Called by main MIDI callback when in MODE_LEARN
void Player_onNoteOn(uint8_t note);
void Player_onNoteOff(uint8_t note);

void checkMidi();