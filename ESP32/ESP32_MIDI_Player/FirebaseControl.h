#pragma once
#include <Arduino.h>

void FirebaseControl_init();
bool FirebaseControl_checkForPlayCommand(String &outRemotePath);
bool FirebaseControl_downloadToSD(const String &remotePath,
                                  String &outLocalPath);
