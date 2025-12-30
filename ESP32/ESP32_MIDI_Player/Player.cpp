#include "Player.h"

#include "Config.h"
#include "PlayMode.h"
#include "AudioEngine.h"
#include "LedEngine.h"
#include "MidiParser.h"
#include "FirebaseControl.h"

static void playOnce(const String &path, PlayMode mode) {
  currentMode = mode;
  Audio_allNotesOff();

  MidiParser midi;
  if (!midi.open(path)) return;

  uint16_t division = midi.getDivision();
  uint32_t tempoUS = 500000;

  uint64_t globalTicks = 0;
  uint64_t globalTimeUS = 0;
  uint64_t startUS = micros();
  uint64_t lastBeat = 0;

  MidiEvent ev;
  uint64_t eventTicks;

  while (!stopRequested && midi.nextEvent(ev, eventTicks)) {

    uint64_t deltaTicks = eventTicks - globalTicks;
    if (deltaTicks > 0) {
      uint64_t addUS = (deltaTicks * tempoUS) / division;
      globalTimeUS += addUS;
      globalTicks = eventTicks;

      while (!stopRequested &&
             (int64_t)(startUS + globalTimeUS - micros()) > 0) {
        FirebaseControl_pollStopFlag();
        delayMicroseconds(200);
      }

      if (mode == MODE_SONG_METRONOME) {
        uint64_t beat = globalTicks / division;
        if (beat != lastBeat) {
          lastBeat = beat;
          Audio_triggerMetronome();
        }
      }
    }

    if (stopRequested) break;

    if (ev.type == MIDI_NOTE_ON) {
      Led_noteOn(ev.note, 0x00FF00);
      if (mode == MODE_SONG_AUDIO)
        Audio_noteOn(ev.note, ev.velocity);
    }
    else if (ev.type == MIDI_NOTE_OFF) {
      Led_noteOff(ev.note);
      if (mode == MODE_SONG_AUDIO)
        Audio_noteOff(ev.note);
    }
    else if (ev.type == MIDI_TEMPO) {
      tempoUS = ev.tempoUS;
    }
  }

  midi.close();

  // ✅ Always clean up on exit (end or stop)
  Audio_allNotesOff();
  Led_clearAll();
}

void Player_playSong(const String &path) {
  playOnce(path, MODE_SONG_AUDIO);

  if (stopRequested) {
    // ✅ Immediate cleanup when stop pressed
    Audio_allNotesOff();
    Led_clearAll();
    currentMode = MODE_FREE;
    return;
  }

  playOnce(path, MODE_SONG_METRONOME);

  Audio_allNotesOff();
  Led_clearAll();
  currentMode = MODE_FREE;
}