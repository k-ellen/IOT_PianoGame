#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <SD.h>
#include <SPI.h>
#include <Adafruit_NeoPixel.h>
#include <MIDI.h>

#include "secrets.h"
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// =======================
// CONFIGURATION
// =======================
// 0.8 = User must hold the note for 80% of the MIDI duration to pass
const float DURATION_TOLERANCE = 0.8; 
const unsigned long MIN_HOLD_TIME_MS = 50; // Even for short notes, require 50ms hold to prevent glitches

// =======================
// PINS & HARDWARE
// =======================
#define SD_CS_PIN      5
#define NEO_PIN        14
#define MIDI_RX_PIN    15
#define NUMPIXELS      126

// =======================
// OBJECTS
// =======================
Adafruit_NeoPixel pixels(NUMPIXELS, NEO_PIN, NEO_GRB + NEO_KHZ800);
HardwareSerial MIDI_SERIAL(2);
MIDI_CREATE_INSTANCE(HardwareSerial, MIDI_SERIAL, MIDI);

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
struct TrackState;

// =======================
// GLOBAL VARS
// =======================
unsigned long lastFirebaseCheck = 0;
long lastCounter = -1;
String lastRemoteFile = "";

// Logic Control Variables
bool isLearningMode = false;      
bool notesToPlay[128] = {false};    // Notes required by the song
bool notesPressed[128] = {false};   // Notes physically held down
bool notesSatisfied[128] = {false}; // Notes that have been held LONG ENOUGH
unsigned long noteStartTime[128] = {0}; // Timestamp when key was pressed

bool ledDirty = false;              

// Mapping
#define KEY_SHIFT 36
#define FIRST_KEY 36
#define LAST_KEY 96

int listOfLEDsByKey[][3] = {
  {2,3,-1},{4,5,-1},{6,7,-1},{8,9,-1},{10,11,-1},{12,13,-1},
  {14,15,-1},{16,17,-1},{18,19,-1},{20,21,-1},{22,23,-1},{24,25,-1},
  {26,27,-1},{28,29,-1},{30,31,-1},{32,33,-1},{34,35,-1},{36,37,-1},
  {38,39,-1},{40,41,-1},{42,-1,-1},{43,44,45},{46,-1,-1},{47,48,-1},
  {49,50,-1},{51,52,-1},{53,54,-1},{55,56,-1},{57,58,-1},{59,60,-1},
  {61,62,-1},{63,64,-1},{65,66,-1},{67,68,-1},{69,70,-1},{71,-1,-1},
  {72,73,-1},{74,75,-1},{76,77,-1},{78,79,-1},{80,81,-1},{82,83,-1},
  {84,85,-1},{86,87,-1},{88,89,-1},{90,91,-1},{92,93,-1},{94,95,-1},
  {96,97,-1},{98,99,-1},{100,101,-1},{102,103,-1},{104,105,-1},
  {106,107,-1},{108,109,-1},{110,111,-1},{112,113,-1},{114,115,-1},
  {116,117,-1},{118,119,-1},{120,121,122},{123,124,125}
};

#define MAX_TRACK_COLORS 2
// uint32_t trackColors[MAX_TRACK_COLORS] = {
//   0xFF0000, 0x0000FF, 0xFF00FF, 0xFFA500, 
//   0x00FFFF, 0xFFFF00, 0xFFFFFF, 0x8000FF, 
//   0x0080FF, 0x80FF00, 0xFF0080, 0x808080, 
//   0x00FF80, 0xFF8000, 0x008000  
// };
// Remove the long list and the MAX_TRACK_COLORS definition
uint32_t trackColors[] = { 
  0x0000FF, // 0: Blue
  0xFF00FF  // 1: Purple 
};

