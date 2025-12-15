#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <SD.h>
#include <SPI.h>
#include <Adafruit_NeoPixel.h>

#include "secrets.h"
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// =======================
// Firebase objects
// =======================
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
struct TrackState; 

unsigned long lastFirebaseCheck = 0;
long lastCounter = -1;
String lastRemoteFile = "";

// =======================
// SD card
// =======================
#define SD_CS_PIN 5

// =======================
// NEOPIXEL SETTINGS
// =======================
#define NEO_PIN        14
#define NUMPIXELS      126
Adafruit_NeoPixel pixels(NUMPIXELS, NEO_PIN, NEO_GRB + NEO_KHZ800);

// Key mapping settings
#define KEY_SHIFT 36

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

// =======================
// Helper: extract filename from Firebase Storage path
// =======================
String extractFileName(String remotePath) {
  remotePath.trim();
  int lastSlash = remotePath.lastIndexOf('/');
  if (lastSlash == -1) return remotePath;
  return remotePath.substring(lastSlash + 1);
}

// =======================
// LED HELPERS
// =======================
void lightLEDsByKey(int key, uint32_t color) {
  int index = key - KEY_SHIFT;
  if (index < 0 || index >= (int)(sizeof(listOfLEDsByKey) / sizeof(listOfLEDsByKey[0]))) return;

  for (int i = 0; i < 3; i++) {
    int led = listOfLEDsByKey[index][i];
    if (led != -1) pixels.setPixelColor(led, color);
  }
  pixels.show();
}

void handleNoteOn(uint8_t ch, uint8_t note, uint8_t vel) {
  Serial.printf("NoteOn: %d (ch=%u vel=%u)\n", note, ch, vel);
  lightLEDsByKey(note, pixels.Color(0, 180, 0));
}

void handleNoteOff(uint8_t ch, uint8_t note, uint8_t vel) {
  Serial.printf("NoteOff: %d (ch=%u)\n", note, ch);
  lightLEDsByKey(note, 0);
}

// =======================
// STOP helper
// =======================
unsigned long lastStopCheck = 0;

bool shouldContinuePlaying() {
  if (millis() - lastStopCheck < 200) return true;
  lastStopCheck = millis();

  if (!Firebase.ready()) return true;

  if (!Firebase.RTDB.getString(&fbdo, "/esp32API/playCommand/status")) {
    Serial.print("⚠️ Failed to read status: ");
    Serial.println(fbdo.errorReason());
    return true;
  }

  String status = fbdo.stringData();
  status.trim();
  status.toLowerCase();

  if (status != "playing") {
    Serial.print("🔴 STOP from app, status = ");
    Serial.println(status);
    return false;
  }
  return true;
}

// =======================
// Download file to SD
// =======================
bool downloadFileToSD(String remotePath, String &outLocalPath) {
  String fileName = extractFileName(remotePath);
  String localPath = "/" + fileName;
  outLocalPath = localPath;

  Serial.printf("\n--- Download Request ---\nRemote: %s\nLocal: %s\n",
                remotePath.c_str(), localPath.c_str());

  if (SD.exists(localPath.c_str())) SD.remove(localPath.c_str());

  bool ok = Firebase.Storage.download(
      &fbdo,
      STORAGE_BUCKET_ID,
      remotePath.c_str(),
      localPath.c_str(),
      mem_storage_type_sd
  );

  if (!ok) {
    Serial.println("❌ Download FAILED");
    Serial.print("Reason: ");
    Serial.println(fbdo.errorReason());
    return false;
  }

  Serial.printf("✅ Saved to SD as: %s\n", localPath.c_str());
  return true;
}

// =======================
// MIDI MERGE PLAYER (Proper Format-1 support)
// =======================

