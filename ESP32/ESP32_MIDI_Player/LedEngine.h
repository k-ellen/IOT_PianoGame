#pragma once
#include <stdint.h>

void Led_init();
void Led_noteOn(uint8_t note, uint32_t color);
void Led_noteOff(uint8_t note);

void Led_clear();

void setLedBuffer(int note, uint32_t color);

void Led_animateStartup();

// turn off all LEDs immediately
void Led_clearAll();
