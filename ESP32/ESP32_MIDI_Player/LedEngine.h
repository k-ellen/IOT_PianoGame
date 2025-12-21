#pragma once
#include <Arduino.h>

void Led_init();
void Led_noteOn(uint8_t note, uint32_t color);
void Led_noteOff(uint8_t note);
