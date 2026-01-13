#include "Player.h"
#include "Config.h"
#include "PlayMode.h"
#include "AudioEngine.h"
#include "LedEngine.h"
#include "MidiParser.h"
#include "FirebaseControl.h"
#include "SegmentBuilder.h"

// =====================================================
// GLOBAL STATE
// =====================================================

// Firebase-configurable segment size (bars per segment)
static int  g_memorizeBarsPerSegment = 2;   // default fallback
static bool g_ignoreUserInput        = false;
static bool g_segmentHadMistake      = false;

static Segment segments[MAX_SEGMENTS];

float playbackSpeed      = 1.0f;
bool  g_metronomeEnabled = false;

void Player_setMetronome(bool enabled) {
  g_metronomeEnabled = enabled;
  Serial.printf("⏰ Metronome set to: %s\n", enabled ? "ON" : "OFF");
}

// Drain MIDI UART buffer right after demo
static const unsigned long MIDI_FLUSH_MS = 250;

// =====================================================
// "started" reporting to APP (RTDB field: playCommand/started)
// =====================================================
// App logic:
// - App sets: started=false when user taps play
// - ESP must set: started=true when audio/visual really begins
static bool g_startedReported = false;

static inline void resetStartedReportFlag() {
  g_startedReported = false;
}

static inline void reportStartedOnce() {
  if (g_startedReported) return;
  g_startedReported = true;
  FirebaseControl_setStarted(true);
  // FirebaseControl_setStatusMessage("Started");
}

// =====================================================
// MEMORIZE MODE STATE
// =====================================================

bool isLearningMode = false;

static bool notesToPlay[128];
static bool notesPressed[128];
static bool notesSatisfied[128];
static unsigned long noteStartTime[128];

const float        DURATION_TOLERANCE = 0.8f;
const unsigned long MIN_HOLD_TIME_MS  = 50;

// =====================================================
// SIMON MODE (CHORD-AWARE) - kept as in your code
// =====================================================

#define MAX_SIMON_CHORDS 256
#define MAX_CHORD_NOTES 8

struct SimonChord {
  uint8_t notes[MAX_CHORD_NOTES];
  uint8_t count;
};

static SimonChord simonChords[MAX_SIMON_CHORDS];
static int  simonChordCount = 0;
static int  simonChordPos   = 0;
static bool chordPressed[128];

// =====================================================

#define MAX_TRACK_COLORS 2
static uint32_t TRACK_COLORS[] = { 0x0000FF, 0xFF00FF };

extern void checkMidi();

// =====================================================
// HELPERS
// =====================================================

static void followCountIn(uint32_t tempoUS, uint8_t firstNote) {
  Serial.println("⏱ Follow mode count-in (audio only)");

  Audio_playEffect("/feedback/first_note.wav");

  // Preview first note (dim)
  Led_clear();
  if (firstNote != 255) {
    Led_noteOn(firstNote, 0x002020);
  }

  Audio_playEffect("/feedback/count_in.wav");
  Audio_playEffect("/feedback/three_two_one.wav");

  Led_clear();
}

void Player_setMemorizeBars(int bars) {
  if (bars <= 0) {
    Serial.println("⚠️ Invalid segment count from Firebase, keeping previous value");
    return;
  }
  g_memorizeBarsPerSegment = bars;
  Serial.printf("📐 Memorize mode segments set to %d bars\n", g_memorizeBarsPerSegment);
}

// Drain any buffered NoteOn/NoteOff that happened during demo.
// Force ignore + demo mode while draining.
static void flushMidiInput(unsigned long ms) {
  unsigned long t0 = millis();

  bool prevIgnore = g_ignoreUserInput;
  PlayMode prevMode = currentMode;

  g_ignoreUserInput = true;
  currentMode = MODE_SONG_AUDIO;

  while (!stopRequested && (millis() - t0) < ms) {
    checkMidi();                // reads & dispatches, but Player_onNoteOn returns early
    FirebaseControl_checkStop();
    delay(1);
  }

  currentMode = prevMode;
  g_ignoreUserInput = prevIgnore;
}

static void resetUserInputState() {
  g_segmentHadMistake = false;
  memset(chordPressed, 0, sizeof(chordPressed));
  memset(notesPressed, 0, sizeof(notesPressed));
}

static bool areAnyNotesUnsatisfied() {
  for (int i = 0; i < 128; i++) {
    if (notesToPlay[i] && !notesSatisfied[i]) return true;
  }
  return false;
}

