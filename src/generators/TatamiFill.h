// ---------------------------------------------------------------------------
//  StickCore  –  TatamiFill.h
//
//  Tatami (scan-line) fill for an arbitrary closed region with holes.
// ---------------------------------------------------------------------------
#pragma once

#include "core/StitchTypes.h"
#include <QPolygonF>
#include <QVector>

namespace stick {

class TatamiFill {
public:
    enum class PatternType {
        StandardTatami, ///< 1/4 Versatz (0.25) – klassische glatte Stickfüllung
        Brick,          ///< 1/2 Versatz (0.50) – Ziegelverbund / Mauerwerk
        Twill,          ///< 1/3 Versatz (0.33) – Diagonalköper mit Seidenglanz
        Basketweave,    ///< Korbgeflecht – plastische Textur
        Honeycomb,      ///< Waben- & Gitternetzwerk (Doppelkreuz)
        ContourEcho     ///< Konturfüllung / Wellen (konzentrisch)
    };

    static QString patternName(PatternType p);

    struct Params {
        double      fillAngleDeg = 0.0;   ///< Fill direction θ (0° - 360°).
        double      rowSpacingMm = 0.40;  ///< Distance between scan rows S.
        double      maxStitchMm  = 4.00;  ///< Maximum stitch length L_max.
        double      phaseFrac    = 0.25;  ///< Per-row puncture phase shift (× L_max).
        PatternType pattern      = PatternType::StandardTatami; ///< Pattern style
        bool        underlay     = true;  ///< Lay underlay (edge + cross) before the fill.
        int         colorIdx     = 0;
    };

    /// region[0] is the outer boundary; region[1..] are holes.
    /// Returns absolute-coordinate stitches (mm) in boustrophedon order.
    static StitchSequence generate(const QVector<QPolygonF>& region,
                                   const Params& p);

    /// Convenience overload for a single boundary without holes.
    static StitchSequence generate(const QPolygonF& outer, const Params& p);

    /// The pure scan-line cover pass (no underlay). Exposed so the underlay
    /// module can reuse it for a sparse cross layer.
    static StitchSequence fillOnly(const QVector<QPolygonF>& region,
                                   double angleDeg, double rowSpacingMm,
                                   double maxStitchMm, double phaseFrac, int colorIdx);
};

} // namespace stick
