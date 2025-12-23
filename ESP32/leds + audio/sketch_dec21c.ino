#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <SD.h>
#include <SPI.h>
#include <Adafruit_NeoPixel.h>
#include <MIDI.h>
#include <driver/i2s.h>
#include <math.h>


// Include your secrets file (WiFi/Firebase creds)
#include "secrets.h"
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// =======================
// PINS & HARDWARE
// =======================
#define SD_CS_PIN      5
#define NEO_PIN        14  // Using Pin 14 from your main sketch
#define MIDI_RX_PIN    15  // Using Pin 16 from your test sketch
#define NUMPIXELS      126
#define I2S_BCK   26   // PCM5102A BCK
#define I2S_DOUT  25   // PCM5102A DIN
#define I2S_WS    33   // PCM5102A LRCK

#define SAMPLE_RATE 44100
#define MAX_VOICES 8



// =======================
// OBJECTS
// =======================
Adafruit_NeoPixel pixels(NUMPIXELS, NEO_PIN, NEO_GRB + NEO_KHZ800);

// MIDI on Serial2
HardwareSerial MIDI_SERIAL(2);
MIDI_CREATE_INSTANCE(HardwareSerial, MIDI_SERIAL, MIDI);

// Firebase
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// structs
struct TrackState;
struct Voice;

// =======================
// GLOBAL VARS
// =======================
unsigned long lastFirebaseCheck = 0;
long lastCounter = -1;
String lastRemoteFile = "";
float masterVolume = 0.8f; // 0.0 – 1.0

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

#define MAX_TRACK_COLORS 16
uint32_t trackColors[MAX_TRACK_COLORS] = {
  0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0xFF00FF, 0x00FFFF, 0xFFA500, 0xFFFFFF,
  0x8000FF, 0x0080FF, 0x80FF00, 0xFF0080, 0x808080, 0x00FF80, 0xFF8000, 0x008000  
};


struct Voice {
  bool active = false;
  float phase = 0.0f;
  float freq = 0.0f;
  float velocity = 0.0f;
};

Voice voices[MAX_VOICES];

float noteToFreq(uint8_t note) {
  return 440.0f * powf(2.0f, (note - 69) / 12.0f);
}

