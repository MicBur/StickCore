// ---------------------------------------------------------------------------
//  StickCore  –  MultiHoopSplitter.cpp
//
//  Multi-Hooping & Auto-Split Implementation.
// ---------------------------------------------------------------------------
#include "generators/MultiHoopSplitter.h"

#include <algorithm>
#include <cmath>

namespace stick {

bool MultiHoopSplitter::exceedsHoop(const StitchSequence& seq, double hoopW, double hoopH)
{
    if (seq.empty()) return false;
    const QRectF bb = seq.boundingRect();
    return (bb.width() > hoopW || bb.height() > hoopH);
}

StitchSequence MultiHoopSplitter::makeCrosshair(const QPointF& center, double sizeMm, int colorIdx)
{
    StitchSequence seq;
    const double r = sizeMm * 0.5;
    const double lockStep = 0.8;

    // 1. Jump to center and perform 3 micro-lock stitches (Tie-in) so needle thread never pulls out
    seq.add(center.x(), center.y(), SF_Jump, colorIdx);
    seq.add(center.x() + lockStep, center.y(), SF_Normal, colorIdx);
    seq.add(center.x() - lockStep, center.y(), SF_Normal, colorIdx);
    seq.add(center.x(), center.y(), SF_Normal, colorIdx);

    // 2. Horizontal bar: out to right, through center to left, back to center
    seq.add(center.x() + r, center.y(), SF_Normal, colorIdx);
    seq.add(center.x() - r, center.y(), SF_Normal, colorIdx);
    seq.add(center.x(), center.y(), SF_Normal, colorIdx);

    // 3. Vertical bar: out to top, through center to bottom, back to center
    seq.add(center.x(), center.y() + r, SF_Normal, colorIdx);
    seq.add(center.x(), center.y() - r, SF_Normal, colorIdx);
    seq.add(center.x(), center.y(), SF_Normal, colorIdx);

    // 4. Micro-lock stitches at center (Tie-off)
    seq.add(center.x(), center.y() + lockStep, SF_Normal, colorIdx);
    seq.add(center.x(), center.y() - lockStep, SF_Normal, colorIdx);
    seq.add(center.x(), center.y(), SF_Normal, colorIdx);

    return seq;
}

MultiHoopSplitter::Result MultiHoopSplitter::split(const StitchSequence& source, const Params& p)
{
    Result res;
    if (source.empty()) {
        res.summary = QStringLiteral("Leeres Stickmuster kann nicht geteilt werden.");
        return res;
    }

    const QRectF bb = source.boundingRect();
    SplitDirection dir = p.direction;
    if (dir == SplitDirection::Auto) {
        const double wRatio = bb.width() / std::max(10.0, p.hoopWidthMm);
        const double hRatio = bb.height() / std::max(10.0, p.hoopHeightMm);
        dir = (hRatio >= wRatio) ? SplitDirection::Horizontal : SplitDirection::Vertical;
    }

    const double halfOverlap = p.overlapMm * 0.5;
    double seamPos = 0.0;

    if (dir == SplitDirection::Horizontal) {
        seamPos = bb.center().y();
        res.crosshair1 = QPointF(bb.left() + 4.0, seamPos);
        res.crosshair2 = QPointF(bb.right() - 4.0, seamPos);
    } else {
        seamPos = bb.center().x();
        res.crosshair1 = QPointF(seamPos, bb.top() + 4.0);
        res.crosshair2 = QPointF(seamPos, bb.bottom() - 4.0);
    }

    res.hoop1.palette = source.palette;
    res.hoop2.palette = source.palette;

    bool h1Active = false;
    bool h2Active = false;

    // Partition stitches across the seam with overlap tolerance
    for (size_t i = 0; i < source.stitches.size(); ++i) {
        const Stitch& st = source.stitches[i];
        const double coord = (dir == SplitDirection::Horizontal) ? st.y : st.x;

        // Hoop 1 takes the lower/left portion up to seamPos + halfOverlap
        if (coord <= seamPos + halfOverlap) {
            quint32 flags = st.flags;
            if (!h1Active) {
                flags |= SF_Jump | SF_Trim;
                h1Active = true;
            }
            res.hoop1.add(st.x, st.y, flags, st.colorIdx);
        } else {
            h1Active = false;
        }

        // Hoop 2 takes the upper/right portion from seamPos - halfOverlap upwards
        if (coord >= seamPos - halfOverlap) {
            quint32 flags = st.flags;
            if (!h2Active) {
                flags |= SF_Jump | SF_Trim;
                h2Active = true;
            }
            res.hoop2.add(st.x, st.y, flags, st.colorIdx);
        } else {
            h2Active = false;
        }
    }

    if (res.hoop1.empty() || res.hoop2.empty()) {
        res.summary = QStringLiteral("Teilung erzeugte leere Rahmen-Hälfte. Schnittlinie prüfen.");
        return res;
    }

    // Add registration crosshairs
    if (p.addCrosshairs) {
        const int regColor1 = static_cast<int>(res.hoop1.palette.size());
        res.hoop1.palette.emplace_back(QColor(220, 20, 60), QStringLiteral("Passkreuze (Rahmen 1)"));

        // End Hoop 1 with machine stop for thread change / mark inspection
        res.hoop1.stitches.back().flags |= (SF_Stop | SF_ColorChange | SF_Trim);

        StitchSequence ch1A = makeCrosshair(res.crosshair1, p.crosshairSizeMm, regColor1);
        StitchSequence ch1B = makeCrosshair(res.crosshair2, p.crosshairSizeMm, regColor1);
        res.hoop1.stitches.insert(res.hoop1.stitches.end(), ch1A.stitches.begin(), ch1A.stitches.end());
        res.hoop1.stitches.insert(res.hoop1.stitches.end(), ch1B.stitches.begin(), ch1B.stitches.end());

        // Prepend registration crosshairs to Hoop 2
        StitchSequence h2Combined;
        h2Combined.palette.emplace_back(QColor(220, 20, 60), QStringLiteral("Passkreuze Ausrichtung"));
        // Shift existing palette in Hoop 2
        for (const auto& th : res.hoop2.palette) {
            h2Combined.palette.push_back(th);
        }

        StitchSequence ch2A = makeCrosshair(res.crosshair1, p.crosshairSizeMm, 0);
        StitchSequence ch2B = makeCrosshair(res.crosshair2, p.crosshairSizeMm, 0);
        h2Combined.stitches.insert(h2Combined.stitches.end(), ch2A.stitches.begin(), ch2A.stitches.end());
        h2Combined.stitches.insert(h2Combined.stitches.end(), ch2B.stitches.begin(), ch2B.stitches.end());

        // Stop machine after crosshair verification so user can switch back to design thread
        h2Combined.stitches.back().flags |= (SF_Stop | SF_ColorChange | SF_Trim);

        // Add body stitches of hoop 2 (shifting their color index by +1)
        for (const Stitch& s : res.hoop2.stitches) {
            h2Combined.add(s.x, s.y, s.flags, s.colorIdx + 1);
        }
        res.hoop2 = h2Combined;
    }

    // Optionally center each hoop to machine origin (0, 0)
    if (p.centerEachHoop) {
        const QPointF c1 = res.hoop1.boundingRect().center();
        for (Stitch& s : res.hoop1.stitches) {
            s.x -= c1.x();
            s.y -= c1.y();
        }
        const QPointF c2 = res.hoop2.boundingRect().center();
        for (Stitch& s : res.hoop2.stitches) {
            s.x -= c2.x();
            s.y -= c2.y();
        }
    }

    res.success = true;
    res.summary = QString("Erfolgreich geteilt (%1 Richtung): Teil 1 (%2 Stiche), Teil 2 (%3 Stiche)")
                      .arg(dir == SplitDirection::Horizontal ? "Horizontal" : "Vertikal")
                      .arg(res.hoop1.size())
                      .arg(res.hoop2.size());
    return res;
}

MultiHoopSplitter::MultiResult MultiHoopSplitter::splitMulti(const StitchSequence& source, const Params& p, int numParts)
{
    MultiResult res;
    if (source.empty()) {
        res.summary = QStringLiteral("Leeres Stickmuster kann nicht geteilt werden.");
        return res;
    }

    const QRectF bb = source.boundingRect();
    SplitDirection dir = p.direction;
    if (dir == SplitDirection::Auto) {
        const double wRatio = bb.width() / std::max(10.0, p.hoopWidthMm);
        const double hRatio = bb.height() / std::max(10.0, p.hoopHeightMm);
        dir = (hRatio >= wRatio) ? SplitDirection::Horizontal : SplitDirection::Vertical;
    }

    const double totalDim = (dir == SplitDirection::Horizontal) ? bb.height() : bb.width();
    const double hoopDim  = (dir == SplitDirection::Horizontal) ? p.hoopHeightMm : p.hoopWidthMm;
    const double usableDim = std::max(20.0, hoopDim - p.overlapMm);

    if (numParts <= 0) {
        numParts = std::max(2, static_cast<int>(std::ceil(totalDim / usableDim)));
    }
    numParts = std::clamp(numParts, 2, 8);

    const double halfOverlap = p.overlapMm * 0.5;
    const double startCoord  = (dir == SplitDirection::Horizontal) ? bb.bottom() : bb.left();
    const double step        = totalDim / static_cast<double>(numParts);

    // Compute seam coordinates and registration points
    std::vector<double> seams(numParts - 1);
    for (int k = 0; k < numParts - 1; ++k) {
        seams[k] = startCoord + step * (k + 1);
        if (dir == SplitDirection::Horizontal) {
            res.crosshairs.push_back(QPointF(bb.left() + 4.0, seams[k]));
            res.crosshairs.push_back(QPointF(bb.right() - 4.0, seams[k]));
        } else {
            res.crosshairs.push_back(QPointF(seams[k], bb.top() + 4.0));
            res.crosshairs.push_back(QPointF(seams[k], bb.bottom() - 4.0));
        }
    }

    res.parts.resize(numParts);

    for (int k = 0; k < numParts; ++k) {
        StitchSequence& part = res.parts[k];
        part.palette = source.palette;

        const double lowerBound = (k == 0) ? -1e9 : (seams[k - 1] - halfOverlap);
        const double upperBound = (k == numParts - 1) ? 1e9 : (seams[k] + halfOverlap);

        bool active = false;
        for (const Stitch& st : source.stitches) {
            const double coord = (dir == SplitDirection::Horizontal) ? st.y : st.x;
            if (coord >= lowerBound && coord <= upperBound) {
                quint32 flags = st.flags;
                if (!active) {
                    flags |= SF_Jump | SF_Trim;
                    active = true;
                }
                part.add(st.x, st.y, flags, st.colorIdx);
            } else {
                active = false;
            }
        }

        // Add alignment crosshairs if requested
        if (p.addCrosshairs && !part.empty()) {
            StitchSequence partCombined;

            // If not the first part, prepend registration crosshairs matching previous seam
            if (k > 0) {
                partCombined.palette.emplace_back(QColor(220, 20, 60), QStringLiteral("Passkreuze Einpassung"));
                for (const auto& th : part.palette) partCombined.palette.push_back(th);

                const QPointF chA = res.crosshairs[(k - 1) * 2];
                const QPointF chB = res.crosshairs[(k - 1) * 2 + 1];
                StitchSequence markA = makeCrosshair(chA, p.crosshairSizeMm, 0);
                StitchSequence markB = makeCrosshair(chB, p.crosshairSizeMm, 0);
                partCombined.stitches.insert(partCombined.stitches.end(), markA.stitches.begin(), markA.stitches.end());
                partCombined.stitches.insert(partCombined.stitches.end(), markB.stitches.begin(), markB.stitches.end());
                partCombined.stitches.back().flags |= (SF_Stop | SF_ColorChange | SF_Trim);

                // Add body stitches with shifted color index
                for (const Stitch& s : part.stitches) {
                    partCombined.add(s.x, s.y, s.flags, s.colorIdx + 1);
                }
                part = partCombined;
            }

            // If not the last part, append registration crosshairs for next seam
            if (k < numParts - 1) {
                const int postColor = static_cast<int>(part.palette.size());
                part.palette.emplace_back(QColor(220, 20, 60), QStringLiteral("Passkreuze Folgerahmen"));
                part.stitches.back().flags |= (SF_Stop | SF_ColorChange | SF_Trim);

                const QPointF chA = res.crosshairs[k * 2];
                const QPointF chB = res.crosshairs[k * 2 + 1];
                StitchSequence markA = makeCrosshair(chA, p.crosshairSizeMm, postColor);
                StitchSequence markB = makeCrosshair(chB, p.crosshairSizeMm, postColor);
                part.stitches.insert(part.stitches.end(), markA.stitches.begin(), markA.stitches.end());
                part.stitches.insert(part.stitches.end(), markB.stitches.begin(), markB.stitches.end());
            }
        }

        // Optional centering per part
        if (p.centerEachHoop && !part.empty()) {
            const QPointF c = part.boundingRect().center();
            for (Stitch& s : part.stitches) {
                s.x -= c.x();
                s.y -= c.y();
            }
        }
    }

    res.success = true;
    res.summary = QString("Muster erfolgreich in %1 Teil-Rahmen zerlegt (%2)")
                      .arg(numParts)
                      .arg(dir == SplitDirection::Horizontal ? "Horizontal" : "Vertikal");
    return res;
}

} // namespace stick
