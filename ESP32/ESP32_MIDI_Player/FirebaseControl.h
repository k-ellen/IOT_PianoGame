#pragma once
#include <Arduino.h>

// Init Firebase (config + auth + begin)
void FirebaseControl_init();

// Returns true only when a NEW play command is detected (counter changed)
// and status == "playing". Also sets stopRequested=true when status != "playing".
bool FirebaseControl_checkForPlayCommand(String &outRemotePath);

// Download a file from Firebase Storage to SD, returns local path in outLocalPath
bool FirebaseControl_downloadToSD(const String &remotePath, String &outLocalPath);

// ✅ NEW: Poll only the "status" field periodically while a song is playing.
// If status != "playing" -> sets stopRequested=true
bool FirebaseControl_pollStopFlag();

bool FirebaseControl_downloadPianoSamples();