static inline uint32_t readBE32(File &f) {
  return ((uint32_t)f.read() << 24) |
         ((uint32_t)f.read() << 16) |
         ((uint32_t)f.read() << 8)  |
         (uint32_t)f.read();
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

// Wait until a target time, checking STOP
bool waitUntilMicros(uint64_t target) {
  while ((int64_t)(target - (uint64_t)micros()) > 0) {
    if (!shouldContinuePlaying()) return false;
    delayMicroseconds(200);
  }
  return true;
}

enum EvType : uint8_t {
  EV_NONE = 0,
  EV_NOTE_ON,
  EV_NOTE_OFF,
  EV_TEMPO,
  EV_END
};

struct Event {
  EvType type = EV_NONE;
  uint8_t ch = 0;
  uint8_t note = 0;
  uint8_t vel = 0;
  uint32_t tempoUS = 0;
};

#define MAX_TRACKS 16

struct TrackState {
  uint32_t startPos = 0;
  uint32_t endPos = 0;
  uint32_t curPos = 0;

  uint8_t runningStatus = 0;

  bool ended = false;

  uint64_t nextAbsTicks = 0; // absolute tick time of next event
  Event nextEvent;
};

// Read next event from a track and advance its cursor.
bool preloadNextEvent(File &f, TrackState &tr) {
  if (tr.ended) return false;
  if (tr.curPos >= tr.endPos) {
    tr.ended = true;
    tr.nextEvent.type = EV_END;
    return false;
  }

  f.seek(tr.curPos);

  uint32_t delta = readVLQ(f);
  tr.nextAbsTicks += delta;

  int peek = f.peek();
  if (peek < 0) {
    tr.ended = true;
    tr.nextEvent.type = EV_END;
    tr.curPos = f.position();
    return false;
  }

  uint8_t status;
  if ((uint8_t)peek < 0x80) {
    status = tr.runningStatus; // running
  } else {
    status = (uint8_t)f.read();
    tr.runningStatus = status;
  }

  Event ev;
  ev.type = EV_NONE;

  uint8_t cmd = status & 0xF0;
  uint8_t ch  = status & 0x0F;

  if (cmd == 0x90) {
    uint8_t note = (uint8_t)f.read();
    uint8_t vel  = (uint8_t)f.read();
    if (vel == 0) {
      ev.type = EV_NOTE_OFF;
      ev.ch = ch; ev.note = note; ev.vel = vel;
    } else {
      ev.type = EV_NOTE_ON;
      ev.ch = ch; ev.note = note; ev.vel = vel;
    }
  }
  else if (cmd == 0x80) {
    uint8_t note = (uint8_t)f.read();
    uint8_t vel  = (uint8_t)f.read();
    ev.type = EV_NOTE_OFF;
    ev.ch = ch; ev.note = note; ev.vel = vel;
  }
  else if (status == 0xFF) {
    uint8_t type = (uint8_t)f.read();
    uint32_t len = readVLQ(f);

    if (type == 0x2F && len == 0) {
      ev.type = EV_END;
      tr.ended = true;
    }
    else if (type == 0x51 && len == 3) {
      uint32_t tempo =
        ((uint32_t)f.read() << 16) |
        ((uint32_t)f.read() << 8)  |
         (uint32_t)f.read();
      ev.type = EV_TEMPO;
      ev.tempoUS = tempo;
    }
    else {
      f.seek(f.position() + len);
    }
  }
  else if (status == 0xF0 || status == 0xF7) {
    uint32_t len = readVLQ(f);
    f.seek(f.position() + len);
  }
  else {
    // Skip other messages
    if (cmd == 0xC0 || cmd == 0xD0) {
      f.read();
    } else {
      f.read();
      f.read();
    }
  }

  tr.curPos = f.position();
  tr.nextEvent = ev;
  return !tr.ended;
}

// Proper merged playback
void playMidiFile(const String &localPath) {
  Serial.print("Opening MIDI file: ");
  Serial.println(localPath);

  File f = SD.open(localPath.c_str());
  if (!f) {
    Serial.println("❌ Cannot open MIDI file on SD");
    return;
  }

  pixels.clear();
  pixels.show();

  // --- Header ---
  uint8_t hdr[4];
  if (f.read(hdr, 4) != 4 || memcmp(hdr, "MThd", 4) != 0) {
    Serial.println("❌ Not a valid MIDI file (missing MThd)");
    f.close();
    return;
  }

  uint32_t hdrLen   = readBE32(f);
  uint16_t format   = readBE16(f);
  uint16_t nTracks  = readBE16(f);
  uint16_t division = readBE16(f);
  if (division == 0) division = 480;

  if (hdrLen > 6) {
    uint32_t extra = hdrLen - 6;
    Serial.printf("Header extra bytes: %lu (skipping)\n", (unsigned long)extra);
    f.seek(f.position() + extra);
  }

  Serial.printf("MIDI: Fmt=%d Trks=%d Div=%d (hdrLen=%lu)\n",
                format, nTracks, division, (unsigned long)hdrLen);

  if (nTracks > MAX_TRACKS) {
    Serial.printf("❌ Too many tracks (%u). Increase MAX_TRACKS.\n", nTracks);
    f.close();
    return;
  }

  TrackState tracks[MAX_TRACKS];

  // Read track chunk headers to fill start/end positions
  for (uint16_t t = 0; t < nTracks; t++) {
    if (f.read(hdr, 4) != 4 || memcmp(hdr, "MTrk", 4) != 0) {
      Serial.printf("❌ Failed to read MTrk for track %u\n", t + 1);
      f.close();
      return;
    }

    uint32_t len = readBE32(f);
    uint32_t start = f.position();
    uint32_t end = start + len;

    tracks[t].startPos = start;
    tracks[t].endPos = end;
    tracks[t].curPos = start;
    tracks[t].runningStatus = 0;
    tracks[t].ended = false;
    tracks[t].nextAbsTicks = 0;
    tracks[t].nextEvent = {};

    Serial.printf("Track %u: start=%lu end=%lu len=%lu\n",
                  t + 1,
                  (unsigned long)start,
                  (unsigned long)end,
                  (unsigned long)len);

    f.seek(end); // jump to next track chunk
  }

  // Preload first event for each track
  for (uint16_t t = 0; t < nTracks; t++) {
    preloadNextEvent(f, tracks[t]);
  }

  // Global tempo and time
  uint32_t tempoUS = 500000;
  uint64_t globalTicks = 0;
  uint64_t globalTimeUS = 0;
  uint64_t startUS = micros();

  Serial.println("▶️ Starting merged playback");

  while (true) {
    if (!shouldContinuePlaying()) {
      Serial.println("Stopping playback, clearing LEDs.");
      break;
    }

    // Pick next event among tracks by smallest absolute tick time
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

    if (best < 0) {
      Serial.println("MIDI playback done (all tracks ended).");
      break;
    }

    // Advance time to the next tick using current tempo
    uint64_t deltaTicks = bestTicks - globalTicks;
    if (deltaTicks > 0) {
      uint64_t addUS = (deltaTicks * (uint64_t)tempoUS) / (uint64_t)division;
      globalTimeUS += addUS;
      globalTicks = bestTicks;

      if (!waitUntilMicros(startUS + globalTimeUS)) {
        Serial.println("Stopped during wait.");
        break;
      }
    }

    // Execute event
    Event ev = tracks[best].nextEvent;

    if (ev.type == EV_NOTE_ON) {
      handleNoteOn(ev.ch, ev.note, ev.vel);
    } else if (ev.type == EV_NOTE_OFF) {
      handleNoteOff(ev.ch, ev.note, ev.vel);
    } else if (ev.type == EV_TEMPO) {
      tempoUS = ev.tempoUS;
      Serial.printf("🎵 Tempo change: %lu us/qn (%.1f BPM) at tick=%llu\n",
                    (unsigned long)tempoUS,
                    60000000.0 / (double)tempoUS,
                    (unsigned long long)globalTicks);
    }

    // Load next event from that track
    preloadNextEvent(f, tracks[best]);
  }

  pixels.clear();
  pixels.show();
  f.close();
}

// =======================
// Setup
// =======================
void setup() {
  Serial.begin(115200);
  delay(1000);

  // --- WiFi ---
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi connected");

  // --- SD card ---
  Serial.println("Mounting SD card...");
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("❌ SD.begin failed! Check wiring & CS pin.");
  } else {
    Serial.println("✅ SD mounted OK");
  }

  // --- LEDs ---
  pixels.begin();
  pixels.clear();
  pixels.show();

  // --- Firebase config ---
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

  Serial.println("Setup complete.");
}

