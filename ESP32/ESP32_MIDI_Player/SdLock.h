#pragma once
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Call once in setup()
void SdLock_init();

// Use these around ANY SD operation (open/read/seek/close/exists/mkdir/remove)
void SdLock_take();
void SdLock_give();

// Optional RAII helper
struct SdGuard {
  SdGuard() { SdLock_take(); }
  ~SdGuard() { SdLock_give(); }
};
