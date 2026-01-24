#include "PianoSamples.h"
#include <Arduino.h>
#include <SD.h>

// Your roots list (skipping odds as you described)
static const uint8_t ROOTS[] = {
  21,25,29,33,37,41,45,49,53,57,61,65,69,73,77,81,85,89,93,97,101,105,108
};
static const int NUM_ROOTS = sizeof(ROOTS)/sizeof(ROOTS[0]);

// Helper: format filename by index (000,002,...044)
static void buildName(char* out, size_t outLen, PianoDyn dyn, int fileIndex /*0..44 step2*/) {
  // Example: /piano/Player_dyn2_rr1_042.wav
  snprintf(out, outLen, "/piano/Player_dyn%d_rr1_%03d.wav", (int)dyn, fileIndex);
}

// Build the table at compile time-ish (Arduino doesn’t love heavy dynamic init),
// so we just write it explicitly in code generation style:
const PianoSample PIANO_SAMPLES[] = {
  // dyn1
  {21,  DYN1, "/piano/esp_Player_dyn1_rr1_000.wav"},
  {25,  DYN1, "/piano/esp_Player_dyn1_rr1_002.wav"},
  {29,  DYN1, "/piano/esp_Player_dyn1_rr1_004.wav"},
  {33,  DYN1, "/piano/esp_Player_dyn1_rr1_006.wav"},
  {37,  DYN1, "/piano/esp_Player_dyn1_rr1_008.wav"},
  {41,  DYN1, "/piano/esp_Player_dyn1_rr1_010.wav"},
  {45,  DYN1, "/piano/esp_Player_dyn1_rr1_012.wav"},
  {49,  DYN1, "/piano/esp_Player_dyn1_rr1_014.wav"},
  {53,  DYN1, "/piano/esp_Player_dyn1_rr1_016.wav"},
  {57,  DYN1, "/piano/esp_Player_dyn1_rr1_018.wav"},
  {61,  DYN1, "/piano/esp_Player_dyn1_rr1_020.wav"},
  {65,  DYN1, "/piano/esp_Player_dyn1_rr1_022.wav"},
  {69,  DYN1, "/piano/esp_Player_dyn1_rr1_024.wav"},
  {73,  DYN1, "/piano/esp_Player_dyn1_rr1_026.wav"},
  {77,  DYN1, "/piano/esp_Player_dyn1_rr1_028.wav"},
  {81,  DYN1, "/piano/esp_Player_dyn1_rr1_030.wav"},
  {85,  DYN1, "/piano/esp_Player_dyn1_rr1_032.wav"},
  {89,  DYN1, "/piano/esp_Player_dyn1_rr1_034.wav"},
  {93,  DYN1, "/piano/esp_Player_dyn1_rr1_036.wav"},
  {97,  DYN1, "/piano/esp_Player_dyn1_rr1_038.wav"},
  {101, DYN1, "/piano/esp_Player_dyn1_rr1_040.wav"},
  {105, DYN1, "/piano/esp_Player_dyn1_rr1_042.wav"},
  {108, DYN1, "/piano/esp_Player_dyn1_rr1_044.wav"},

  // dyn2
  {21,  DYN2, "/piano/esp_Player_dyn2_rr1_000.wav"},
  {25,  DYN2, "/piano/esp_Player_dyn2_rr1_002.wav"},
  {29,  DYN2, "/piano/esp_Player_dyn2_rr1_004.wav"},
  {33,  DYN2, "/piano/esp_Player_dyn2_rr1_006.wav"},
  {37,  DYN2, "/piano/esp_Player_dyn2_rr1_008.wav"},
  {41,  DYN2, "/piano/esp_Player_dyn2_rr1_010.wav"},
  {45,  DYN2, "/piano/esp_Player_dyn2_rr1_012.wav"},
  {49,  DYN2, "/piano/esp_Player_dyn2_rr1_014.wav"},
  {53,  DYN2, "/piano/esp_Player_dyn2_rr1_016.wav"},
  {57,  DYN2, "/piano/esp_Player_dyn2_rr1_018.wav"},
  {61,  DYN2, "/piano/esp_Player_dyn2_rr1_020.wav"},
  {65,  DYN2, "/piano/esp_Player_dyn2_rr1_022.wav"},
  {69,  DYN2, "/piano/esp_Player_dyn2_rr1_024.wav"},
  {73,  DYN2, "/piano/esp_Player_dyn2_rr1_026.wav"},
  {77,  DYN2, "/piano/esp_Player_dyn2_rr1_028.wav"},
  {81,  DYN2, "/piano/esp_Player_dyn2_rr1_030.wav"},
  {85,  DYN2, "/piano/esp_Player_dyn2_rr1_032.wav"},
  {89,  DYN2, "/piano/esp_Player_dyn2_rr1_034.wav"},
  {93,  DYN2, "/piano/esp_Player_dyn2_rr1_036.wav"},
  {97,  DYN2, "/piano/esp_Player_dyn2_rr1_038.wav"},
  {101, DYN2, "/piano/esp_Player_dyn2_rr1_040.wav"},
  {105, DYN2, "/piano/esp_Player_dyn2_rr1_042.wav"},
  {108, DYN2, "/piano/esp_Player_dyn2_rr1_044.wav"},

  // dyn3
  {21,  DYN3, "/piano/esp_Player_dyn3_rr1_000.wav"},
  {25,  DYN3, "/piano/esp_Player_dyn3_rr1_002.wav"},
  {29,  DYN3, "/piano/esp_Player_dyn3_rr1_004.wav"},
  {33,  DYN3, "/piano/esp_Player_dyn3_rr1_006.wav"},
  {37,  DYN3, "/piano/esp_Player_dyn3_rr1_008.wav"},
  {41,  DYN3, "/piano/esp_Player_dyn3_rr1_010.wav"},
  {45,  DYN3, "/piano/esp_Player_dyn3_rr1_012.wav"},
  {49,  DYN3, "/piano/esp_Player_dyn3_rr1_014.wav"},
  {53,  DYN3, "/piano/esp_Player_dyn3_rr1_016.wav"},
  {57,  DYN3, "/piano/esp_Player_dyn3_rr1_018.wav"},
  {61,  DYN3, "/piano/esp_Player_dyn3_rr1_020.wav"},
  {65,  DYN3, "/piano/esp_Player_dyn3_rr1_022.wav"},
  {69,  DYN3, "/piano/esp_Player_dyn3_rr1_024.wav"},
  {73,  DYN3, "/piano/esp_Player_dyn3_rr1_026.wav"},
  {77,  DYN3, "/piano/esp_Player_dyn3_rr1_028.wav"},
  {81,  DYN3, "/piano/esp_Player_dyn3_rr1_030.wav"},
  {85,  DYN3, "/piano/esp_Player_dyn3_rr1_032.wav"},
  {89,  DYN3, "/piano/esp_Player_dyn3_rr1_034.wav"},
  {93,  DYN3, "/piano/esp_Player_dyn3_rr1_036.wav"},
  {97,  DYN3, "/piano/esp_Player_dyn3_rr1_038.wav"},
  {101, DYN3, "/piano/esp_Player_dyn3_rr1_040.wav"},
  {105, DYN3, "/piano/esp_Player_dyn3_rr1_042.wav"},
  {108, DYN3, "/piano/esp_Player_dyn3_rr1_044.wav"},
};

