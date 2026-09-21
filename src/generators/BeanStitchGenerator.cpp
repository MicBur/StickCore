// ---------------------------------------------------------------------------
//  StickCore  –  BeanStitchGenerator.cpp
// ---------------------------------------------------------------------------
#include "generators/BeanStitchGenerator.h"

#include <cmath>
#include <algorithm>

namespace stick {

namespace {

QVector<QPointF> subdividePolyline(const QVector<QPointF>& pts, double maxStepMm, bool closed)
{
    QVector<QPointF> result;
    if (pts.size() < 2) return pts;

    const int count = pts.size();
    const int numEdges = closed ? count : (count - 1);

    result.push_back(pts[0]);
    for (int i = 0; i < numEdges; ++i) {
        const QPointF& p0 = pts[i];
        const QPointF& p1 = pts[(i + 1) % count];
        const double dist = std::hypot(p1.x() - p0.x(), p1.y() - p0.y());
        if (dist <= 1e-4) continue;

        const int segs = std::max(1, static_cast<int>(std::round(dist / maxStepMm)));
        for (int s = 1; s <= segs; ++s) {
            const double t = static_cast<double>(s) / segs;
            result.push_back(QPointF(p0.x() + t * (p1.x() - p0.x()),
                                    p0.y() + t * (p1.y() - p0.y())));
        }
    }
    return result;
}

void emitBeanStitches(const QVector<QPointF>& subPts,
                      BeanStitchGenerator::Mode mode,
                      int colorIdx,
                      bool isFirstSubpath,
                      StitchSequence& seq)
{
    if (subPts.size() < 2) return;

    // First node of the subpath
    quint32 initialFlags = SF_Jump;
    seq.stitches.emplace_back(subPts[0].x(), subPts[0].y(), initialFlags, colorIdx);

    // Each consecutive segment is stepped forward-backward-forward
    for (int i = 0; i + 1 < subPts.size(); ++i) {
        const QPointF& from = subPts[i];
        const QPointF& to   = subPts[i + 1];

        // 1. Forward to next node
        seq.stitches.emplace_back(to.x(), to.y(), SF_Normal, colorIdx);

        // 2. Backward to previous node
        seq.stitches.emplace_back(from.x(), from.y(), SF_Normal, colorIdx);

        // 3. Forward to next node
        seq.stitches.emplace_back(to.x(), to.y(), SF_Normal, colorIdx);

        if (mode == BeanStitchGenerator::Mode::FivePass) {
            // 4. Backward to previous node
            seq.stitches.emplace_back(from.x(), from.y(), SF_Normal, colorIdx);
            // 5. Forward to next node
            seq.stitches.emplace_back(to.x(), to.y(), SF_Normal, colorIdx);
        }
    }
}

} // anonymous namespace

StitchSequence BeanStitchGenerator::generate(const QVector<QPointF>& points, const Params& p)
{
    StitchSequence seq;
    if (points.size() < 2) return seq;

    seq.palette.push_back(p.threadColor);

    const double step = std::clamp(p.stitchLengthMm, 1.0, 6.0);
    const QVector<QPointF> subPts = subdividePolyline(points, step, p.closed);
    emitBeanStitches(subPts, p.mode, p.colorIdx, true, seq);

    if (!seq.stitches.empty()) {
        seq.stitches.back().flags |= SF_End;
    }
    return seq;
}

StitchSequence BeanStitchGenerator::generate(const QPolygonF& poly, const Params& p)
{
    Params cp = p;
    cp.closed = poly.isClosed();
    return generate(poly.toList().toVector(), cp);
}

StitchSequence BeanStitchGenerator::generate(const QPainterPath& path, const Params& p)
{
    StitchSequence seq;
    if (path.isEmpty()) return seq;

    seq.palette.push_back(p.threadColor);

    const double step = std::clamp(p.stitchLengthMm, 1.0, 6.0);
    const QList<QPolygonF> subpaths = path.toSubpathPolygons();

    bool isFirst = true;
    for (const auto& poly : subpaths) {
        if (poly.size() < 2) continue;
        const bool closed = poly.isClosed();
        const QVector<QPointF> subPts = subdividePolyline(poly.toList().toVector(), step, closed);
        emitBeanStitches(subPts, p.mode, p.colorIdx, isFirst, seq);
        isFirst = false;
    }

    if (!seq.stitches.empty()) {
        seq.stitches.back().flags |= SF_End;
    }
    return seq;
}

} // namespace stick
