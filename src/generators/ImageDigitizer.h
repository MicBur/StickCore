// ---------------------------------------------------------------------------
//  StickCore  –  ImageDigitizer.h
//
//  Comprehensive High-End Image Digitizing Suite for Embroidery:
//
//    • Colors      – Vector-Tatami: K-Means color reduction, boundary & hole
//                    vectorization, true TatamiFill with patterns (Standard,
//                    Köper/Twill, Ziegel, Basketweave), automatic Underlay
//                    (edge run + 90° cross) and Pull Compensation (Zugausgleich).
//    • Sfumato     – Photorealistic continuous-density meandering stipple waves
//                    for artistic portraits, animal coats, and photos.
//    • CrossStitch – Authentic counted cross-stitch on standardized Aida fabric
//                    grids (11ct, 14ct, 16ct, 18ct) with full or double cross.
//    • LineArt     – Outline/sketch tracing with single run or 3-pass Bean Stitch.
//    • Portrait    – "Konterfei": Comic silhouette or multi-tone poster.
// ---------------------------------------------------------------------------
#pragma once

#include "core/StitchTypes.h"
#include "editor/BezierPath.h"
#include "generators/TatamiFill.h"
#include "generators/SfumatoGenerator.h"
#include "generators/CrossStitchGenerator.h"
#include "generators/BeanStitchGenerator.h"
#include <QColor>
#include <QImage>
#include <QString>
#include <QVector>
#include <vector>

namespace stick {

class ImageDigitizer {
public:
    enum class Mode {
        Colors,       ///< Vector Trace (Bitmap to Path wie Inkscape in 1..8 Farben / Tönen)
        Sfumato,      ///< Sfumato Photo-Stitch: continuous density sine waves
        CrossStitch,  ///< Traditional counted cross-stitch on Aida grid
        LineArt,      ///< Outlines / sketches (running or 3-pass Bean stitch)
        Portrait      ///< Konterfei (comic silhouette, tones)
    };

    enum class SeparationMode {
        Tones,        ///< Brightness steps (1 = Schablone/Silhouette, 2 = Duo-Ton, 3 = Trio-Ton) – optimal für Fotos & Kontraste
        Colors        ///< K-Means Farbreduktion mit Kantenerhalt – optimal für mehrfarbige Logos & Clipart
    };

    enum class LineArtStitch {
        SingleRun,    ///< Standard 1-pass running stitch (2.0 mm)
        BeanStitch,   ///< 3-pass Triple / Bean Stitch (raised relief outline)
        FivePass      ///< 5-pass heavy relief outline
    };

    enum class PortraitStyle {
        Comic,        ///< solid silhouette + adaptive detail (Variante C)
        Silhouette,   ///< solid only (bilateral + Otsu)        (Variante A)
        Sketch,       ///< fine adaptive line drawing            (Variante B)
        Detailed,     ///< CLAHE + Otsu, photo-like              (Variante F)
        Stylized,     ///< stylise + Otsu                        (Variante E)
        Tones         ///< 2–6 grey tones (uses `tones`)         (Variante D)
    };

    static QString portraitStyleName(PortraitStyle s);
    static QString modeName(Mode m);

    struct QuantizedColor {
        int         id           = 0;
        QColor      rgb;
        ThreadColor thread;
        double      percentage   = 0.0;
        bool        isBackground = false;
        bool        active       = true;
    };

    struct Params {
        Mode    mode          = Mode::Colors;

        // --- shared ---------------------------------------------------------
        double  widthMm       = 100.0;  ///< target design width in mm
        int     maxProcessPx  = 360;    ///< analysis buffer resolution (up to 480/600)
        double  maxStitchMm   = 4.0;
        QString brand;                  ///< snap palette to this brand ("" = any)

