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

static bool g_ignoreUserInput = false;
static bool g_segmentHadMistake = false;

static Segment segments[MAX_SEGMENTS];
float playbackSpeed = 1.0f;
bool g_metronomeEnabled = false;

void Player_setMetronome(bool enabled) {
    g_metronomeEnabled = enabled;
    Serial.printf("⏰ Metronome set to: %s\n", enabled ? "ON" : "OFF");
}

// Grace window for practice start (ignore stray buffered/early presses)
static unsigned long g_practiceStartMs = 0;
static const unsigned long PRACTICE_GRACE_MS = 300;

// Drain MIDI UART buffer right after demo
static const unsigned long MIDI_FLUSH_MS = 250;

// =====================================================
// MEMORIZE MODE STATE
// =====================================================

bool isLearningMode = false;

static bool notesToPlay[128];
static bool notesPressed[128];
static bool notesSatisfied[128];
static unsigned long noteStartTime[128];

const float DURATION_TOLERANCE = 0.8;
const unsigned long MIN_HOLD_TIME_MS = 50;

// =====================================================
// SIMON MODE (CHORD-AWARE)
// =====================================================

#define MAX_SIMON_CHORDS 256
#define MAX_CHORD_NOTES 8

struct SimonChord {
  uint8_t notes[MAX_CHORD_NOTES];
  uint8_t count;
};

static SimonChord simonChords[MAX_SIMON_CHORDS];
static int simonChordCount = 0;
static int simonChordPos = 0;
static bool chordPressed[128];

// =====================================================

#define MAX_TRACK_COLORS 2
static uint32_t TRACK_COLORS[] = { 0x0000FF, 0xFF00FF };

extern void checkMidi();

// =====================================================
// HELPERS
// =====================================================

