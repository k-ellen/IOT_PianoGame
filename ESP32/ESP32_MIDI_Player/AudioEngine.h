#pragma once
#include <stdint.h>
#include "PlayMode.h"

void Audio_init();

void Audio_noteOn(uint8_t note, uint8_t velocity);
void Audio_noteOff(uint8_t note);
void Audio_allNotesOff();

void Audio_triggerMetronome();
void Audio_setMuted(bool muted);

// play specific file - for audio effects
void Audio_playEffect(const char* filename);

// shared globals
extern volatile PlayMode currentMode;
extern volatile bool stopRequested;
