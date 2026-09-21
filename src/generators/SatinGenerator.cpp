// ---------------------------------------------------------------------------
//  StickCore  –  SatinGenerator.cpp
//
//  ORTHOGONAL SATIN COLUMN GENERATION
//  ----------------------------------
//  A satin column is the filled area swept between two rails R1(t) and R2(t).
//  The classic zig-zag satin stitch crosses this area repeatedly, penetrating
//  alternately on rail 1 and rail 2.
//
//  Steps (mirrors the specification):
//   1. Re-parameterise BOTH rails by arc length so that a fraction s in [0,1]
//      refers to the same proportion of true length on each rail.  This keeps
//      the rungs evenly spaced even when the two rails have different lengths
//      or curvature (ArcLengthCurve does the heavy lifting).
//   2. Walk s from 0..1 in steps whose arc-length equals the density (pitch).
//      At every step:
//        a. Sample p1 = R1(s), p2 = R2(s) and the local rail tangents.
//        b. Build the transverse "rung".  When 'orthogonalize' is on the rung
//           is forced perpendicular to the column centreline (the average of
//           the two rail tangents) instead of simply spanning p1..p2; this is
//           what keeps the satin threads square to the stroke.
//        c. Apply PULL COMPENSATION: push each rung endpoint outward by
//           pullComp millimetres so the finished, puckered fabric still meets
//           the intended outline.
//        d. Emit ONE penetration, alternating rail 1 / rail 2 each step -> the
//           zig-zag layout.
//   3. Any single move longer than maxRung is broken into collinear
//      split-stitches (a machine cannot make an arbitrarily long stitch).
// ---------------------------------------------------------------------------
#include "generators/SatinGenerator.h"
#include "core/Geometry.h"

#include <algorithm>
#include <cmath>

