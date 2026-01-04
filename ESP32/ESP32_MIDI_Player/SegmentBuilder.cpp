#include "SegmentBuilder.h"
#include "MidiParser.h"

// Compute ticks-per-bar
static uint64_t ticksPerBar(uint16_t division, uint8_t num, uint8_t denPow) {
  uint32_t denom = 1U << denPow;
  return (uint64_t)num * ((uint64_t)division * 4ULL) / denom;
}

int buildSegmentsByBars(
  MidiParser& midi,
  Segment* outSegments,
  int maxSegments,
  int barsPerSegment
) {
  if (!outSegments || maxSegments <= 0) return 0;
  if (barsPerSegment < 1) barsPerSegment = 1;

  // 🔥 RESET parser instead of reopening file
  midi.rewind();

  uint8_t tsNum = 4;
  uint8_t tsDenPow = 2;

  uint16_t division = midi.getDivision();
  uint64_t tpBar = ticksPerBar(division, tsNum, tsDenPow);
  if (!tpBar) tpBar = (uint64_t)division * 4ULL;

  int segCount = 0;
  outSegments[0].startTick = 0;
  outSegments[0].endTick = (uint64_t)-1;

  uint64_t lastCutBar = 0;
  uint64_t absTicks = 0;

  MidiEvent ev;
  uint64_t evTicks = 0;

  while (midi.nextEvent(ev, evTicks)) {
    absTicks = evTicks;

    if (ev.type == MIDI_TIME_SIG) {
      tsNum = ev.tsNum;
      tsDenPow = ev.tsDenPow;
      tpBar = ticksPerBar(division, tsNum, tsDenPow);
      if (!tpBar) tpBar = (uint64_t)division * 4ULL;
    }

    if (ev.type == MIDI_END) break;

    uint64_t currentBar = absTicks / tpBar;

    if ((currentBar - lastCutBar) >= (uint64_t)barsPerSegment) {
      outSegments[segCount].endTick = absTicks;
      segCount++;

      if (segCount >= maxSegments) break;

      outSegments[segCount].startTick = absTicks;
      outSegments[segCount].endTick = (uint64_t)-1;
      lastCutBar = currentBar;
    }
  }

  return segCount + 1;
}
