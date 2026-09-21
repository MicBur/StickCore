// ---------------------------------------------------------------------------
//  StickCore  –  AppliqueGenerator.h
//
//  Automated 3-stage appliqué / patch generator:
//   1. Placement Line  – marks where to place the fabric piece.
//   2. Tack-down Line  – fixes fabric in the hoop (with ColorChange stop).
//   3. Satin Cover     – dense satin border finishing and securing edges.
// ---------------------------------------------------------------------------
#pragma once

#include "core/StitchTypes.h"
#include <QPolygonF>
#include <QPainterPath>
#include <QColor>

namespace stick {

struct AppliqueParams {
    enum class TackStyle {
        DoubleRun,  ///< Two running passes
        EStitch,    ///< Blanket/E-stitch
        ZigZag      ///< Narrow zigzag
    };

    double satinWidthMm  = 3.2;   ///< Width of the final cover satin (mm)
    double satinPitchMm  = 0.40;  ///< Density / pitch of the satin cover (mm)
    double runStepMm     = 2.5;   ///< Stitch length for placement and tack (mm)
    TackStyle tackStyle  = TackStyle::ZigZag;
    bool   underlay      = true;  ///< Underlay for the satin border
    QColor placementColor = QColor(100, 100, 100);
    QColor tackColor      = QColor(60, 120, 200);
    QColor coverColor     = QColor(220, 40, 40);
};

class AppliqueGenerator {
public:
    using TackStyle = AppliqueParams::TackStyle;
    using Params = AppliqueParams;

    /// Generate full 3-step applique sequence from a closed polygon or path.
    static StitchSequence generate(const QPolygonF& boundary, const Params& p = Params());
    static StitchSequence generate(const QPainterPath& boundary, const Params& p = Params());
};

} // namespace stick
