#include "SegmentBuilder.h"
#include "MidiParser.h"

// Compute ticks-per-bar from division and time signature
// denomPow is MIDI dd where denom = 2^dd
static uint64_t ticksPerBar(uint16_t division, uint8_t num, uint8_t denPow) {
  uint32_t denom = 1U << denPow; // 1,2,4,8...
  // ticks per bar = num * (quarter_note_ticks * 4 / denom)
  return (uint64_t)num * ((uint64_t)division * 4ULL) / (uint64_t)denom;
}

int buildSegmentsByBars(
  const String& midiPath,
  Segment* outSegments,
  int maxSegments,
  int barsPerSegment
) {
  if (!outSegments || maxSegments <= 0) return 0;
  if (barsPerSegment < 1) barsPerSegment = 1;

  MidiParser midi;
  if (!midi.open(midiPath)) {
    return 0;
  }

  // Defaults if no time signature meta event exists
  uint8_t tsNum = 4;
  uint8_t tsDenPow = 2; // 4/4

  uint16_t division = midi.getDivision();
  uint64_t tpBar = ticksPerBar(division, tsNum, tsDenPow);
  if (tpBar == 0) tpBar = (uint64_t)division * 4ULL;

  // =================================================
  // PASS 1: Find first musical NOTE_ON
  // =================================================
  uint64_t firstNoteTick = (uint64_t)-1;

  MidiEvent scanEv;
  uint64_t scanTicks = 0;

  while (midi.nextEvent(scanEv, scanTicks)) {
    if (scanEv.type == MIDI_NOTE_ON && scanEv.velocity > 0) {
      firstNoteTick = scanTicks;
      break;
    }
  }

  midi.close();

  // Reopen for real segmentation pass
  if (!midi.open(midiPath)) {
    return 0;
  }

  // =================================================
  // PASS 2: Build segments aligned to first note bar
  // =================================================

  // Compute first musical bar
  uint64_t firstBar = 0;
  if (firstNoteTick != (uint64_t)-1) {
    firstBar = firstNoteTick / tpBar;
  }

  uint64_t firstSegmentTick = firstBar * tpBar;

  // ================================
  // Segment state
  // ================================
  int segCount = 0;

  outSegments[0].startTick = firstSegmentTick;
  outSegments[0].endTick   = (uint64_t)-1;
  outSegments[0].noteCount = 0;

  uint64_t lastCutBar = firstBar;
  uint64_t absTicks = 0;

  MidiEvent ev;
  uint64_t evTicks = 0;

  // ================================
  // MIDI SCAN LOOP
  // ================================
  while (midi.nextEvent(ev, evTicks)) {
    absTicks = evTicks;

    // Ignore silence before music starts
    if (absTicks < firstSegmentTick) continue;

    // Handle time signature change
    if (ev.type == MIDI_TIME_SIG) {
      tsNum = ev.tsNum;
      tsDenPow = ev.tsDenPow;
      tpBar = ticksPerBar(division, tsNum, tsDenPow);
      if (tpBar == 0) tpBar = (uint64_t)division * 4ULL;
    }

    if (ev.type == MIDI_END) break;

    // Count notes in current segment
    if (ev.type == MIDI_NOTE_ON && ev.velocity > 0) {
      outSegments[segCount].noteCount++;
    }

    // Convert absolute ticks → bar index
    uint64_t currentBar = absTicks / tpBar;

    // Cut when we've advanced barsPerSegment bars
    if ((currentBar - lastCutBar) >= (uint64_t)barsPerSegment) {
      outSegments[segCount].endTick = absTicks;

      segCount++;
      if (segCount >= maxSegments) {
        outSegments[maxSegments - 1].endTick = (uint64_t)-1;
        midi.close();
        return maxSegments;
      }

      outSegments[segCount].startTick = absTicks;
      outSegments[segCount].endTick   = (uint64_t)-1;
      outSegments[segCount].noteCount = 0;

      lastCutBar = currentBar;
    }
  }

  midi.close();

  int total = segCount + 1;
  if (total < 1) total = 1;
  if (total > maxSegments) total = maxSegments;

  // ================================
  // ✂️ TRIM LEADING EMPTY SEGMENTS
  // (extra safety, should now rarely trigger)
  // ================================
  int firstNonEmpty = -1;

  for (int i = 0; i < total; i++) {
    if (outSegments[i].noteCount > 0) {
      firstNonEmpty = i;
      break;
    }
  }

  if (firstNonEmpty > 0) {
    Serial.printf("✂️ Trimming %d empty intro segments\n", firstNonEmpty);

    int newCount = 0;
    for (int i = firstNonEmpty; i < total; i++) {
      outSegments[newCount++] = outSegments[i];
    }
    total = newCount;
  }

  // // ================================
  // // DEBUG
  // // ================================
  // for (int i = 0; i < total; i++) {
  //   Serial.printf("SEG %d: %llu → %llu | notes=%d\n",
  //     i,
  //     outSegments[i].startTick,
  //     outSegments[i].endTick,
  //     outSegments[i].noteCount);
  // }

  return total;
}
