#pragma once
#include <Arduino.h>
#include <SD.h>

// =======================
// MIDI EVENT TYPES
// =======================

enum MidiEventType : uint8_t {
  MIDI_NONE = 0,
  MIDI_NOTE_ON,
  MIDI_NOTE_OFF,
  MIDI_TEMPO,
  MIDI_END
};

struct MidiEvent {
  MidiEventType type = MIDI_NONE;
  uint8_t note = 0;
  uint8_t velocity = 0;
  uint32_t tempoUS = 0;   // microseconds per quarter note
};

// =======================
// MIDI PARSER CLASS
// =======================

class MidiParser {
public:
  bool open(const String& path);
  bool nextEvent(MidiEvent& outEvent, uint64_t& outAbsTicks);
  uint16_t getDivision() const;
  void close();

private:
  struct TrackState {
    uint32_t startPos = 0;
    uint32_t endPos = 0;
    uint32_t curPos = 0;
    uint64_t nextAbsTicks = 0;
    uint8_t runningStatus = 0;
    bool ended = false;
    MidiEvent nextEvent;
  };

  static const int MAX_TRACKS = 16;

  File file;
  TrackState tracks[MAX_TRACKS];
  uint16_t numTracks = 0;
  uint16_t division = 480;

  bool preloadNext(uint8_t trackIndex);

  uint16_t readBE16();
  uint32_t readBE32();
  uint32_t readVLQ();
};
