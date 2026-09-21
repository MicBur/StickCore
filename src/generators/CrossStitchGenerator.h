// ---------------------------------------------------------------------------
//  StickCore  –  CrossStitchGenerator.h
//
//  Traditional Cross-Stitch (Kreuzstich) Generator:
//  Converts images or bitmaps into authentic counted cross-stitch embroidery
//  aligned to standard Aida fabric grids (11ct, 14ct, 16ct, 18ct).
// ---------------------------------------------------------------------------
#pragma once

#include "core/StitchTypes.h"
#include <QColor>
#include <QImage>
#include <QString>
#include <QVector>

namespace stick {

class CrossStitchGenerator {
public:
    enum class AidaCount {
        Count11, ///< 11 ct (~2.31 mm / stitch) - Groß & rustikal
        Count14, ///< 14 ct (~1.81 mm / stitch) - Standard Aida
        Count16, ///< 16 ct (~1.59 mm / stitch) - Fein
        Count18, ///< 18 ct (~1.41 mm / stitch) - Sehr fein
        Custom   ///< Benutzerdefiniertes Raster
    };

    enum class StitchStyle {
        FullCross,   ///< Klassisches Kreuz (unterer Stich /, oberer Deckstich \)
        DoubleCross, ///< Smyrna- / Sternkreuz (X + + für maximale Plastizität)
        HalfCross    ///< Halbes Kreuz / Petit Point (nur /)
    };

    struct Params {
        AidaCount   aida        = AidaCount::Count14;
        double      customMm    = 1.8;      ///< Cell pitch in mm if AidaCount::Custom
        StitchStyle style       = StitchStyle::FullCross;
        int         maxColors   = 8;        ///< Max quantized palette colors
        bool        skipWhite   = true;     ///< Skip pure white / transparent background
        double      maxJumpMm   = 3.5;      ///< Max stitch jump distance before inserting machine trim
    };

    /// Returns cell pitch in mm for given Aida count
    static double aidaToPitchMm(AidaCount count, double customMm = 1.8);

    /// Generates counted cross-stitch sequence from an image
    static StitchSequence generate(const QImage& source, const Params& p);

    static QString aidaName(AidaCount c);
};

} // namespace stick
