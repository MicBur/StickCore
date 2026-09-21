// ---------------------------------------------------------------------------
//  StickCore  –  GradientFill.cpp
// ---------------------------------------------------------------------------
#include "generators/GradientFill.h"
#include "generators/Underlay.h"
#include "core/Geometry.h"

#include <QTransform>
#include <algorithm>
#include <cmath>
#include <limits>

namespace stick {

namespace {

void scanPolygon(const QPolygonF& poly, double yScan, QVector<double>& xs)
{
    const int n = poly.size();
    if (n < 2) return;
    for (int i = 0; i < n; ++i) {
        const QPointF& a = poly[i];
        const QPointF& b = poly[(i + 1) % n];
        const double y0 = a.y(), y1 = b.y();
        const double yLo = std::min(y0, y1);
        const double yHi = std::max(y0, y1);
        if (yScan < yLo || yScan >= yHi) continue;
        const double t = (yScan - y0) / (y1 - y0);
        xs.push_back(a.x() + t * (b.x() - a.x()));
    }
}

struct RunPt {
    QPointF pt;
    bool jump = false;
};

void emitScanRun(double x0, double x1, double y, double maxLen, double phase,
                 bool reverse, bool isNewSpan, QVector<RunPt>& out)
{
    const double span = x1 - x0;
    if (span <= 1e-6) {
        out.push_back({ QPointF(x0, y), isNewSpan });
        return;
    }

    QVector<double> pts;
    pts.push_back(x0);

    const double interiorPhase = std::fmod(std::abs(phase), maxLen);
    double curX = x0 + (interiorPhase > 0.1 ? interiorPhase : maxLen);

    while (curX < x1 - 0.2) {
        pts.push_back(curX);
        curX += maxLen;
    }
    pts.push_back(x1);

    if (reverse) {
        std::reverse(pts.begin(), pts.end());
    }

    for (int i = 0; i < pts.size(); ++i) {
        const bool jumpFlag = (i == 0 && isNewSpan);
        out.push_back({ QPointF(pts[i], y), jumpFlag });
    }
}

} // anonymous namespace

StitchSequence GradientFill::generate(const QVector<QPolygonF>& region, const Params& p)
{
    StitchSequence seq;
    if (region.isEmpty() || region[0].size() < 3) return seq;

    seq.palette.push_back(p.color1);
    seq.palette.push_back(p.color2);

    // Rotate region so fill direction θ aligns with horizontal axis
    const double rad = p.angleDeg * M_PI / 180.0;
    const double cosA = std::cos(-rad), sinA = std::sin(-rad);

    auto rotatePt = [cosA, sinA](const QPointF& pt) -> QPointF {
        return QPointF(pt.x() * cosA - pt.y() * sinA,
                       pt.x() * sinA + pt.y() * cosA);
    };

    QVector<QPolygonF> rotRegion;
    rotRegion.reserve(region.size());
    for (const auto& poly : region) {
        QPolygonF rotPoly;
        rotPoly.reserve(poly.size());
        for (const auto& pt : poly) rotPoly.push_back(rotatePt(pt));
        rotRegion.push_back(rotPoly);
    }

    // Determine Y bounds
    double yMin = std::numeric_limits<double>::max();
    double yMax = -std::numeric_limits<double>::max();
    for (const auto& pt : rotRegion[0]) {
        yMin = std::min(yMin, pt.y());
        yMax = std::max(yMax, pt.y());
    }

    const double totalH = yMax - yMin;
    if (totalH <= 0.5) return seq;

    // Optional underlay with Color 1
    if (p.underlay) {
        const double underlayStep = std::clamp(p.maxStitchMm * 0.75, 1.5, 3.0);
        const auto& outer = region[0];
        for (int i = 0; i < outer.size(); ++i) {
            const QPointF& p0 = outer[i];
            const QPointF& p1 = outer[(i + 1) % outer.size()];
            const double len = std::hypot(p1.x() - p0.x(), p1.y() - p0.y());
            const int segs = std::max(1, static_cast<int>(std::ceil(len / underlayStep)));
            for (int s = 0; s < segs; ++s) {
                const double t = static_cast<double>(s) / segs;
                const double x = p0.x() + t * (p1.x() - p0.x());
                const double y = p0.y() + t * (p1.y() - p0.y());
                seq.stitches.emplace_back(x, y, (seq.stitches.empty() ? SF_Jump : SF_Normal), 0);
            }
        }
    }

    auto evalSpacing = [&p](double tNorm, bool invert) -> double {
        double w = std::clamp(tNorm, 0.0, 1.0);
        if (p.curve == Transition::SmoothStep) {
            w = w * w * (3.0 - 2.0 * w);
        }
        if (invert) w = 1.0 - w;
        return p.minSpacingMm + w * (p.maxSpacingMm - p.minSpacingMm);
    };

    QVector<RunPt> pass1Pts;
    QVector<RunPt> pass2Pts;

    // -----------------------------------------------------------------------
    // Pass 1: Color 1 (Dense at yMin -> Sparse at yMax)
    // -----------------------------------------------------------------------
    {
        double yCur = yMin + 0.2;
        int rowIndex = 0;
        while (yCur < yMax - 0.1) {
            const double tNorm = (yCur - yMin) / totalH;
            const double rowSpacing = evalSpacing(tNorm, false);

            QVector<double> xs;
            for (const auto& poly : rotRegion) scanPolygon(poly, yCur, xs);
            std::sort(xs.begin(), xs.end());

            const bool reverse = (rowIndex % 2 == 1);
            const double phase = std::fmod(rowIndex * p.phaseFrac * p.maxStitchMm, p.maxStitchMm);

            for (int i = 0; i + 1 < xs.size(); i += 2) {
                const double x0 = xs[i], x1 = xs[i + 1];
                if (x1 - x0 < 0.2) continue;
                emitScanRun(x0, x1, yCur, p.maxStitchMm, phase, reverse, (i > 0), pass1Pts);
            }

            yCur += rowSpacing;
            rowIndex++;
        }
    }

    // -----------------------------------------------------------------------
    // Pass 2: Color 2 (Sparse at yMin -> Dense at yMax, interleaved starting Y)
    // -----------------------------------------------------------------------
    {
        const double initSpacing2 = evalSpacing(0.0, true);
        double yCur = yMin + 0.2 + initSpacing2 * 0.5;
        int rowIndex = 0;
        while (yCur < yMax - 0.1) {
            const double tNorm = (yCur - yMin) / totalH;
            const double rowSpacing = evalSpacing(tNorm, true);

            QVector<double> xs;
            for (const auto& poly : rotRegion) scanPolygon(poly, yCur, xs);
            std::sort(xs.begin(), xs.end());

            const bool reverse = (rowIndex % 2 == 1);
            // 0.5 phase shift relative to pass 1 prevents puncture collisions
            const double phase = std::fmod((rowIndex + 0.5) * p.phaseFrac * p.maxStitchMm, p.maxStitchMm);

            for (int i = 0; i + 1 < xs.size(); i += 2) {
                const double x0 = xs[i], x1 = xs[i + 1];
                if (x1 - x0 < 0.2) continue;
                emitScanRun(x0, x1, yCur, p.maxStitchMm, phase, reverse, (i > 0), pass2Pts);
            }

            yCur += rowSpacing;
            rowIndex++;
        }
    }

    const double uncosA = std::cos(rad), unsinA = std::sin(rad);
    auto unrotatePt = [uncosA, unsinA](const QPointF& pt) -> QPointF {
        return QPointF(pt.x() * uncosA - pt.y() * unsinA,
                       pt.x() * unsinA + pt.y() * uncosA);
    };

    // Append Pass 1 stitches (Color 0)
    for (int i = 0; i < pass1Pts.size(); ++i) {
        const QPointF worldPt = unrotatePt(pass1Pts[i].pt);
        quint32 flags = pass1Pts[i].jump ? SF_Jump : SF_Normal;
        if (seq.stitches.empty() && flags == SF_Normal) flags = SF_Jump;
        seq.stitches.emplace_back(worldPt.x(), worldPt.y(), flags, 0);
    }

    // Append Pass 2 stitches (Color 1) preceded by SF_ColorChange
    bool firstPass2 = true;
    for (int i = 0; i < pass2Pts.size(); ++i) {
        const QPointF worldPt = unrotatePt(pass2Pts[i].pt);
        quint32 flags = SF_Normal;
        if (firstPass2) {
            flags = SF_ColorChange | SF_Jump;
            firstPass2 = false;
        } else if (pass2Pts[i].jump) {
            flags = SF_Jump;
        }
        seq.stitches.emplace_back(worldPt.x(), worldPt.y(), flags, 1);
    }

    if (!seq.stitches.empty()) {
        seq.stitches.back().flags |= SF_End;
    }

    return seq;
}

StitchSequence GradientFill::generate(const QPolygonF& outer, const Params& p)
{
    return generate(QVector<QPolygonF>{ outer }, p);
}

StitchSequence GradientFill::generate(const QPainterPath& path, const Params& p)
{
    const QList<QPolygonF> polys = path.toSubpathPolygons();
    QVector<QPolygonF> region;
    region.reserve(polys.size());
    for (const auto& poly : polys) {
        if (poly.size() >= 3) region.push_back(poly);
    }
    return generate(region, p);
}

} // namespace stick
