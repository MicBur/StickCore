// ---------------------------------------------------------------------------
//  StickCore  –  SfumatoGenerator.h
//
//  Embird-style Sfumato Photo-Stitch:
//  Generates continuous density-modulated meandering stipple paths from
//  photographs and grayscale images. Modulates line waviness and stitch density
//  according to local pixel luminance for continuous-tone photorealistic
//  embroidery without harsh color quantization boundaries.
// ---------------------------------------------------------------------------
#pragma once

#include "core/StitchTypes.h"
#include <QColor>
#include <QImage>
#include <QVector>

namespace stick {

class SfumatoGenerator {
public:
    enum class AngleMode {
        Horizontal,   ///< 0° Boustrophedon sweep lines
        Vertical,     ///< 90° Boustrophedon sweep lines
        CrossHatch    ///< Two orthogonal passes (0° + 90°) for deep shadows & rich texture
    };

    struct Params {
        double    widthMm         = 100.0; ///< Output design width in mm
        double    heightMm        = 100.0; ///< Output design height in mm
        double    lineSpacingMm   = 1.4;   ///< Distance between sweep lines (mm)
        double    maxAmplitudeMm  = 0.9;   ///< Maximum oscillation ripple amplitude in darkest areas (mm)
        double    minStitchMm     = 0.8;   ///< Minimum stitch length in shadow areas (mm)
        double    maxStitchMm     = 3.2;   ///< Maximum stitch length in highlight areas (mm)
        double    contrast        = 1.15;  ///< Contrast multiplier (1.0 = normal)
        double    brightness      = 0.0;   ///< Brightness offset (-1.0 to +1.0)
        double    gamma           = 1.0;   ///< Gamma correction exponent
        bool      invertLuminance = false; ///< True if stitching with light thread on dark fabric
        double    cutOffThreshold = 0.04;  ///< Density cutoff below which lines stay flat
        AngleMode angleMode       = AngleMode::Horizontal;
        QColor    threadColor     = QColor(40, 35, 30); ///< Thread color
        int       colorIdx        = 0;
    };

    /// Converts an input image into a continuous Sfumato embroidery sequence.
    static StitchSequence generate(const QImage& sourceImage, const Params& p);

    /// Samples normalized darkness density D in [0.0, 1.0] at world coordinates (xMm, yMm)
    /// where 1.0 is maximum dark/thread density and 0.0 is pure highlight/fabric.
    static double sampleDensity(const QImage& preprocessedImg, double xMm, double yMm,
                                double widthMm, double heightMm);

    /// Preprocesses image (grayscale conversion, contrast, brightness, gamma, inversion).
    static QImage preprocessImage(const QImage& source, const Params& p);
};

} // namespace stick