// =======================
// Loop
// =======================
void loop() {
  if (!Firebase.ready()) return;
  if (millis() - lastFirebaseCheck < 2000) return;
  lastFirebaseCheck = millis();

  // 1) counter
  if (!Firebase.RTDB.getInt(&fbdo, "/esp32API/playCommand/commandsCounter")) {
    Serial.print("❌ Failed to read commandsCounter: ");
    Serial.println(fbdo.errorReason());
    return;
  }

  long currentCounter = fbdo.intData();

  if (lastCounter == -1) {
    lastCounter = currentCounter;
    Serial.printf("Initial counter sync: %ld\n", lastCounter);
    return;
  }

  if (currentCounter == lastCounter) return;

  Serial.printf("🔥 Command changed! New counter: %ld\n", currentCounter);
  lastCounter = currentCounter;

  // 2) fileToPlay
  if (!Firebase.RTDB.getString(&fbdo, "/esp32API/playCommand/fileToPlay")) {
    Serial.print("❌ Failed to read fileToPlay: ");
    Serial.println(fbdo.errorReason());
    return;
  }

  String remoteFile = fbdo.stringData();
  remoteFile.trim();

  // 3) status
  String status;
  if (Firebase.RTDB.getString(&fbdo, "/esp32API/playCommand/status")) {
    status = fbdo.stringData();
    status.trim();
    status.toLowerCase();
  } else {
    Serial.print("⚠️ Failed to read status: ");
    Serial.println(fbdo.errorReason());
    pixels.clear();
    pixels.show();
    return;
  }

  if (status != "playing") {
    Serial.print("Status is not 'playing' (");
    Serial.print(status);
    Serial.println(") → not starting playback, clearing LEDs.");
    pixels.clear();
    pixels.show();
    return;
  }

  if (remoteFile.length() == 0) {
    Serial.println("⚠️ fileToPlay is empty.");
    return;
  }

  Serial.print("File requested from Storage: ");
  Serial.println(remoteFile);

  // 4) download/use existing
  String localPath;
  if (remoteFile == lastRemoteFile) {
    Serial.println("ℹ️ Same song as last time → using existing file on SD.");
    String fileName = extractFileName(remoteFile);
    localPath = "/" + fileName;
  } else {
    if (!downloadFileToSD(remoteFile, localPath)) return;
    lastRemoteFile = remoteFile;
  }

  // 5) play
  playMidiFile(localPath);
}


// #include <WiFi.h>
// #include <Firebase_ESP_Client.h>
// #include <SD.h>
// #include <SPI.h>
// #include <Adafruit_NeoPixel.h>

// #include "secrets.h"
// #include "addons/TokenHelper.h"
// #include "addons/RTDBHelper.h"

// // =======================
// // Firebase objects
// // =======================
// FirebaseData fbdo;
// FirebaseAuth auth;
// FirebaseConfig config;

// unsigned long lastFirebaseCheck = 0;
// long lastCounter = -1;
// String lastRemoteFile = "";

// // =======================
// // SD card
// // =======================
// #define SD_CS_PIN 5

// // =======================
// // NEOPIXEL SETTINGS
// // =======================
// #define NEO_PIN   14
// #define NUMPIXELS 126
// Adafruit_NeoPixel pixels(NUMPIXELS, NEO_PIN, NEO_GRB + NEO_KHZ800);

// // =======================
// // MIDI TIMING
// // =======================
// uint32_t midiTempoUS = 500000;   // default 120 BPM
// uint16_t midiDivision = 480;     // ticks per quarter note

// // =======================
// // Key mapping
// // =======================
// #define KEY_SHIFT 36

