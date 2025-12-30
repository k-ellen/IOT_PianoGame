#include "Player.h"
#include "Config.h"
#include "PlayMode.h"
#include "AudioEngine.h"
#include "LedEngine.h"
#include "MidiParser.h"
#include "FirebaseControl.h"


// Segment learning helpers
static bool g_ignoreUserInput = false;
static bool g_segmentHadMistake = false;
// static uint64_t chordTicks[4096];

// Build segments (each segment = SEGMENT_CHORDS chord-steps)
struct Segment { uint64_t startTick; uint64_t endTick; };
// Limit segment array to save memory, or use dynamic if needed
static Segment segments[MAX_SEGMENTS]; 

// =======================
// INTERACTIVE STATE
// =======================
bool isLearningMode = false;
static bool notesToPlay[128];     // Notes the song demands
static bool notesPressed[128];    // Notes the user is physically holding
static bool notesSatisfied[128];  // Notes held long enough
static unsigned long noteStartTime[128];

// Configuration
const float DURATION_TOLERANCE = 0.8;
const unsigned long MIN_HOLD_TIME_MS = 50;

// Track colors (Blue / Purple)
#define MAX_TRACK_COLORS 2
static uint32_t TRACK_COLORS[] = { 0x0000FF, 0xFF00FF };

// =======================
// INPUT HANDLERS
// =======================

void Player_onNoteOn(uint8_t note) {
  if (note < FIRST_KEY || note > LAST_KEY) return;
  if (g_ignoreUserInput) return;
  
  if (!isLearningMode) {
    // --- FREE PLAY ---
    setLedBuffer(note, 0x00B400);
  } 
  else {
    // --- LEARNING MODE ---
    if (notesToPlay[note]) {
       // CORRECT NOTE PRESSED
       notesPressed[note] = true;
       noteStartTime[note] = millis(); // Start Timer
       
       // LOGIC CHANGE: Do NOT turn Green yet.
       // We keep it the original color (Blue/Red/etc) until they hold it long enough.
       // Since the "Wait Logic" already painted it the track color, we do nothing here visually.
    } else {
       // WRONG NOTE
       g_segmentHadMistake = true;
       setLedBuffer(note, 0xFF0000); // Red
    }
  }
}

void Player_onNoteOff(uint8_t note) {
  // if (currentMode != MODE_LEARN) return;
  
  // notesPressed[note] = false;

  // if (notesToPlay[note]) {
  //   // If we already won this note, we can turn it off now
  //   if (notesSatisfied[note]) {
  //      Led_noteOff(note);
  //   }
  //   // If not satisfied yet, we keep the LED on (Blue/Purple) so user knows to press again
  // } else {
  //   // Wrong note released -> Off
  //   Led_noteOff(note);
  // }
    if (note >= FIRST_KEY && note <= LAST_KEY) {
    notesPressed[note] = false; // Stop Timer flag

    // LOGIC FIX:
    // If the note is currently needed by the song (notesToPlay is true)...
    if (isLearningMode && notesToPlay[note]) {
       
       // CHECK: Have we already "won" this note?
       if (notesSatisfied[note]) {
           // Yes, we held it long enough. 
           // So if the user lets go now, we SHOULD turn off the LED to clear it up.
           setLedBuffer(note, 0);
       } else {
           // No, we haven't held it long enough yet.
           // Ignore the release (keep the blue/red light on) so they know to press it again.
           return; 
       }

    } else {
       // Note is not needed by song, just turn it off standardly
       setLedBuffer(note, 0);
    }
  }
}

// =======================
// LOGIC HELPERS
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
      unsigned long required = targetDurationMs * DURATION_TOLERANCE;
      if (required < MIN_HOLD_TIME_MS) required = MIN_HOLD_TIME_MS;

      if (timeHeld >= required) {
        notesSatisfied[i] = true;
        Led_noteOn(i, 0x00FF00); // Turn GREEN on success
      }
    }
  }
}

// =======================
// PLAYBACK ENGINES
// =======================

