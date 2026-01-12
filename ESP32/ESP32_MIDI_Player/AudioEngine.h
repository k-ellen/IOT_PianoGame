#pragma once
#include <stdint.h>

void Audio_init();

void Audio_setMuted(bool muted);
void Audio_triggerMetronome();

void Audio_allNotesOff();
void Audio_noteOn(uint8_t note, uint8_t velocity);
void Audio_noteOff(uint8_t note);