        // --- Pre-processing -------------------------------------------------
        int     blur          = 0;      ///< pre-blur radius in analysis px (0 = off)
        double  contrast      = 1.0;    ///< contrast boost (1.0 = none)
        double  brightness    = 0.0;    ///< brightness offset (-1.0 to 1.0)
        int     threshold     = -1;     ///< 0..255, or -1 for automatic (Otsu)
        bool    invert        = false;  ///< swap foreground / background

        // --- Colors / Vektor-Zerlegung (Bitmap to Path wie Inkscape) --------
        SeparationMode separation     = SeparationMode::Colors; ///< Colors (K-Means) vs Tones (Helligkeit)
        int     colors        = 3;      ///< number of thread colours / tones (1..16)
        int     stencilThreshold = -1;  ///< 0..255 manual threshold for 1-color stencil (-1 = auto Otsu)
        double  densityMm     = 0.40;   ///< fill row spacing (0.30 - 0.65 mm)
        bool    dropBackground= true;   ///< skip dominant background color
        double  fillAngleDeg  = 0.0;    ///< stitch angle in degrees
        bool    multiAngle    = false;  ///< auto angle shift (+45° per color)
        TatamiFill::PatternType tatamiPattern = TatamiFill::PatternType::StandardTatami;
        bool    underlay      = true;   ///< generate underlay (edge run + 90° cross layer)
        double  pullCompMm    = 0.25;   ///< pull compensation in mm (0.0 - 0.6 mm)
        double  smoothing     = 1.0;    ///< Douglas-Peucker contour smoothing in px
        int     minSpeckleArea= 16;     ///< minimum pixel cluster area to retain
        bool    satinBorder   = false;  ///< raised satin border around motif
        double  borderWidthMm = 1.5;
        std::vector<bool> activeColors; ///< per-color enable/disable mask (empty = auto)

        // --- Sfumato Photo-Stitch -------------------------------------------
        double  sfumatoLineSpacingMm = 1.4;
        double  sfumatoAmplitudeMm   = 0.9;
        SfumatoGenerator::AngleMode sfumatoAngle = SfumatoGenerator::AngleMode::Horizontal;
        QColor  sfumatoColor         = QColor(40, 35, 30);

        // --- Cross-Stitch ---------------------------------------------------
        CrossStitchGenerator::AidaCount   crossStitchAida  = CrossStitchGenerator::AidaCount::Count14;
        CrossStitchGenerator::StitchStyle crossStitchStyle = CrossStitchGenerator::StitchStyle::FullCross;

        // --- LineArt --------------------------------------------------------
        LineArtStitch lineArtStitch = LineArtStitch::SingleRun;
        double  runMm         = 2.0;    ///< running-stitch length along outlines
        int     edgeThreshold = 60;     ///< edge sensitivity

        // --- Portrait -------------------------------------------------------
        PortraitStyle portraitStyle = PortraitStyle::Comic;
        int     tones         = 1;      ///< Tones style: 2..6 grey tones
        QColor  ink           = QColor(20, 20, 24);  ///< colour of the darkest tone

        // --- OpenCV-powered options (ignored when built without OpenCV) -----
        bool    autoFaceCrop  = false;  ///< detect & crop to the face first
        bool    fineDetail    = true;   ///< Portrait 1-tone: adaptive threshold
    };

    /// Extracts quantized color swatches, Madeira mapping and coverage % for UI palette management.
    static std::vector<QuantizedColor> extractPalette(const QImage& image, const Params& p);

    /// Vectorizes the segmented image into clean cubic-Bézier paths (QVector<EditPath>) like Inkscape's Trace Bitmap.
    static QVector<EditPath> traceToPaths(const QImage& image, const Params& p);

    /// Generates production-ready stitches for the given image and parameters.
    static StitchSequence generate(const QImage& image, const Params& p);

    /// Fast preview generation for live UI slider interaction (< 20 ms).
    static StitchSequence generateFastPreview(const QImage& image, const Params& p);

    /// Render what the machine-analysis stage "sees" for UI preview.
    static QImage preview(const QImage& image, const Params& p);
};

} // namespace stick
