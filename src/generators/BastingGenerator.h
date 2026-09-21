// ---------------------------------------------------------------------------
//  StickCore  –  BastingGenerator.h
//
//  Heftrahmen / Basting Box Generator:
//  Stitches a loose perimeter with long running stitches (5–10 mm) before
//  the design starts, securely fixing fabric to stabilizer and enabling
//  "floating" of delicate materials (velvet, leather, loden) without hoop marks.
// ---------------------------------------------------------------------------
#pragma once

#include "core/StitchTypes.h"
#include <QColor>

namespace stick {

class BastingGenerator {
public:
    enum class Mode {
        MotifBounds,     ///< Rechteck um das Motiv mit einstellbarem Abstand
        HoopBounds,      ///< Rechteck entlang der maximalen Rahmenfläche
        ContourSilhouette///< Weite Hüllkurve um die Motivkontur
    };

    struct Params {
        Mode    mode            = Mode::MotifBounds;
        double  stitchLengthMm  = 6.0;   ///< Weite Heftstich-Länge (leicht ausziehbar)
        double  marginMm        = 4.0;   ///< Randabstand zum Motiv / Rahmen
        bool    doublePass      = false; ///< Zweimal umrunden für festen Halt
        bool    addStopAfter    = true;  ///< Maschinenstopp nach dem Heften
        QColor  bastingColor    = QColor(255, 255, 255); ///< Kontrast-Heftfaden (Weiß)
    };

    using Target = Mode;

    /// Generates pure basting stitches for the given sequence and hoop.
    static StitchSequence generate(const StitchSequence& baseSeq,
                                  const Params& p,
                                  HoopType hoop = HoopType::HoopB_140x200);

    /// Prepends basting stitches directly to the target sequence and inserts
    /// a stop/color-change before the main design begins.
    static bool prependTo(StitchSequence& targetSeq,
                          const Params& p,
                          HoopType hoop = HoopType::HoopB_140x200);

    /// Convenience overload returning new sequence with basting prepended.
    static StitchSequence prependTo(const StitchSequence& targetSeq,
                                   const Params& p,
                                   double hoopWidthMm, double hoopHeightMm);
};

} // namespace stick
