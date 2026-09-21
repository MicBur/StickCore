// ---------------------------------------------------------------------------
//  StickCore  –  TatamiFill.cpp
//
//  TATAMI SCAN-LINE PATTERN FILL
//  -----------------------------
//   1. Rotate the region by -θ so the requested fill direction becomes the
//      horizontal axis; all scan-line maths is then trivial.
//   2. Sweep horizontal scan-lines from yMin to yMax, spaced by rowSpacing S.
//   3. For each scan-line collect edge intersections (outer boundary AND
//      holes), sort by x and pair them with the even-odd rule -> the inside
//      spans.
//   4. Split every span into stitches no longer than L_max.
//   5. Shift the interior puncture points along successive rows by a phase
//      offset (phaseFrac × L_max) so the punctures never line up column-to-
//      column — this is what prevents the tell-tale vertical "groove".
//   6. Rows are stitched in boustrophedon order (alternating direction) to
//      minimise travel, then everything is rotated back by +θ.
// ---------------------------------------------------------------------------
#include "generators/TatamiFill.h"
#include "generators/Underlay.h"
#include "core/Geometry.h"

#include <QTransform>
#include <algorithm>
#include <cmath>
#include <limits>

namespace stick {

namespace {

// Intersect the horizontal line y = yScan against one polygon, appending the
// x-coordinates of all crossings. Uses the half-open [yLo, yHi) rule so shared
// vertices are counted exactly once.
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
        if (yScan < yLo || yScan >= yHi) continue;      // half-open
        const double t = (yScan - y0) / (y1 - y0);
        xs.push_back(a.x() + t * (b.x() - a.x()));
    }
}

struct RunPoint {
    QPointF pt;
    bool jump = false;
};

// Emit a run from x0 to x1 (x0 < x1) at height y, broken into <= L_max
// segments, with the interior breakpoints shifted by 'phase'. 'reverse'
// stitches the run right-to-left. 'isNewSpan' flags the first point as a travel move.
void emitRun(double x0, double x1, double y, double maxLen, double phase,
             bool reverse, bool isNewSpan, QVector<RunPoint>& out)
{
    const double span = x1 - x0;
    if (span <= 1e-6) { out.push_back({ QPointF(x0, y), isNewSpan }); return; }

    // Build the list of x break positions left-to-right.
    QVector<double> xb;
    xb.push_back(x0);
    // First interior break is pulled in by 'phase' so rows do not align.
    double ph = std::fmod(phase, maxLen);
    if (ph < 0) ph += maxLen;
    double first = x0 + (maxLen - ph);
    if (first <= x0 + 1e-6) first += maxLen;
    for (double x = first; x < x1 - 1e-6; x += maxLen)
        xb.push_back(x);
    xb.push_back(x1);

    bool firstPt = true;
    if (reverse) {
        for (int i = xb.size() - 1; i >= 0; --i) {
            out.push_back({ QPointF(xb[i], y), firstPt && isNewSpan });
            firstPt = false;
        }
    } else {
        for (double x : xb) {
            out.push_back({ QPointF(x, y), firstPt && isNewSpan });
            firstPt = false;
        }
    }
}

} // namespace

StitchSequence TatamiFill::generate(const QPolygonF& outer, const Params& p)
{
    QVector<QPolygonF> region;
    region.push_back(outer);
    return generate(region, p);
}

StitchSequence TatamiFill::generate(const QVector<QPolygonF>& region,
                                    const Params& p)
{
    StitchSequence seq;
    if (region.isEmpty() || region.front().size() < 3)
        return seq;

    // Underlay first (edge run + a sparse cross layer), then the cover fill.
    if (p.underlay) {
        StitchSequence ul = Underlay::forFill(region, p.fillAngleDeg, p.colorIdx);
        seq.stitches.insert(seq.stitches.end(), ul.stitches.begin(), ul.stitches.end());
    }
    StitchSequence cover = fillOnly(region, p.fillAngleDeg, p.rowSpacingMm,
                                    p.maxStitchMm, p.phaseFrac, p.colorIdx);
    seq.stitches.insert(seq.stitches.end(), cover.stitches.begin(), cover.stitches.end());

    if (seq.palette.empty())
        seq.palette.emplace_back(QColor(40, 60, 140), QStringLiteral("Tatami"));
    return seq;
}

StitchSequence TatamiFill::fillOnly(const QVector<QPolygonF>& region,
                                    double angleDeg, double rowSpacingMm,
                                    double maxStitchMm, double phaseFrac, int colorIdx)
{
    StitchSequence seq;
    if (region.isEmpty() || region.front().size() < 3)
        return seq;

    const double theta = angleDeg;
    QTransform rot;     rot.rotate(-theta);   // region -> scan frame
    QTransform rotInv;  rotInv.rotate(theta);  // scan frame -> region

    // Rotate every ring into the scan frame and find the bounding box.
    QVector<QPolygonF> rr;
    rr.reserve(region.size());
    double minY =  std::numeric_limits<double>::max();
    double maxY = -std::numeric_limits<double>::max();
    for (const QPolygonF& ring : region) {
        QPolygonF r = rot.map(ring);
        rr.push_back(r);
        for (const QPointF& pt : r) {
            minY = std::min(minY, pt.y());
            maxY = std::max(maxY, pt.y());
        }
    }
    if (!(maxY > minY)) return seq;

    const double S      = std::max(rowSpacingMm, 1e-3);
    const double maxLen = std::max(maxStitchMm, 0.5);
    const double phaseStep = phaseFrac * maxLen;

    QVector<RunPoint> pts;              // ordered stitch points (scan frame)
    int row = 0;
    bool reverse = false;               // boustrophedon toggle
    // Nudge the first scan-line inside the shape to avoid grazing a vertex.
    for (double y = minY + 0.5 * S; y < maxY; y += S, ++row) {
        QVector<double> xs;
        for (const QPolygonF& r : rr)
            scanPolygon(r, y, xs);
        if (xs.size() < 2) continue;
        std::sort(xs.begin(), xs.end());

        const double phase = phaseStep * row;

        // Pair crossings (even-odd). For boustrophedon we consume spans in the
        // travel direction of this row.
        const int pairs = xs.size() / 2;
        if (!reverse) {
            for (int k = 0; k < pairs; ++k) {
                const bool isNewSpan = (k > 0) || pts.isEmpty();
                emitRun(xs[2 * k], xs[2 * k + 1], y, maxLen, phase, false, isNewSpan, pts);
            }
        } else {
            for (int k = pairs - 1; k >= 0; --k) {
                const bool isNewSpan = (k < pairs - 1) || pts.isEmpty();
                emitRun(xs[2 * k], xs[2 * k + 1], y, maxLen, phase, true, isNewSpan, pts);
            }
        }
        reverse = !reverse;
    }

    if (pts.isEmpty()) return seq;

    // Rotate the whole path back into the region frame and emit stitches.
    // Explicit span transitions or any move longer than L_max become SF_Jump.
    bool first = true;
    QPointF prev;
    for (const RunPoint& rp : pts) {
        const QPointF wp = rotInv.map(rp.pt);
        if (first || rp.jump) {
            seq.add(wp.x(), wp.y(), SF_Jump, colorIdx);
            first = false;
        } else {
            const double d = length(wp - prev);
            const quint32 fl = (d > maxLen * 1.0001) ? SF_Jump : SF_Normal;
            seq.add(wp.x(), wp.y(), fl, colorIdx);
        }
        prev = wp;
    }

    return seq;
}

} // namespace stick
