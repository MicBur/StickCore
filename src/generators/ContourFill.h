// ---------------------------------------------------------------------------
//  StickCore  –  ContourFill.h
//
//  An artistic "echo" / contour fill: instead of straight tatami rows, the
//  stitches follow the SHAPE of the region inward in concentric rings. On
//  letters this catches the light along the strokes — the signature look of
//  fine calligraphic embroidery (cf. a gold metallic logo). Implemented with
//  OpenCV's distance transform; falls back to empty when OpenCV is absent so
//  the caller can use the ordinary fill instead.
// ---------------------------------------------------------------------------
#pragma once

#include "core/StitchTypes.h"
#include <QPainterPath>

namespace stick {

class ContourFill {
public:
    struct Params {
        double spacingMm    = 0.9;   ///< distance between contour rings
        double maxStitchMm  = 3.0;   ///< running-stitch length along a ring
        int    colorIdx     = 0;
        bool   outlineFirst = true;  ///< stitch the crisp outer edge first
    };

    /// `pathMm` is a glyph/region outline already in millimetres (+Y up),
    /// as produced by TextDigitizer::textPath. Returns concentric contour
    /// rings as running stitches. Empty if OpenCV is unavailable or the path
    /// is degenerate.
    static StitchSequence generate(const QPainterPath& pathMm, const Params& p);

    /// True when this fill is actually available (OpenCV compiled in).
    static bool available();
};

} // namespace stick