// 1. Audio Playback (Non-interactive)
// FIX: Defines function to take MidiParser&, not String
static void playAudioMode(MidiParser &midi) {
  uint32_t tempoUS = 500000;
  uint64_t globalTicks = 0;
  uint64_t globalTimeUS = 0;
  uint64_t startUS = micros();
  uint16_t division = midi.getDivision();

  MidiEvent ev;
  uint64_t eventTicks;

  while (!stopRequested && midi.nextEvent(ev, eventTicks)) {
    FirebaseControl_checkStop(); 
    if (stopRequested) break;

    uint64_t deltaTicks = eventTicks - globalTicks;
    
    if (deltaTicks > 0) {
      uint64_t addUS = (deltaTicks * tempoUS) / division;
      globalTimeUS += addUS;
      globalTicks = eventTicks;

      // Simple wait
      while (!stopRequested && (int64_t)(startUS + globalTimeUS - micros()) > 0) {
         delay(1);
      }
    }

    if (ev.type == MIDI_NOTE_ON) {
      Led_noteOn(ev.note, 0x00FF00); // Green for listening
      Audio_noteOn(ev.note, ev.velocity);
    } 
    else if (ev.type == MIDI_NOTE_OFF) {
      Led_noteOff(ev.note);
      Audio_noteOff(ev.note);
    }
    else if (ev.type == MIDI_TEMPO) {
      tempoUS = ev.tempoUS;
    }
  }
  Audio_allNotesOff();
}

// 2. Interactive Mode (Wait for keys)
static void playInteractiveMode(MidiParser &midi) {
  // Reset State
  for(int i=0; i<128; i++) {
    notesToPlay[i] = false;
    notesPressed[i] = false;
    notesSatisfied[i] = false;
  }

  // Mark learning mode active so NoteOn/Off handlers validate user input
  isLearningMode = true;

  uint32_t tempoUS = 500000;
  uint64_t globalTicks = 0;
  uint64_t globalTimeUS = 0;
  uint64_t startUS = micros();
  uint16_t division = midi.getDivision();

  MidiEvent ev;
  uint64_t eventTicks;

  Serial.println("🎹 Interactive Mode: Waiting for inputs...");

  while (!stopRequested && midi.nextEvent(ev, eventTicks)) {
    FirebaseControl_checkStop();
    checkMidi();
    if (stopRequested) break;
    uint64_t deltaTicks = eventTicks - globalTicks;
    
    if (deltaTicks > 0) {
      // Calculate how long this step *should* take
      uint64_t stepDurationUS = (deltaTicks * tempoUS) / division;
      unsigned long stepDurationMs = stepDurationUS / 1000;

      // === WAIT LOGIC ===
      // If there are unsatisfied notes, we FREEZE time until user plays them
      if (areAnyNotesUnsatisfied()) {
         uint64_t waitStart = micros();
         
         while(!stopRequested && areAnyNotesUnsatisfied()) {
             checkMidi(); // <-- poll incoming MIDI while time is frozen
            verifyNoteHolds(stepDurationMs);
            FirebaseControl_checkStop();
            delay(5); // Prevent WDT, keep loop tight
         }
         
         // Add the time we spent waiting to the start time so the song doesn't "speed up"
         startUS += (micros() - waitStart);
         
         // Small visual pause after success
         if (!stopRequested) delay(50);
      }

      // === NORMAL TIME ADVANCE ===
      globalTimeUS += stepDurationUS;
      globalTicks = eventTicks;
      
      while (!stopRequested && (int64_t)(startUS + globalTimeUS - micros()) > 0) {
        checkMidi();
        verifyNoteHolds(stepDurationMs); // Check holds even during empty spaces
        FirebaseControl_checkStop();
        delay(1);
      }
    }

    // Process Event
    if (ev.type == MIDI_NOTE_ON) {
      notesToPlay[ev.note] = true;
      notesPressed[ev.note] = false;
      notesSatisfied[ev.note] = false;
      
      // Visuals: Blue/Purple to indicate "Hit Me!"
      uint32_t color = TRACK_COLORS[ev.track % MAX_TRACK_COLORS]; 
      Led_noteOn(ev.note, color);
        Audio_noteOn(ev.note, ev.velocity ? ev.velocity : 100);
    }
    else if (ev.type == MIDI_NOTE_OFF) {
      notesToPlay[ev.note] = false;
      notesSatisfied[ev.note] = false;
      Led_noteOff(ev.note);
    }
    else if (ev.type == MIDI_TEMPO) {
      tempoUS = ev.tempoUS;
    }
  }
  // Leaving interactive playback
  isLearningMode = false;
}