// int listOfLEDsByKey[][3] = {
//   {2,3,-1},{4,5,-1},{6,7,-1},{8,9,-1},{10,11,-1},{12,13,-1},
//   {14,15,-1},{16,17,-1},{18,19,-1},{20,21,-1},{22,23,-1},{24,25,-1},
//   {26,27,-1},{28,29,-1},{30,31,-1},{32,33,-1},{34,35,-1},{36,37,-1},
//   {38,39,-1},{40,41,-1},{42,-1,-1},{43,44,45},{46,-1,-1},{47,48,-1},
//   {49,50,-1},{51,52,-1},{53,54,-1},{55,56,-1},{57,58,-1},{59,60,-1},
//   {61,62,-1},{63,64,-1},{65,66,-1},{67,68,-1},{69,70,-1},{71,-1,-1},
//   {72,73,-1},{74,75,-1},{76,77,-1},{78,79,-1},{80,81,-1},{82,83,-1},
//   {84,85,-1},{86,87,-1},{88,89,-1},{90,91,-1},{92,93,-1},{94,95,-1},
//   {96,97,-1},{98,99,-1},{100,101,-1},{102,103,-1},{104,105,-1},
//   {106,107,-1},{108,109,-1},{110,111,-1},{112,113,-1},{114,115,-1},
//   {116,117,-1},{118,119,-1},{120,121,122},{123,124,125}
// };

// // =======================
// // Helpers
// // =======================
// String extractFileName(String path) {
//   int idx = path.lastIndexOf('/');
//   return (idx < 0) ? path : path.substring(idx + 1);
// }

// uint32_t readVLQ(File &f) {
//   uint32_t value = 0;
//   int c;
//   do {
//     c = f.read();
//     value = (value << 7) | (c & 0x7F);
//   } while (c & 0x80);
//   return value;
// }

// // =======================
// // STOP helper
// // =======================
// unsigned long lastStopCheck = 0;

// bool shouldContinuePlaying() {
//   if (millis() - lastStopCheck < 200) return true;
//   lastStopCheck = millis();

//   if (!Firebase.ready()) return true;

//   if (!Firebase.RTDB.getString(&fbdo, "/esp32API/playCommand/status")) {
//     Serial.print("⚠️ Failed to read status: ");
//     Serial.println(fbdo.errorReason());
//     return true;
//   }

//   String s = fbdo.stringData();
//   s.trim(); s.toLowerCase();

//   if (s != "playing") {
//     Serial.print("🔴 STOP from app, status = ");
//     Serial.println(s);
//     return false;
//   }

//   return true;
// }

// // =======================
// // LED helpers
// // =======================
// void lightLEDsByKey(int key, uint32_t color) {
//   int idx = key - KEY_SHIFT;
//   if (idx < 0 || idx >= 61) return;

//   for (int i = 0; i < 3; i++) {
//     int led = listOfLEDsByKey[idx][i];
//     if (led >= 0) pixels.setPixelColor(led, color);
//   }
//   pixels.show();
// }

// void handleNoteOn(uint8_t ch, uint8_t note, uint8_t vel) {
//   Serial.printf("NoteOn: %d\n", note);
//   lightLEDsByKey(note, pixels.Color(0, 180, 0));
// }

// void handleNoteOff(uint8_t ch, uint8_t note, uint8_t vel) {
//   Serial.printf("NoteOff: %d\n", note);
//   lightLEDsByKey(note, 0);
// }

// // =======================
// // MIDI PLAYER
// // =======================
// void playMidiFile(const String &path) {
//   Serial.print("Opening MIDI file: ");
//   Serial.println(path);

//   File f = SD.open(path);
//   if (!f) {
//     Serial.println("❌ Cannot open MIDI file on SD");
//     return;
//   }

//   pixels.clear();
//   pixels.show();

//   uint8_t hdr[4];
//   if (f.read(hdr, 4) != 4 || memcmp(hdr, "MThd", 4)) {
//     Serial.println("❌ Invalid MIDI header");
//     f.close();
//     return;
//   }

//   uint32_t hdrLen =
//     (uint32_t)f.read() << 24 |
//     (uint32_t)f.read() << 16 |
//     (uint32_t)f.read() << 8  |
//     (uint32_t)f.read();

//   uint16_t format  = (uint16_t)f.read() << 8 | (uint16_t)f.read();
//   uint16_t nTracks = (uint16_t)f.read() << 8 | (uint16_t)f.read();
//   midiDivision     = (uint16_t)f.read() << 8 | (uint16_t)f.read();

//   if (midiDivision == 0) {
//     Serial.println("⚠️ Invalid division, using 480");
//     midiDivision = 480;
//   }

//   if (hdrLen > 6) {
//     Serial.printf("Skipping %lu extra header bytes\n", hdrLen - 6);
//     f.seek(f.position() + (hdrLen - 6));
//   }

//   midiTempoUS = 500000;

//   Serial.printf("MIDI: Fmt=%d Trks=%d Div=%d (hdrLen=%lu)\n",
//                 format, nTracks, midiDivision, hdrLen);

//   bool skipFirstTrack = (format == 1);

//   for (int t = 0; t < nTracks; t++) {

//     if (f.read(hdr, 4) != 4 || memcmp(hdr, "MTrk", 4)) {
//       Serial.println("❌ Missing MTrk header");
//       break;
//     }