// =======================
// LED HELPER
// =======================
void setLedBuffer(int key, uint32_t color) {
  int index = key - KEY_SHIFT;
  if (index < 0 || index >= (int)(sizeof(listOfLEDsByKey) / sizeof(listOfLEDsByKey[0]))) return;
  for (int i = 0; i < 3; i++) {
    int led = listOfLEDsByKey[index][i];
    if (led != -1) pixels.setPixelColor(led, color);
  }
  ledDirty = true;
}

// =======================
// MIDI CALLBACKS
// =======================
void handleRealTimeNoteOn(byte channel, byte note, byte velocity) {
  if (note < FIRST_KEY || note > LAST_KEY) return;
  
  if (!isLearningMode) {
    // --- FREE PLAY ---
    setLedBuffer(note, pixels.Color(0, 180, 0));
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
       setLedBuffer(note, pixels.Color(255, 0, 0)); // Red
    }
  }
}

// void handleRealTimeNoteOff(byte channel, byte note, byte velocity) {
//   if (note >= FIRST_KEY && note <= LAST_KEY) {
//     notesPressed[note] = false; // Stop Timer flag

//     // If we are in learning mode and this note is still needed, 
//     // do NOT turn off the LED (keep it waiting color).
//     if (isLearningMode && notesToPlay[note]) {
//        return; 
//     }
//     setLedBuffer(note, 0);
//   }
// }
void handleRealTimeNoteOff(byte channel, byte note, byte velocity) {
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
// HELPER FUNCTIONS
// =======================
String extractFileName(String remotePath) {
  remotePath.trim();
  int lastSlash = remotePath.lastIndexOf('/');
  if (lastSlash == -1) return remotePath;
  return remotePath.substring(lastSlash + 1);
}

bool downloadFileToSD(String remotePath, String &outLocalPath) {
  String fileName = extractFileName(remotePath);
  String localPath = "/" + fileName;
  outLocalPath = localPath;

  Serial.printf("\n--- Download Request ---\nRemote: %s\nLocal: %s\n", remotePath.c_str(), localPath.c_str());
  if (SD.exists(localPath.c_str())) SD.remove(localPath.c_str());
  bool ok = Firebase.Storage.download(&fbdo, STORAGE_BUCKET_ID, remotePath.c_str(), localPath.c_str(), mem_storage_type_sd);
  if (!ok) {
    Serial.println("❌ Download FAILED: " + fbdo.errorReason());
    return false;
  }
  Serial.printf("✅ Saved to SD: %s\n", localPath.c_str());
  return true;
}

unsigned long lastStopCheck = 0;
bool shouldContinuePlaying() {
  if (millis() - lastStopCheck < 200) return true;
  lastStopCheck = millis();

  if (!Firebase.ready()) return true;
  if (!Firebase.RTDB.getString(&fbdo, "/esp32API/playCommand/status")) return true;

  String status = fbdo.stringData();
  status.trim();
  status.toLowerCase();
  if (status != "playing") {
    Serial.println("🔴 STOP command received.");
    return false;
  }
  return true;
}

// Logic: Are there any notes that need to be played but haven't been held long enough?
bool areAnyNotesUnsatisfied() {
  for (int i = 0; i < 128; i++) {
    if (notesToPlay[i] && !notesSatisfied[i]) return true;
  }
  return false;
}

// Logic: Check timers for all pressed notes
void verifyNoteHolds(unsigned long targetDurationMs) {
  for (int i = 0; i < 128; i++) {
    // Only check notes that are required, currently pressed, and not yet finished
    if (notesToPlay[i] && notesPressed[i] && !notesSatisfied[i]) {
      
      unsigned long timeHeld = millis() - noteStartTime[i];
      
      // Calculate required hold time (with tolerance)
      unsigned long required = targetDurationMs * DURATION_TOLERANCE;
      if (required < MIN_HOLD_TIME_MS) required = MIN_HOLD_TIME_MS;

      if (timeHeld >= required) {
        // SUCCESS: Note held long enough!
        notesSatisfied[i] = true;
        setLedBuffer(i, pixels.Color(0, 255, 0)); // Turn Green
      }
    }
  }
}

// MIDI PARSING UTILS
static inline uint32_t readBE32(File &f) {
  return ((uint32_t)f.read() << 24) | ((uint32_t)f.read() << 16) | ((uint32_t)f.read() << 8) | (uint32_t)f.read();
}
static inline uint16_t readBE16(File &f) {
  return ((uint16_t)f.read() << 8) | (uint16_t)f.read();
}
uint32_t readVLQ(File &f) {
  uint32_t value = 0;
  int c;
  do {
    c = f.read();
    if (c < 0) return value;
    value = (value << 7) | (c & 0x7F);
  } while (c & 0x80);
  return value;
}

bool waitUntilMicros(uint64_t target) {
  while ((int64_t)(target - (uint64_t)micros()) > 0) {
    if (!shouldContinuePlaying()) return false;
    while(MIDI.read()) {} // Keep reading MIDI inputs
    if (ledDirty) {
       pixels.show();
       ledDirty = false;
    }
    yield();
  }
  return true;
}

enum EvType : uint8_t { EV_NONE = 0, EV_NOTE_ON, EV_NOTE_OFF, EV_TEMPO, EV_END };
struct Event {
  EvType type = EV_NONE;
  uint8_t ch = 0; uint8_t note = 0; uint8_t vel = 0;
  uint32_t tempoUS = 0; uint8_t track = 0;
};
#define MAX_TRACKS 16
struct TrackState {
  uint32_t startPos = 0;
  uint32_t endPos = 0; uint32_t curPos = 0;
  uint8_t runningStatus = 0; bool ended = false;
  uint64_t nextAbsTicks = 0;
  Event nextEvent;
};

bool preloadNextEvent(File &f, TrackState &tr, uint8_t trackIndex) {
  if (tr.ended) return false;
  if (tr.curPos >= tr.endPos) { tr.ended = true; tr.nextEvent.type = EV_END; return false; }
  f.seek(tr.curPos);
  uint32_t delta = readVLQ(f);
  tr.nextAbsTicks += delta;
  int peek = f.peek();
  if (peek < 0) { tr.ended = true; tr.nextEvent.type = EV_END; tr.curPos = f.position(); return false; }
  
  uint8_t status;
  if ((uint8_t)peek < 0x80) status = tr.runningStatus;
  else { status = (uint8_t)f.read(); tr.runningStatus = status; }

  Event ev; ev.type = EV_NONE;
  uint8_t cmd = status & 0xF0; uint8_t ch  = status & 0x0F;
  if (cmd == 0x90) {
    uint8_t note = (uint8_t)f.read(); uint8_t vel = (uint8_t)f.read();
    ev.type = (vel == 0) ? EV_NOTE_OFF : EV_NOTE_ON;
    ev.ch = ch; ev.note = note; ev.vel = vel;
  } else if (cmd == 0x80) {
    uint8_t note = (uint8_t)f.read(); uint8_t vel = (uint8_t)f.read();
    ev.type = EV_NOTE_OFF; ev.ch = ch; ev.note = note; ev.vel = vel;
  } else if (status == 0xFF) {
    uint8_t type = (uint8_t)f.read(); uint32_t len = readVLQ(f);
    if (type == 0x2F && len == 0) { ev.type = EV_END; tr.ended = true; }
    else if (type == 0x51 && len == 3) {
      uint32_t tempo = ((uint32_t)f.read() << 16) | ((uint32_t)f.read() << 8) | (uint32_t)f.read();
      ev.type = EV_TEMPO; ev.tempoUS = tempo;
    } else f.seek(f.position() + len);
  } else if (status == 0xF0 || status == 0xF7) {
    uint32_t len = readVLQ(f);
    f.seek(f.position() + len);
  } else {
    if (cmd == 0xC0 || cmd == 0xD0) f.read();
    else { f.read(); f.read(); }
  }
  tr.curPos = f.position(); ev.track = trackIndex; tr.nextEvent = ev;
  return !tr.ended;
}

// =======================
// PLAY MIDI FILE
// =======================
void playMidiFile(const String &localPath) {
  Serial.println("▶ Starting Interactive Playback");
  
  pixels.clear(); pixels.show();
  isLearningMode = true;
  for(int i=0; i<128; i++) { 
    notesToPlay[i] = false; 
    notesPressed[i] = false; 
    notesSatisfied[i] = false;
  }

  File f = SD.open(localPath.c_str());
  if (!f) { Serial.println("❌ Cannot open MIDI file"); isLearningMode = false; return; }
  
  // Header parsing
  uint8_t hdr[4];
  if (f.read(hdr, 4) != 4 || memcmp(hdr, "MThd", 4) != 0) { f.close(); isLearningMode = false; return; }
  readBE32(f); readBE16(f); 
  uint16_t nTracks = readBE16(f);
  uint16_t division = readBE16(f);
  if (division == 0) division = 480;
  f.seek(14); 

  if (nTracks > MAX_TRACKS) nTracks = MAX_TRACKS; 
  TrackState tracks[MAX_TRACKS];
  for (uint16_t t = 0; t < nTracks; t++) {
    if (f.read(hdr, 4) != 4 || memcmp(hdr, "MTrk", 4) != 0) { f.close(); isLearningMode = false; return; }
    uint32_t len = readBE32(f);
    tracks[t].startPos = f.position();
    tracks[t].endPos = tracks[t].startPos + len;
    tracks[t].curPos = tracks[t].startPos;
    tracks[t].runningStatus = 0;
    tracks[t].ended = false;
    tracks[t].nextAbsTicks = 0;
    f.seek(tracks[t].endPos);
  }

  for (uint16_t t = 0; t < nTracks; t++) preloadNextEvent(f, tracks[t], t);

  uint32_t tempoUS = 500000;
  uint64_t globalTicks = 0;
  uint64_t globalTimeUS = 0;
  uint64_t startUS = micros();

  // MAIN PLAYBACK LOOP
  while (true) {
    if (!shouldContinuePlaying()) break;

    int best = -1;
    uint64_t bestTicks = 0;
    for (uint16_t t = 0; t < nTracks; t++) {
      if (tracks[t].ended) continue;
      if (tracks[t].nextEvent.type == EV_END) continue;
      if (best < 0 || tracks[t].nextAbsTicks < bestTicks) {
        best = (int)t;
        bestTicks = tracks[t].nextAbsTicks;
      }
    }
    if (best < 0) { Serial.println("✅ MIDI playback done."); break; }

    uint64_t deltaTicks = bestTicks - globalTicks;

    // CALCULATE DURATION FOR NEXT STEP
    // We calculate how long this step lasts to know how long the user must hold
    uint64_t stepDurationUS = (deltaTicks * (uint64_t)tempoUS) / (uint64_t)division;
    unsigned long stepDurationMs = stepDurationUS / 1000;

    // ============================================================
    //  WAIT LOGIC (HOLD VERIFICATION)
    // ============================================================
    // If time advances AND we have pending notes, wait for them to be HELD
    if (deltaTicks > 0 && areAnyNotesUnsatisfied()) {
      uint64_t waitStart = micros();
      
      while (true) {
        while(MIDI.read()) { /* Input processing */ }
        
        // Check if held notes have passed the duration threshold
        verifyNoteHolds(stepDurationMs);

        if (ledDirty) { pixels.show(); ledDirty = false; }

        // Exit loop only when all required notes are satisfied
        if (!areAnyNotesUnsatisfied()) {
            delay(50); // Small pause to show the Green success
            break;
        }
        if (!shouldContinuePlaying()) goto end_playback;
      }
      
      uint64_t waitEnd = micros();
      startUS += (waitEnd - waitStart) + 50000; 
    }

    // ============================================================
    //  TIME ADVANCE
    // ============================================================
    if (deltaTicks > 0) {
      globalTimeUS += stepDurationUS;
      globalTicks = bestTicks;
      if (!waitUntilMicros(startUS + globalTimeUS)) break;
    }

    // ============================================================
    //  PROCESS EVENT
    // ============================================================
    Event ev = tracks[best].nextEvent;
    if (ev.type == EV_NOTE_ON) {
      notesToPlay[ev.note] = true;
      notesPressed[ev.note] = false;
      notesSatisfied[ev.note] = false; // Reset satisfaction
      
      uint32_t trackColor = trackColors[ev.track % MAX_TRACK_COLORS];
      setLedBuffer(ev.note, trackColor); 
    } 
    else if (ev.type == EV_NOTE_OFF) {
      notesToPlay[ev.note] = false;
      notesSatisfied[ev.note] = false;
      setLedBuffer(ev.note, 0);
    } 
    else if (ev.type == EV_TEMPO) {
      tempoUS = ev.tempoUS;
    }

    if (ledDirty) { pixels.show(); ledDirty = false; }

    preloadNextEvent(f, tracks[best], best);
  }

  end_playback:
  pixels.clear(); pixels.show();
  f.close();
  isLearningMode = false;
  for(int i=0; i<128; i++) { notesToPlay[i] = false; }
  Serial.println("⏸ Returning to Free Play Mode");
}

// =======================
// SETUP & LOOP (Standard)
// =======================
void setup() {
  Serial.begin(115200);
  delay(1000);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) { delay(300); Serial.print("."); }
  Serial.println("\n✅ WiFi connected");

  if (!SD.begin(SD_CS_PIN)) Serial.println("❌ SD Failed");
  else Serial.println("✅ SD OK");

  pixels.begin(); pixels.clear(); pixels.show();

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  config.token_status_callback = tokenStatusCallback;
  config.fcs.download_buffer_size = 4096;
  fbdo.setBSSLBufferSize(8192, 2048);
  fbdo.setResponseSize(64 * 1024);
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  MIDI_SERIAL.begin(31250, SERIAL_8N1, MIDI_RX_PIN, -1);
  MIDI.begin(MIDI_CHANNEL_OMNI);
  MIDI.setHandleNoteOn(handleRealTimeNoteOn);
  MIDI.setHandleNoteOff(handleRealTimeNoteOff);

  Serial.println("System Ready. Mode: FREE PLAY");
}

