// ---------------------------------------------------------------------------
//  StickCore  –  MonogramGenerator.h
//
//  Elegant monogram initials (1–3 letters). By convention a three-letter
//  monogram enlarges the centre letter (traditionally the surname initial),
//  flanked by the two others; an optional oval or circle frame is stitched
//  around it. Letters are raised (filled + satin edge) in an elegant serif.
// ---------------------------------------------------------------------------
#pragma once

#include "core/StitchTypes.h"
#include <QColor>
#include <QString>

namespace stick {

class MonogramGenerator {
public:
    enum class Frame {
        None,
        Oval,
        Circle,
        OakWreath,        ///< Eichenlaub-Kranz mit Eicheln (Tradition & Meisterschaft)
        LaurelWreath,     ///< Römischer Lorbeerkranz (Ehrenkranz)
        ShieldCrest,      ///< Wappenschild / Diamond Crest
        BaroqueCartouche  ///< Barocke Rokoko-Kartusche mit Ornamenten
    };

    // Lettering styles for the initials. Each maps to a set of font families
    // (with cross-platform fallbacks) so it looks right on Windows and here.
    enum class Style {
        Serif,      ///< klassische Serifen (edel, gut füllbar)
        Sans,       ///< moderne, klare serifenlose Schrift
        Script,     ///< Schreibschrift / kursiv, geschwungen
        SlabBold,   ///< kräftige, breite Blockschrift
        Elegant,    ///< schlanke Serifen-Kursive
        // --- bundled calligraphy fonts (artistic) ---
        GreatVibes, ///< geschwungene Kalligrafie mit Schwüngen
        Tangerine,  ///< feine, elegante Kalligrafie
        Pinyon,     ///< formelle Kupferstich-Schreibschrift
        Parisienne, ///< charmante, moderne Schreibschrift
        Allura,     ///< Commercial-Script-Stil (wie Danielas Logo)
        PetitFormal,///< feine formelle Schreibschrift
        Muellerhoff,///< zarte englische Schreibschrift
        AlexBrush   ///< traditionelle Pinselschrift (bester Logo-Match)
    };

    // How the letter bodies are stitched.
    enum class Fill {
        Raised,   ///< tatami fill + raised satin border (classic embossed look)
        Contour   ///< artistic echo/contour fill — stitches follow the shape
    };

    struct Params {
        QString letters     = QStringLiteral("ABC"); ///< 1–3 initials, in display order
        bool    centerLarge = true;   ///< enlarge the middle letter (3-letter style)
        Frame   frame       = Frame::Oval;
        Style   style       = Style::Serif;
        Fill    fill        = Fill::Raised;
        double  heightMm    = 30.0;   ///< height of the (large) letters
        double  densityMm   = 0.45;   ///< fill row / contour-ring spacing
        double  maxStitchMm = 4.0;
        double  fillAngleDeg= 45.0;   ///< Füllwinkel der Initialen (0° - 360°)
        bool    underlay    = true;
        QColor  color       = QColor(40, 55, 90);      ///< Buchstaben-Garnfarbe
        QColor  frameColor  = QColor(180, 140, 45);    ///< Rahmen-Garnfarbe (z.B. Gold)
    };

    /// Human-readable names (German) for UI menus.
    static QString styleName(Style s);
    static QString fillName(Fill f);
    static QString frameName(Frame f);

    static StitchSequence generate(const Params& p);
};

} // namespace stick