//     uint32_t trackLen =
//       (uint32_t)f.read() << 24 |
//       (uint32_t)f.read() << 16 |
//       (uint32_t)f.read() << 8  |
//       (uint32_t)f.read();

//     uint32_t trackEnd = f.position() + trackLen;

//     if (skipFirstTrack && t == 0) {
//       Serial.println("⏩ Skipping tempo-only track (Format 1)");
//       f.seek(trackEnd);
//       continue;
//     }

//     Serial.printf("Track %d (len=%lu)\n", t + 1, trackLen);

//     uint8_t runningStatus = 0;

//     while ((uint32_t)f.position() < trackEnd) {

//       if (!shouldContinuePlaying()) {
//         Serial.println("Playback stopped by app");
//         pixels.clear();
//         pixels.show();
//         f.close();
//         return;
//       }

//       uint32_t delta = readVLQ(f);

//       uint32_t delayMs =
//         (uint64_t)delta * midiTempoUS / midiDivision / 1000;

//       if (delayMs > 2000) {
//         Serial.printf("⚠️ Large delay (%lu ms), clamped\n", delayMs);
//         delayMs = 2000;
//       }

//       uint32_t waited = 0;
//       while (waited < delayMs) {
//         delay(5);
//         waited += 5;
//         if (!shouldContinuePlaying()) {
//           Serial.println("Playback stopped during delay");
//           pixels.clear();
//           pixels.show();
//           f.close();
//           return;
//         }
//       }

//       int peek = f.peek();
//       if (peek < 0) {
//         Serial.println("⚠️ EOF inside track");
//         break;
//       }

//       uint8_t status;
//       if (peek < 0x80) {
//         status = runningStatus;
//       } else {
//         status = f.read();
//         runningStatus = status;
//       }

//       uint8_t cmd = status & 0xF0;
//       uint8_t ch  = status & 0x0F;

//       if (cmd == 0x90) {
//         uint8_t n = f.read(), v = f.read();
//         v ? handleNoteOn(ch,n,v) : handleNoteOff(ch,n,v);
//       }
//       else if (cmd == 0x80) {
//         handleNoteOff(ch, f.read(), f.read());
//       }
//       else if (status == 0xFF) {
//         uint8_t type = f.read();
//         uint32_t len = readVLQ(f);

//         if (type == 0x51 && len == 3) {
//           midiTempoUS =
//             (uint32_t)f.read() << 16 |
//             (uint32_t)f.read() << 8  |
//             (uint32_t)f.read();

//           Serial.printf("🎵 Tempo change: %lu us/qn (%.1f BPM)\n",
//                         midiTempoUS, 60000000.0 / midiTempoUS);
//         } else {
//           f.seek(f.position() + len);
//         }
//       }
//       else {
//         f.read();
//         f.read();
//       }
//     }
//   }

//   Serial.println("MIDI playback finished");
//   pixels.clear();
//   pixels.show();
//   f.close();
// }

// // =======================
// // SETUP
// // =======================
// void setup() {
//   Serial.begin(115200);

//   Serial.print("Connecting to WiFi");
//   WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
//   while (WiFi.status() != WL_CONNECTED) {
//     delay(300);
//     Serial.print(".");
//   }
//   Serial.println("\n✅ WiFi connected");

//   Serial.println("Mounting SD card...");
//   if (!SD.begin(SD_CS_PIN)) {
//     Serial.println("❌ SD.begin failed!");
//   } else {
//     Serial.println("✅ SD mounted");
//   }

//   pixels.begin();
//   pixels.clear();
//   pixels.show();

//   config.api_key = API_KEY;
//   config.database_url = DATABASE_URL;
//   auth.user.email = USER_EMAIL;
//   auth.user.password = USER_PASSWORD;

//   Firebase.begin(&config, &auth);
//   Firebase.reconnectWiFi(true);

//   Serial.println("Setup complete.");
// }

// // =======================
// // LOOP
// // =======================
// void loop() {
//   if (!Firebase.ready()) return;
//   if (millis() - lastFirebaseCheck < 2000) return;
//   lastFirebaseCheck = millis();

//   if (!Firebase.RTDB.getInt(&fbdo, "/esp32API/playCommand/commandsCounter")) {
//     Serial.print("❌ Failed to read commandsCounter: ");
//     Serial.println(fbdo.errorReason());
//     return;
//   }

//   long currentCounter = fbdo.intData();

//   if (lastCounter == -1) {
//     lastCounter = currentCounter;
//     Serial.printf("Initial counter sync: %ld\n", lastCounter);
//     return;
//   }

//   if (currentCounter == lastCounter) return;
//   lastCounter = currentCounter;

//   Serial.printf("🔥 Command changed! New counter: %ld\n", currentCounter);

//   if (!Firebase.RTDB.getString(&fbdo, "/esp32API/playCommand/fileToPlay")) {
//     Serial.print("❌ Failed to read fileToPlay: ");
//     Serial.println(fbdo.errorReason());
//     return;
//   }

//   String remote = fbdo.stringData();
//   remote.trim();

//   Serial.print("File requested from Storage: ");
//   Serial.println(remote);

//   String local = "/" + extractFileName(remote);

