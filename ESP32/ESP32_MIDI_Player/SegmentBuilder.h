#pragma once
#include <stdint.h>

struct Segment {
  uint64_t startTick;
  uint64_t endTick;
};

class MidiParser;

// 🔥 CHANGE: accept MidiParser&, NOT path
int buildSegmentsByBars(
  MidiParser& midi,
  Segment* outSegments,
  int maxSegments,
  int barsPerSegment
);
