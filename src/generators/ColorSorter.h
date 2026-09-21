// ---------------------------------------------------------------------------
//  StickCore  –  ColorSorter.h
//
//  Smart Color Sort (Intelligente Farb-Zusammenfassung):
//  Optimizes thread color sequences by grouping disjoint blocks of the same
//  color while strictly respecting 2D layer stacking order (no overlapping
//  under/over conflicts).
// ---------------------------------------------------------------------------
#pragma once

#include "core/StitchTypes.h"
#include <vector>
#include <QString>

namespace stick {

class ColorSorter {
public:
    struct Stats {
        int colorChangesBefore = 0;
        int colorChangesAfter  = 0;
        int blocksBefore       = 0;
        int blocksAfter        = 0;
        double savedPercent    = 0.0;
        QString summary;
    };

    /// Optimizes the stitch sequence in-place or returns an optimized copy.
    /// Preserves design visual appearance 100% while minimizing thread changes.
    static StitchSequence sort(const StitchSequence& in, Stats* stats = nullptr);

    /// Checks whether color sorting would save any thread changes on this sequence.
    static bool canOptimize(const StitchSequence& in);
};

} // namespace stick