// =======================
// MAIN ENTRY POINT
// =======================

void Player_playSong(const String &path) {
  int segmentCount = 0;
  Serial.println("📂 Analyzing song structure...");

  int primaryTrack = 0;

  // --- PASS 1: FIND PRIMARY TRACK (MELODY) ---
  // We assume the track with the most notes is the melody/learner track.
  {
    MidiParser scan;
    if (!scan.open(path)) {
      Serial.println("❌ Cannot open MIDI");
      return;
    }

    int trackNoteCounts[16] = {0};
    MidiEvent ev;
    uint64_t absTicks = 0;

    while (scan.nextEvent(ev, absTicks)) {
      if ((absTicks % 100) == 0) delay(0); // Watchdog feed
      if (ev.type == MIDI_NOTE_ON && ev.velocity > 0) {
        if (ev.track < 16) trackNoteCounts[ev.track]++;
      }
      if (ev.type == MIDI_END) break;
    }
    scan.close();

    // Find winner
    int maxNotes = -1;
    for(int i=0; i<16; i++) {
      if (trackNoteCounts[i] > maxNotes) {
        maxNotes = trackNoteCounts[i];
        primaryTrack = i;
      }
    }
    Serial.printf("🎹 Primary Track detected: %d (has %d notes)\n", primaryTrack, maxNotes);
    
    if (maxNotes == 0) {
        Serial.println("⚠️ No notes found in song.");
        return;
    }
  }

  // --- PASS 2: GENERATE SEGMENTS BASED ON PRIMARY TRACK ---
  {
    MidiParser scan;
    scan.open(path);

    MidiEvent ev;
    uint64_t absTicks = 0;
    
    uint64_t lastPrimaryTick = (uint64_t)(-1);
    int stepCounter = 0;
    
    // Start the first segment at 0
    segments[0].startTick = 0;
    
    while (scan.nextEvent(ev, absTicks)) {
      if ((absTicks % 100) == 0) delay(0); // Watchdog feed

      // Only count rhythm/steps from the Primary Track
      if (ev.type == MIDI_NOTE_ON && ev.velocity > 0 && ev.track == primaryTrack) {
        
        // Debounce simultaneous notes (chords) on the primary track
        if (absTicks != lastPrimaryTick) {
            lastPrimaryTick = absTicks;
            stepCounter++;

            // If we have collected enough melody steps, CUT HERE.
            // This timestamp becomes the End of current segment and Start of next.
            if (stepCounter >= SEGMENT_CHORDS) {
                segments[segmentCount].endTick = absTicks;
                
                segmentCount++;
                if (segmentCount >= MAX_SEGMENTS) break;

                // Start next segment
                segments[segmentCount].startTick = absTicks;
                stepCounter = 0; 
            }
        }
      }
      if (ev.type == MIDI_END) break;
    }
    
    // Close the final segment to the end of the song
    if (segmentCount < MAX_SEGMENTS) {
        segments[segmentCount].endTick = (uint64_t)(-1); // To end of file
        segmentCount++;
    }
    
    scan.close();
  }

  Serial.printf("✅ Segmentation complete: %d segments (synced to Track %d).\n", segmentCount, primaryTrack);


  // --- HELPER LAMBDAS (Unchanged Logic) ---

  auto resetLearningState = []() {
    for (int i = 0; i < 128; i++) {
      notesToPlay[i] = false;
      notesPressed[i] = false;
      notesSatisfied[i] = false;
    }
    Led_clear();
    Audio_allNotesOff();
  };

  // --- PLAY DEMO (Listen Only) ---
  auto playSegmentDemo = [&](uint64_t segStart, uint64_t segEnd) {
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

    // Fast Forward
    while (midi.nextEvent(ev, absTicks) && absTicks < segStart) {
      if (ev.type == MIDI_TEMPO) tempoUS = ev.tempoUS;
    }

    uint64_t globalTicks = segStart;
    uint64_t globalTimeUS = 0;
    uint64_t startUS = micros();
    bool haveEv = (absTicks >= segStart && ev.type != MIDI_NONE);

    while (!stopRequested) {
      if (!haveEv && !midi.nextEvent(ev, absTicks)) break;
      haveEv = false;

      if (ev.type == MIDI_END) break;
      if (absTicks < segStart) continue;
      // Use strictly >= to stop exactly where the next segment begins
      if (segEnd != (uint64_t)(-1) && absTicks >= segEnd) break;

      uint64_t deltaTicks = absTicks - globalTicks;
      if (deltaTicks > 0) {
        uint64_t stepDurationUS = (deltaTicks * (uint64_t)tempoUS) / (uint64_t)division;
        globalTimeUS += stepDurationUS;
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
  };

  // --- PRACTICE (Interactive) ---
  auto practiceSegment = [&](uint64_t segStart, uint64_t segEnd) {
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

    // Fast Forward
    while (midi.nextEvent(ev, absTicks) && absTicks < segStart) {
      if (ev.type == MIDI_TEMPO) tempoUS = ev.tempoUS;
    }

    uint64_t globalTicks = segStart;
    uint64_t globalTimeUS = 0;
    uint64_t startUS = micros();
    bool haveEv = (absTicks >= segStart && ev.type != MIDI_NONE);

    while (!stopRequested) {
      if (!haveEv && !midi.nextEvent(ev, absTicks)) break;
      haveEv = false;

      if (ev.type == MIDI_END) break;
      if (absTicks < segStart) continue;
      if (segEnd != (uint64_t)(-1) && absTicks >= segEnd) break;

      uint64_t deltaTicks = absTicks - globalTicks;
      if (deltaTicks > 0) {
        uint64_t stepDurationUS = (deltaTicks * (uint64_t)tempoUS) / (uint64_t)division;
        unsigned long stepDurationMs = (unsigned long)(stepDurationUS / 1000);

        // === WAIT LOGIC ===
        if (areAnyNotesUnsatisfied()) {
          uint64_t waitStart = micros();
          while (!stopRequested && areAnyNotesUnsatisfied()) {
            checkMidi();
            verifyNoteHolds(stepDurationMs);
            FirebaseControl_checkStop();
            delay(5); 
          }
          startUS += (micros() - waitStart); 
          if (!stopRequested) delay(50);
        }

        globalTimeUS += stepDurationUS;
        globalTicks = absTicks;

        while (!stopRequested && (int64_t)(startUS + globalTimeUS - micros()) > 0) {
          checkMidi();
          verifyNoteHolds(stepDurationMs);
          FirebaseControl_checkStop();
          delay(1);
        }
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
  };

  // --- MAIN LOOP ---
  stopRequested = false;
  
  for (int s = 0; s < segmentCount && !stopRequested; s++) {
    Serial.printf("▶ Learning Segment %d/%d\n", s + 1, segmentCount);

    // 1. Play Demo
    playSegmentDemo(segments[s].startTick, segments[s].endTick);
    if (stopRequested) break;

    // 2. Loop Practice
    while (!stopRequested) {
      practiceSegment(segments[s].startTick, segments[s].endTick);
      if (stopRequested) break;

      if (!g_segmentHadMistake) {
        Serial.println("✨ Segment Cleared! Next...");
        delay(500); 
        break; 
      }

      Serial.println("⚠️ Mistakes made. Replaying Demo...");
      delay(500);
      playSegmentDemo(segments[s].startTick, segments[s].endTick);
    }
  }

  Audio_allNotesOff();
  Led_clear();
  currentMode = MODE_FREE;
  Serial.println("🏁 Song finished!");
}
