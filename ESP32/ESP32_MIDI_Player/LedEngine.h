#pragma once
#include <Arduino.h>

void Led_init();
void Led_noteOn(uint8_t note, uint32_t color);
void Led_noteOff(uint8_t note);

void Led_clear();

void setLedBuffer(int note, uint32_t color);