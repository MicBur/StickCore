// ---------------------------------------------------------------------------
//  StickCore  –  SvgDigitizer.h
//
//  Converts SVG vector paths and files into artistic embroidery stitch sequences
//  using professional digitizing algorithms (Florentine Contour Echo, Royal
//  Tatami Weave, and Two-Tone Haute Couture Gold).
// ---------------------------------------------------------------------------
#pragma once

#include "core/StitchTypes.h"
#include <QPainterPath>
#include <QColor>
#include <QString>

namespace stick {

class SvgDigitizer {
public:
    enum class StitchStyle {
        AuthenticPatchSatin, ///< Real embroidered patch: dense radial satin oval border + inner accent ring + solid raised satin lettering
        ContourEcho,         ///< Concentric distance-field contour echo (Florentine shimmer effect)
        TatamiWeave,         ///< Angle-aligned dense tatami weave with contour edge lock
        RoyalDuotoneGold,    ///< Madeira 1083 (Antique Gold base) + 1070 (Brilliant Gold topstitch)
        FineOutlineRun       ///< Triple-pass bean/stem running stitch
    };

    struct Params {
        StitchStyle style          = StitchStyle::ContourEcho;
        double      targetWidthMm  = 150.0;                 ///< Auto-fit width (150 mm fits Janome Hoop B)
        double      spacingMm      = 0.40;                  ///< Distance between contour rings or tatami rows
        double      maxStitchMm    = 2.40;                  ///< Max running stitch length in mm
        double      tatamiAngleDeg = 15.0;                  ///< Weave angle
        bool        addUnderlay    = true;                  ///< Stabilizing underlay pass
        QColor      primaryColor   = QColor(179, 139, 69);  ///< Atelier Antique Gold (#b38b45 / Madeira 1083)
        QColor      secondaryColor = QColor(218, 178, 85);  ///< Brilliant Gold (#dab255 / Madeira 1070)
    };

    /// Digitize a QPainterPath (in SVG or arbitrary coordinates) into an embroidery sequence.
    static StitchSequence digitize(const QPainterPath& path, const Params& params);
    static StitchSequence digitize(const QPainterPath& path);

    /// Parse an SVG file from disk or resource (':/svg/...') and digitize it.
    static StitchSequence digitizeSvgFile(const QString& filePath, const Params& params);
    static StitchSequence digitizeSvgFile(const QString& filePath);
};

} // namespace stick
