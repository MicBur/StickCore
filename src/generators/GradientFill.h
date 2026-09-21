// ---------------------------------------------------------------------------
//  StickCore  –  GradientFill.h
//
//  Two-color Ombré / Gradient Tatami Fill with progressive density variation,
//  interleaved puncture grids, and automatic SF_ColorChange machine command.
// ---------------------------------------------------------------------------
#pragma once

#include "core/StitchTypes.h"
#include <QPainterPath>
#include <QPolygonF>
#include <QVector>

namespace stick {

class GradientFill {
public:
    enum class Transition {
        Linear,     ///< Constant rate of density variation
        SmoothStep  ///< Sigmoidal S-curve for softer, natural fade
    };

    struct Params {
        double      angleDeg      = 0.0;    ///< Fill & scanline angle in degrees (0° - 360°)
        double      minSpacingMm  = 0.35;   ///< Tight row spacing (mm) at dense end
        double      maxSpacingMm  = 1.80;   ///< Loose row spacing (mm) at sparse end
        double      maxStitchMm   = 4.00;   ///< Maximum stitch length (mm)
        double      phaseFrac     = 0.25;   ///< Stagger puncture offset fraction
        Transition  curve         = Transition::SmoothStep;
        bool        underlay      = true;   ///< Prepend stabilizing edge-run underlay
        ThreadColor color1        = ThreadColor(QColor(212, 175, 55), QStringLiteral("Gold"));
        ThreadColor color2        = ThreadColor(QColor(160, 82, 45), QStringLiteral("Siena / Bronze"));
    };

    /// Generates a two-color gradient fill over the specified polygon region (with optional holes).
    static StitchSequence generate(const QVector<QPolygonF>& region, const Params& p);

    /// Convenience overload for a single boundary without holes.
    static StitchSequence generate(const QPolygonF& outer, const Params& p);

    /// Convenience overload for a QPainterPath.
    static StitchSequence generate(const QPainterPath& path, const Params& p);
};

} // namespace stick
