// ---------------------------------------------------------------------------
//  StickCore  –  BeanStitchGenerator.h
//
//  Triple Run / Bean Stitch Generator (3-pass or 5-pass) for prominent,
//  raised outlines with a rustic hand-stitched look on Loden, wool, and leather.
// ---------------------------------------------------------------------------
#pragma once

#include "core/StitchTypes.h"
#include <QPainterPath>
#include <QPolygonF>
#include <QVector>

namespace stick {

class BeanStitchGenerator {
public:
    enum class Mode {
        TriplePass,  ///< 3-pass (forward-backward-forward) – standard Bean Stitch
        FivePass     ///< 5-pass – extra heavy, bold relief outline
    };

    struct Params {
        double      stitchLengthMm = 2.50;  ///< Nominal stitch step length along the path (mm)
        Mode        mode           = Mode::TriplePass;
        int         colorIdx       = 0;
        bool        closed         = false; ///< If true, loop back to the first point
        ThreadColor threadColor    = ThreadColor(QColor(40, 40, 40), QStringLiteral("Anthrazit Kontur"));
    };

    /// Converts a polygonal line / contour into a Bean Stitch sequence.
    static StitchSequence generate(const QVector<QPointF>& points, const Params& p);

    /// Overload for QPolygonF.
    static StitchSequence generate(const QPolygonF& poly, const Params& p);

    /// Overload for QPainterPath (handles multiple subpaths and bezier curves).
    static StitchSequence generate(const QPainterPath& path, const Params& p);
};

} // namespace stick
