#include "Player.h"
#include "Config.h"
#include "PlayMode.h"
#include "AudioEngine.h"
#include "LedEngine.h"
#include "MidiParser.h"
#include "FirebaseControl.h"

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
       Serial.println(note);
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
         delayMicroseconds(500);
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
        delayMicroseconds(500);
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
  MidiParser midi;

  // // --- PASS 1: AUDIO LISTEN ---
  // if (midi.open(path)) {
  //   currentMode = MODE_SONG_AUDIO;
  //   Serial.println("▶ Playing Audio Demo...");
  //   playAudioMode(midi);
  //   midi.close();
  // }

  if (stopRequested) {
    Audio_allNotesOff();
    Led_clear();
    currentMode = MODE_FREE;
    return;
  }

  // --- PASS 2: INTERACTIVE PRACTICE ---
  if (midi.open(path)) {
    currentMode = MODE_LEARN;
    Serial.println("▶ Starting Interactive Practice...");
    isLearningMode = true;
        playInteractiveMode(midi);
    isLearningMode = false;
    midi.close();
  }

  Audio_allNotesOff();
  Led_clear();
  currentMode = MODE_FREE;
  Serial.println("⏸ Song finished. Back to Free Play.");
}