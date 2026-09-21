// ---------------------------------------------------------------------------
//  StickCore  –  BastingGenerator.cpp
// ---------------------------------------------------------------------------
#include "generators/BastingGenerator.h"
#include "core/MachineProfile.h"
#include <algorithm>
#include <cmath>

namespace stick {

namespace {

void traceSegment(StitchSequence& seq, double xA, double yA, double xB, double yB,
                  double stepMm, int colIdx)
{
    const double dx = xB - xA;
    const double dy = yB - yA;
    const double dist = std::hypot(dx, dy);
    if (dist < 1e-3) return;

    const int steps = std::max(1, int(std::ceil(dist / std::max(1.0, stepMm))));
    for (int i = 1; i <= steps; ++i) {
        const double t = double(i) / double(steps);
        seq.add(xA + dx * t, yA + dy * t, SF_Normal, colIdx);
    }
}

void traceRect(StitchSequence& seq, double x0, double y0, double x1, double y1,
               double stepMm, int colIdx, bool doublePass)
{
    // Start with jump to bottom-left corner
    seq.add(x0, y0, SF_Jump | SF_Trim, colIdx);

    // Segment 1: Bottom edge (x0, y0) -> (x1, y0)
    traceSegment(seq, x0, y0, x1, y0, stepMm, colIdx);
    // Segment 2: Right edge (x1, y0) -> (x1, y1)
    traceSegment(seq, x1, y0, x1, y1, stepMm, colIdx);
    // Segment 3: Top edge (x1, y1) -> (x0, y1)
    traceSegment(seq, x1, y1, x0, y1, stepMm, colIdx);
    // Segment 4: Left edge (x0, y1) -> (x0, y0)
    traceSegment(seq, x0, y1, x0, y0, stepMm, colIdx);

    if (doublePass) {
        // Reverse pass for extra locking
        traceSegment(seq, x0, y0, x0, y1, stepMm, colIdx);
        traceSegment(seq, x0, y1, x1, y1, stepMm, colIdx);
        traceSegment(seq, x1, y1, x1, y0, stepMm, colIdx);
        traceSegment(seq, x1, y0, x0, y0, stepMm, colIdx);
    }
}

} // namespace

StitchSequence BastingGenerator::generate(const StitchSequence& baseSeq,
                                         const Params& p,
                                         HoopType hoop)
{
    StitchSequence out;
    out.palette = { ThreadColor(p.bastingColor, QStringLiteral("Heftfaden (Basting)"), 1) };

    double x0 = -50, y0 = -50, x1 = 50, y1 = 50;

    if (p.mode == Mode::HoopBounds) {
        HoopSpec spec = hoopSpec(hoop);
        const double hw = spec.widthMm * 0.5;
        const double hh = spec.heightMm * 0.5;
        const double m = std::max(1.0, p.marginMm);
        x0 = -hw + m; x1 = hw - m;
        y0 = -hh + m; y1 = hh - m;
    } else {
        // MotifBounds or ContourSilhouette fallback to outer box
        double minX, minY, maxX, maxY;
        if (baseSeq.bounds(minX, minY, maxX, maxY)) {
            const double m = std::max(0.5, p.marginMm);
            x0 = minX - m; x1 = maxX + m;
            y0 = minY - m; y1 = maxY + m;
        }
    }

    traceRect(out, x0, y0, x1, y1, p.stitchLengthMm, 0, p.doublePass);

    if (!out.stitches.empty()) {
        const Stitch& last = out.stitches.back();
        out.add(last.x, last.y, SF_End, 0);
    }
    return out;
}

bool BastingGenerator::prependTo(StitchSequence& targetSeq,
                                const Params& p,
                                HoopType hoop)
{
    if (targetSeq.stitches.empty()) {
        targetSeq = generate(targetSeq, p, hoop);
        return true;
    }

    StitchSequence basting = generate(targetSeq, p, hoop);
    if (basting.stitches.empty()) return false;

    // Shift all colors in targetSeq by +1 to reserve color 0 for basting
    for (auto& st : targetSeq.stitches) {
        st.colorIdx += 1;
    }

    // Insert basting color at front of palette
    targetSeq.palette.insert(targetSeq.palette.begin(),
                             ThreadColor(p.bastingColor, QStringLiteral("Heftfaden (Basting)"), 1));

    // Combine: basting stitches (without SF_End) + stop + target stitches
    std::vector<Stitch> combined;
    combined.reserve(basting.stitches.size() + targetSeq.stitches.size() + 2);

    for (const auto& s : basting.stitches) {
        if (s.flags & SF_End) continue;
        combined.push_back(s);
    }

    // Insert stop/color-change transition before the main design
    if (!combined.empty() && !targetSeq.stitches.empty()) {
        const Stitch& bastingEnd = combined.back();
        const Stitch& mainStart  = targetSeq.stitches.front();

        // Flag transition with Stop / ColorChange
        combined.emplace_back(bastingEnd.x, bastingEnd.y,
                              p.addStopAfter ? (SF_Stop | SF_ColorChange | SF_Trim)
                                             : (SF_ColorChange | SF_Trim),
                              0);
        // Jump to first stitch of main design
        combined.emplace_back(mainStart.x, mainStart.y, SF_Jump | SF_Trim, mainStart.colorIdx);
    }

    for (const auto& s : targetSeq.stitches) {
        combined.push_back(s);
    }

    targetSeq.stitches = std::move(combined);
    return true;
}

StitchSequence BastingGenerator::prependTo(const StitchSequence& targetSeq,
                                           const Params& p,
                                           double hoopWidthMm, double hoopHeightMm)
{
    StitchSequence basting;
    if (p.mode == Mode::HoopBounds) {
        const double hw = hoopWidthMm * 0.5;
        const double hh = hoopHeightMm * 0.5;
        const double m = std::max(1.0, p.marginMm);
        const double x0 = -hw + m, x1 = hw - m;
        const double y0 = -hh + m, y1 = hh - m;
        basting.palette = { ThreadColor(p.bastingColor, QStringLiteral("Heftfaden (Basting)"), 1) };
        auto traceSide = [&](double sx, double sy, double ex, double ey) {
            const double len = std::hypot(ex - sx, ey - sy);
            const int steps = std::max(1, static_cast<int>(std::ceil(len / std::max(1.0, p.stitchLengthMm))));
            for (int i = 1; i <= steps; ++i) {
                const double t = double(i) / steps;
                basting.add(sx + t * (ex - sx), sy + t * (ey - sy), SF_Normal, 0);
            }
        };
        basting.add(x0, y0, SF_Jump, 0);
        traceSide(x0, y0, x1, y0);
        traceSide(x1, y0, x1, y1);
        traceSide(x1, y1, x0, y1);
        traceSide(x0, y1, x0, y0);
        if (p.doublePass) {
            traceSide(x0, y0, x1, y0);
            traceSide(x1, y0, x1, y1);
            traceSide(x1, y1, x0, y1);
            traceSide(x0, y1, x0, y0);
        }
    } else {
        basting = generate(targetSeq, p);
    }

    if (basting.empty()) return targetSeq;

    StitchSequence combined;
    combined.palette = basting.palette;
    for (const auto& th : targetSeq.palette) combined.palette.push_back(th);

    for (const Stitch& s : basting.stitches) combined.add(s);

    if (p.addStopAfter && !combined.empty()) {
        combined.stitches.back().flags |= (SF_Stop | SF_ColorChange | SF_Trim);
    }

    for (const Stitch& s : targetSeq.stitches) {
        combined.add(s.x, s.y, s.flags, s.colorIdx + 1);
    }

    return combined;
}

} // namespace stick
