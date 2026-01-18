#pragma once
#include <Arduino.h>

// Init Firebase (config + auth + begin)
void FirebaseControl_init();

// Returns true only when a NEW play command is detected (counter changed)
// and status == "playing". Also sets stopRequested=true when status != "playing".
bool FirebaseControl_checkForPlayCommand(String &outRemotePath);
bool FirebaseControl_downloadToSD(const String &remotePath,
                                  String &outLocalPath);
void FirebaseControl_checkStop();

void FirebaseControl_setStatus(const String &status);

void FirebaseControl_setStarted(bool started);

void FirebaseControl_reportFailure(const String& reason);
