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
static void playSegmentDemo(MidiParser& midi,
                            uint64_t segStart,
                            uint64_t segEnd)
{
  midi.rewind();
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
  uint64_t startUS = micros();

  while (!stopRequested) {
    if (ev.type == MIDI_END) break;
    if (segEnd != (uint64_t)-1 && absTicks >= segEnd) break;

    uint64_t deltaTicks = absTicks - globalTicks;
    if (deltaTicks) {
      uint64_t waitUS = (deltaTicks * tempoUS) / division;
      while ((int64_t)(startUS + waitUS - micros()) > 0) delay(1);
      globalTicks = absTicks;
      startUS += waitUS;
    }

    if (ev.type == MIDI_NOTE_ON) {
      Led_noteOn(ev.note, 0x0000FF);
      Audio_noteOn(ev.note, ev.velocity);
    } else if (ev.type == MIDI_NOTE_OFF) {
      Led_noteOff(ev.note);
      Audio_noteOff(ev.note);
    } else if (ev.type == MIDI_TEMPO) {
      tempoUS = ev.tempoUS;
    }

    if (!midi.nextEvent(ev, absTicks)) break;
  }

  Audio_allNotesOff();
  Led_clear();
}


// --- PRACTICE (Interactive) ---
static void practiceSegment(MidiParser& midi,
                            uint64_t segStart,
                            uint64_t segEnd)
{
  midi.rewind();
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

  while (!stopRequested) {
    if (ev.type == MIDI_END) break;
    if (segEnd != (uint64_t)-1 && absTicks >= segEnd) break;

    if (ev.type == MIDI_NOTE_ON) {
      notesToPlay[ev.note] = true;
      Led_noteOn(ev.note, 0xFF00FF);
    }
    else if (ev.type == MIDI_NOTE_OFF) {
      notesToPlay[ev.note] = false;
      if (notesSatisfied[ev.note]) Led_noteOff(ev.note);
    }
    else if (ev.type == MIDI_TEMPO) {
      tempoUS = ev.tempoUS;
    }

    while (areAnyNotesUnsatisfied()) {
      checkMidi();
      verifyNoteHolds(1);
      delay(5);
    }

    if (!midi.nextEvent(ev, absTicks)) break;
  }

  Audio_allNotesOff();
  Led_clear();
  isLearningMode = false;
}


// =======================
// MAIN ENTRY POINT
// =======================

void Player_playSong(const String &path) {
  Serial.println("📂 Building segments by BARS...");

  MidiParser midi;
  if (!midi.open(path)) {
    Serial.println("❌ Failed to open MIDI");
    return;
  }

  // Build segments ONCE
  int segmentCount = buildSegmentsByBars(midi, segments, MAX_SEGMENTS, 2);
  if (segmentCount <= 0) {
    Serial.println("❌ Segment build failed");
    midi.close();
    return;
  }

  Serial.printf("✅ Built %d bar-based segments.\n", segmentCount);

  stopRequested = false;

  for (int s = 0; s < segmentCount && !stopRequested; s++) {
    Serial.printf("▶ Learning Segment %d/%d\n", s + 1, segmentCount);

    // ---------- DEMO ----------
    midi.rewind();
    playSegmentDemo(midi, segments[s].startTick, segments[s].endTick);
    if (stopRequested) break;

    // ---------- PRACTICE LOOP ----------
    while (!stopRequested) {
      midi.rewind();
      practiceSegment(midi, segments[s].startTick, segments[s].endTick);
      if (stopRequested) break;

      if (!g_segmentHadMistake) {
        Serial.println("✨ Segment Cleared! Next...");
        delay(400);
        break;
      }

      Serial.println("⚠️ Mistakes made. Replaying Demo...");
      delay(300);
      midi.rewind();
      playSegmentDemo(midi, segments[s].startTick, segments[s].endTick);
    }
  }

  midi.close();            // ✅ CLOSE ONCE
  Audio_allNotesOff();
  Led_clear();
  currentMode = MODE_FREE;

  Serial.println("🏁 Song finished!");
}