void setupI2S() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S, // ok on Arduino-ESP32
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pins = {
    .bck_io_num = I2S_BCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  esp_err_t err;

  err = i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
  if (err != ESP_OK) {
    Serial.printf("❌ i2s_driver_install failed: %d\n", (int)err);
    return;
  }

  err = i2s_set_pin(I2S_NUM_0, &pins);
  if (err != ESP_OK) {
    Serial.printf("❌ i2s_set_pin failed: %d\n", (int)err);
    return;
  }

  err = i2s_set_clk(I2S_NUM_0, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
  if (err != ESP_OK) {
    Serial.printf("❌ i2s_set_clk failed: %d\n", (int)err);
    return;
  }

  i2s_zero_dma_buffer(I2S_NUM_0);
  i2s_start(I2S_NUM_0);

  Serial.println("✅ I2S started");
}

void audioTask(void *) {
  int16_t buffer[256 * 2];

  while (true) {
    for (int i = 0; i < 256; i++) {
      float sample = 0.0f;

      for (int v = 0; v < MAX_VOICES; v++) {
        if (!voices[v].active) continue;

        sample += sinf(voices[v].phase) * voices[v].velocity;
        voices[v].phase += 2.0f * M_PI * voices[v].freq / SAMPLE_RATE;
        if (voices[v].phase > 2.0f * M_PI) voices[v].phase -= 2.0f * M_PI;
      }

      sample = constrain(sample, -1.0f, 1.0f);
      sample *= masterVolume;
      int16_t s = (int16_t)(sample * 12000);


      buffer[i * 2]     = s;
      buffer[i * 2 + 1] = s;
    }

    size_t bytesWritten;
    i2s_write(I2S_NUM_0, buffer, sizeof(buffer), &bytesWritten, portMAX_DELAY);
  }
}

void synthNoteOn(uint8_t note, uint8_t velocity) {
  for (int i = 0; i < MAX_VOICES; i++) {
    if (!voices[i].active) {
      voices[i].active = true;
      voices[i].freq = noteToFreq(note);
      voices[i].velocity = velocity / 127.0f * 0.6f;
      voices[i].phase = 0;
      return;
    }
  }
}

void synthNoteOff(uint8_t note) {
  float f = noteToFreq(note);
  for (int i = 0; i < MAX_VOICES; i++) {
    if (voices[i].active && fabs(voices[i].freq - f) < 1.0f) {
      voices[i].active = false;
    }
  }
}

// =======================
// LED HELPER (Shared)
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

// =======================
// FREE PLAY CALLBACKS
// =======================
// These run ONLY when called by MIDI.read() in the loop
void handleRealTimeNoteOn(byte channel, byte note, byte velocity) {
  if (note >= FIRST_KEY && note <= LAST_KEY) {
    lightLEDsByKey(note, pixels.Color(0, 180, 0)); // Green for live play
  }
}

void handleRealTimeNoteOff(byte channel, byte note, byte velocity) {
  if (note >= FIRST_KEY && note <= LAST_KEY) {
    lightLEDsByKey(note, 0); // Off
  }
}

// =======================
// FILE PLAYBACK HELPERS
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

// Stop check logic
unsigned long lastStopCheck = 0;
bool shouldContinuePlaying() {
  // Check every 200ms
  if (millis() - lastStopCheck < 200) return true;
  lastStopCheck = millis();

  if (!Firebase.ready()) return true;

  if (!Firebase.RTDB.getString(&fbdo, "/esp32API/playCommand/status")) {
    return true; // Ignore errors, keep playing
  }

  String status = fbdo.stringData();
  status.trim();
  status.toLowerCase();
  
  // If status changed to anything other than "playing"
  if (status != "playing") {
    Serial.println("🔴 STOP command received.");
    return false;
  }
  return true;
}

// MIDI FILE READING STRUCTS
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
    // While waiting, we check if we should stop
    if (!shouldContinuePlaying()) return false;
    delayMicroseconds(200);
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
  uint32_t startPos = 0; uint32_t endPos = 0; uint32_t curPos = 0;
  uint8_t runningStatus = 0; bool ended = false;
  uint64_t nextAbsTicks = 0; Event nextEvent;
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
    uint32_t len = readVLQ(f); f.seek(f.position() + len);
  } else {
    if (cmd == 0xC0 || cmd == 0xD0) f.read(); else { f.read(); f.read(); }
  }
  tr.curPos = f.position(); ev.track = trackIndex; tr.nextEvent = ev;
  return !tr.ended;
}

// =======================
// SONG PLAYBACK FUNCTION (Blocking)
// =======================
void playMidiFile(const String &localPath) {
  Serial.println("▶️ Starting merged playback (Free Play Disabled)");
  
  File f = SD.open(localPath.c_str());
  if (!f) { Serial.println("❌ Cannot open MIDI file"); return; }
  
  pixels.clear(); pixels.show();

  // Header parsing
  uint8_t hdr[4];
  if (f.read(hdr, 4) != 4 || memcmp(hdr, "MThd", 4) != 0) { f.close(); return; }
  readBE32(f); readBE16(f); // skip len/fmt
  uint16_t nTracks = readBE16(f);
  uint16_t division = readBE16(f);
  if (division == 0) division = 480;
  
  // Skip extra header
  f.seek(14); 

  if (nTracks > MAX_TRACKS) nTracks = MAX_TRACKS; 
  TrackState tracks[MAX_TRACKS];

  for (uint16_t t = 0; t < nTracks; t++) {
    if (f.read(hdr, 4) != 4 || memcmp(hdr, "MTrk", 4) != 0) { f.close(); return; }
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
    // 1. Check stop condition
    if (!shouldContinuePlaying()) break;

    // 2. Determine next event
    int best = -1;
    uint64_t bestTicks = 0;
    for (uint16_t t = 0; t < nTracks; t++) {
      if (tracks[t].ended) continue;
      if (tracks[t].nextEvent.type == EV_END) continue;
      if (best < 0 || tracks[t].nextAbsTicks < bestTicks) {
        best = (int)t; bestTicks = tracks[t].nextAbsTicks;
      }
    }
    if (best < 0) { Serial.println("✅ MIDI playback done."); break; }

    // 3. Wait for time
    uint64_t deltaTicks = bestTicks - globalTicks;
    if (deltaTicks > 0) {
      uint64_t addUS = (deltaTicks * (uint64_t)tempoUS) / (uint64_t)division;
      globalTimeUS += addUS;
      globalTicks = bestTicks;
      if (!waitUntilMicros(startUS + globalTimeUS)) break;
    }

    // 4. Process Event
    Event ev = tracks[best].nextEvent;
    if (ev.type == EV_NOTE_ON) {
      uint32_t color = trackColors[ev.track % MAX_TRACK_COLORS];
      lightLEDsByKey(ev.note, color);
      synthNoteOn(ev.note, ev.vel);
    } 
    else if (ev.type == EV_NOTE_OFF) {
      lightLEDsByKey(ev.note, 0);
      synthNoteOff(ev.note);
    } 
    else if (ev.type == EV_TEMPO) {
      tempoUS = ev.tempoUS;
    }

    // 5. Load next
    preloadNextEvent(f, tracks[best], best);
  }

  pixels.clear();
  pixels.show();
  f.close();
  Serial.println("⏸️ Returning to Free Play Mode");
}

