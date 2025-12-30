#include <WiFi.h>
#include <SD.h>
#include <SPI.h>
#include <MIDI.h>

#include "SdLock.h"
#include "Config.h"
#include "PlayMode.h"
#include "AudioEngine.h"
#include "LedEngine.h"
#include "Player.h"
#include "FirebaseControl.h"
#include "secrets.h"

// =======================
// MIDI FREE PLAY
// =======================

HardwareSerial MIDI_SERIAL(2);
MIDI_CREATE_INSTANCE(HardwareSerial, MIDI_SERIAL, MIDI);

void handleFreePlayNoteOn(byte ch, byte note, byte vel) {
  if (currentMode != MODE_FREE) return;
  if (note < FIRST_KEY || note > LAST_KEY) return;

  Led_noteOn(note, 0x00FF00);
}

void handleFreePlayNoteOff(byte ch, byte note, byte vel) {
  if (currentMode != MODE_FREE) return;
  if (note < FIRST_KEY || note > LAST_KEY) return;

  Led_noteOff(note);
}


// =======================
// SETUP
// =======================

void setup() {
  Serial.begin(115200);
  delay(1000);

  SdLock_init();

  // ---- WIFI ----
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi connected");

  // ---- SD ----
  SdLock_take();
  bool sdOk = SD.begin(SD_CS_PIN);
  SdLock_give();

  if (!sdOk) Serial.println("❌ SD init failed");
  else Serial.println("✅ SD ready");

  // ---- ENGINES ----
  Audio_init();
  Led_init();

  // ---- FIREBASE ----
  FirebaseControl_init();

  // // ---- DOWNLOAD PIANO SAMPLES (RUN ONCE) ----
  // if (!FirebaseControl_downloadPianoSamples()) {
  //   Serial.println("❌ Piano sample download failed");
  // } else {
  //   Serial.println("🎹 Piano samples ready");
  // }


  // ---- MIDI FREE PLAY ----
  MIDI_SERIAL.begin(31250, SERIAL_8N1, MIDI_RX_PIN, -1);
  MIDI.begin(MIDI_CHANNEL_OMNI);
  MIDI.setHandleNoteOn(handleFreePlayNoteOn);
  MIDI.setHandleNoteOff(handleFreePlayNoteOff);

  Serial.println("🎹 System ready — FREE PLAY mode");
}

// =======================
// LOOP
// =======================

void loop() {
  // Always allow FREE PLAY MIDI
  MIDI.read();

  // Check Firebase for new play command
  String remotePath;
  if (FirebaseControl_checkForPlayCommand(remotePath)) {

    String localPath;
    if (!FirebaseControl_downloadToSD(remotePath, localPath)) {
      Serial.println("❌ Failed to download MIDI file");
      return;
    }

    Serial.println("▶️ Starting song playback");
    Player_playSong(localPath);
    Serial.println("🎹 Returned to FREE PLAY");
  }
}