void loop() {
  while(MIDI.read()) {
     if (ledDirty) { pixels.show(); ledDirty = false; }
  }

  if (Firebase.ready() && (millis() - lastFirebaseCheck > 2000)) {
    lastFirebaseCheck = millis();
    if (Firebase.RTDB.getInt(&fbdo, "/esp32API/playCommand/commandsCounter")) {
      long currentCounter = fbdo.intData();
      if (lastCounter == -1) { lastCounter = currentCounter; return; }

      if (currentCounter != lastCounter) {
        lastCounter = currentCounter;
        String remoteFile = "";
        if (Firebase.RTDB.getString(&fbdo, "/esp32API/playCommand/fileToPlay")) {
          remoteFile = fbdo.stringData();
          remoteFile.trim();
        }

        String status = "";
        if (Firebase.RTDB.getString(&fbdo, "/esp32API/playCommand/status")) {
          status = fbdo.stringData();
          status.trim(); status.toLowerCase();
        }

        if (status == "playing" && remoteFile.length() > 0) {
          String localPath;
          if (remoteFile == lastRemoteFile) {
             String fileName = extractFileName(remoteFile);
             localPath = "/" + fileName;
          } else {
             if (downloadFileToSD(remoteFile, localPath)) {
               lastRemoteFile = remoteFile;
             } else {
               return;
             }
          }
          playMidiFile(localPath);
        }
      }
    }
  }
}