//   if (remote != lastRemoteFile) {
//     Serial.println("--- Download Request ---");
//     Serial.print("Remote: ");
//     Serial.println(remote);
//     Serial.print("Local: ");
//     Serial.println(local);

//     if (!Firebase.Storage.download(
//           &fbdo,
//           STORAGE_BUCKET_ID,
//           remote.c_str(),
//           local.c_str(),
//           mem_storage_type_sd)) {
//       Serial.print("❌ Download FAILED: ");
//       Serial.println(fbdo.errorReason());
//       return;
//     }

//     Serial.print("✅ Saved to SD as: ");
//     Serial.println(local);
//     lastRemoteFile = remote;
//   }

//   playMidiFile(local);
// }
// --------------------------------------------------
// #include <WiFi.h>
// #include <Firebase_ESP_Client.h>
// #include <SD.h>
// #include <SPI.h>
// #include <Adafruit_NeoPixel.h>

// #include "secrets.h"             // WIFI_SSID, WIFI_PASSWORD, API_KEY, DATABASE_URL, USER_EMAIL, USER_PASSWORD, STORAGE_BUCKET_ID

// #include "addons/TokenHelper.h"
// #include "addons/RTDBHelper.h"

// // =======================
// // Firebase objects
// // =======================
// FirebaseData fbdo;
// FirebaseAuth auth;
// FirebaseConfig config;

// unsigned long lastFirebaseCheck = 0;
// long lastCounter = -1;
// String lastRemoteFile = "";      // השיר האחרון שהורדנו

// // =======================
// // SD card
// // =======================
// #define SD_CS_PIN 5   // תשני אם ה-CS מחובר לפין אחר

// // =======================
// // NEOPIXEL SETTINGS
// // =======================
// #define NEO_PIN        14   // Pin connected to NeoPixels
// #define NUMPIXELS      126 
// Adafruit_NeoPixel pixels(NUMPIXELS, NEO_PIN, NEO_GRB + NEO_KHZ800);

// // Key mapping settings
// #define KEY_SHIFT 36
// #define FIRST_KEY 36
// #define LAST_KEY  96

// int listOfLEDsByKey[][3] = {
//   {2,3,-1},{4,5,-1},{6,7,-1},{8,9,-1},{10,11,-1},{12,13,-1},
//   {14,15,-1},{16,17,-1},{18,19,-1},{20,21,-1},{22,23,-1},{24,25,-1},
//   {26,27,-1},{28,29,-1},{30,31,-1},{32,33,-1},{34,35,-1},{36,37,-1},
//   {38,39,-1},{40,41,-1},{42,-1,-1},{43,44,45},{46,-1,-1},{47,48,-1},
//   {49,50,-1},{51,52,-1},{53,54,-1},{55,56,-1},{57,58,-1},{59,60,-1},
//   {61,62,-1},{63,64,-1},{65,66,-1},{67,68,-1},{69,70,-1},{71,-1,-1},
//   {72,73,-1},{74,75,-1},{76,77,-1},{78,79,-1},{80,81,-1},{82,83,-1},
//   {84,85,-1},{86,87,-1},{88,89,-1},{90,91,-1},{92,93,-1},{94,95,-1},
//   {96,97,-1},{98,99,-1},{100,101,-1},{102,103,-1},{104,105,-1},
//   {106,107,-1},{108,109,-1},{110,111,-1},{112,113,-1},{114,115,-1},
//   {116,117,-1},{118,119,-1},{120,121,122},{123,124,125}
// };

// // =======================
// // Helper: extract filename from Firebase Storage path
// // =======================
// String extractFileName(String remotePath) {
//   remotePath.trim();
//   int lastSlash = remotePath.lastIndexOf('/');
//   if (lastSlash == -1) {
//     return remotePath;  // אין '/', כל המחרוזת היא שם הקובץ
//   }
//   return remotePath.substring(lastSlash + 1);
// }

// // =======================
// // LED HELPERS
// // =======================
// void lightLEDsByKey(int key, uint32_t color) {
//   int index = key - KEY_SHIFT;
//   if (index < 0 || index >= (int)(sizeof(listOfLEDsByKey) / sizeof(listOfLEDsByKey[0]))) {
//     return;
//   }

//   for (int i = 0; i < 3; i++) {
//     int led = listOfLEDsByKey[index][i];
//     if (led != -1) {
//       pixels.setPixelColor(led, color);
//     }
//   }
//   pixels.show();
// }

// void handleNoteOn(uint8_t ch, uint8_t note, uint8_t vel) {
//   Serial.printf("NoteOn: %d\n", note);
//   lightLEDsByKey(note, pixels.Color(0, 180, 0)); // Green
// }

// void handleNoteOff(uint8_t ch, uint8_t note, uint8_t vel) {
//   lightLEDsByKey(note, 0); // Turn off
// }

// // =======================
// // MIDI HELPERS
// // =======================
// uint32_t readVLQ(File &f) {
//   uint32_t value = 0;
//   int c;
//   do {
//     c = f.read();
//     if (c < 0) return value; // EOF safety
//     value = (value << 7) | (c & 0x7F);
//   } while (c & 0x80);
//   return value;
// }

// // =======================
// // STOP helper – בודק status בזמן ניגון
// // =======================
// unsigned long lastStopCheck = 0;

