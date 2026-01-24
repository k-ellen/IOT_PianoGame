#pragma once

enum PlayMode {
  MODE_FREE = 0,
  MODE_SONG_AUDIO,
  MODE_SONG_METRONOME,
  MODE_LEARN, // 0
  MODE_FOLLOW, // 1
  MODE_SIMON
};

// Current playback mode
extern volatile PlayMode currentMode;

// Stop request (set by Firebase, read by Player)
extern volatile bool stopRequested;

extern float playbackSpeed;