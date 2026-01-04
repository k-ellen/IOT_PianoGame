#pragma once
#include <Arduino.h>
#include <SD.h>
#include <stdint.h>

#define MAX_TRACKS 8

enum MidiEventType {
  MIDI_NONE,
  MIDI_NOTE_ON,
  MIDI_NOTE_OFF,
  MIDI_TEMPO,
  MIDI_TIME_SIG,
  MIDI_END
};

struct MidiEvent {
  MidiEventType type = MIDI_NONE;
  uint8_t track = 0;
  uint8_t ch = 0;
  uint8_t note = 0;
  uint8_t velocity = 0;
  uint32_t tempoUS = 0;
  uint8_t tsNum = 4;
  uint8_t tsDenPow = 2;
};

struct TrackState {
  uint32_t startPos = 0;
  uint32_t endPos = 0;
  uint32_t curPos = 0;
  uint64_t nextAbsTicks = 0;
  uint8_t runningStatus = 0;
  bool ended = false;
  MidiEvent nextEvent;
};

class MidiParser {
public:
  bool open(const String& path);
  void close();

  // 🔥 NEW
  void rewind();

  bool nextEvent(MidiEvent& out, uint64_t& outTicks);
  uint16_t getDivision() const;

private:
  File file;
  TrackState tracks[MAX_TRACKS];
  uint8_t numTracks = 0;
  uint16_t division = 480;

  bool preloadNext(uint8_t i);

  uint16_t readBE16();
  uint32_t readBE32();
  uint32_t readVLQ();
};