// Drain any buffered NoteOn/NoteOff that happened during demo.
// IMPORTANT: we force demo mode + ignore flag while draining, so nothing can set mistakes.
static void flushMidiInput(unsigned long ms) {
  unsigned long t0 = millis();

  bool prevIgnore = g_ignoreUserInput;
  PlayMode prevMode = currentMode;

  g_ignoreUserInput = true;
  currentMode = MODE_SONG_AUDIO;

  while (!stopRequested && (millis() - t0) < ms) {
    checkMidi();                // will read & dispatch, but Player_onNoteOn returns early
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
  for (int i = 0; i < 128; i++)
    if (notesToPlay[i] && !notesSatisfied[i]) return true;
  return false;
}

static void verifyNoteHolds(unsigned long targetMs) {
  for (int i = 0; i < 128; i++) {
    if (notesToPlay[i] && notesPressed[i] && !notesSatisfied[i]) {
      unsigned long held = millis() - noteStartTime[i];
      unsigned long req = max((unsigned long)(targetMs * DURATION_TOLERANCE),
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

static void resetSimonState() {
  simonChordPos = 0;
  simonChordCount = 0;
  memset(chordPressed, 0, sizeof(chordPressed));
  g_segmentHadMistake = false;
  Led_clear();
}

// =====================================================
// INPUT HANDLERS
// =====================================================

void Player_onNoteOn(uint8_t note) {
  if (note < FIRST_KEY || note > LAST_KEY) return;
  if (g_ignoreUserInput) return;

  // demo guard (extra safety)
  if (currentMode == MODE_SONG_AUDIO) return;

  // Grace window: ignore any early/stray presses right after practice begins
  if ((currentMode == MODE_SIMON || isLearningMode) &&
      (millis() - g_practiceStartMs) < PRACTICE_GRACE_MS) {
    return;
  }

  // ===== SIMON MODE =====
  if (currentMode == MODE_SIMON) {
    if (simonChordPos >= simonChordCount) return;

    SimonChord &ch = simonChords[simonChordPos];

    bool valid = false;
    for (int i = 0; i < ch.count; i++) {
      if (ch.notes[i] == note) {
        valid = true;
        break;
      }
    }

    if (!valid) {
      Led_noteOn(note, 0xFF0000);
      g_segmentHadMistake = true;
      return;
    }

    chordPressed[note] = true;
    Led_noteOn(note, 0x00FF00);

    bool complete = true;
    for (int i = 0; i < ch.count; i++) {
      if (!chordPressed[ch.notes[i]]) {
        complete = false;
        break;
      }
    }

    if (complete) {
      simonChordPos++;
      memset(chordPressed, 0, sizeof(chordPressed));
    }
    return;
  }

  // ===== MEMORIZE MODE =====
  if (isLearningMode) {
    if (notesToPlay[note]) {
      notesPressed[note] = true;
      noteStartTime[note] = millis();
    } else {
      g_segmentHadMistake = true;
      Led_noteOn(note, 0xFF0000);
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

  uint32_t tempoUS = 500000;
  uint16_t division = midi.getDivision();
  MidiEvent ev;
  uint64_t absTicks = 0;

  while (midi.nextEvent(ev, absTicks) && absTicks < segStart) {
    if (ev.type == MIDI_TEMPO) tempoUS = ev.tempoUS;
  }

  // Audio_setMetronomeConfig(0, 0); // turn off for the demo
  // if (g_metronomeEnabled) {
  //    Audio_setMetronomeConfig(tempoUS, 1.0f);
  // } else {
  //    Audio_setMetronomeConfig(0, 0);
  // }

  uint64_t globalTicks = segStart;
  uint64_t accUS = 0;              // 🔥 accumulated adjusted time
  uint64_t startUS = micros();

  while (!stopRequested && absTicks < segEnd) {
    uint64_t dt = absTicks - globalTicks;
    if (dt) {
      uint64_t normalUS = (dt * tempoUS) / division;

      // ✅ APPLY PLAYBACK SPEED HERE
      uint64_t adjustedUS = (uint64_t)(normalUS / playbackSpeed);

      accUS += adjustedUS;
      globalTicks = absTicks;

      while ((int64_t)(startUS + accUS - micros()) > 0) {
        FirebaseControl_checkStop();
        delay(1);
      }
    }

    if (ev.type == MIDI_NOTE_ON) {
      Led_noteOn(ev.note, TRACK_COLORS[ev.track % MAX_TRACK_COLORS]);
      Audio_noteOn(ev.note, ev.velocity);
    }
    else if (ev.type == MIDI_NOTE_OFF) {
      Led_noteOff(ev.note);
      Audio_noteOff(ev.note);
    } else if (ev.type == MIDI_TEMPO) {
      tempoUS = ev.tempoUS;
      // if (g_metronomeEnabled) {
      //    Audio_setMetronomeConfig(tempoUS, 1.0f);
      // }
    }
    else if (ev.type == MIDI_TEMPO) {
      tempoUS = ev.tempoUS; // tempo changes still respected
    }

    if (!midi.nextEvent(ev, absTicks)) break;
  }

  Audio_setMetronomeConfig(0, 0);

  Audio_allNotesOff();
  Led_clear();
  midi.close();

  g_ignoreUserInput = true;
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

  if (g_metronomeEnabled) {
     Audio_setMetronomeConfig(tempoUS, 1.0f); // Always 1.0 speed for practice
  } else {
     Audio_setMetronomeConfig(0, 0);
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
      if (g_metronomeEnabled) {
         Audio_setMetronomeConfig(tempoUS, 1.0f);
      }
    }
  }

  Audio_setMetronomeConfig(0, 0); // turn off at the end of the segment

  Audio_allNotesOff();
  Led_clear();
  isLearningMode = false;
  midi.close();
}

// =====================================================
// SIMON PRACTICE (CHORDS)
// =====================================================

static void practiceSimon(const String& path, int uptoSegment) {
  resetSimonState();
  currentMode = MODE_SIMON;
  g_ignoreUserInput = false;

  // start grace window
  g_practiceStartMs = millis();

  MidiParser midi;
  if (!midi.open(path)) return;

  uint64_t startTick = segments[0].startTick;
  uint64_t endTick   = segments[uptoSegment].endTick;

  MidiEvent ev;
  uint64_t absTicks = 0;
  uint64_t lastTick = (uint64_t)-1;

  while (midi.nextEvent(ev, absTicks) && absTicks < startTick) {}

  while (midi.nextEvent(ev, absTicks) && absTicks < endTick) {
    if (ev.type == MIDI_NOTE_ON && ev.velocity > 0) {
      if (absTicks != lastTick) {
        simonChords[simonChordCount].count = 0;
        lastTick = absTicks;
        simonChordCount++;
        if (simonChordCount >= MAX_SIMON_CHORDS) break;
      }
      SimonChord &ch = simonChords[simonChordCount - 1];
      if (ch.count < MAX_CHORD_NOTES) ch.notes[ch.count++] = ev.note;
    }
  }

  midi.close();

  while (!stopRequested) {
    checkMidi();
    FirebaseControl_checkStop();
    if (g_segmentHadMistake) return;
    if (simonChordPos >= simonChordCount) return;
    delay(2);
  }
}

// =======================
// FOLLOW MODE
// =======================
static void playVisualSong(const String& path) {
  MidiParser midi;
  if (!midi.open(path)) return;

  Led_clear();
  
  uint32_t tempoUS = 500000; 
  uint16_t division = midi.getDivision(); 
  unsigned long lastStopCheck = 0;
  
  MidiEvent ev;
  uint64_t absTicks = 0;
  uint64_t globalTicks = 0;
  uint64_t startUS = micros();
  uint64_t accumulatedDelayUS = 0; 

  Serial.printf("🚀 Follow Mode: Speed %.1fx | Metronome: AUDIO ENGINE\n", playbackSpeed);
  
  // start metronome if enabled
  if (g_metronomeEnabled) {
     Audio_setMetronomeConfig(tempoUS, playbackSpeed);
  } else {
     Audio_setMetronomeConfig(0, 0); // Ensure it's off
  }

  bool haveEv = false;

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
        // NO MANUAL METRONOME HERE ANYMORE!
        // The Audio Task handles it in the background.
        // checkMidi();
        if (millis() - lastStopCheck > 100) {
           FirebaseControl_checkStop(); 
           lastStopCheck = millis();
        }
        delay(1); 
      }
    }

    if (ev.type == MIDI_NOTE_ON) {
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

  // 3. STOP METRONOME
  Audio_setMetronomeConfig(0, 0);

  Led_clear();
  midi.close();
}

// =====================================================
// MAIN ENTRY
// =====================================================

void Player_playSong(const String &path) {
  stopRequested = false;

  if (currentMode == MODE_FOLLOW) {
    playVisualSong(path);
  }
  else if (currentMode == MODE_SIMON) {
    int count = buildSegmentsByBars(path, segments, MAX_SEGMENTS, 1);
    for (int r = 0; r < count && !stopRequested; r++) {
      while (!stopRequested) {
        g_segmentHadMistake = false;

        // DEMO
        playSegmentDemo(path, segments[0].startTick, segments[r].endTick);

        // 🔥 drain buffered UART input + reset state BEFORE practice
        flushMidiInput(MIDI_FLUSH_MS);
        resetUserInputState();

        // PRACTICE
        practiceSimon(path, r);

        if (!g_segmentHadMistake) break;
        delay(800); // retry same round
      }
    }
  }
  else {
    Serial.println("📂 Mode: MEMORIZE (Interactive)");
    Serial.println("📂 Building segments by BARS...");

    int segmentCount = buildSegmentsByBars(path, segments, MAX_SEGMENTS, 2);

    if (segmentCount <= 0) {
      Serial.println("❌ Segment build failed.");
      return;
    }
    
    // Loop through segments
    for (int s = 0; s < segmentCount && !stopRequested; s++) {
        Serial.printf("▶ Learning Segment %d/%d\n", s + 1, segmentCount);

        // This runs at normal speed (logic unchanged)
        playSegmentDemo(path, segments[s].startTick, segments[s].endTick);
        if (stopRequested) break;
        
        while (!stopRequested) {
          practiceSegment(path, segments[s].startTick, segments[s].endTick);
          if (stopRequested) break;

          if (!g_segmentHadMistake) {
              Serial.println("✨ Segment Cleared!");
              Audio_playEffect("/feedback/continue.wav");
            //  delay(1500); 
              break;
          }

          Serial.println("⚠️ Mistakes made. Replaying...");
          Audio_playEffect("/feedback/try_again.wav");
        //  delay(1500); 
          playSegmentDemo(path, segments[s].startTick, segments[s].endTick);
        }
      
    }
  }

  Audio_allNotesOff();
  Led_clear();

  // Drain the MIDI buffer so any keys pressed during "Success.wav" 
  // do not immediately light up as Free Play notes.
  g_ignoreUserInput = true; 
  unsigned long flushStart = millis();
  while (millis() - flushStart < 100) { 
     checkMidi(); 
  }
  g_ignoreUserInput = false;

  currentMode = MODE_FREE;
  Serial.println("🏁 Song finished!");
  // If finished naturally, update App status
  if (!stopRequested) {
    FirebaseControl_setStatus("stopped");
    Audio_playEffect("/feedback/finished.wav");
  }
}
