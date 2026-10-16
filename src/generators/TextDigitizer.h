// ---------------------------------------------------------------------------
//  StickCore  –  TextDigitizer.h
//
//  Turns a text string into embroidery stitches. The "raised" effect fills the
//  glyph bodies with tatami first and then lays a satin band along every glyph
//  contour — the classic embossed / raised lettering look ("vorher gefüllt,
//  dann erhabener Satin-Rand").
// ---------------------------------------------------------------------------
#pragma once

#include "core/StitchTypes.h"
#include "generators/TatamiFill.h"
#include <QPainterPath>
#include <QString>
#include <QStringList>

namespace stick {

class TextDigitizer {
public:
    enum class Baseline {
        Straight,       ///< Classic horizontal baseline
        ArcUp,          ///< Arched upward over top of circle (convex)
        ArcDown,        ///< Arched downward like a smile curve (concave)
        Circle,         ///< Placed around full circle
        Wave            ///< Harmonious wave / S-curve baseline
    };

    enum class StitchStyle {
        Raised,         ///< Tatami fill + embossed satin border (default)
        SatinOutline,   ///< Pure satin band contour
        PaddedSatin,    ///< 3D-Relief: Dual-pass padding underlay (2.4mm) + cover satin (3.5mm)
        TatamiOnly,     ///< Dense tatami weave fill only
        BeanStitch,     ///< 3-pass bean stitch contour (plastic triple run)
        RunStitch       ///< Delicate running stitch contour
    };

    struct Params {
        QString text        = QStringLiteral("Text");
        QString family      = QStringLiteral("DejaVu Sans");
        QStringList families;         ///< preferred families (first available wins);
                                      ///< overrides serif/family when non-empty
        bool    serif       = false;  ///< prefer an elegant serif face
        bool    italic      = false;
        double  heightMm    = 18.0;   ///< cap height of the text block
        bool    bold        = true;
        double  densityMm   = 0.40;   ///< tatami row spacing
        double  maxStitchMm = 4.0;    ///< max stitch length inside the fill
        bool    underlay    = true;   ///< lay underlay before the fill
        double  fillAngleDeg= 45.0;
        TatamiFill::PatternType pattern = TatamiFill::PatternType::StandardTatami;
        bool    raised      = true;   ///< add satin border on top of the fill
        double  borderWidthMm = 3.5;  ///< width of satin border / cover satin
        double  borderPitchMm = 0.34; ///< stitch pitch of satin border / cover
        double  paddingWidthMm = 2.4; ///< width of 3D relief underlay padding (Pass 1)
        double  paddingPitchMm = 0.48;///< stitch pitch of 3D relief padding (Pass 1)
        int     colorIdx    = 0;

        // Janome Digitizer Jr Lettering parameters
        Baseline    baseline        = Baseline::Straight;
        double      arcRadiusMm     = 50.0;   ///< radius for ArcUp / ArcDown / Circle (mm)
        double      letterSpacingMm = 0.0;    ///< kerning / extra space between letters (mm)
        double      slantDeg        = 0.0;    ///< slant angle in degrees (-30 to +30)
        StitchStyle style           = StitchStyle::Raised;
    };

    /// Glyph outline path in mm space (+Y up), centred on the origin.
    static QPainterPath textPath(const Params& p);

    /// Full stitch sequence (fill + optional raised satin border).
    static StitchSequence generate(const Params& p);
};

} // namespace stick