static void verifyNoteHolds(unsigned long targetMs) {
  for (int i = 0; i < 128; i++) {
    if (notesToPlay[i] && notesPressed[i] && !notesSatisfied[i]) {
      unsigned long held = millis() - noteStartTime[i];
      unsigned long req  = max((unsigned long)(targetMs * DURATION_TOLERANCE),
                               MIN_HOLD_TIME_MS);
      if (held >= req) {
        notesSatisfied[i] = true;
        Led_noteOn(i, 0x00FF00);
      }
    }
  }
}

static void resetLearningState() {
  memset(notesToPlay, 0, sizeof(notesToPlay));
  memset(notesPressed, 0, sizeof(notesPressed));
  memset(notesSatisfied, 0, sizeof(notesSatisfied));
  Led_clear();
  Audio_allNotesOff();
}

// =====================================================
// INPUT HANDLERS
// =====================================================

void Player_onNoteOn(uint8_t note) {
  if (note < FIRST_KEY || note > LAST_KEY) return;
  if (g_ignoreUserInput) return;

  // demo guard
  if (currentMode == MODE_SONG_AUDIO) return;

  // MEMORIZE / SIMON practice input
  if (isLearningMode || currentMode == MODE_SIMON) {
    if (notesToPlay[note]) {
      notesPressed[note] = true;
      noteStartTime[note] = millis();
    } else {
      g_segmentHadMistake = true;
      Led_noteOn(note, 0xFF0000);
    }
    return;
  }

  // FREE PLAY
  Led_noteOn(note, 0x00B400);
  Audio_noteOn(note, 100);
}

void Player_onNoteOff(uint8_t note) {
  if (note < FIRST_KEY || note > LAST_KEY) return;

  if (isLearningMode || currentMode == MODE_SIMON) {
    notesPressed[note] = false;
    return;
  }

  // FREE PLAY
  Led_noteOff(note);
  if (currentMode == MODE_FREE) Audio_noteOff(note);
}

// =====================================================
// SHARED DEMO (MEMORIZE + SIMON)
// =====================================================

static void playSegmentDemo(const String& path, uint64_t segStart, uint64_t segEnd) {
  g_ignoreUserInput = true;
  g_segmentHadMistake = false;

  MidiParser midi;
  if (!midi.open(path)) return;

  resetLearningState();
  currentMode = MODE_SONG_AUDIO;

  uint32_t tempoUS  = 500000;
  uint16_t division = midi.getDivision();

  MidiEvent ev;
  uint64_t absTicks = 0;

  // Seek to segStart and capture latest tempo
  while (midi.nextEvent(ev, absTicks) && absTicks < segStart) {
    if (ev.type == MIDI_TEMPO) tempoUS = ev.tempoUS;
  }

  uint64_t globalTicks = segStart;
  uint64_t accUS       = 0;
  uint64_t startUS     = micros();

  while (!stopRequested && absTicks < segEnd) {
    uint64_t dt = absTicks - globalTicks;
    if (dt) {
      uint64_t normalUS   = (dt * (uint64_t)tempoUS) / (uint64_t)division;
      uint64_t adjustedUS = (uint64_t)(normalUS / playbackSpeed);

      accUS += adjustedUS;
      globalTicks = absTicks;

      while (!stopRequested && (int64_t)(startUS + accUS - micros()) > 0) {
        FirebaseControl_checkStop();
        delay(1);
      }
    }

    if (ev.type == MIDI_NOTE_ON) {
      if (ev.velocity > 0) reportStartedOnce(); // ✅ tell app "started=true" on first real note

      Led_noteOn(ev.note, TRACK_COLORS[ev.track % MAX_TRACK_COLORS]);
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

  Audio_setMetronomeConfig(0, 0);
  Audio_allNotesOff();
  Led_clear();
  midi.close();

  // Keep ignoring input until practice setup drains UART
  g_ignoreUserInput = true;
}

// =====================================================
// SHARED PRACTICE (MEMORIZE + SIMON)
// =====================================================

static void practiceSegment(const String& path, uint64_t segStart, uint64_t segEnd, bool isSimon) {
  MidiParser midi;
  if (!midi.open(path)) return;

  resetLearningState();

  // Drain any buffered presses from demo time
  flushMidiInput(MIDI_FLUSH_MS);

  currentMode = MODE_LEARN;
  g_ignoreUserInput = false;
  isLearningMode = true;
  g_segmentHadMistake = false;

  uint32_t tempoUS  = 500000;
  uint16_t division = midi.getDivision();

  MidiEvent ev;
  uint64_t absTicks = 0;

  // Seek to segStart, capture tempo
  while (midi.nextEvent(ev, absTicks) && absTicks < segStart) {
    if (ev.type == MIDI_TEMPO) tempoUS = ev.tempoUS;
  }

  if (g_metronomeEnabled) {
    Audio_setMetronomeConfig(tempoUS, 1.0f); // practice at 1.0
  } else {
    Audio_setMetronomeConfig(0, 0);
  }

  uint64_t globalTicks = segStart;
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
      // time step (used only for "wait until user satisfied")
      (void)((deltaTicks * (uint64_t)tempoUS) / (uint64_t)division);

      // If waiting for user to satisfy notes, pause the timeline
      if (areAnyNotesUnsatisfied()) {
        while (!stopRequested && areAnyNotesUnsatisfied()) {
          checkMidi();
          verifyNoteHolds(1);
          FirebaseControl_checkStop();
          if (isSimon && g_segmentHadMistake) break;
          delay(5);
        }
        if (!stopRequested) delay(50);
      }

      globalTicks = absTicks;
    }

    if (ev.type == MIDI_NOTE_ON) {
      notesToPlay[ev.note]      = true;
      notesPressed[ev.note]     = false;
      notesSatisfied[ev.note]   = false;

      uint32_t color = TRACK_COLORS[ev.track % MAX_TRACK_COLORS];
      if (!isSimon) {
        Led_noteOn(ev.note, color);
      }
    }
    else if (ev.type == MIDI_NOTE_OFF) {
      notesToPlay[ev.note] = false;

      if (!isSimon) {
        if (notesSatisfied[ev.note]) Led_noteOff(ev.note);
      }
      if (notesSatisfied[ev.note]) Led_noteOff(ev.note);

      notesSatisfied[ev.note] = false;
    }
    else if (ev.type == MIDI_TEMPO) {
      tempoUS = ev.tempoUS;
      if (g_metronomeEnabled) {
        Audio_setMetronomeConfig(tempoUS, 1.0f);
      }
    }
  }

  Audio_setMetronomeConfig(0, 0);
  Audio_allNotesOff();
  Led_clear();
  isLearningMode = false;
  midi.close();
}

