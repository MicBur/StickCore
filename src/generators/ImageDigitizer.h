// ---------------------------------------------------------------------------
//  StickCore  –  ImageDigitizer.h
//
//  Turns a raster image into an embroidery design. Three modes:
//
//    • Colors    – reduce the picture to a handful of thread colours (k-means)
//                  and fill each colour region with a raster tatami scan.
//    • Portrait  – "Konterfei": reduce to a silhouette (1 colour, Otsu
//                  threshold) or a few grey tones (posterise) and fill them.
//                  Ideal for turning a photo of a face into a clean, comic-like
//                  single/few-colour motif.
//    • LineArt   – trace the outlines of the picture (contour following on a
//                  threshold mask) into running stitches — a "coloring-book"
//                  look, one colour.
// ---------------------------------------------------------------------------
#pragma once

#include "core/StitchTypes.h"
#include <QColor>
#include <QImage>
#include <QString>

namespace stick {

class ImageDigitizer {
public:
    enum class Mode { Colors, Portrait, LineArt };

    // Portrait sub-styles (from the on-photo comparison). Comic is the default.
    enum class PortraitStyle {
        Comic,       ///< solid silhouette + adaptive detail (Variante C)
        Silhouette,  ///< solid only (bilateral + Otsu)        (Variante A)
        Sketch,      ///< fine adaptive line drawing            (Variante B)
        Detailed,    ///< CLAHE + Otsu, photo-like              (Variante F)
        Stylized,    ///< stylise + Otsu                        (Variante E)
        Tones        ///< 2–5 grey tones (uses `tones`)         (Variante D)
    };

    static QString portraitStyleName(PortraitStyle s);

    struct Params {
        Mode    mode          = Mode::Colors;

        // --- shared ---------------------------------------------------------
        double  widthMm       = 100.0;  ///< target design width
        int     maxProcessPx  = 180;    ///< downscale for analysis
        double  maxStitchMm   = 4.0;
        QString brand;                  ///< snap palette to this brand ("" = any)

        // --- Colors ---------------------------------------------------------
        int     colors        = 6;      ///< number of thread colours
        double  densityMm     = 0.50;   ///< fill row spacing
        bool    dropBackground= true;   ///< skip a dominant near-white background
        double  fillAngleDeg  = 45.0;   ///< Stichwinkel in Grad (0° - 360°)
        bool    multiAngle    = true;   ///< Automatischer Verzugsausgleich (je Farbe +45°)
        bool    satinBorder   = false;  ///< Farbkonturen mit Satinstich nachzeichnen
        double  borderWidthMm = 1.2;

        // --- Portrait / LineArt pre-processing ------------------------------
        int     blur          = 1;      ///< pre-blur radius in analysis px (0 = off)
        double  contrast      = 1.0;    ///< contrast boost (1.0 = none)
        int     threshold     = -1;     ///< 0..255, or -1 for automatic (Otsu)
        bool    invert        = false;  ///< swap foreground / background

        // --- Portrait -------------------------------------------------------
        PortraitStyle portraitStyle = PortraitStyle::Comic;
        int     tones         = 1;      ///< Tones style: 2..6 grey tones
        QColor  ink           = QColor(20, 20, 24);  ///< colour of the darkest tone

        // --- LineArt --------------------------------------------------------
        double  runMm         = 2.0;    ///< running-stitch length along outlines

        // --- OpenCV-powered options (ignored when built without OpenCV) -----
        bool    autoFaceCrop  = false;  ///< detect & crop to the face first
        bool    fineDetail    = true;   ///< Portrait 1-tone: adaptive threshold
                                        ///< (captures eyes/nose/mouth, comic look)
    };

    static StitchSequence generate(const QImage& image, const Params& p);

    /// Render what the machine-analysis stage "sees" for the Portrait/LineArt
    /// modes — used by the UI for a live preview. Returns an ARGB image the
    /// size of the (downscaled) analysis buffer.
    static QImage preview(const QImage& image, const Params& p);
};

} // namespace stick
