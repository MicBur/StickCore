// ---------------------------------------------------------------------------
//  StickCore  –  MultiHoopSplitter.h
//
//  Embird-style Multi-Hooping & Auto-Split:
//  Splits oversized designs that exceed hoop boundaries into multiple sequential
//  hoopings (Hoop 1 & Hoop 2), inserting high-precision alignment crosshairs
//  (Passkreuze / Registrierstiche) for flawless fabric re-positioning.
// ---------------------------------------------------------------------------
#pragma once

#include "core/StitchTypes.h"
#include <QPointF>
#include <QRectF>
#include <QString>

namespace stick {

class MultiHoopSplitter {
public:
    enum class SplitDirection {
        Auto,       ///< Auto-detect (splits along largest dimension)
        Horizontal, ///< Horizontal seam (Split into Top Hoop & Bottom Hoop)
        Vertical    ///< Vertical seam (Split into Left Hoop & Right Hoop)
    };

    struct Params {
        double         hoopWidthMm     = 140.0; ///< Target hoop width (e.g. 140 mm for standard Hoop B)
        double         hoopHeightMm    = 200.0; ///< Target hoop height (e.g. 200 mm)
        double         overlapMm       = 8.0;   ///< Overlap safety zone between hoopings
        double         crosshairSizeMm = 6.0;   ///< Arm length of registration crosshairs (+)
        SplitDirection direction       = SplitDirection::Auto;
        bool           addCrosshairs   = true;  ///< Insert registration alignment crosshairs
        bool           centerEachHoop  = false; ///< Re-center each part to hoop origin (0, 0)
    };

    struct Result {
        bool           success = false;
        StitchSequence hoop1;
        StitchSequence hoop2;
        QPointF        crosshair1;
        QPointF        crosshair2;
        QString        summary;
    };

    /// Checks whether the design exceeds the specified hoop bounds
    static bool exceedsHoop(const StitchSequence& seq, double hoopW, double hoopH);

    /// Splits a sequence into two sequential multi-hooping parts with registration marks
    static Result split(const StitchSequence& source, const Params& p);

    /// Creates an alignment crosshair (+) at the specified position
    static StitchSequence makeCrosshair(const QPointF& center, double sizeMm, int colorIdx);
};

} // namespace stick
