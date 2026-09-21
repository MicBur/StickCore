// ---------------------------------------------------------------------------
//  StickCore  –  SatinGenerator.h
//
//  Generates an orthogonal satin column between two Bézier rails.
//  See the .cpp for the full algorithm description.
// ---------------------------------------------------------------------------
#pragma once

#include "core/StitchTypes.h"
#include <QPainterPath>

namespace stick {

class SatinGenerator {
public:
    struct Params {
        double densityMm      = 0.40;  ///< Pitch between successive penetrations (mm).
        double pullCompMm     = 0.15;  ///< Widen each rung by this on each side (mm).
        double maxRungMm      = 12.0;  ///< Split a rung wider than this into split-stitches.
        bool   orthogonalize  = true;  ///< Enforce rungs orthogonal to the centreline.
        bool   underlay       = true;  ///< Center/zigzag underlay before the cover satin.
        int    colorIdx       = 0;
    };

    /// Produce a stitch list for the column bounded by railA and railB.
    /// Both rails should run in the *same* direction (start-to-start).
    static StitchSequence generate(const QPainterPath& railA,
                                   const QPainterPath& railB,
                                   double density  = 0.40,
                                   double pullComp  = 0.15);

    static StitchSequence generate(const QPainterPath& railA,
                                   const QPainterPath& railB,
                                   const Params& p);

    /// Produce a satin border / band along a single centerline path of given width.
    static StitchSequence fromCenterline(const QPainterPath& centerPath,
                                         double widthMm,
                                         double pitchMm = 0.40,
                                         int colorIdx = 0);
};

} // namespace stick
