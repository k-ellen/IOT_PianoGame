#include <WiFi.h>
#include <SD.h>
#include <SPI.h>
#include <MIDI.h>

#include "Config.h"
#include "PlayMode.h"
#include "AudioEngine.h"
#include "LedEngine.h"
#include "Player.h"
#include "FirebaseControl.h"
#include "secrets.h"

// =======================
// MIDI SERIAL (ESP32)
// =======================
HardwareSerial MIDI_SERIAL(2);
MIDI_CREATE_INSTANCE(HardwareSerial, MIDI_SERIAL, MIDI);

// Allow Player.cpp to poll incoming MIDI while blocking in playback loops
void checkMidi() {
  MIDI.read();
}

// =======================
// REAL-TIME MIDI CALLBACKS
// =======================

static void handleNoteOn(byte channel, byte note, byte velocity) {
  if (note < FIRST_KEY || note > LAST_KEY) return;

  if (currentMode == MODE_LEARN) {
    Player_onNoteOn(note);
  } else if (currentMode == MODE_FREE) {
    // Free play: LED + Synth
    Led_noteOn(note, 0x00B400);          // Green
    Audio_noteOn(note, velocity);
  }
}

static void handleNoteOff(byte channel, byte note, byte velocity) {
  if (note < FIRST_KEY || note > LAST_KEY) return;

  if (currentMode == MODE_LEARN) {
    Player_onNoteOff(note);
  } else if (currentMode == MODE_FREE) {
    Led_noteOff(note);
    Audio_noteOff(note);
  }
}

// =======================
// SETUP
// =======================

static void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("📶 Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("✅ WiFi connected. IP: ");
  Serial.println(WiFi.localIP());
}

static void initSD() {
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("❌ SD init failed");
  } else {
    Serial.println("✅ SD OK");
  }
}

static void initMidiIn() {
  // RX only (TX pin = -1)
  MIDI_SERIAL.begin(31250, SERIAL_8N1, MIDI_RX_PIN, -1);
  MIDI.begin(MIDI_CHANNEL_OMNI);
  MIDI.setHandleNoteOn(handleNoteOn);
  MIDI.setHandleNoteOff(handleNoteOff);
  MIDI.turnThruOff();
  Serial.println("✅ MIDI input ready");
}

void setup() {
  Serial.begin(115200);
  delay(200);

  connectWiFi();
  initSD();

  Led_init();
  Audio_init();
  FirebaseControl_init();
  initMidiIn();

  Serial.println("🎹 Ready (FREE PLAY)");
}

// =======================
// LOOP
// =======================

void loop() {
  // Always keep reading incoming MIDI
  MIDI.read();

  // Check Firebase for a new play command
  String remotePath;
  if (FirebaseControl_checkForPlayCommand(remotePath)) {

    String localPath;
    if (!FirebaseControl_downloadToSD(remotePath, localPath)) {
      Serial.println("❌ Failed to download MIDI file");
      return;
    }

    Serial.print("▶️ Starting song playback: ");
    Serial.println(localPath);

    Player_playSong(localPath);

    Serial.println("🎹 Returned to FREE PLAY");
  }
}
