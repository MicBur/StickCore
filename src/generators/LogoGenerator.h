// ---------------------------------------------------------------------------
//  StickCore  –  LogoGenerator.h
//
//  Composes multi-line calligraphic badge logos: a double oval border with
//  arched top/bottom lines and centred lines, each stitched with the artistic
//  contour fill. Built for Daniela's atelier badge ("Modewerkstatt Knüppel"),
//  but the builder is parameterised so other badges can reuse it.
// ---------------------------------------------------------------------------
#pragma once

#include "core/StitchTypes.h"
#include <QColor>
#include <QString>

namespace stick {

class LogoGenerator {
public:
    struct Badge {
        QString topArc;      ///< arched along the top (e.g. "Meisterbetrieb")
        QString centerBig;   ///< large centre line (e.g. "Modewerkstatt")
        QString centerSmall; ///< line just below centre (e.g. "Knüppel")
        QString bottomArc;   ///< arched along the bottom (e.g. "Haute Couture")
        QString family = QStringLiteral("Great Vibes");
        double  widthMm = 132.0;
        QColor  gold = QColor(176, 141, 50);
    };

    static StitchSequence generate(const Badge& b);

    /// Daniela's ready-made atelier badge (procedural font approximation).
    static StitchSequence knueppelBadge(const QColor& gold = QColor(176, 141, 50));

    /// Daniela's authentic atelier logo digitized from the original vector artwork (meflogoplain.svg).
    static StitchSequence mefOriginalBadge(int style = 0,
                                           double widthMm = 150.0,
                                           const QColor& gold = QColor(179, 139, 69));
};

} // namespace stick
