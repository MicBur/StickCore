// ---------------------------------------------------------------------------
//  StickCore  –  SfumatoGenerator.cpp
//
//  Embird-style Sfumato Photo-Stitch Implementation.
// ---------------------------------------------------------------------------
#include "generators/SfumatoGenerator.h"

#include <algorithm>
#include <cmath>

namespace stick {

namespace {
constexpr double PI = 3.14159265358979323846;
constexpr double GOLDEN_RATIO = 1.618033988749895;
} // namespace

QImage SfumatoGenerator::preprocessImage(const QImage& source, const Params& p)
{
    if (source.isNull()) return QImage();

    QImage rgb = source.convertToFormat(QImage::Format_ARGB32);
    const int w = rgb.width();
    const int h = rgb.height();
    QImage gray(w, h, QImage::Format_Grayscale8);

    const double gammaInv = 1.0 / std::max(0.1, p.gamma);

    for (int y = 0; y < h; ++y) {
        const QRgb* srcLine = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
        uchar* dstLine = gray.scanLine(y);
        for (int x = 0; x < w; ++x) {
            QRgb pixel = srcLine[x];
            double lum = (0.299 * qRed(pixel) + 0.587 * qGreen(pixel) + 0.114 * qBlue(pixel)) / 255.0;

            // Brightness
            lum = std::clamp(lum + p.brightness, 0.0, 1.0);

            // Contrast
            lum = std::clamp((lum - 0.5) * p.contrast + 0.5, 0.0, 1.0);

            // Gamma
            lum = std::pow(lum, gammaInv);

            // Darkness Density: 1.0 = dark shadow, 0.0 = white/fabric
            double density = p.invertLuminance ? lum : (1.0 - lum);
            density = std::clamp(density, 0.0, 1.0);

            dstLine[x] = static_cast<uchar>(std::round(density * 255.0));
        }
    }
    return gray;
}

double SfumatoGenerator::sampleDensity(const QImage& prep, double xMm, double yMm,
                                      double widthMm, double heightMm)
{
    if (prep.isNull() || widthMm <= 1e-4 || heightMm <= 1e-4)
        return 0.0;

    const double u = std::clamp((xMm / widthMm) * (prep.width() - 1), 0.0, static_cast<double>(prep.width() - 1));
    const double v = std::clamp((yMm / heightMm) * (prep.height() - 1), 0.0, static_cast<double>(prep.height() - 1));

    const int x0 = static_cast<int>(u);
    const int y0 = static_cast<int>(v);
    const int x1 = std::min(x0 + 1, prep.width() - 1);
    const int y1 = std::min(y0 + 1, prep.height() - 1);

    const double fx = u - x0;
    const double fy = v - y0;

    const uchar p00 = prep.constScanLine(y0)[x0];
    const uchar p10 = prep.constScanLine(y0)[x1];
    const uchar p01 = prep.constScanLine(y1)[x0];
    const uchar p11 = prep.constScanLine(y1)[x1];

    const double top = (1.0 - fx) * p00 + fx * p10;
    const double bot = (1.0 - fx) * p01 + fx * p11;
    const double val = (1.0 - fy) * top + fy * bot;

    return val / 255.0;
}

static void sweepHorizontal(const QImage& prep, const SfumatoGenerator::Params& p,
                            stick::StitchSequence& seq, double xOffsetMm, double yOffsetMm,
                            double phaseBias = 0.0)
{
    const double lineSpacing = std::max(0.4, p.lineSpacingMm);
    const int rowCount = std::max(2, static_cast<int>(std::ceil(p.heightMm / lineSpacing)));

    bool firstStitch = seq.empty();

    for (int r = 0; r <= rowCount; ++r) {
        const double yBase = std::min(p.heightMm, r * lineSpacing);
        const bool ltr = (r % 2 == 0);
        double xCurr = ltr ? 0.0 : p.widthMm;
        const double xTarget = ltr ? p.widthMm : 0.0;

        bool rowDone = false;
        while (!rowDone) {
            double density = SfumatoGenerator::sampleDensity(prep, xCurr, yBase, p.widthMm, p.heightMm);
            if (density < p.cutOffThreshold)
                density = 0.0;

            const double step = std::clamp(p.maxStitchMm - density * (p.maxStitchMm - p.minStitchMm),
                                           p.minStitchMm, p.maxStitchMm);

            // Modulate sinusoidal amplitude and wavelength with density
            const double amp = p.maxAmplitudeMm * (density * density);
            const double wavelength = std::max(1.0, 4.0 - density * 2.2);
            const double omega = (2.0 * PI) / wavelength;
            const double phi = r * GOLDEN_RATIO + phaseBias;
            const double dy = amp * std::sin(omega * xCurr + phi);

            const double worldX = xOffsetMm + xCurr;
            const double worldY = yOffsetMm + yBase + dy;

            if (firstStitch) {
                seq.add(worldX, worldY, SF_Jump, p.colorIdx);
                firstStitch = false;
            } else {
                seq.add(worldX, worldY, SF_Normal, p.colorIdx);
            }

            if (ltr) {
                if (xCurr >= xTarget - 1e-4) {
                    rowDone = true;
                } else {
                    xCurr = std::min(xTarget, xCurr + step);
                }
            } else {
                if (xCurr <= xTarget + 1e-4) {
                    rowDone = true;
                } else {
                    xCurr = std::max(xTarget, xCurr - step);
                }
            }
        }
    }
}

static void sweepVertical(const QImage& prep, const SfumatoGenerator::Params& p,
                          stick::StitchSequence& seq, double xOffsetMm, double yOffsetMm,
                          double phaseBias = 0.0)
{
    const double colSpacing = std::max(0.4, p.lineSpacingMm);
    const int colCount = std::max(2, static_cast<int>(std::ceil(p.widthMm / colSpacing)));

    bool firstStitch = seq.empty();

    for (int c = 0; c <= colCount; ++c) {
        const double xBase = std::min(p.widthMm, c * colSpacing);
        const bool ttb = (c % 2 == 0); // top-to-bottom
        double yCurr = ttb ? 0.0 : p.heightMm;
        const double yTarget = ttb ? p.heightMm : 0.0;

        bool colDone = false;
        while (!colDone) {
            double density = SfumatoGenerator::sampleDensity(prep, xBase, yCurr, p.widthMm, p.heightMm);
            if (density < p.cutOffThreshold)
                density = 0.0;

            const double step = std::clamp(p.maxStitchMm - density * (p.maxStitchMm - p.minStitchMm),
                                           p.minStitchMm, p.maxStitchMm);

            const double amp = p.maxAmplitudeMm * (density * density);
            const double wavelength = std::max(1.0, 4.0 - density * 2.2);
            const double omega = (2.0 * PI) / wavelength;
            const double phi = c * GOLDEN_RATIO + phaseBias;
            const double dx = amp * std::sin(omega * yCurr + phi);

            const double worldX = xOffsetMm + xBase + dx;
            const double worldY = yOffsetMm + yCurr;

            if (firstStitch) {
                seq.add(worldX, worldY, SF_Jump, p.colorIdx);
                firstStitch = false;
            } else {
                seq.add(worldX, worldY, SF_Normal, p.colorIdx);
            }

            if (ttb) {
                if (yCurr >= yTarget - 1e-4) {
                    colDone = true;
                } else {
                    yCurr = std::min(yTarget, yCurr + step);
                }
            } else {
                if (yCurr <= yTarget + 1e-4) {
                    colDone = true;
                } else {
                    yCurr = std::max(yTarget, yCurr - step);
                }
            }
        }
    }
}

StitchSequence SfumatoGenerator::generate(const QImage& sourceImage, const Params& p)
{
    StitchSequence seq;
    if (sourceImage.isNull() || p.widthMm <= 1.0 || p.heightMm <= 1.0)
        return seq;

    QImage prep = preprocessImage(sourceImage, p);
    if (prep.isNull())
        return seq;

    const double xOffset = -p.widthMm / 2.0;
    const double yOffset = -p.heightMm / 2.0;

    if (p.angleMode == AngleMode::Horizontal) {
        sweepHorizontal(prep, p, seq, xOffset, yOffset);
    } else if (p.angleMode == AngleMode::Vertical) {
        sweepVertical(prep, p, seq, xOffset, yOffset);
    } else if (p.angleMode == AngleMode::CrossHatch) {
        sweepHorizontal(prep, p, seq, xOffset, yOffset, 0.0);
        if (!seq.empty()) {
            // Machine jump between horizontal and vertical passes
            sweepVertical(prep, p, seq, xOffset, yOffset, PI * 0.5);
        }
    }

    if (seq.palette.empty()) {
        seq.palette.emplace_back(p.threadColor, QStringLiteral("Sfumato Garn"));
    }

    return seq;
}

} // namespace stick
