#include "Player.h"
#include "Config.h"
#include "PlayMode.h"
#include "AudioEngine.h"
#include "LedEngine.h"
#include "MidiParser.h"
#include "FirebaseControl.h"
#include "SegmentBuilder.h"   // ✅ NEW

// Segment learning helpers
static bool g_ignoreUserInput = false;
static bool g_segmentHadMistake = false;

// Keep segments global/static (not stack)
static Segment segments[MAX_SEGMENTS];

// =======================
// INTERACTIVE STATE
// =======================
bool isLearningMode = false;
static bool notesToPlay[128];
static bool notesPressed[128];
static bool notesSatisfied[128];
static unsigned long noteStartTime[128];

const float DURATION_TOLERANCE = 0.8;
const unsigned long MIN_HOLD_TIME_MS = 50;

#define MAX_TRACK_COLORS 2
static uint32_t TRACK_COLORS[] = { 0x0000FF, 0xFF00FF };

// Provided by .ino
extern void checkMidi();

// =======================
// INPUT HANDLERS (unchanged)
// =======================

void Player_onNoteOn(uint8_t note) {
  if (note < FIRST_KEY || note > LAST_KEY) return;
  if (g_ignoreUserInput) return;

  if (!isLearningMode) {
    setLedBuffer(note, 0x00B400);
  } else {
    if (notesToPlay[note]) {
      notesPressed[note] = true;
      noteStartTime[note] = millis();
    } else {
      g_segmentHadMistake = true;
      setLedBuffer(note, 0xFF0000);
    }
  }
}

void Player_onNoteOff(uint8_t note) {
  if (note < FIRST_KEY || note > LAST_KEY) return;

  notesPressed[note] = false;

  if (isLearningMode && notesToPlay[note]) {
    if (notesSatisfied[note]) {
      setLedBuffer(note, 0);
    } else {
      return;
    }
  } else {
    setLedBuffer(note, 0);
  }
}

// =======================
// HELPERS
// =======================

static bool areAnyNotesUnsatisfied() {
  for (int i = 0; i < 128; i++) {
    if (notesToPlay[i] && !notesSatisfied[i]) return true;
  }
  return false;
}

static void verifyNoteHolds(unsigned long targetDurationMs) {
  for (int i = 0; i < 128; i++) {
    if (notesToPlay[i] && notesPressed[i] && !notesSatisfied[i]) {
      unsigned long timeHeld = millis() - noteStartTime[i];
      unsigned long required = (unsigned long)(targetDurationMs * DURATION_TOLERANCE);
      if (required < MIN_HOLD_TIME_MS) required = MIN_HOLD_TIME_MS;

      if (timeHeld >= required) {
        notesSatisfied[i] = true;
        Led_noteOn(i, 0x00FF00);
      }
    }
  }
}

static void resetLearningState() {
  for (int i = 0; i < 128; i++) {
    notesToPlay[i] = false;
    notesPressed[i] = false;
    notesSatisfied[i] = false;
  }
  Led_clear();
  Audio_allNotesOff();
}

// --- PLAY DEMO (Listen Only) ---
static void playSegmentDemo(const String& path, uint64_t segStart, uint64_t segEnd) {
  MidiParser midi;
  if (!midi.open(path)) return;

  resetLearningState();
  currentMode = MODE_SONG_AUDIO;
  g_ignoreUserInput = true;
  isLearningMode = false;

  uint32_t tempoUS = 500000;
  uint16_t division = midi.getDivision();

  MidiEvent ev;
  uint64_t absTicks = 0;

  while (midi.nextEvent(ev, absTicks) && absTicks < segStart) {
    if (ev.type == MIDI_TEMPO) tempoUS = ev.tempoUS;
  }

  uint64_t globalTicks = segStart;
  uint64_t globalTimeUS = 0;
  uint64_t startUS = micros();
  bool haveEv = true;

  while (!stopRequested) {
    if (!haveEv) {
      if (!midi.nextEvent(ev, absTicks)) break;
    }
    haveEv = false;

    if (ev.type == MIDI_END) break;
    if (absTicks < segStart) continue;
    if (segEnd != (uint64_t)(-1) && absTicks >= segEnd) break;

    uint64_t deltaTicks = absTicks - globalTicks;
    if (deltaTicks > 0) {
      uint64_t stepUS = (deltaTicks * (uint64_t)tempoUS) / (uint64_t)division;
      globalTimeUS += stepUS;
      globalTicks = absTicks;

      while (!stopRequested && (int64_t)(startUS + globalTimeUS - micros()) > 0) {
        FirebaseControl_checkStop();
        delay(1);
      }
    }

    if (ev.type == MIDI_NOTE_ON) {
      uint32_t color = TRACK_COLORS[ev.track % MAX_TRACK_COLORS];
      Led_noteOn(ev.note, color);
      Audio_noteOn(ev.note, ev.velocity);
    } else if (ev.type == MIDI_NOTE_OFF) {
      Led_noteOff(ev.note);
      Audio_noteOff(ev.note);
    } else if (ev.type == MIDI_TEMPO) {
      tempoUS = ev.tempoUS;
    }
  }

  Audio_allNotesOff();
  Led_clear();
  midi.close();
}