// bool shouldContinuePlaying() {
//   // לא להציק לפיירבייס כל מילישניה – נבדוק בערך כל 200ms
//   if (millis() - lastStopCheck < 200) {
//     return true;
//   }
//   lastStopCheck = millis();

//   if (!Firebase.ready()) {
//     return true; // אם אין חיבור, לא נעצור סתם
//   }

//   if (!Firebase.RTDB.getString(&fbdo, "/esp32API/playCommand/status")) {
//     Serial.print("⚠️ Failed to read status: ");
//     Serial.println(fbdo.errorReason());
//     return true; // במקרה תקלה – נמשיך לנגן
//   }

//   String status = fbdo.stringData();
//   status.trim();
//   status.toLowerCase();

//   // רק "playing" ממשיך לנגן, כל דבר אחר = STOP
//   if (status != "playing") {
//     Serial.print("🔴 STOP from app, status = ");
//     Serial.println(status);
//     return false;
//   }

//   return true;
// }

// // =======================
// // Download file to SD (saves using original file name)
// // =======================
// bool downloadFileToSD(String remotePath, String &outLocalPath) {

//   String fileName = extractFileName(remotePath);
//   String localPath = "/" + fileName;
//   outLocalPath = localPath;

//   Serial.printf("\n--- Download Request ---\nRemote: %s\nLocal: %s\n",
//                 remotePath.c_str(), localPath.c_str());

//   if (SD.exists(localPath.c_str())) {
//     SD.remove(localPath.c_str());
//   }

//   bool ok = Firebase.Storage.download(
//       &fbdo,
//       STORAGE_BUCKET_ID,
//       remotePath.c_str(),
//       localPath.c_str(),
//       mem_storage_type_sd
//   );

//   if (!ok) {
//     Serial.println("❌ Download FAILED");
//     Serial.print("Reason: ");
//     Serial.println(fbdo.errorReason());
//     return false;
//   }

//   Serial.printf("✅ Saved to SD as: %s\n", localPath.c_str());
//   return true;
// }

// // =======================
// // MIDI PLAYER: reads from SD and drives LEDs
// // =======================
// void playMidiFile(const String &localPath) {
//   Serial.print("Opening MIDI file: ");
//   Serial.println(localPath);

//   File f = SD.open(localPath.c_str());
//   if (!f) {
//     Serial.println("Cannot open MIDI file on SD");
//     return;
//   }

//   pixels.clear();
//   pixels.show();

//   // --- Read Header ---
//   uint8_t hdr[4];
//   if (f.read(hdr, 4) != 4) {
//     Serial.println("Invalid MIDI header (short read)");
//     f.close();
//     return;
//   }

//   if (hdr[0] != 'M' || hdr[1] != 'T' || hdr[2] != 'h' || hdr[3] != 'd') {
//     Serial.println("Not a valid MIDI file (missing MThd)");
//     f.close();
//     return;
//   }

//   uint32_t hdrLen = (uint32_t)f.read() << 24 | (uint32_t)f.read() << 16 | (uint32_t)f.read() << 8 | (uint32_t)f.read();
//   uint16_t format  = (uint16_t)f.read() << 8 | (uint16_t)f.read();
//   uint16_t nTracks = (uint16_t)f.read() << 8 | (uint16_t)f.read();
//   uint16_t division= (uint16_t)f.read() << 8 | (uint16_t)f.read();

//   Serial.printf("MIDI: Fmt=%d Trks=%d Div=%d (hdrLen=%lu)\n", format, nTracks, division, (unsigned long)hdrLen);

//   // --- Loop Tracks ---
//   for (int t = 0; t < nTracks; t++) {
//     Serial.printf("Track %d\n", t + 1);

//     if (f.read(hdr, 4) != 4) {
//       Serial.println("Failed to read MTrk header");
//       break;
//     }

//     uint32_t trackLen = (uint32_t)f.read() << 24 | (uint32_t)f.read() << 16 | (uint32_t)f.read() << 8 | (uint32_t)f.read();
//     uint32_t trackEnd = f.position() + trackLen;

//     uint8_t runningStatus = 0;

//     while ((uint32_t)f.position() < trackEnd) {

//       // 🔴 בדיקה אם האפליקציה ביקשה STOP
//       if (!shouldContinuePlaying()) {
//         Serial.println("Stopping playback, clearing LEDs.");
//         pixels.clear();
//         pixels.show();
//         f.close();
//         return;
//       }

//       uint32_t delta = readVLQ(f);

//       // במקום delay(delta) אחד גדול – מחלקים לקטנים ובודקים STOP באמצע
//       if (delta > 0) {
//         uint32_t waited = 0;
//         const uint32_t step = 5; // 5ms
//         while (waited < delta) {
//           delay(step);
//           waited += step;

//           if (!shouldContinuePlaying()) {
//             Serial.println("Stopping during delay, clearing LEDs.");
//             pixels.clear();
//             pixels.show();
//             f.close();
//             return;
//           }
//         }
//       }

//       int peekByte = f.peek();
//       if (peekByte < 0) {
//         Serial.println("EOF inside track");
//         break;
//       }

//       uint8_t status = (uint8_t)peekByte;

//       if (status < 0x80) {
//         status = runningStatus;   // running status
//       } else {
//         f.read();                 // consume byte
//         runningStatus = status;
//       }

//       uint8_t cmd = status & 0xF0;
//       uint8_t ch  = status & 0x0F;

