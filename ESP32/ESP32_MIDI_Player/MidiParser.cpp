#include "MidiParser.h"
#include "SdLock.h"
#include <string.h>

// =======================
// LOW-LEVEL READERS
// =======================

uint16_t MidiParser::readBE16() {
  SdGuard g;
  return ((uint16_t)file.read() << 8) | file.read();
}

uint32_t MidiParser::readBE32() {
  SdGuard g;
  return ((uint32_t)file.read() << 24) |
         ((uint32_t)file.read() << 16) |
         ((uint32_t)file.read() << 8) |
         file.read();
}

uint32_t MidiParser::readVLQ() {
  uint32_t v = 0;
  int c;
  do {
    SdGuard g;
    c = file.read();
    v = (v << 7) | (c & 0x7F);
  } while (c & 0x80);
  return v;
}

// =======================
// OPEN MIDI FILE
// =======================

bool MidiParser::open(const String& path) {
  {
    SdGuard g;
    file = SD.open(path.c_str(), FILE_READ);
  }
  if (!file) return false;

  char hdr[4];
  {
    SdGuard g;
    file.read((uint8_t*)hdr, 4);
  }
  if (memcmp(hdr, "MThd", 4) != 0) return false;

  readBE32();            // header length
  readBE16();            // format
  numTracks = (uint8_t)readBE16();
  division = readBE16();
  if (division == 0) division = 480;

  if (numTracks > MAX_TRACKS) numTracks = MAX_TRACKS;

  for (int i = 0; i < numTracks; i++) {
    {
      SdGuard g;
      file.read((uint8_t*)hdr, 4); // MTrk
    }
    uint32_t len = readBE32();

    {
      SdGuard g;
      tracks[i].startPos = file.position();
    }
    tracks[i].endPos = tracks[i].startPos + len;
    tracks[i].curPos = tracks[i].startPos;
    tracks[i].nextAbsTicks = 0;
    tracks[i].runningStatus = 0;
    tracks[i].ended = false;

    preloadNext((uint8_t)i);

    {
      SdGuard g;
      file.seek(tracks[i].endPos);
    }
  }

  return true;
}

// =======================
// PRELOAD NEXT EVENT PER TRACK
// =======================

bool MidiParser::preloadNext(uint8_t i) {
  TrackState &tr = tracks[i];
  if (tr.ended || tr.curPos >= tr.endPos) {
    tr.ended = true;
    tr.nextEvent.type = MIDI_END;
    return false;
  }

  {
    SdGuard g;
    file.seek(tr.curPos);
  }
  tr.nextAbsTicks += readVLQ();

  uint8_t status;
  int peek;
  {
    SdGuard g;
    peek = file.peek();
  }

  if (peek < 0x80) {
    status = tr.runningStatus;
  } else {
    SdGuard g;
    status = file.read();
    tr.runningStatus = status;
  }

  MidiEvent ev{};
  ev.track = i;

  uint8_t cmd = status & 0xF0;
  if (cmd >= 0x80 && cmd <= 0xE0) ev.ch = status & 0x0F;
  else ev.ch = 0;

  if (cmd == 0x90) {
    SdGuard g;
    ev.note = file.read();
    ev.velocity = file.read();
    ev.type = (ev.velocity == 0) ? MIDI_NOTE_OFF : MIDI_NOTE_ON;
  }
  else if (cmd == 0x80) {
    SdGuard g;
    ev.note = file.read();
    ev.velocity = file.read();
    ev.type = MIDI_NOTE_OFF;
  }
  else if (status == 0xFF) {
    ev.ch = 0;

    uint8_t metaType;
    {
      SdGuard g;
      metaType = file.read();
    }
    uint32_t metaLen = readVLQ();

    if (metaType == 0x2F) {
      ev.type = MIDI_END;
      tr.ended = true;
    }
    else if (metaType == 0x51 && metaLen == 3) {
      ev.type = MIDI_TEMPO;
      SdGuard g;
      ev.tempoUS = ((uint32_t)file.read() << 16) |
                   ((uint32_t)file.read() << 8) |
                   file.read();
    }
    else if (metaType == 0x58 && metaLen == 4) {
      // ✅ Time Signature: nn dd cc bb
      ev.type = MIDI_TIME_SIG;
      SdGuard g;
      ev.tsNum = file.read();     // nn
      ev.tsDenPow = file.read();  // dd (power of 2)
      file.read();                // cc (ignore)
      file.read();                // bb (ignore)
    }
    else {
      SdGuard g;
      file.seek(file.position() + metaLen);
      ev.type = MIDI_NONE;
    }
  }
  else {
    // Skip unsupported messages
    SdGuard g;
    if (cmd == 0xC0 || cmd == 0xD0) file.read();
    else { file.read(); file.read(); }
    ev.type = MIDI_NONE;
  }

  {
    SdGuard g;
    tr.curPos = file.position();
  }
  tr.nextEvent = ev;
  return true;
}

// =======================
// GET NEXT MERGED EVENT
// =======================

bool MidiParser::nextEvent(MidiEvent& out, uint64_t& outTicks) {
  int best = -1;
  uint64_t bestTicks = 0;

  for (int i = 0; i < numTracks; i++) {
    if (tracks[i].ended) continue;
    if (best < 0 || tracks[i].nextAbsTicks < bestTicks) {
      best = i;
      bestTicks = tracks[i].nextAbsTicks;
    }
  }

  if (best < 0) return false;

  out = tracks[best].nextEvent;
  outTicks = tracks[best].nextAbsTicks;

  preloadNext((uint8_t)best);
  return true;
}

uint16_t MidiParser::getDivision() const {
  return division;
}

void MidiParser::close() {
  SdGuard g;
  if (file) file.close();
}
