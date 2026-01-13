#include "FirebaseControl.h"

#include <Arduino.h>
#include <WiFi.h>
#include <SD.h>
#include <Firebase_ESP_Client.h>

#include "PlayMode.h"
#include "secrets.h"
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

#include "Player.h"

static FirebaseData fbdo;
static FirebaseAuth auth;
static FirebaseConfig config;

static unsigned long lastCheck = 0;
static long lastCounter = -1;

void FirebaseControl_init() {
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
}

bool FirebaseControl_checkForPlayCommand(String &outRemotePath) {
  if (!Firebase.ready()) return false;
   if (millis() - lastCheck < 300) return false;

  lastCheck = millis();

  if (!Firebase.RTDB.getInt(&fbdo, "/esp32API/playCommand/commandsCounter"))
    return false;

  long currentCounter = fbdo.intData();

  if (lastCounter == -1) {
    lastCounter = currentCounter;
    return false;
  }

  if (currentCounter == lastCounter)
    return false;

  lastCounter = currentCounter;

  if (!Firebase.RTDB.getString(&fbdo, "/esp32API/playCommand/status"))
    return false;

  String status = fbdo.stringData();
  status.trim();
  status.toLowerCase();

  // stop handling
  if (status != "playing") {
    stopRequested = true;
    return false;
  }

  stopRequested = false;

  if (!Firebase.RTDB.getString(&fbdo, "/esp32API/playCommand/fileToPlay"))
    return false;

  outRemotePath = fbdo.stringData();
  outRemotePath.trim();

  if (Firebase.RTDB.getFloat(&fbdo, "/esp32API/playCommand/speed")) {
     float s = fbdo.floatData();
     // Limit speed range (0.1x to 2.0x)
     if (s >= 0.1 && s <= 2.0) playbackSpeed = s;
     else playbackSpeed = 1.0; 
  } else {
     playbackSpeed = 1.0;
  }
  
  // GET PLAY MODE (0 = Memorize/Interactive, 1 = Follow/Visual)
  if (Firebase.RTDB.getInt(&fbdo, "/esp32API/playCommand/playMode")) {
    int mode = fbdo.intData();

    switch (mode) {
      case 0:
        currentMode = MODE_FOLLOW;
        break;

      case 2:
        currentMode = MODE_SIMON;   // ✅ NEW
        break;

      case 1:
      default:
        currentMode = MODE_LEARN;
        break;
    }
  } else {
    currentMode = MODE_LEARN; // Default
  }

  if (Firebase.RTDB.getBool(&fbdo, "/esp32API/playCommand/metronome")) {
      bool metaOn = fbdo.boolData();
      Player_setMetronome(metaOn);
  } else {
      // Default to OFF if missing, or ON if you prefer
      Player_setMetronome(false); 
  }

  if (Firebase.RTDB.getInt(&fbdo, "/esp32API/playCommand/segments")) {
    int bars = fbdo.intData();
    Player_setMemorizeBars(bars);
  }

  return true;
}

bool FirebaseControl_downloadToSD(const String &remotePath,
                                 String &outLocalPath) {
  String fileName = remotePath.substring(remotePath.lastIndexOf('/') + 1);
  outLocalPath = "/" + fileName;

  if (SD.exists(outLocalPath)) SD.remove(outLocalPath);

  return Firebase.Storage.download(
      &fbdo,
      STORAGE_BUCKET_ID,
      remotePath.c_str(),
      outLocalPath.c_str(),
      mem_storage_type_sd
  );
}

void FirebaseControl_checkStop() {
  static unsigned long lastStopCheck = 0;
  
  // Only check every 500ms to avoid audio stutter
  if (millis() - lastStopCheck < 500) return;
  lastStopCheck = millis();

  if (!Firebase.ready()) return;

  // Check the status node directly
  if (Firebase.RTDB.getString(&fbdo, "/esp32API/playCommand/status")) {
    String status = fbdo.stringData();
    status.trim();
    status.toLowerCase();

    // If status changed to anything other than "playing", STOP!
    if (status != "playing") {
      stopRequested = true;
      Serial.println("🛑 Stop command detected!");
    }
  }
}

void FirebaseControl_setStatus(const String &status) {
  if (Firebase.ready()) {
    // Write to the same node the App listens to
    Firebase.RTDB.setString(&fbdo, "/esp32API/playCommand/status", status);
  }
}

void FirebaseControl_setStarted(bool v) {
    if (Firebase.ready()) {
    Firebase.RTDB.setBool(&fbdo, "/esp32API/playCommand/started", v);
  }
}

void FirebaseControl_setStatusMessage(const String& msg) {
  if (Firebase.ready()) {
    Firebase.RTDB.setString(&fbdo, "/esp32API/playCommand/statusMessage", msg);
  }
}