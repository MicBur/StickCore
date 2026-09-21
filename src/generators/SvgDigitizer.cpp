// ---------------------------------------------------------------------------
//  StickCore  –  SvgDigitizer.cpp
// ---------------------------------------------------------------------------
#include "generators/SvgDigitizer.h"
#include "generators/ContourFill.h"
#include "generators/TatamiFill.h"
#include "generators/SatinGenerator.h"
#include "core/SvgPathParser.h"

#include <QTransform>
#include <QRectF>
#include <QPolygonF>
#include <QVector>
#include <cmath>

namespace stick {

StitchSequence SvgDigitizer::digitizeSvgFile(const QString& filePath, const Params& params)
{
    const QPainterPath path = SvgPathParser::parseSvgFile(filePath);
    return digitize(path, params);
}

StitchSequence SvgDigitizer::digitizeSvgFile(const QString& filePath)
{
    return digitizeSvgFile(filePath, Params());
}

StitchSequence SvgDigitizer::digitize(const QPainterPath& inPath)
{
    return digitize(inPath, Params());
}

StitchSequence SvgDigitizer::digitize(const QPainterPath& inPath, const Params& p)
{
    StitchSequence result;
    if (inPath.elementCount() == 0) return result;

    const QRectF bb = inPath.boundingRect();
    if (bb.width() < 1.0 || bb.height() < 1.0) return result;

    // Transform from SVG coords (+Y down) to centered embroidery mm coords (+Y up)
    QPainterPath pathMm;
    if (p.targetWidthMm > 0.0) {
        const double s = p.targetWidthMm / bb.width();
        QTransform tf;
        tf.scale(s, -s); // Invert Y so top is +Y in embroidery
        tf.translate(-bb.center().x(), -bb.center().y());
        pathMm = tf.map(inPath);
    } else {
        QTransform tf;
        tf.scale(1.0, -1.0);
        tf.translate(-bb.center().x(), -bb.center().y());
        pathMm = tf.map(inPath);
    }

    switch (p.style) {
    case StitchStyle::AuthenticPatchSatin: {
        const auto subpaths = pathMm.toSubpathPolygons();
        const QRectF bounds = pathMm.boundingRect();
        const double wTotal = bounds.width();
        const double hTotal = bounds.height();

        // 1. Oval dimensions
        const double a_out = wTotal * 0.5;
        const double b_out = hTotal * 0.5;
        const double borderW = 3.8; // mm satin border width
        const double a_in = a_out - borderW;
        const double b_in = b_out - borderW;
        const double a_mid = 0.5 * (a_out + a_in);
        const double b_mid = 0.5 * (b_out + b_in);

        // A) Underlay for the satin border:
        // Center-walk track
        const double circMid = M_PI * (3.0 * (a_mid + b_mid) - std::sqrt((3.0 * a_mid + b_mid) * (a_mid + 3.0 * b_mid)));
        const int nMid = std::max(60, int(circMid / 2.5));
        result.add(a_mid, 0.0, SF_Jump, 0);
        for (int i = 1; i <= nMid; ++i) {
            double t = 2.0 * M_PI * i / nMid;
            result.add(a_mid * std::cos(t), b_mid * std::sin(t), SF_Normal, 0);
        }
        // Inner edge-walk
        const double a_eIn = a_in + 0.4, b_eIn = b_in + 0.4;
        for (int i = 0; i <= nMid; ++i) {
            double t = 2.0 * M_PI * i / nMid;
            result.add(a_eIn * std::cos(t), b_eIn * std::sin(t), SF_Normal, 0);
        }
        // Outer edge-walk
        const double a_eOut = a_out - 0.4, b_eOut = b_out - 0.4;
        for (int i = 0; i <= nMid; ++i) {
            double t = 2.0 * M_PI * i / nMid;
            result.add(a_eOut * std::cos(t), b_eOut * std::sin(t), SF_Normal, 0);
        }

        // B) Cover Satin: Dense radial rungs with pull compensation
        const double pull = 0.15; // mm pull compensation
        const double pitchMm = std::max(0.28, std::min(0.42, p.spacingMm));
        const int nRungs = std::max(120, int(circMid / pitchMm));
        for (int i = 0; i <= nRungs; ++i) {
            const double t = 2.0 * M_PI * i / nRungs;
            const double cosT = std::cos(t);
            const double sinT = std::sin(t);
            const double xOut = (a_out + pull) * cosT;
            const double yOut = (b_out + pull) * sinT;
            const double xIn  = (a_in - pull)  * cosT;
            const double yIn  = (b_in - pull)  * sinT;

            result.add(xIn,  yIn,  SF_Normal, 0);
            result.add(xOut, yOut, SF_Normal, 0);
        }
        result.add(a_in - pull, 0.0, SF_Normal, 0);

        // C) Inner Accent Ring (double-pass running stitch at 1.8 mm inset)
        const double a_acc = a_in - 1.8;
        const double b_acc = b_in - 1.8;
        const double circAcc = M_PI * (3.0 * (a_acc + b_acc) - std::sqrt((3.0 * a_acc + b_acc) * (a_acc + 3.0 * b_acc)));
        const int nAcc = std::max(60, int(circAcc / 2.2));
        result.add(a_acc, 0.0, SF_Jump, 0);
        for (int i = 1; i <= nAcc; ++i) {
            double t = 2.0 * M_PI * i / nAcc;
            result.add(a_acc * std::cos(t), b_acc * std::sin(t), SF_Normal, 0);
        }
        for (int i = nAcc - 1; i >= 0; --i) {
            double t = 2.0 * M_PI * i / nAcc;
            result.add(a_acc * std::cos(t), b_acc * std::sin(t), SF_Normal, 0);
        }

        // 2. The Letter Contours:
        QVector<QPolygonF> letterPolys;
        for (const auto& poly : subpaths) {
            if (poly.size() < 3) continue;
            const QRectF r = poly.boundingRect();
            if (r.width() > wTotal * 0.45 && r.height() > hTotal * 0.45) {
                // Border subpaths, skip
                continue;
            }
            letterPolys.push_back(poly);
        }

        if (!letterPolys.isEmpty()) {
            for (const auto& poly : letterPolys) {
                if (poly.size() < 3) continue;
                const QVector<QPolygonF> singleLetter = { poly };

                // A) Underlay for this letter (45 deg stabilizing pass)
                TatamiFill::Params tpUnder;
                tpUnder.fillAngleDeg = 45.0;
                tpUnder.rowSpacingMm = 0.65;
                tpUnder.maxStitchMm  = 2.2;
                tpUnder.underlay     = false;
                tpUnder.colorIdx     = 0;
                StitchSequence letterUnder = TatamiFill::generate(singleLetter, tpUnder);
                for (const auto& st : letterUnder.stitches) result.stitches.push_back(st);

                // B) Top dense satin/tatami fill (15 deg angle for script sheen)
                TatamiFill::Params tpTop;
                tpTop.fillAngleDeg = 15.0;
                tpTop.rowSpacingMm = 0.30; // dense 0.30mm pitch
                tpTop.maxStitchMm  = 2.4;
                tpTop.underlay     = false;
                tpTop.colorIdx     = 0;
                StitchSequence letterCover = TatamiFill::generate(singleLetter, tpTop);
                for (const auto& st : letterCover.stitches) result.stitches.push_back(st);

                // C) Raised Satin Contour Edge Lock (0.60 mm satin band around each letter)
                QPainterPath lp;
                lp.moveTo(poly.first());
                for (int i = 1; i < poly.size(); ++i) lp.lineTo(poly[i]);
                lp.closeSubpath();
                StitchSequence edgeSatin = SatinGenerator::fromCenterline(lp, 0.60, 0.30, 0);
                for (const auto& st : edgeSatin.stitches) result.stitches.push_back(st);
            }
        }

        result.palette.clear();
        result.palette.emplace_back(p.primaryColor, QStringLiteral("Madeira 1083 Gold"));
        break;
    }

    case StitchStyle::ContourEcho: {
        ContourFill::Params cp;
        cp.spacingMm    = std::max(0.25, p.spacingMm);
        cp.maxStitchMm  = std::max(1.0,  p.maxStitchMm);
        cp.colorIdx     = 0;
        cp.outlineFirst = true;

        result = ContourFill::generate(pathMm, cp);
        result.palette.clear();
        result.palette.emplace_back(p.primaryColor, QStringLiteral("Atelier Gold"));
        break;
    }

    case StitchStyle::TatamiWeave: {
        // Collect subpath polygons
        QVector<QPolygonF> region;
        const auto subpaths = pathMm.toSubpathPolygons();
        for (const auto& poly : subpaths) {
            if (poly.size() >= 3) region << poly;
        }

        TatamiFill::Params tp;
        tp.fillAngleDeg = p.tatamiAngleDeg;
        tp.rowSpacingMm = std::max(0.30, p.spacingMm);
        tp.maxStitchMm  = std::max(1.5,  p.maxStitchMm * 1.5);
        tp.phaseFrac    = 0.25;
        tp.underlay     = p.addUnderlay;
        tp.colorIdx     = 0;

        result = TatamiFill::generate(region, tp);

        // Add an outline contour pass for crisp letters and borders
        ContourFill::Params cp;
        cp.spacingMm    = 100.0; // Only generate outer boundary ring
        cp.maxStitchMm  = p.maxStitchMm;
        cp.colorIdx     = 0;
        cp.outlineFirst = true;
        StitchSequence outline = ContourFill::generate(pathMm, cp);

        for (const auto& st : outline.stitches) {
            result.stitches.push_back(st);
        }

        result.palette.clear();
        result.palette.emplace_back(p.primaryColor, QStringLiteral("Meister Gold"));
        break;
    }

    case StitchStyle::RoyalDuotoneGold: {
        // Layer 1: Madeira 1083 Antique Gold (Deep Base Contour)
        ContourFill::Params cp1;
        cp1.spacingMm    = std::max(0.50, p.spacingMm * 1.5);
        cp1.maxStitchMm  = std::max(1.0,  p.maxStitchMm);
        cp1.colorIdx     = 0;
        cp1.outlineFirst = true;
        StitchSequence layer1 = ContourFill::generate(pathMm, cp1);

        // Layer 2: Madeira 1070 Brilliant Gold (Fine Surface Contour)
        ContourFill::Params cp2;
        cp2.spacingMm    = std::max(0.30, p.spacingMm);
        cp2.maxStitchMm  = std::max(1.0,  p.maxStitchMm);
        cp2.colorIdx     = 1;
        cp2.outlineFirst = true;
        StitchSequence layer2 = ContourFill::generate(pathMm, cp2);

        result.stitches.reserve(layer1.stitches.size() + layer2.stitches.size() + 2);

        for (const auto& st : layer1.stitches) {
            result.stitches.push_back(st);
        }

        if (!layer2.stitches.empty()) {
            // Signal color change on the first stitch of layer 2
            bool first = true;
            for (auto st : layer2.stitches) {
                if (first) {
                    st.flags |= SF_ColorChange;
                    st.colorIdx = 1;
                    first = false;
                }
                result.stitches.push_back(st);
            }
        }

        result.palette.clear();
        result.palette.emplace_back(p.primaryColor,   QStringLiteral("Madeira 1083 Tiefengold"));
        result.palette.emplace_back(p.secondaryColor, QStringLiteral("Madeira 1070 Glanzgold"));
        break;
    }

    case StitchStyle::FineOutlineRun: {
        const double maxStitch = std::max(1.0, p.maxStitchMm);
        const auto subpaths = pathMm.toSubpathPolygons();

        for (const auto& poly : subpaths) {
            if (poly.size() < 2) continue;

            result.add(poly[0].x(), poly[0].y(), SF_Jump, 0);
            double acc = 0.0;
            QPointF prev = poly[0];

            // Forward pass
            for (int i = 1; i <= poly.size(); ++i) {
                const QPointF curr = poly[i % poly.size()];
                const double d = std::hypot(curr.x() - prev.x(), curr.y() - prev.y());
                acc += d;
                if (acc >= maxStitch || i == poly.size()) {
                    result.add(curr.x(), curr.y(), SF_Normal, 0);
                    acc = 0.0;
                }
                prev = curr;
            }

            // Reverse pass (double-run for clean thread density)
            prev = poly.last();
            acc = 0.0;
            for (int i = poly.size() - 2; i >= 0; --i) {
                const QPointF curr = poly[i];
                const double d = std::hypot(curr.x() - prev.x(), curr.y() - prev.y());
                acc += d;
                if (acc >= maxStitch || i == 0) {
                    result.add(curr.x(), curr.y(), SF_Normal, 0);
                    acc = 0.0;
                }
                prev = curr;
            }
        }

        result.palette.clear();
        result.palette.emplace_back(p.primaryColor, QStringLiteral("Gold Steppstich"));
        break;
    }
    }

    return result;
}

} // namespace stick
