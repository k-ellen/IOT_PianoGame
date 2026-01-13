#include "SdLock.h"

static SemaphoreHandle_t g_sdMutex = nullptr;

void SdLock_init() {
  if (!g_sdMutex) {
    g_sdMutex = xSemaphoreCreateMutex();
  }
}

void SdLock_take() {
  if (!g_sdMutex) SdLock_init();
  xSemaphoreTake(g_sdMutex, portMAX_DELAY);
}

void SdLock_give() {
  if (!g_sdMutex) return;
  xSemaphoreGive(g_sdMutex);
}
