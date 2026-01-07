#include "Player.h"
#include "Config.h"
#include "PlayMode.h"
#include "AudioEngine.h"
#include "LedEngine.h"
#include "MidiParser.h"
#include "FirebaseControl.h"
#include "SegmentBuilder.h"

// =======================
// GLOBAL STATE
// =======================

static bool g_ignoreUserInput = false;
static bool g_segmentHadMistake = false;

static Segment segments[MAX_SEGMENTS];

// playback speed (FOLLOW mode only)
float playbackSpeed = 1.0f;

// =======================
// SIMON STATE
// =======================

static uint8_t simonSeq[512];
static int simonLen = 0;
static int simonPos = 0;
static bool simonWaitingForRelease = false;

// =======================
// INTERACTIVE STATE
// =======================

bool isLearningMode = false;

// Provided by .ino
extern void checkMidi();

// =======================
// SIMON HELPERS
// =======================

static void resetSimonInputState() {
  simonPos = 0;
  simonWaitingForRelease = false;
  g_segmentHadMistake = false;
  Led_clear();
}

static void resetLearningState() {
  Led_clear();
  Audio_allNotesOff();
}

// =======================
// INPUT HANDLERS
// =======================

void Player_onNoteOn(uint8_t note) {
  if (note < FIRST_KEY || note > LAST_KEY) return;
  if (g_ignoreUserInput) return;

  // ===== SIMON MODE =====
  if (currentMode == MODE_SIMON) {
    if (simonWaitingForRelease) return;

    if (simonPos < simonLen && note == simonSeq[simonPos]) {
      Led_noteOn(note, 0x00FF00); // green
      simonPos++;
      simonWaitingForRelease = true;
    } else {
      Led_noteOn(note, 0xFF0000); // red
      g_segmentHadMistake = true;
    }
    return;
  }

  // ===== FREE PLAY =====
  Led_noteOn(note, 0x00B400);
  Audio_noteOn(note, 100);
}

void Player_onNoteOff(uint8_t note) {
  if (note < FIRST_KEY || note > LAST_KEY) return;

  Led_noteOff(note);

  if (currentMode == MODE_SIMON) {
    simonWaitingForRelease = false;
    return;
  }

  Audio_noteOff(note);
}

// =======================
// SIMON DEMO
// =======================

static void playSimonDemo(const String& path, int uptoSegment, Segment* segments) {
  MidiParser midi;
  if (!midi.open(path)) return;

  resetLearningState();
  currentMode = MODE_SONG_AUDIO;
  g_ignoreUserInput = true;

  uint32_t tempoUS = 500000;
  uint16_t division = midi.getDivision();

  uint64_t startTick = segments[0].startTick;
  uint64_t endTick   = segments[uptoSegment].endTick;

  MidiEvent ev;
  uint64_t absTicks = 0;

  while (midi.nextEvent(ev, absTicks) && absTicks < startTick) {}

  uint64_t globalTicks = startTick;
  uint64_t globalTimeUS = 0;
  uint64_t startUS = micros();

  while (!stopRequested && absTicks < endTick) {
    uint64_t dt = absTicks - globalTicks;
    if (dt) {
      uint64_t us = (dt * tempoUS) / division;
      globalTimeUS += us;
      globalTicks = absTicks;

      while (!stopRequested &&
             (int64_t)(startUS + globalTimeUS - micros()) > 0) {
        FirebaseControl_checkStop();
        delay(1);
      }
    }

    if (ev.type == MIDI_NOTE_ON) {
      Led_noteOn(ev.note, 0x0000FF);
      Audio_noteOn(ev.note, ev.velocity);
    }
    else if (ev.type == MIDI_NOTE_OFF) {
      Led_noteOff(ev.note);
      Audio_noteOff(ev.note);
    }
    else if (ev.type == MIDI_TEMPO) {
      tempoUS = ev.tempoUS;
    }

    if (!midi.nextEvent(ev, absTicks)) break;
  }

  Audio_allNotesOff();
  Led_clear();
  midi.close();
}

// =======================
// SIMON PRACTICE
// =======================

static void practiceSimon(const String& path, int uptoSegment, Segment* segments) {
  MidiParser midi;
  if (!midi.open(path)) return;

  resetLearningState();
  resetSimonInputState();

  currentMode = MODE_SIMON;
  g_ignoreUserInput = false;
  isLearningMode = true;

  simonLen = 0;

  uint64_t startTick = segments[0].startTick;
  uint64_t endTick   = segments[uptoSegment].endTick;

  MidiEvent ev;
  uint64_t absTicks = 0;

  // Skip to start
  while (midi.nextEvent(ev, absTicks) && absTicks < startTick) {}

  // Build Simon sequence
  while (midi.nextEvent(ev, absTicks) && absTicks < endTick) {
    if (ev.type == MIDI_NOTE_ON && ev.velocity > 0) {
      simonSeq[simonLen++] = ev.note;
      if (simonLen >= (int)sizeof(simonSeq)) break;
    }
  }

  midi.close();

  // ===== USER INPUT LOOP =====
  while (!stopRequested) {
    checkMidi();
    FirebaseControl_checkStop();

    if (g_segmentHadMistake) {
      isLearningMode = false;
      return;   // ❌ FAIL
    }

    if (simonPos >= simonLen) {
      isLearningMode = false;
      return;   // ✅ SUCCESS
    }

    delay(2);
  }

  isLearningMode = false;
}

// =======================
// MAIN ENTRY POINT
// =======================

void Player_playSong(const String &path) {
  stopRequested = false;

  // ===== SIMON MODE =====
  if (currentMode == MODE_SIMON) {
    Serial.println("📂 Mode: SIMON SAYS");

    int segmentCount = buildSegmentsByBars(path, segments, MAX_SEGMENTS, 1);
    if (segmentCount <= 0) return;

    for (int round = 0; round < segmentCount && !stopRequested; round++) {
      Serial.printf("🟦 Simon Round %d\n", round + 1);

      // 🔁 Attempt loop: replay DEMO + PRACTICE until correct
      while (!stopRequested) {
        // Always clear state before an attempt
        resetSimonInputState();

        // ✅ Replay demo every attempt (THIS is the missing "round restart")
        playSimonDemo(path, round, segments);
        if (stopRequested) break;

        // Practice attempt
        practiceSimon(path, round, segments);
        if (stopRequested) break;

        if (g_segmentHadMistake) {
          Serial.println("❌ Wrong — replaying round (demo + practice)");
          delay(800);
          continue; // 🔁 demo again + practice again
        }

        Serial.println("✅ Correct!");
        delay(800);
        break; // ➡ next round
      }
    }
  }

  // ===== CLEANUP =====
  Audio_allNotesOff();
  Led_clear();
  currentMode = MODE_FREE;
  Serial.println("🏁 Song finished!");

  if (!stopRequested) {
    FirebaseControl_setStatus("stopped");
  }
}