// --- PRACTICE (Interactive) ---
static void practiceSegment(const String& path, uint64_t segStart, uint64_t segEnd) {
  MidiParser midi;
  if (!midi.open(path)) return;

  resetLearningState();
  currentMode = MODE_LEARN;
  g_ignoreUserInput = false;
  isLearningMode = true;
  g_segmentHadMistake = false;

  uint32_t tempoUS = 500000;
  uint16_t division = midi.getDivision();

  MidiEvent ev;
  uint64_t absTicks = 0;

  while (midi.nextEvent(ev, absTicks) && absTicks < segStart) {
    if (ev.type == MIDI_TEMPO) tempoUS = ev.tempoUS;
  }

  uint64_t globalTicks = segStart;
  uint64_t globalTimeUS = 0;
  uint64_t startUS = micros();
  bool haveEv = true;

  while (!stopRequested) {
    if (!haveEv) {
      if (!midi.nextEvent(ev, absTicks)) break;
    }
    haveEv = false;

    if (ev.type == MIDI_END) break;
    if (absTicks < segStart) continue;
    if (segEnd != (uint64_t)(-1) && absTicks >= segEnd) break;

    uint64_t deltaTicks = absTicks - globalTicks;
    if (deltaTicks > 0) {
      uint64_t stepUS = (deltaTicks * (uint64_t)tempoUS) / (uint64_t)division;
      unsigned long stepMs = (unsigned long)(stepUS / 1000);

      if (areAnyNotesUnsatisfied()) {
        uint64_t waitStart = micros();
        while (!stopRequested && areAnyNotesUnsatisfied()) {
          checkMidi();
          // verifyNoteHolds(stepMs);
          verifyNoteHolds(1);
          FirebaseControl_checkStop();
          delay(5);
        }
        startUS += (micros() - waitStart);
        if (!stopRequested) delay(50);
      }

      globalTimeUS += stepUS;
      globalTicks = absTicks;

      // while (!stopRequested && (int64_t)(startUS + globalTimeUS - micros()) > 0) {
      //   checkMidi();
      //   verifyNoteHolds(stepMs);
      //   FirebaseControl_checkStop();
      //   delay(1);
      // }
    }

    if (ev.type == MIDI_NOTE_ON) {
      notesToPlay[ev.note] = true;
      notesPressed[ev.note] = false;
      notesSatisfied[ev.note] = false;

      uint32_t color = TRACK_COLORS[ev.track % MAX_TRACK_COLORS];
      Led_noteOn(ev.note, color);
    } else if (ev.type == MIDI_NOTE_OFF) {
      notesToPlay[ev.note] = false;
      if (notesSatisfied[ev.note]) Led_noteOff(ev.note);
      notesSatisfied[ev.note] = false;
    } else if (ev.type == MIDI_TEMPO) {
      tempoUS = ev.tempoUS;
    }
  }

  Audio_allNotesOff();
  Led_clear();
  isLearningMode = false;
  midi.close();
}

// =======================
// MAIN ENTRY POINT
// =======================

void Player_playSong(const String &path) {
  Serial.println("📂 Building segments by BARS...");

  // 2 bars per segment => more “musical sense”
  int segmentCount = buildSegmentsByBars(path, segments, MAX_SEGMENTS, 2);

  if (segmentCount <= 0) {
    Serial.println("❌ Segment build failed (could not open MIDI or parse).");
    return;
  }

  Serial.printf("✅ Built %d bar-based segments.\n", segmentCount);

  stopRequested = false;

  for (int s = 0; s < segmentCount && !stopRequested; s++) {
    Serial.printf("▶ Learning Segment %d/%d\n", s + 1, segmentCount);

    playSegmentDemo(path, segments[s].startTick, segments[s].endTick);
    if (stopRequested) break;

    while (!stopRequested) {
      practiceSegment(path, segments[s].startTick, segments[s].endTick);
      if (stopRequested) break;

      if (!g_segmentHadMistake) {
        Serial.println("✨ Segment Cleared! Next...");
        delay(500);
        break;
      }

      Serial.println("⚠️ Mistakes made. Replaying Demo...");
      delay(500);
      playSegmentDemo(path, segments[s].startTick, segments[s].endTick);
    }
  }

  Audio_allNotesOff();
  Led_clear();
  currentMode = MODE_FREE;
  Serial.println("🏁 Song finished!");
}
