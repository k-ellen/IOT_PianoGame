#pragma once

#include <Arduino.h>
#include <stdint.h>

struct Segment {
  uint64_t startTick;
  uint64_t endTick;
  int noteCount;        // 👈 NEW: how many notes inside this segment
};

int buildSegmentsByBars(
  const String& midiPath,
  Segment* outSegments,
  int maxSegments,
  int barsPerSegment
);
