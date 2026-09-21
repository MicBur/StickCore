// ---------------------------------------------------------------------------
//  StickCore  –  Underlay.cpp
// ---------------------------------------------------------------------------
#include "generators/Underlay.h"
#include "generators/TatamiFill.h"
#include "core/Geometry.h"

#include <cmath>

namespace stick {

namespace {

// Inset a closed polygon by 'insetMm' toward the interior so underlay
// does not peek out beyond the finished cover stitches.
QPolygonF insetPolygon(const QPolygonF& poly, double insetMm)
{
    const int n = poly.size();
    if (n < 3 || insetMm <= 1e-4) return poly;

    double area = 0.0;
    for (int i = 0; i < n; ++i) {
        const QPointF& p1 = poly[i];
        const QPointF& p2 = poly[(i + 1) % n];
        area += (p1.x() * p2.y() - p2.x() * p1.y());
    }
    const bool ccw = (area > 0.0);

    QPolygonF res;
    res.reserve(n);

    for (int i = 0; i < n; ++i) {
        const QPointF& pPrev = poly[(i - 1 + n) % n];
        const QPointF& pCurr = poly[i];
        const QPointF& pNext = poly[(i + 1) % n];

        const QPointF v1 = normalized(pCurr - pPrev);
        const QPointF v2 = normalized(pNext - pCurr);

        if (length(v1) < 1e-6 || length(v2) < 1e-6) {
            res.push_back(pCurr);
            continue;
        }

        const QPointF n1 = ccw ? perpLeft(v1) : perpRight(v1);
        const QPointF n2 = ccw ? perpLeft(v2) : perpRight(v2);

        QPointF bisector = normalized(n1 + n2);
        double denom = dot(bisector, n1);
        if (denom < 0.2) {
            bisector = n1;
            denom = 1.0;
        }
        const double miterLen = std::min(insetMm / denom, insetMm * 2.5);
        res.push_back(pCurr + bisector * miterLen);
    }
    return res;
}

} // namespace

void Underlay::edgeRun(const QPolygonF& poly, double stepMm, int colorIdx,
                       StitchSequence& out, double insetMm)
{
    if (poly.size() < 2) return;
    const double step = std::max(stepMm, 0.5);
    const QPolygonF work = (poly.size() >= 3 && insetMm > 1e-4) ? insetPolygon(poly, insetMm) : poly;

    out.add(work.first().x(), work.first().y(), SF_Jump, colorIdx);
    auto run = [&](const QPointF& a, const QPointF& b) {
        const double L = length(b - a);
        const int n = std::max(1, int(std::ceil(L / step)));
        for (int k = 1; k <= n; ++k) {
            const double f = double(k) / n;
            out.add(a.x() + (b.x()-a.x())*f, a.y() + (b.y()-a.y())*f, SF_Normal, colorIdx);
        }
    };
    for (int i = 1; i < work.size(); ++i) run(work[i-1], work[i]);
    run(work.last(), work.first());   // close the loop
}

StitchSequence Underlay::forFill(const QVector<QPolygonF>& region,
                                 double fillAngleDeg, int colorIdx, double stepMm, double insetMm)
{
    StitchSequence out;
    // 1) Edge run around every ring (outer boundary + holes), inset inward.
    for (const QPolygonF& ring : region)
        edgeRun(ring, stepMm, colorIdx, out, insetMm);

    // 2) Sparse cross layer ~90° to the top fill, wide row spacing.
    StitchSequence cross = TatamiFill::fillOnly(region, fillAngleDeg + 90.0,
                                                /*rowSpacing*/ 2.6, /*maxStitch*/ 2.6,
                                                /*phase*/ 0.0, colorIdx);
    out.stitches.insert(out.stitches.end(), cross.stitches.begin(), cross.stitches.end());
    return out;
}

} // namespace stick