// =======================
// FOLLOW MODE
// =======================

static void playVisualSong(const String& path) {
  MidiParser midi2;
  if (!midi2.open(path)) return;

  Led_clear();

  uint32_t tempoUS  = 500000;
  uint16_t division = midi2.getDivision();

  MidiEvent ev2;
  uint64_t absTicks     = 0;
  uint64_t firstNoteTick = 0;

  Serial.printf("🚀 Follow Mode: Speed %.1fx | Metronome: AUDIO ENGINE\n", playbackSpeed);

  // capture initial tempo + first note tick
  uint8_t firstNote = 255;
  while (midi2.nextEvent(ev2, absTicks)) {
    if (ev2.type == MIDI_TEMPO) tempoUS = ev2.tempoUS;
    if (ev2.type == MIDI_NOTE_ON && ev2.velocity > 0 && firstNote == 255) {
      firstNote = ev2.note;
      firstNoteTick = absTicks;
      break;
    }
  }
  midi2.close();

  followCountIn(tempoUS, firstNote);

  MidiParser midi;
  if (!midi.open(path)) return;

  Led_clear();

  tempoUS  = 500000;
  division = midi.getDivision();

  MidiEvent ev;
  absTicks = 0;

  // Seek to firstNoteTick
  bool haveEv = false;
  while (midi.nextEvent(ev, absTicks)) {
    if (ev.type == MIDI_TEMPO) tempoUS = ev.tempoUS;
    if (absTicks >= firstNoteTick) {
      haveEv = true;
      break;
    }
  }

  uint64_t globalTicks = absTicks;
  uint64_t startUS = micros();
  uint64_t accumulatedDelayUS = 0;

  if (g_metronomeEnabled) {
    Audio_setMetronomeConfig(tempoUS, playbackSpeed);
  } else {
    Audio_setMetronomeConfig(0, 0);
  }

  unsigned long lastStopCheck = 0;

  while (!stopRequested) {
    if (!haveEv) {
      if (!midi.nextEvent(ev, absTicks)) break;
      haveEv = true;
    }

    uint64_t deltaTicks = absTicks - globalTicks;
    if (deltaTicks > 0) {
      uint64_t standardStepUS = (deltaTicks * (uint64_t)tempoUS) / (uint64_t)division;
      uint64_t adjustedStepUS = (uint64_t)(standardStepUS / playbackSpeed);

      accumulatedDelayUS += adjustedStepUS;
      globalTicks = absTicks;

      while (!stopRequested && (int64_t)(startUS + accumulatedDelayUS - micros()) > 0) {
        if (millis() - lastStopCheck > 100) {
          FirebaseControl_checkStop();
          lastStopCheck = millis();
        }
        delay(1);
      }
    }

    if (ev.type == MIDI_NOTE_ON) {
      if (ev.velocity > 0) reportStartedOnce(); // ✅ tell app started=true on first real note
      uint32_t color = TRACK_COLORS[ev.track % MAX_TRACK_COLORS];
      Led_noteOn(ev.note, color);
    }
    else if (ev.type == MIDI_NOTE_OFF) {
      Led_noteOff(ev.note);
    }
    else if (ev.type == MIDI_TEMPO) {
      tempoUS = ev.tempoUS;
      if (g_metronomeEnabled) {
        Audio_setMetronomeConfig(tempoUS, playbackSpeed);
      }
    }

    haveEv = false;
  }

  Audio_setMetronomeConfig(0, 0);
  Led_clear();
  midi.close();
}

