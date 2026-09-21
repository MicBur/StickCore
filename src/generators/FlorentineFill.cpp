// ---------------------------------------------------------------------------
//  StickCore  –  FlorentineFill.cpp
// ---------------------------------------------------------------------------
#include "generators/FlorentineFill.h"

#include <cmath>
#include <algorithm>
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

// Subdivide a polygon so long edges bend smoothly under coordinate warping
QPolygonF densifyPolygon(const QPolygonF& poly, double maxEdgeLenMm)
{
    QPolygonF result;
    const int n = poly.size();
    if (n < 2) return poly;

    for (int i = 0; i < n; ++i) {
        const QPointF& p0 = poly[i];
        const QPointF& p1 = poly[(i + 1) % n];
        const double len = std::hypot(p1.x() - p0.x(), p1.y() - p0.y());
        const int segs = std::max(1, static_cast<int>(std::ceil(len / maxEdgeLenMm)));
        for (int s = 0; s < segs; ++s) {
            const double t = static_cast<double>(s) / segs;
            result.push_back(QPointF(p0.x() + t * (p1.x() - p0.x()),
                                     p0.y() + t * (p1.y() - p0.y())));
        }
    }
    return result;
}

} // anonymous namespace

StitchSequence FlorentineFill::generate(const QVector<QPolygonF>& region, const Params& p)
{
    StitchSequence seq;
    if (region.isEmpty() || region[0].size() < 3) return seq;

    seq.palette.push_back(p.threadColor);

    // Rotate region so wave progression aligns horizontally
    const double rad = p.angleDeg * M_PI / 180.0;
    const double cosA = std::cos(-rad), sinA = std::sin(-rad);

    auto rotatePt = [cosA, sinA](const QPointF& pt) -> QPointF {
        return QPointF(pt.x() * cosA - pt.y() * sinA,
                       pt.x() * sinA + pt.y() * cosA);
    };

    const double wavelength = std::max(5.0, p.wavelengthMm);
    const double k = (2.0 * M_PI) / wavelength;
    const double amp = p.amplitudeMm;
    const double phaseRad = p.phaseRad;

    auto waveOffset = [amp, k, phaseRad](double x) -> double {
        return amp * std::sin(k * x + phaseRad);
    };

    // Forward warp: (x, y) -> (x, y - waveOffset(x))
    QVector<QPolygonF> warpedRegion;
    warpedRegion.reserve(region.size());
    for (const auto& poly : region) {
        const QPolygonF dense = densifyPolygon(poly, 0.60);
        QPolygonF warped;
        warped.reserve(dense.size());
        for (const auto& pt : dense) {
            const QPointF rPt = rotatePt(pt);
            warped.push_back(QPointF(rPt.x(), rPt.y() - waveOffset(rPt.x())));
        }
        warpedRegion.push_back(warped);
    }

    // Determine Y bounds in warped coordinate space
    double yMin = std::numeric_limits<double>::max();
    double yMax = -std::numeric_limits<double>::max();
    for (const auto& pt : warpedRegion[0]) {
        yMin = std::min(yMin, pt.y());
        yMax = std::max(yMax, pt.y());
    }

    if (yMax - yMin <= 0.2) return seq;

    // Optional underlay
    if (p.underlay) {
        const auto& outer = region[0];
        const double underlayStep = 2.5;
        for (int i = 0; i < outer.size(); ++i) {
            const QPointF& p0 = outer[i];
            const QPointF& p1 = outer[(i + 1) % outer.size()];
            const double len = std::hypot(p1.x() - p0.x(), p1.y() - p0.y());
            const int segs = std::max(1, static_cast<int>(std::ceil(len / underlayStep)));
            for (int s = 0; s < segs; ++s) {
                const double t = static_cast<double>(s) / segs;
                const double x = p0.x() + t * (p1.x() - p0.x());
                const double y = p0.y() + t * (p1.y() - p0.y());
                seq.stitches.emplace_back(x, y, (seq.stitches.empty() ? SF_Jump : SF_Normal), p.colorIdx);
            }
        }
    }

    const double rowSpacing = std::clamp(p.rowSpacingMm, 0.20, 2.00);
    const double maxStitch = std::clamp(p.maxStitchMm, 1.5, 6.0);

    const double uncosA = std::cos(rad), unsinA = std::sin(rad);
    auto unrotatePt = [uncosA, unsinA](const QPointF& pt) -> QPointF {
        return QPointF(pt.x() * uncosA - pt.y() * unsinA,
                       pt.x() * unsinA + pt.y() * uncosA);
    };

    int rowIndex = 0;
    for (double yCur = yMin + rowSpacing * 0.5; yCur < yMax; yCur += rowSpacing, ++rowIndex) {
        QVector<double> xs;
        for (const auto& poly : warpedRegion) scanPolygon(poly, yCur, xs);
        std::sort(xs.begin(), xs.end());

        const bool reverse = (rowIndex % 2 == 1);
        const double phase = std::fmod(rowIndex * p.phaseFrac * maxStitch, maxStitch);

        for (int i = 0; i + 1 < xs.size(); i += 2) {
            const double x0 = xs[i], x1 = xs[i + 1];
            if (x1 - x0 < 0.2) continue;

            QVector<double> rowX;
            rowX.push_back(x0);
            const double interiorPhase = std::fmod(std::abs(phase), maxStitch);
            double curX = x0 + (interiorPhase > 0.1 ? interiorPhase : maxStitch);
            while (curX < x1 - 0.2) {
                rowX.push_back(curX);
                curX += maxStitch;
            }
            rowX.push_back(x1);

            if (reverse) std::reverse(rowX.begin(), rowX.end());

            for (int k = 0; k < rowX.size(); ++k) {
                const double x = rowX[k];
                // Inverse warp: y = yCur + waveOffset(x)
                const double yRot = yCur + waveOffset(x);
                const QPointF worldPt = unrotatePt(QPointF(x, yRot));
                quint32 flags = (k == 0 && i > 0) ? SF_Jump : SF_Normal;
                if (seq.stitches.empty() && flags == SF_Normal) flags = SF_Jump;
                seq.stitches.emplace_back(worldPt.x(), worldPt.y(), flags, p.colorIdx);
            }
        }
    }

    if (!seq.stitches.empty()) {
        seq.stitches.back().flags |= SF_End;
    }
    return seq;
}

StitchSequence FlorentineFill::generate(const QPolygonF& outer, const Params& p)
{
    return generate(QVector<QPolygonF>{ outer }, p);
}

StitchSequence FlorentineFill::generate(const QPainterPath& path, const Params& p)
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
