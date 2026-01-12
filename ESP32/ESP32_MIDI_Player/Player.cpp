#include "Player.h"
#include "Config.h"
#include "PlayMode.h"
#include "AudioEngine.h"
#include "LedEngine.h"
#include "MidiParser.h"
#include "FirebaseControl.h"
#include "SegmentBuilder.h"

// Segment learning helpers
static bool g_ignoreUserInput = false;
static bool g_segmentHadMistake = false;

// Keep segments global/static (not stack)
static Segment segments[MAX_SEGMENTS];

// playback speed
float playbackSpeed = 1.0f;
bool g_metronomeEnabled = true;

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
// --- FOLLOW MODE (Visual Only, Speed Controlled) ---
static void playVisualSong(const String& path) {
  MidiParser midi;
  if (!midi.open(path)) return;

  Led_clear();
  
  // 1. Time Tracking Variables
  uint32_t tempoUS = 500000; // Default 120 BPM
  uint16_t division = midi.getDivision(); 
  
  unsigned long lastStopCheck = 0;
  
  MidiEvent ev;
  uint64_t absTicks = 0;
  uint64_t globalTicks = 0;
  uint64_t startUS = micros();
  uint64_t accumulatedDelayUS = 0; 

  // 2. METRONOME SETUP
  bool localMetronomeEnabled = true; // Set to 'false' if you want it off by default
  double beatIntervalUS = tempoUS / playbackSpeed; 
  double nextBeatTime = micros(); 

  Serial.printf("🚀 Follow Mode: Speed %.1fx | Metronome: ON\n", playbackSpeed);

  bool haveEv = false;

  auto runMetronomeLogic = [&]() {
    if (localMetronomeEnabled && micros() >= nextBeatTime) {
        Audio_triggerMetronome(); 
        
        // Recalculate interval (in case playbackSpeed changed)
        beatIntervalUS = (double)tempoUS / playbackSpeed;
        nextBeatTime += beatIntervalUS;
        
        // Catch up if we lagged behind (prevents rapid-fire clicks)
        if (micros() > nextBeatTime + beatIntervalUS) {
          nextBeatTime = micros() + beatIntervalUS;
        }
    }
  };

  while (!stopRequested) {
    runMetronomeLogic();
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
        runMetronomeLogic();
        // // 1. Check if it is time for a Metronome Click
        // if (localMetronomeEnabled && micros() >= nextBeatTime) {
           
        //    Audio_triggerMetronome(); 
           
        //    // Calculate when the next beat should happen
        //    beatIntervalUS = (double)tempoUS / playbackSpeed;
        //    nextBeatTime += beatIntervalUS;
           
        //    // Safety: If we lagged behind, catch up to current time
        //    if (micros() > nextBeatTime + beatIntervalUS) {
        //      nextBeatTime = micros() + beatIntervalUS;
        //    }
        // }

        // 2. Standard Checks
        // Only check WiFi every 100ms. checking every 1ms causes lag.
        if (millis() - lastStopCheck > 100) {
           FirebaseControl_checkStop(); 
           lastStopCheck = millis();
        }
        // FirebaseControl_checkStop(); 
        delay(1); // Short delay to prevent crashing
      }
    }

    // Process MIDI Events
    if (ev.type == MIDI_NOTE_ON) {
      uint32_t color = TRACK_COLORS[ev.track % MAX_TRACK_COLORS];
      Led_noteOn(ev.note, color);
    } 
    else if (ev.type == MIDI_NOTE_OFF) {
      Led_noteOff(ev.note);
    } 
    else if (ev.type == MIDI_TEMPO) {
      tempoUS = ev.tempoUS;
      // Important: Update metronome speed immediately when tempo changes
      beatIntervalUS = (double)tempoUS / playbackSpeed; 
    }

    haveEv = false;
  }

  Led_clear();
  midi.close();
}

void Player_playSong(const String &path) {
  stopRequested = false;

  // 1. FOLLOW MODE (Firebase Mode 1)
  // Visual only, Continuous, Respects 'playbackSpeed'
  if (currentMode == MODE_FOLLOW) {
    Serial.println("📂 Mode: FOLLOW (Visual + Speed)");
    playVisualSong(path);
  } 
  
  // 2. MEMORIZE MODE (Firebase Mode 0)
  // Interactive, Bar-by-Bar, ALWAYS 1.0x Speed
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

  // Common Cleanup
  Audio_allNotesOff();
  Led_clear();
  currentMode = MODE_FREE;
  Serial.println("🏁 Song finished!");
  Audio_playEffect("/feedback/finished.wav");
  // If finished naturally, update App status
  if (!stopRequested) {
     FirebaseControl_setStatus("stopped");
  }
}
