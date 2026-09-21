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
#include "generators/ContourFill.h"
#include "core/Geometry.h"

#include <QPainterPath>
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
// segments, with the interior breakpoints shifted by 'phase'.
// Any carving needle penetrations in rowCarvingXs that fall inside [x0, x1]
// are enforced as mandatory puncture milestones.
// 'reverse' stitches the run right-to-left. 'isNewSpan' flags the first point as a travel move.
void emitRun(double x0, double x1, double y, double maxLen, double phase,
             const QVector<double>& rowCarvingXs,
             bool reverse, bool isNewSpan, QVector<RunPoint>& out)
{
    const double span = x1 - x0;
    if (span <= 1e-6) { out.push_back({ QPointF(x0, y), isNewSpan }); return; }

    // Find carving intersections within this span with a safety margin from edges
    QVector<double> cWithin;
    for (double cx : rowCarvingXs) {
        if (cx > x0 + 0.25 && cx < x1 - 0.25) {
            cWithin.push_back(cx);
        }
    }
    std::sort(cWithin.begin(), cWithin.end());

    // Filter points that are too close to each other (< 0.3mm)
    QVector<double> filteredCarve;
    for (double cx : cWithin) {
        if (filteredCarve.isEmpty() || std::abs(cx - filteredCarve.back()) >= 0.3) {
            filteredCarve.push_back(cx);
        }
    }

    // Build milestones: x0, carve1, carve2, ..., x1
    QVector<double> milestones;
    milestones.push_back(x0);
    for (double cx : filteredCarve) {
        milestones.push_back(cx);
    }
    milestones.push_back(x1);

    // Build the list of x break positions left-to-right.
    QVector<double> xb;
    xb.push_back(x0);

    for (int m = 0; m < milestones.size() - 1; ++m) {
        const double segStart = milestones[m];
        const double segEnd = milestones[m + 1];
        const double segLen = segEnd - segStart;
        if (segLen <= 1e-6) continue;

        if (segLen <= maxLen) {
            xb.push_back(segEnd);
        } else {
            if (filteredCarve.isEmpty() && m == 0) {
                // Classic phase-offset tatami
                double ph = std::fmod(phase, maxLen);
                if (ph < 0) ph += maxLen;
                double first = segStart + (maxLen - ph);
                if (first <= segStart + 1e-6) first += maxLen;
                for (double x = first; x < segEnd - 1e-6; x += maxLen)
                    xb.push_back(x);
            } else {
                const int steps = static_cast<int>(std::ceil(segLen / maxLen));
                const double stepSize = segLen / steps;
                for (int s = 1; s < steps; ++s) {
                    xb.push_back(segStart + s * stepSize);
                }
            }
            xb.push_back(segEnd);
        }
    }

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

QString TatamiFill::patternName(PatternType p)
{
    switch (p) {
        case PatternType::StandardTatami: return QStringLiteral("Standard-Tatami (1/4 Versatz, glatt)");
        case PatternType::Brick:          return QStringLiteral("Brick / Ziegel (1/2 Versatz, robust)");
        case PatternType::Twill:          return QStringLiteral("Twill / Köper (1/3 Versatz, Seidenglanz)");
        case PatternType::Basketweave:    return QStringLiteral("Korbgeflecht (Block-Textur)");
        case PatternType::Honeycomb:      return QStringLiteral("Honeycomb / Waben (Gitternetz)");
        case PatternType::ContourEcho:     return QStringLiteral("Kontur / Jahresringe (Folgt der Form)");
    }
    return QStringLiteral("Standard-Tatami");
}

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

    // 1. Contour / Echo Fill (concentric rings following region boundaries)
    if (p.pattern == PatternType::ContourEcho) {
        if (p.underlay) {
            StitchSequence ul = Underlay::forFill(region, p.fillAngleDeg, p.colorIdx);
            seq.stitches.insert(seq.stitches.end(), ul.stitches.begin(), ul.stitches.end());
        }
        QPainterPath path;
        path.addPolygon(region[0]);
        for (int i = 1; i < region.size(); ++i) {
            QPainterPath hole;
            hole.addPolygon(region[i]);
            path = path.subtracted(hole);
        }
        ContourFill::Params cp;
        cp.spacingMm = std::max(0.3, p.rowSpacingMm * 1.5);
        cp.maxStitchMm = p.maxStitchMm;
        cp.colorIdx = p.colorIdx;
        StitchSequence cseq = ContourFill::generate(path, cp);
        if (!cseq.empty()) {
            seq.stitches.insert(seq.stitches.end(), cseq.stitches.begin(), cseq.stitches.end());
            if (seq.palette.empty())
                seq.palette.emplace_back(QColor(40, 60, 140), QStringLiteral("Kontur-Füllung"));
            return seq;
        }
    }

    // 2. Honeycomb / Waben Lattice (two cross-hatching passes at θ - 30° and θ + 30°)
    if (p.pattern == PatternType::Honeycomb) {
        if (p.underlay) {
            StitchSequence ul = Underlay::forFill(region, p.fillAngleDeg, p.colorIdx);
            seq.stitches.insert(seq.stitches.end(), ul.stitches.begin(), ul.stitches.end());
        }
        const double latticeSpacing = p.rowSpacingMm * 1.75;
        StitchSequence pass1 = fillOnly(region, p.fillAngleDeg - 30.0, latticeSpacing,
                                        p.maxStitchMm, 0.5, p.colorIdx);
        StitchSequence pass2 = fillOnly(region, p.fillAngleDeg + 30.0, latticeSpacing,
                                        p.maxStitchMm, 0.5, p.colorIdx);
        seq.stitches.insert(seq.stitches.end(), pass1.stitches.begin(), pass1.stitches.end());
        seq.stitches.insert(seq.stitches.end(), pass2.stitches.begin(), pass2.stitches.end());
        if (seq.palette.empty())
            seq.palette.emplace_back(QColor(40, 60, 140), QStringLiteral("Waben"));
        return seq;
    }

    // 3. Scan-line patterns: StandardTatami, Brick, Twill, Basketweave
    double effectivePhase = p.phaseFrac;
    if (p.pattern == PatternType::StandardTatami)
        effectivePhase = 0.25;
    else if (p.pattern == PatternType::Brick)
        effectivePhase = 0.50;
    else if (p.pattern == PatternType::Twill)
        effectivePhase = 0.333333;
    else if (p.pattern == PatternType::Basketweave)
        effectivePhase = -1.0; // Sentinel for 4-phase block cycle

    if (p.underlay) {
        StitchSequence ul = Underlay::forFill(region, p.fillAngleDeg, p.colorIdx);
        seq.stitches.insert(seq.stitches.end(), ul.stitches.begin(), ul.stitches.end());
    }

    QPainterPath cPath = p.carvingPath;
    if (cPath.isEmpty() && p.carving != CarvingPattern::Preset::None) {
        QRectF rBox = region[0].boundingRect();
        for (int i = 1; i < region.size(); ++i)
            rBox = rBox.united(region[i].boundingRect());
        cPath = CarvingPattern::createPath(p.carving, p.carvingScaleMm, p.carvingScaleMm, rBox.center());
    }

    StitchSequence cover = fillOnly(region, p.fillAngleDeg, p.rowSpacingMm,
                                    p.maxStitchMm, effectivePhase, p.colorIdx, cPath);
    seq.stitches.insert(seq.stitches.end(), cover.stitches.begin(), cover.stitches.end());

    if (seq.palette.empty())
        seq.palette.emplace_back(QColor(40, 60, 140), patternName(p.pattern));
    return seq;
}