// =====================================================
// MAIN ENTRY
// =====================================================

void Player_playSong(const String &path) {
  stopRequested = false;

  // NEW: start session -> app should show loader until first playback note
  resetStartedReportFlag();
  FirebaseControl_setStarted(false);
  // FirebaseControl_setStatusMessage("Preparing...");

  if (currentMode == MODE_FOLLOW) {
    playVisualSong(path);
  }
  else if (currentMode == MODE_SIMON) {
    int count = buildSegmentsByBars(path, segments, MAX_SEGMENTS, g_memorizeBarsPerSegment);
    Audio_playEffect("/feedback/repeat.wav");

    for (int r = 0; r < count && !stopRequested; r++) {
      while (!stopRequested) {
        g_segmentHadMistake = false;

        // DEMO (this will flip started=true on first NOTE_ON)
        playSegmentDemo(path, segments[0].startTick, segments[r].endTick);

        flushMidiInput(MIDI_FLUSH_MS);
        resetUserInputState();

        // PRACTICE
        practiceSegment(path, segments[0].startTick, segments[r].endTick, true);

        if (!g_segmentHadMistake) break;
        Serial.println("⚠️ Mistakes made. Replaying...");
        Audio_playEffect("/feedback/try_again.wav");
      }
    }
  }
  else {
    Serial.println("📂 Mode: MEMORIZE (Interactive)");
    Serial.println("📂 Building segments by BARS...");

    int segmentCount = buildSegmentsByBars(path, segments, MAX_SEGMENTS, g_memorizeBarsPerSegment);
    if (segmentCount <= 0) {
      Serial.println("❌ Segment build failed.");
      // don't leave app stuck in loader
      FirebaseControl_setStarted(true);
      return;
    }

    Audio_playEffect("/feedback/put_out.wav");

    for (int s = 0; s < segmentCount && !stopRequested; s++) {
      Serial.printf("▶ Learning Segment %d/%d\n", s + 1, segmentCount);

      // DEMO (this will flip started=true on first NOTE_ON)
      playSegmentDemo(path, segments[s].startTick, segments[s].endTick);
      if (stopRequested) break;

      while (!stopRequested) {
        practiceSegment(path, segments[s].startTick, segments[s].endTick, false);
        if (stopRequested) break;

        if (!g_segmentHadMistake) {
          Serial.println("✨ Segment Cleared!");
          delay(1500);
          break;
        }

        Serial.println("⚠️ Mistakes made. Replaying...");
        Audio_playEffect("/feedback/try_again.wav");
        playSegmentDemo(path, segments[s].startTick, segments[s].endTick);
      }
    }
  }

  Audio_allNotesOff();
  Led_clear();

  // Drain MIDI buffer so stray presses don't leak to Free Play
  g_ignoreUserInput = true;
  unsigned long flushStart = millis();
  while (millis() - flushStart < 100) {
    checkMidi();
  }
  g_ignoreUserInput = false;

  currentMode = MODE_FREE;
  Serial.println("🏁 Song finished / stopped!");

  // IMPORTANT: never leave app stuck at started=false
  // If we stopped before first NOTE_ON, this releases the loader.
  FirebaseControl_setStarted(true);
  // FirebaseControl_setStatusMessage("");

  // If finished naturally (not a stop command), update status on RTDB
  if (!stopRequested) {
    FirebaseControl_setStatus("stopped");
    Audio_playEffect("/feedback/finished.wav");
  }
}