//       if (cmd == 0x90) { // Note On
//         uint8_t note = f.read();
//         uint8_t vel  = f.read();
//         if (vel == 0) handleNoteOff(ch, note, vel);
//         else          handleNoteOn(ch, note, vel);
//       }
//       else if (cmd == 0x80) { // Note Off
//         uint8_t note = f.read();
//         uint8_t vel  = f.read();
//         handleNoteOff(ch, note, vel);
//       }
//       else if (status == 0xFF) { // Meta event
//         uint8_t type = f.read();
//         uint32_t len = readVLQ(f);
//         f.seek(f.position() + len);
//       }
//       else if (status == 0xF0 || status == 0xF7) { // SysEx
//         uint32_t len = readVLQ(f);
//         f.seek(f.position() + len);
//       }
//       else {
//         if (cmd == 0xC0 || cmd == 0xD0) {
//           f.read();          // 1 data byte
//         } else {
//           f.read();          // 2 data bytes
//           f.read();
//         }
//       }
//     }
//   }

//   Serial.println("MIDI playback done.");
//   pixels.clear();
//   pixels.show();
//   f.close();
// }

// // =======================
// // Setup
// // =======================
// void setup() {
//   Serial.begin(115200);
//   delay(1000);

//   // --- WiFi ---
//   Serial.print("Connecting to WiFi");
//   WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
//   while (WiFi.status() != WL_CONNECTED) {
//     delay(300);
//     Serial.print(".");
//   }
//   Serial.println("\n✅ WiFi connected");

//   // --- SD card ---
//   Serial.println("Mounting SD card...");
//   if (!SD.begin(SD_CS_PIN)) {
//     Serial.println("❌ SD.begin failed! Check wiring & CS pin.");
//   } else {
//     Serial.println("✅ SD mounted OK");
//   }

//   // --- LEDs ---
//   pixels.begin();
//   pixels.clear();
//   pixels.show();

//   // --- Firebase config ---
//   config.api_key = API_KEY;
//   config.database_url = DATABASE_URL;

//   auth.user.email = USER_EMAIL;
//   auth.user.password = USER_PASSWORD;

//   config.token_status_callback = tokenStatusCallback;

//   config.fcs.download_buffer_size = 4096;
//   fbdo.setBSSLBufferSize(8192, 2048);
//   fbdo.setResponseSize(64 * 1024);

//   Firebase.begin(&config, &auth);
//   Firebase.reconnectWiFi(true);

//   Serial.println("Setup complete.");
// }

// // =======================
// // Loop
// // =======================
// void loop() {
//   if (!Firebase.ready())
//     return;

//   if (millis() - lastFirebaseCheck < 2000)
//     return;

//   lastFirebaseCheck = millis();

//   // --- 1. read counter ---
//   if (!Firebase.RTDB.getInt(&fbdo, "/esp32API/playCommand/commandsCounter")) {
//     Serial.print("❌ Failed to read commandsCounter: ");
//     Serial.println(fbdo.errorReason());
//     return;
//   }

//   long currentCounter = fbdo.intData();

//   // סינכרון ראשון – לא מנגנים
//   if (lastCounter == -1) {
//     lastCounter = currentCounter;
//     Serial.printf("Initial counter sync: %ld\n", lastCounter);
//     return;
//   }

//   // אין פקודה חדשה
//   if (currentCounter == lastCounter)
//     return;

//   Serial.printf("🔥 Command changed! New counter: %ld\n", currentCounter);
//   lastCounter = currentCounter;

//   // --- 2. read fileToPlay ---
//   if (!Firebase.RTDB.getString(&fbdo, "/esp32API/playCommand/fileToPlay")) {
//     Serial.print("❌ Failed to read fileToPlay: ");
//     Serial.println(fbdo.errorReason());
//     return;
//   }

//   String remoteFile = fbdo.stringData();
//   remoteFile.trim();

//   // --- 3. read status (playing / stopped) ---
//   String status;
//   if (Firebase.RTDB.getString(&fbdo, "/esp32API/playCommand/status")) {
//     status = fbdo.stringData();
//     status.trim();
//     status.toLowerCase();
//   } else {
//     Serial.print("⚠️ Failed to read status: ");
//     Serial.println(fbdo.errorReason());
//     pixels.clear();
//     pixels.show();
//     return;
//   }

//   if (status != "playing") {
//     Serial.print("Status is not 'playing' (");
//     Serial.print(status);
//     Serial.println(") → not starting playback, clearing LEDs.");
//     pixels.clear();
//     pixels.show();
//     return;
//   }

//   if (remoteFile.length() == 0) {
//     Serial.println("⚠️ fileToPlay is empty.");
//     return;
//   }

//   Serial.print("File requested from Storage: ");
//   Serial.println(remoteFile);

//   // --- 4. decide whether to download or use existing file ---
//   String localPath;
//   if (remoteFile == lastRemoteFile) {
//     Serial.println("ℹ️ Same song as last time → using existing file on SD.");
//     String fileName = extractFileName(remoteFile);
//     localPath = "/" + fileName;
//   } else {
//     if (!downloadFileToSD(remoteFile, localPath)) {
//       return; // הורדה נכשלה
//     }
//     lastRemoteFile = remoteFile;
//   }

//   // --- 5. play MIDI from SD ---
//   playMidiFile(localPath);
// }