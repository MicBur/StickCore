// ---------------------------------------------------------------------------
//  StickCore  –  StipplingGenerator.cpp
// ---------------------------------------------------------------------------
#include "generators/StipplingGenerator.h"

#include <QRectF>
#include <cmath>
#include <algorithm>
#include <vector>

namespace stick {

namespace {

bool isPointInsideRegion(const QPointF& pt, const QVector<QPolygonF>& region, double marginMm)
{
    if (region.isEmpty()) return false;
    if (!region[0].containsPoint(pt, Qt::OddEvenFill)) return false;

    for (int h = 1; h < region.size(); ++h) {
        if (region[h].containsPoint(pt, Qt::OddEvenFill)) return false;
    }

    if (marginMm > 0.0) {
        const double marginSq = marginMm * marginMm;
        for (const auto& poly : region) {
            const int n = poly.size();
            for (int i = 0; i < n; ++i) {
                const QPointF& a = poly[i];
                const QPointF& b = poly[(i + 1) % n];
                const double dx = b.x() - a.x(), dy = b.y() - a.y();
                const double lenSq = dx * dx + dy * dy;
                double distSq;
                if (lenSq < 1e-6) {
                    distSq = (pt.x() - a.x()) * (pt.x() - a.x()) + (pt.y() - a.y()) * (pt.y() - a.y());
                } else {
                    const double u = std::clamp(((pt.x() - a.x()) * dx + (pt.y() - a.y()) * dy) / lenSq, 0.0, 1.0);
                    const double px = a.x() + u * dx;
                    const double py = a.y() + u * dy;
                    distSq = (pt.x() - px) * (pt.x() - px) + (pt.y() - py) * (pt.y() - py);
                }
                if (distSq < marginSq) return false;
            }
        }
    }
    return true;
}

// Generates smooth meandering cubic splines through a sequence of waypoints
QVector<QPointF> sampleCubicSpline(const QVector<QPointF>& wpts, double stepMm)
{
    QVector<QPointF> result;
    if (wpts.size() < 2) return wpts;

    result.push_back(wpts[0]);
    for (int i = 0; i + 1 < wpts.size(); ++i) {
        const QPointF& p0 = (i > 0) ? wpts[i - 1] : wpts[i];
        const QPointF& p1 = wpts[i];
        const QPointF& p2 = wpts[i + 1];
        const QPointF& p3 = (i + 2 < wpts.size()) ? wpts[i + 2] : p2;

        const double dist = std::hypot(p2.x() - p1.x(), p2.y() - p1.y());
        const int segs = std::max(2, static_cast<int>(std::ceil(dist / stepMm)));

        for (int s = 1; s <= segs; ++s) {
            const double t = static_cast<double>(s) / segs;
            const double t2 = t * t;
            const double t3 = t2 * t;

            // Catmull-Rom spline formulation
            const double f1 = -0.5 * t3 + t2 - 0.5 * t;
            const double f2 =  1.5 * t3 - 2.5 * t2 + 1.0;
            const double f3 = -1.5 * t3 + 2.0 * t2 + 0.5 * t;
            const double f4 =  0.5 * t3 - 0.5 * t2;

            const double x = f1 * p0.x() + f2 * p1.x() + f3 * p2.x() + f4 * p3.x();
            const double y = f1 * p0.y() + f2 * p1.y() + f3 * p2.y() + f4 * p3.y();
            result.push_back(QPointF(x, y));
        }
    }
    return result;
}

} // anonymous namespace

StitchSequence StipplingGenerator::generate(const QVector<QPolygonF>& region, const Params& p)
{
    StitchSequence seq;
    if (region.isEmpty() || region[0].size() < 3) return seq;

    seq.palette.push_back(p.threadColor);

    const QRectF bounds = region[0].boundingRect();
    const double spacing = std::clamp(p.loopSpacingMm, 2.0, 10.0);
    const double margin = std::max(0.0, p.marginMm);

    const int cols = std::max(1, static_cast<int>(std::ceil(bounds.width() / spacing)));
    const int rows = std::max(1, static_cast<int>(std::ceil(bounds.height() / spacing)));

    // Grid of interior points
    std::vector<std::vector<QPointF>> grid(rows, std::vector<QPointF>(cols, QPointF(0, 0)));
    std::vector<std::vector<bool>> valid(rows, std::vector<bool>(cols, false));

    for (int r = 0; r < rows; ++r) {
        const double baseCy = bounds.top() + (r + 0.5) * (bounds.height() / rows);
        for (int c = 0; c < cols; ++c) {
            const double baseCx = bounds.left() + (c + 0.5) * (bounds.width() / cols);

            // Add organic wiggle so the loops form classic rounded puzzle shapes
            const double phase = r * 1.7 + c * 2.3;
            const double jx = std::sin(phase) * (spacing * 0.22);
            const double jy = std::cos(phase) * (spacing * 0.22);
            const QPointF testPt(baseCx + jx, baseCy + jy);

            if (isPointInsideRegion(testPt, region, margin)) {
                grid[r][c] = testPt;
                valid[r][c] = true;
            }
        }
    }

    // Traverse the valid cells in serpentine meander order
    QVector<QPointF> waypoints;
    for (int r = 0; r < rows; ++r) {
        QVector<QPointF> rowPts;
        if (r % 2 == 0) {
            for (int c = 0; c < cols; ++c) {
                if (valid[r][c]) rowPts.push_back(grid[r][c]);
            }
        } else {
            for (int c = cols - 1; c >= 0; --c) {
                if (valid[r][c]) rowPts.push_back(grid[r][c]);
            }
        }

        // Add intermediate loop nodes to create the characteristic meandering curl
        for (int i = 0; i < rowPts.size(); ++i) {
            waypoints.push_back(rowPts[i]);
            if (i + 1 < rowPts.size()) {
                const QPointF& pA = rowPts[i];
                const QPointF& pB = rowPts[i + 1];
                const double mx = (pA.x() + pB.x()) * 0.5;
                const double my = (pA.y() + pB.y()) * 0.5;
                // Alternate perpendicular loop displacement
                const double curlSign = ((i + r) % 2 == 0) ? 1.0 : -1.0;
                const double nx = -(pB.y() - pA.y()) * 0.35 * curlSign;
                const double ny =  (pB.x() - pA.x()) * 0.35 * curlSign;
                const QPointF loopPt(mx + nx, my + ny);
                if (isPointInsideRegion(loopPt, region, margin * 0.7)) {
                    waypoints.push_back(loopPt);
                }
            }
        }
    }

    if (waypoints.size() < 2) return seq;

    const double step = std::clamp(p.stitchLengthMm, 1.2, 4.0);
    const QVector<QPointF> sampled = sampleCubicSpline(waypoints, step);

    for (int i = 0; i < sampled.size(); ++i) {
        quint32 flags = (i == 0 ? SF_Jump : SF_Normal);
        seq.stitches.emplace_back(sampled[i].x(), sampled[i].y(), flags, p.colorIdx);
    }

    if (!seq.stitches.empty()) {
        seq.stitches.back().flags |= SF_End;
    }
    return seq;
}

StitchSequence StipplingGenerator::generate(const QPolygonF& outer, const Params& p)
{
    return generate(QVector<QPolygonF>{ outer }, p);
}

StitchSequence StipplingGenerator::generate(const QPainterPath& path, const Params& p)
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