StitchSequence TatamiFill::fillOnly(const QVector<QPolygonF>& region,
                                    double angleDeg, double rowSpacingMm,
                                    double maxStitchMm, double phaseFrac, int colorIdx,
                                    const QPainterPath& carvingPath)
{
    StitchSequence seq;
    if (region.isEmpty() || region.front().size() < 3)
        return seq;

    const double theta = angleDeg;
    QTransform rot;     rot.rotate(-theta);   // region -> scan frame
    QTransform rotInv;  rotInv.rotate(theta);  // scan frame -> region

    QPainterPath scanCarving;
    if (!carvingPath.isEmpty()) {
        scanCarving = rot.map(carvingPath);
    }

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
    const double phaseStep = (phaseFrac >= 0.0) ? (phaseFrac * maxLen) : 0.0;

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

        QVector<double> carvingXs;
        if (!scanCarving.isEmpty()) {
            carvingXs = CarvingPattern::findIntersections(scanCarving, y);
        }

        double phase = phaseStep * row;
        if (phaseFrac < 0.0) {
            // Basketweave 4-row block shift cycle
            static const double bwPhases[4] = { 0.0, 0.50, 0.25, 0.75 };
            phase = bwPhases[row % 4] * maxLen;
        }

        // Pair crossings (even-odd). For boustrophedon we consume spans in the
        // travel direction of this row.
        const int pairs = xs.size() / 2;
        if (!reverse) {
            for (int k = 0; k < pairs; ++k) {
                const bool isNewSpan = (k > 0) || pts.isEmpty();
                emitRun(xs[2 * k], xs[2 * k + 1], y, maxLen, phase, carvingXs, false, isNewSpan, pts);
            }
        } else {
            for (int k = pairs - 1; k >= 0; --k) {
                const bool isNewSpan = (k < pairs - 1) || pts.isEmpty();
                emitRun(xs[2 * k], xs[2 * k + 1], y, maxLen, phase, carvingXs, true, isNewSpan, pts);
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