const int NUM_PIANO_SAMPLES = sizeof(PIANO_SAMPLES) / sizeof(PIANO_SAMPLES[0]);

static PianoDyn pickDyn(uint8_t velocity) {
  if (velocity < 45) return DYN1;
  if (velocity < 90) return DYN2;
  return DYN3;
}

const PianoSample* PianoSamples_pick(uint8_t midiNote, uint8_t velocity) {
  PianoDyn dyn = pickDyn(velocity);

  const PianoSample* best = nullptr;
  int bestDiff = 999;

  for (int i = 0; i < NUM_PIANO_SAMPLES; i++) {
    if (PIANO_SAMPLES[i].dyn != dyn) continue;
    int diff = abs((int)midiNote - (int)PIANO_SAMPLES[i].midiRoot);
    if (!best || diff < bestDiff) {
      best = &PIANO_SAMPLES[i];
      bestDiff = diff;
    }
  }
  return best ? best : &PIANO_SAMPLES[0];
}

bool PianoSamples_checkAllOnSD() {
  bool ok = true;
  for (int i = 0; i < NUM_PIANO_SAMPLES; i++) {
    if (!SD.exists(PIANO_SAMPLES[i].filename)) {
      Serial.print("❌ Missing: ");
      Serial.println(PIANO_SAMPLES[i].filename);
      ok = false;
    }
  }
  if (ok) Serial.println("✅ All piano samples exist on SD");
  return ok;
}
