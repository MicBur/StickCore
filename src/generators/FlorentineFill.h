// ---------------------------------------------------------------------------
//  StickCore  –  FlorentineFill.h
//
//  Florentine / Curved Wave Tatami Fill: Bends Tatami stitch rows along a
//  dynamic wave function for striking 3-D light refraction on embroidery thread.
// ---------------------------------------------------------------------------
#pragma once

#include "core/StitchTypes.h"
#include <QPainterPath>
#include <QPolygonF>
#include <QVector>

namespace stick {

class FlorentineFill {
public:
    struct Params {
        double      angleDeg       = 0.0;   ///< Overall direction of the wave rows (degrees)
        double      rowSpacingMm   = 0.40;  ///< Distance between wave rows (mm)
        double      maxStitchMm    = 3.80;  ///< Max stitch length along the wave (mm)
        double      amplitudeMm    = 4.50;  ///< Peak amplitude A of the wave curves (mm)
        double      wavelengthMm   = 28.00; ///< Period length λ of the wave (mm)
        double      phaseRad       = 0.0;   ///< Initial phase shift of the wave
        double      phaseFrac      = 0.25;  ///< Puncture stagger fraction between rows
        bool        underlay       = true;  ///< Lay stabilizing edge-run underlay
        int         colorIdx       = 0;
        ThreadColor threadColor    = ThreadColor(QColor(0, 128, 128), QStringLiteral("Smaragd Wellenglanz"));
    };

    /// Generates a Florentine curved Tatami fill over a polygon region (with optional holes).
    static StitchSequence generate(const QVector<QPolygonF>& region, const Params& p);

    /// Convenience overload for a single boundary.
    static StitchSequence generate(const QPolygonF& outer, const Params& p);

    /// Convenience overload for a QPainterPath.
    static StitchSequence generate(const QPainterPath& path, const Params& p);
};

} // namespace stick
