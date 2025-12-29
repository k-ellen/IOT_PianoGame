#include "FirebaseControl.h"

#include <Arduino.h>
#include <WiFi.h>
#include <SD.h>
#include <Firebase_ESP_Client.h>

#include "PlayMode.h"
#include "secrets.h"
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

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
  if (millis() - lastCheck < 2000) return false;

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

  // 🔴 STOP HANDLING
  if (status != "playing") {
    stopRequested = true;
    return false;
  }

  stopRequested = false;

  if (!Firebase.RTDB.getString(&fbdo, "/esp32API/playCommand/fileToPlay"))
    return false;

  outRemotePath = fbdo.stringData();
  outRemotePath.trim();
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