namespace stick {

namespace {

// Append a move from 'from' to 'to' as one or more stitches, none longer than
// maxRung. The final stitch lands exactly on 'to'.
void appendSplit(StitchSequence& seq, const QPointF& from, const QPointF& to,
                 double maxRung, int colorIdx)
{
    const QPointF d = to - from;
    const double  L = length(d);
    if (L <= maxRung || L < 1e-6) {
        seq.add(to.x(), to.y(), SF_Normal, colorIdx);
        return;
    }
    const int n = static_cast<int>(std::ceil(L / maxRung));
    for (int k = 1; k <= n; ++k) {
        const double f = static_cast<double>(k) / n;
        const QPointF p = from + d * f;
        seq.add(p.x(), p.y(), SF_Normal, colorIdx);
    }
}

} // namespace

StitchSequence SatinGenerator::generate(const QPainterPath& railA,
                                        const QPainterPath& railB,
                                        double density, double pullComp)
{
    Params p;
    p.densityMm  = density;
    p.pullCompMm = pullComp;
    return generate(railA, railB, p);
}

StitchSequence SatinGenerator::generate(const QPainterPath& railA,
                                        const QPainterPath& railB,
                                        const Params& p)
{
    StitchSequence seq;

    ArcLengthCurve a(railA);
    ArcLengthCurve b(railB);
    if (!a.isValid() || !b.isValid())
        return seq;   // Not enough geometry to build a column.

    const double density = std::max(p.densityMm, 1e-3);
    // Centreline length drives the number of rungs.
    const double avgLen  = 0.5 * (a.length() + b.length());
    const int    steps   = std::max(1, static_cast<int>(std::llround(avgLen / density)));

    // Average column width (for choosing the underlay style).
    double avgWidth = 0.0; { const int W = 16;
        for (int i = 0; i <= W; ++i) { double s = double(i)/W; avgWidth += length(b.pointAt(s) - a.pointAt(s)); }
        avgWidth /= (W + 1); }

    // --- underlay first (center run; add a zigzag for wide columns) --------
    if (p.underlay) {
        const int us = std::max(2, int(std::llround(avgLen / 2.5)));
        bool f = true;
        for (int i = 0; i <= us; ++i) { double s = double(i)/us;
            const QPointF m = (a.pointAt(s) + b.pointAt(s)) * 0.5;
            seq.add(m.x(), m.y(), f ? SF_Jump : SF_Normal, p.colorIdx); f = false; }
        if (avgWidth >= 4.0) {
            const int zs = std::max(2, int(std::llround(avgLen / 2.0)));
            bool sideA = true; QPointF zp; bool zf = true;
            for (int i = 0; i <= zs; ++i) { double s = double(i)/zs;
                const QPointF p1 = a.pointAt(s), p2 = b.pointAt(s);
                const QPointF dir = normalized(p2 - p1);
                const QPointF t = sideA ? (p1 + dir*0.6) : (p2 - dir*0.6); sideA = !sideA;
                if (zf) { seq.add(t.x(), t.y(), SF_Jump, p.colorIdx); zp = t; zf = false; }
                else { appendSplit(seq, zp, t, p.maxRungMm, p.colorIdx); zp = t; } }
        }
    }

    bool penetrateA = true;             // Which rail this step lands on.
    bool first       = true;
    QPointF prev;

    for (int i = 0; i <= steps; ++i) {
        const double s = static_cast<double>(i) / steps;

        QPointF p1 = a.pointAt(s);
        QPointF p2 = b.pointAt(s);

        // --- build the rung ------------------------------------------------
        QPointF eA, eB;
        if (p.orthogonalize) {
            // Rung perpendicular to the centreline tangent.
            const QPointF tA = a.tangentAt(s);
            const QPointF tB = b.tangentAt(s);
            QPointF ctr = normalized(tA + tB);
            if (length(ctr) < 1e-6) ctr = tA;      // Degenerate: fall back.
            const QPointF n = perpLeft(ctr);       // Rung direction.

            const QPointF mid = (p1 + p2) * 0.5;
            const double  half = 0.5 * length(p2 - p1);
            // Keep p1 on its original side of the centreline.
            const double  sign = (dot(p1 - mid, n) < 0.0) ? -1.0 : 1.0;
            eA = mid + n * ( sign * (half + p.pullCompMm));
            eB = mid + n * (-sign * (half + p.pullCompMm));
        } else {
            // Simple span p1..p2, widened by pull compensation on each end.
            const QPointF dir = normalized(p2 - p1);
            eA = p1 - dir * p.pullCompMm;
            eB = p2 + dir * p.pullCompMm;
        }

        const QPointF target = penetrateA ? eA : eB;
        penetrateA = !penetrateA;

        if (first) {
            // Travel to the cover start (jump if underlay was laid first).
            seq.add(target.x(), target.y(), seq.empty() ? SF_Normal : SF_Jump, p.colorIdx);
            prev  = target;
            first = false;
        } else {
            appendSplit(seq, prev, target, p.maxRungMm, p.colorIdx);
            prev = target;
        }
    }

    // Register the column's color so the sequence is self-describing.
    if (seq.palette.empty())
        seq.palette.emplace_back(QColor(20, 20, 20), QStringLiteral("Satin"));

    return seq;
}

StitchSequence SatinGenerator::fromCenterline(const QPainterPath& centerPath,
                                              double widthMm,
                                              double pitchMm,
                                              int colorIdx)
{
    StitchSequence seq;
    ArcLengthCurve cv(centerPath);
    if (!cv.isValid()) return seq;

    const double pitch = std::max(pitchMm, 1e-3);
    const int steps = std::max(2, int(std::llround(cv.length() / pitch)));
    bool outer = true;
    bool first = true;
    QPointF prev;

    for (int i = 0; i <= steps; ++i) {
        const double s = double(i) / steps;
        const QPointF p = cv.pointAt(s);
        const QPointF n = cv.normalAt(s);
        const QPointF t = p + n * (outer ? widthMm * 0.5 : -widthMm * 0.5);
        outer = !outer;

        if (first) {
            seq.add(t.x(), t.y(), SF_Jump, colorIdx);
            prev = t;
            first = false;
        } else {
            appendSplit(seq, prev, t, 12.0, colorIdx);
            prev = t;
        }
    }
    return seq;
}

} // namespace stick
