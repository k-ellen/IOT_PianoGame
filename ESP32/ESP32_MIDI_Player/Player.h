#pragma once
#include <Arduino.h>

// Plays a song twice:
// 1) LEDs + synth audio
// 2) LEDs + metronome
// Then returns to FREE PLAY
void Player_playSong(const String &localPath);