// =======================
// SETUP
// =======================
void setup() {
  Serial.begin(115200);
  delay(1000);

  // WIFI
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) { delay(300); Serial.print("."); }
  Serial.println("\n✅ WiFi connected");

  // SD
  if (!SD.begin(SD_CS_PIN)) Serial.println("❌ SD Failed");
  else Serial.println("✅ SD OK");

  // NEOPIXEL
  pixels.begin(); pixels.clear(); pixels.show();

  //AUDIO
  setupI2S();
  xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 3, nullptr, 1);

  // FIREBASE
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

  // MIDI SETUP (For Free Play)
  MIDI_SERIAL.begin(31250, SERIAL_8N1, MIDI_RX_PIN, -1);
  MIDI.begin(MIDI_CHANNEL_OMNI);
  MIDI.setHandleNoteOn(handleRealTimeNoteOn);
  MIDI.setHandleNoteOff(handleRealTimeNoteOff);

  Serial.println("System Ready. Mode: FREE PLAY");
}

// =======================
// LOOP
// =======================
void loop() {
  // 1. ALWAYS Run MIDI (Free Play)
  // This will read the keyboard and trigger handleRealTimeNoteOn/Off
  MIDI.read();

  // 2. Check Firebase periodically (Non-blocking)
  if (Firebase.ready() && (millis() - lastFirebaseCheck > 2000)) {
    lastFirebaseCheck = millis();

    // Check Counter
    if (Firebase.RTDB.getInt(&fbdo, "/esp32API/playCommand/commandsCounter")) {
      long currentCounter = fbdo.intData();
      
      // Initialize counter if first run
      if (lastCounter == -1) { lastCounter = currentCounter; return; }

      // If counter changed -> New Command!
      if (currentCounter != lastCounter) {
        lastCounter = currentCounter;

        // Check file name
        String remoteFile = "";
        if (Firebase.RTDB.getString(&fbdo, "/esp32API/playCommand/fileToPlay")) {
          remoteFile = fbdo.stringData();
          remoteFile.trim();
        }

        // Check Status
        String status = "";
        if (Firebase.RTDB.getString(&fbdo, "/esp32API/playCommand/status")) {
          status = fbdo.stringData();
          status.trim(); status.toLowerCase();
        }

        // EXECUTE PLAYBACK
        if (status == "playing" && remoteFile.length() > 0) {
          // Download if new
          String localPath;
          if (remoteFile == lastRemoteFile) {
             String fileName = extractFileName(remoteFile);
             localPath = "/" + fileName;
          } else {
             if (downloadFileToSD(remoteFile, localPath)) {
               lastRemoteFile = remoteFile;
             } else {
               return; // download failed
             }
          }

          // ** BLOCKING CALL ** // While this runs, MIDI.read() is NOT called, so Free Play is disabled.
          playMidiFile(localPath);
        }
      }
    }
  }
}