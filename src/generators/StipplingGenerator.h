// ---------------------------------------------------------------------------
//  StickCore  –  StipplingGenerator.h
//
//  Continuous Stippling / Meander Fill for quilting, trapunto backgrounds,
//  and cottage-style apparel. Produces a non-intersecting, fluid meandering
//  path without jumps or trims.
// ---------------------------------------------------------------------------
#pragma once

#include "core/StitchTypes.h"
#include <QPainterPath>
#include <QPolygonF>
#include <QVector>

namespace stick {

class StipplingGenerator {
public:
    struct Params {
        double      loopSpacingMm  = 4.00;  ///< Average distance between meander loops (mm)
        double      stitchLengthMm = 2.20;  ///< Running-stitch resolution along the meander curve (mm)
        double      marginMm       = 1.50;  ///< Inset margin from the boundary edge (mm)
        int         colorIdx       = 0;
        ThreadColor threadColor    = ThreadColor(QColor(100, 149, 237), QStringLiteral("Kornblumenblau Quilt"));
    };

    /// Generates a continuous stippling fill inside a closed polygon (with optional holes).
    static StitchSequence generate(const QVector<QPolygonF>& region, const Params& p);

    /// Convenience overload for a single outer polygon.
    static StitchSequence generate(const QPolygonF& outer, const Params& p);

    /// Convenience overload for a QPainterPath.
    static StitchSequence generate(const QPainterPath& path, const Params& p);
};

} // namespace stick
