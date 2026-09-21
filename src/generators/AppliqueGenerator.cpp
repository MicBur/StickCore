// ---------------------------------------------------------------------------
//  StickCore  –  AppliqueGenerator.cpp
// ---------------------------------------------------------------------------
#include "generators/AppliqueGenerator.h"
#include "generators/SatinGenerator.h"
#include "core/Geometry.h"

#include <cmath>

namespace stick {

namespace {

void appendRunLoop(const QPainterPath& path, double stepMm, int colorIdx, StitchSequence& out)
{
    ArcLengthCurve cv(path);
    if (!cv.isValid()) return;

    const double step = std::max(stepMm, 0.5);
    const int n = std::max(2, int(std::llround(cv.length() / step)));

    QPointF start = cv.pointAt(0.0);
    out.add(start.x(), start.y(), SF_Jump, colorIdx);

    for (int i = 1; i <= n; ++i) {
        const double s = double(i) / n;
        const QPointF pt = cv.pointAt(s);
        out.add(pt.x(), pt.y(), SF_Normal, colorIdx);
    }
}

} // namespace

StitchSequence AppliqueGenerator::generate(const QPolygonF& boundary, const Params& p)
{
    if (boundary.size() < 3) return StitchSequence();
    QPainterPath path;
    path.moveTo(boundary.first());
    for (int i = 1; i < boundary.size(); ++i) path.lineTo(boundary[i]);
    path.closeSubpath();
    return generate(path, p);
}

StitchSequence AppliqueGenerator::generate(const QPainterPath& boundary, const Params& p)
{
    StitchSequence seq;
    ArcLengthCurve cv(boundary);
    if (!cv.isValid()) return seq;

    // Palette with 3 process stops
    seq.palette.emplace_back(p.placementColor, QStringLiteral("1. Positionierung (Stoff auflegen)"));
    seq.palette.emplace_back(p.tackColor,      QStringLiteral("2. Heften (Rand abschneiden)"));
    seq.palette.emplace_back(p.coverColor,     QStringLiteral("3. Satinkante (Fertigstellung)"));

    // --- 1. Placement line -------------------------------------------------
    appendRunLoop(boundary, p.runStepMm, 0, seq);

    // Machine stop / color change to place fabric
    if (!seq.empty())
        seq.stitches.back().flags |= (SF_Stop | SF_ColorChange);

    // --- 2. Tack-down line -------------------------------------------------
    if (p.tackStyle == TackStyle::ZigZag) {
        StitchSequence tack = SatinGenerator::fromCenterline(boundary, 1.4, 1.2, 1);
        seq.stitches.insert(seq.stitches.end(), tack.stitches.begin(), tack.stitches.end());
    } else {
        // Double run
        appendRunLoop(boundary, p.runStepMm, 1, seq);
        appendRunLoop(boundary, p.runStepMm * 0.9, 1, seq);
    }

    // Machine stop / color change to trim fabric
    if (!seq.empty())
        seq.stitches.back().flags |= (SF_Stop | SF_ColorChange);

    // --- 3. Satin cover finish ---------------------------------------------
    StitchSequence cover = SatinGenerator::fromCenterline(boundary, p.satinWidthMm, p.satinPitchMm, 2);
    seq.stitches.insert(seq.stitches.end(), cover.stitches.begin(), cover.stitches.end());

    return seq;
}

} // namespace stick
