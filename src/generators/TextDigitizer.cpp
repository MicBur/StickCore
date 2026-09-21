#include "generators/TextDigitizer.h"
#include "generators/TatamiFill.h"
#include "generators/SatinGenerator.h"
#include "core/Geometry.h"

#include <QFont>
#include <QFontMetricsF>
#include <QTransform>
#include <cmath>

namespace stick {

QPainterPath TextDigitizer::textPath(const Params& p)
{
    if (p.text.isEmpty()) return QPainterPath();

    QFont f;
    if (!p.families.isEmpty()) {
        f.setStyleHint(p.serif ? QFont::Serif : QFont::SansSerif);
        f.setFamilies(p.families);
    } else if (p.serif) {
        f.setStyleHint(QFont::Serif); f.setFamily(QStringLiteral("Serif"));
    } else {
        f.setFamily(p.family);
    }
    f.setBold(p.bold);
    f.setItalic(p.italic);
    f.setPixelSize(256);

    // Fast-path for simple straight text without slant/extra kerning
    if (p.baseline == Baseline::Straight &&
        std::abs(p.letterSpacingMm) < 1e-4 &&
        std::abs(p.slantDeg) < 1e-4) {
        QPainterPath raw;
        raw.addText(0, 0, f, p.text);
        const QRectF b = raw.boundingRect();
        if (b.height() < 1e-6) return QPainterPath();
        const double s = p.heightMm / b.height();
        QTransform T;
        T.scale(s, -s);
        QPainterPath sp = T.map(raw);
        const QRectF b2 = sp.boundingRect();
        QTransform C;
        C.translate(-b2.center().x(), -b2.center().y());
        return C.map(sp);
    }

    QFontMetricsF fm(f);
    double capH = fm.capHeight();
    if (capH < 20.0) capH = fm.ascent() * 0.72;
    if (capH < 1.0) capH = 200.0;
    const double scale = p.heightMm / capH;

    struct GlyphItem {
        QPainterPath path;
        double advMm;
    };

    QVector<GlyphItem> items;
    double totalLengthMm = 0.0;
    const double spMm = p.letterSpacingMm;

    for (int i = 0; i < p.text.size(); ++i) {
        const QChar ch = p.text[i];
        QPainterPath gp;
        gp.addText(0, 0, f, QString(ch));
        double advMm = fm.horizontalAdvance(ch) * scale;
        if (advMm < 1e-4) advMm = fm.averageCharWidth() * scale;

        items.push_back({ gp, advMm });
        totalLengthMm += advMm;
        if (i + 1 < p.text.size()) totalLengthMm += spMm;
    }

    QPainterPath combined;
    double cursorMm = -totalLengthMm * 0.5;

    for (int i = 0; i < items.size(); ++i) {
        const auto& it = items[i];
        const double centerMm = cursorMm + it.advMm * 0.5;
        cursorMm += it.advMm + spMm;

        if (it.path.isEmpty()) continue;

        QRectF b = it.path.boundingRect();
        double glyphCenterFontX = (b.left() + b.right()) * 0.5;

        QTransform tGlyph;
        tGlyph.scale(scale, -scale);
        tGlyph.translate(-glyphCenterFontX, 0);

        if (std::abs(p.slantDeg) > 1e-4) {
            double shear = -std::tan(p.slantDeg * M_PI / 180.0);
            tGlyph.shear(shear, 0);
        }

        QPainterPath gMm = tGlyph.map(it.path);

        QTransform tPlace;
        switch (p.baseline) {
        case Baseline::Straight: {
            tPlace.translate(centerMm, 0);
            break;
        }
        case Baseline::ArcUp: {
            double R = std::max(15.0, p.arcRadiusMm);
            double theta = centerMm / R;
            double X = R * std::sin(theta);
            double Y = R * std::cos(theta) - R;
            double rotDeg = -theta * (180.0 / M_PI);
            tPlace.translate(X, Y);
            tPlace.rotate(rotDeg);
            break;
        }
        case Baseline::ArcDown: {
            double R = std::max(15.0, p.arcRadiusMm);
            double theta = centerMm / R;
            double X = R * std::sin(theta);
            double Y = R - R * std::cos(theta);
            double rotDeg = theta * (180.0 / M_PI);
            tPlace.translate(X, Y);
            tPlace.rotate(rotDeg);
            break;
        }
        case Baseline::Circle: {
            double R = std::max(15.0, p.arcRadiusMm);
            double theta = centerMm / R;
            double X = R * std::sin(theta);
            double Y = R * std::cos(theta);
            double rotDeg = -theta * (180.0 / M_PI);
            tPlace.translate(X, Y);
            tPlace.rotate(rotDeg);
            break;
        }
        }

        combined.addPath(tPlace.map(gMm));
    }

    const QRectF bComb = combined.boundingRect();
    if (bComb.isEmpty()) return QPainterPath();

    QTransform C;
    C.translate(-bComb.center().x(), -bComb.center().y());
    return C.map(combined);
}

StitchSequence TextDigitizer::generate(const Params& p)
{
    StitchSequence seq;
    const QPainterPath path = textPath(p);
    if (path.elementCount() == 0) return seq;

    const QList<QPolygonF> contours = path.toSubpathPolygons();
    QVector<QPolygonF> region;
    for (const QPolygonF& c : contours) region.push_back(c);

    TatamiFill::Params tp;
    tp.fillAngleDeg = p.fillAngleDeg;
    tp.pattern      = p.pattern;
    tp.rowSpacingMm = p.densityMm;
    tp.maxStitchMm  = p.maxStitchMm;
    tp.underlay     = p.underlay;
    tp.colorIdx     = p.colorIdx;

    const bool needTatami = (p.style == StitchStyle::Raised || p.style == StitchStyle::TatamiOnly);
    const bool needSatin  = (p.style == StitchStyle::Raised || p.style == StitchStyle::SatinOutline || p.raised);

    if (needTatami) {
        seq = TatamiFill::generate(region, tp);
    }

    if (needSatin) {
        for (const QPolygonF& c : contours) {
            if (c.size() < 2) continue;
            QPainterPath pp;
            pp.moveTo(c.first());
            for (int i = 1; i < c.size(); ++i) pp.lineTo(c[i]);
            pp.closeSubpath();
            StitchSequence sBand = SatinGenerator::fromCenterline(pp, p.borderWidthMm, p.borderPitchMm, p.colorIdx);
            seq.stitches.insert(seq.stitches.end(), sBand.stitches.begin(), sBand.stitches.end());
        }
    } else if (p.style == StitchStyle::RunStitch) {
        for (const QPolygonF& c : contours) {
            if (c.size() < 2) continue;
            bool first = true;
            for (const QPointF& pt : c) {
                seq.add(pt.x(), pt.y(), first ? SF_Jump : SF_Normal, p.colorIdx);
                first = false;
            }
            seq.add(c.first().x(), c.first().y(), SF_Normal, p.colorIdx);
        }
    }

    if (seq.palette.empty())
        seq.palette.emplace_back(QColor(20, 20, 20), QStringLiteral("Text"));

    return seq;
}

} // namespace stick
