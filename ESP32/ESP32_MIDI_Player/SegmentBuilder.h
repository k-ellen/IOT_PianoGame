#pragma once
#include <Arduino.h>
#include <stdint.h>

// Keep ONE definition of Segment in the whole project
struct Segment {
  uint64_t startTick;
  uint64_t endTick;   // [start, end), endTick == -1 means "to EOF"
};

// Build segments by musical bars (measures).
// barsPerSegment = how many bars per segment (2, 4, 8 are typical).
// Returns number of segments written into outSegments.
int buildSegmentsByBars(
  const String& midiPath,
  Segment* outSegments,
  int maxSegments,
  int barsPerSegment = 2
);
