#include "MidiParser.h"
#include "SdLock.h"
#include <string.h>

// =======================
// LOW LEVEL READERS (each locks briefly)
// =======================

uint16_t MidiParser::readBE16() {
  SdGuard g;
  int a = file.read();
  int b = file.read();
  if (a < 0) a = 0;
  if (b < 0) b = 0;
  return ((uint16_t)a << 8) | (uint16_t)b;
}

uint32_t MidiParser::readBE32() {
  SdGuard g;
  int a = file.read();
  int b = file.read();
  int c = file.read();
  int d = file.read();
  if (a < 0) a = 0;
  if (b < 0) b = 0;
  if (c < 0) c = 0;
  if (d < 0) d = 0;
  return ((uint32_t)a << 24) |
         ((uint32_t)b << 16) |
         ((uint32_t)c << 8)  |
         (uint32_t)d;
}

uint32_t MidiParser::readVLQ() {
  uint32_t v = 0;
  int c;
  do {
    SdGuard g;
    c = file.read();
    if (c < 0) c = 0;
    v = (v << 7) | (uint32_t)(c & 0x7F);
  } while (c & 0x80);
  return v;
}

// =======================
// OPEN MIDI (NO NESTED LOCKS)
// =======================

bool MidiParser::open(const String& path) {
  // Close any previous file
  close();

  {
    SdGuard g;
    file = SD.open(path.c_str(), FILE_READ);
  }
  if (!file) return false;

  char hdr[4];
  {
    SdGuard g;
    if (file.read((uint8_t*)hdr, 4) != 4) {
      file.close();
      return false;
    }
  }
  if (memcmp(hdr, "MThd", 4) != 0) {
    close();
    return false;
  }

  (void)readBE32();                 // header length
  (void)readBE16();                 // format
  uint16_t nt = readBE16();         // numTracks
  division = readBE16();
  if (division == 0) division = 480;

  if (nt > MAX_TRACKS) nt = MAX_TRACKS;
  numTracks = (uint8_t)nt;

  for (int i = 0; i < numTracks; i++) {
    {
      SdGuard g;
      if (file.read((uint8_t*)hdr, 4) != 4) { close(); return false; } // "MTrk"
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
// REWIND (RESET TRACKS)
// =======================

void MidiParser::rewind() {
  for (int i = 0; i < numTracks; i++) {
    tracks[i].curPos = tracks[i].startPos;
    tracks[i].nextAbsTicks = 0;
    tracks[i].runningStatus = 0;
    tracks[i].ended = false;
    preloadNext((uint8_t)i);
  }
}

// =======================
// PRELOAD NEXT EVENT PER TRACK (LOCK RAW SD CALLS ONLY)
// =======================

bool MidiParser::preloadNext(uint8_t i) {
  TrackState& tr = tracks[i];
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

  int peekVal;
  {
    SdGuard g;
    peekVal = file.peek();
  }

  uint8_t status;
  if (peekVal < 0x80) {
    status = tr.runningStatus;
  } else {
    SdGuard g;
    int s = file.read();
    if (s < 0) s = 0;
    status = (uint8_t)s;
    tr.runningStatus = status;
  }

  MidiEvent ev{};
  ev.track = i;

  uint8_t cmd = status & 0xF0;
  if (cmd >= 0x80 && cmd <= 0xE0) ev.ch = status & 0x0F;
  else ev.ch = 0;

  if (cmd == 0x90) {
    SdGuard g;
    ev.note = (uint8_t)file.read();
    ev.velocity = (uint8_t)file.read();
    ev.type = (ev.velocity == 0) ? MIDI_NOTE_OFF : MIDI_NOTE_ON;
  }
  else if (cmd == 0x80) {
    SdGuard g;
    ev.note = (uint8_t)file.read();
    ev.velocity = (uint8_t)file.read();
    ev.type = MIDI_NOTE_OFF;
  }
  else if (status == 0xFF) {
    ev.ch = 0;
    uint8_t metaType;
    {
      SdGuard g;
      metaType = (uint8_t)file.read();
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
                   ((uint32_t)file.read() << 8)  |
                   (uint32_t)file.read();
    }
    else if (metaType == 0x58 && metaLen == 4) {
      ev.type = MIDI_TIME_SIG;
      SdGuard g;
      ev.tsNum = (uint8_t)file.read();
      ev.tsDenPow = (uint8_t)file.read();
      file.read(); // cc ignore
      file.read(); // bb ignore
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
