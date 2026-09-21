// ---------------------------------------------------------------------------
//  StickCore  –  CrossStitchGenerator.cpp
//
//  Traditional Counted Cross-Stitch Generator.
// ---------------------------------------------------------------------------
#include "generators/CrossStitchGenerator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace stick {

double CrossStitchGenerator::aidaToPitchMm(AidaCount count, double customMm)
{
    switch (count) {
        case AidaCount::Count11: return 25.4 / 11.0; // ~2.309 mm
        case AidaCount::Count14: return 25.4 / 14.0; // ~1.814 mm
        case AidaCount::Count16: return 25.4 / 16.0; // 1.5875 mm
        case AidaCount::Count18: return 25.4 / 18.0; // ~1.411 mm
        case AidaCount::Custom:  return std::max(0.8, customMm);
    }
    return 25.4 / 14.0;
}

QString CrossStitchGenerator::aidaName(AidaCount c)
{
    switch (c) {
        case AidaCount::Count11: return QStringLiteral("11 ct (2.31 mm - Rustikal)");
        case AidaCount::Count14: return QStringLiteral("14 ct (1.81 mm - Standard Aida)");
        case AidaCount::Count16: return QStringLiteral("16 ct (1.59 mm - Fein)");
        case AidaCount::Count18: return QStringLiteral("18 ct (1.41 mm - Sehr fein)");
        case AidaCount::Custom:  return QStringLiteral("Benutzerdefiniert");
    }
    return QStringLiteral("14 ct");
}

namespace {

struct Cell {
    int col = 0;
    int row = 0;
    int colorIdx = 0;
};

double colorDistSq(QRgb a, QRgb b)
{
    double dr = qRed(a) - qRed(b);
    double dg = qGreen(a) - qGreen(b);
    double db = qBlue(a) - qBlue(b);
    return dr * dr + dg * dg + db * db;
}

} // namespace

StitchSequence CrossStitchGenerator::generate(const QImage& source, const Params& p)
{
    StitchSequence seq;
    if (source.isNull()) return seq;

    const double pitch = aidaToPitchMm(p.aida, p.customMm);

    // Limit grid size to max 100x100 to stay within standard embroidery hoops
    QImage gridImg = source;
    constexpr int MAX_GRID_DIM = 96;
    if (gridImg.width() > MAX_GRID_DIM || gridImg.height() > MAX_GRID_DIM) {
        gridImg = source.scaled(MAX_GRID_DIM, MAX_GRID_DIM, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    gridImg = gridImg.convertToFormat(QImage::Format_ARGB32);

    const int gridW = gridImg.width();
    const int gridH = gridImg.height();

    // 1. Color Quantization / Palette Extraction
    // Collect active pixels
    QVector<QRgb> activePixels;
    for (int y = 0; y < gridH; ++y) {
        const QRgb* scan = reinterpret_cast<const QRgb*>(gridImg.constScanLine(y));
        for (int x = 0; x < gridW; ++x) {
            QRgb c = scan[x];
            if (qAlpha(c) < 64) continue;
            if (p.skipWhite && qRed(c) > 240 && qGreen(c) > 240 && qBlue(c) > 240) continue;
            activePixels.push_back(c);
        }
    }

    if (activePixels.isEmpty()) return seq;

    // Build palette of up to p.maxColors via simple k-means
    const int targetK = std::clamp(p.maxColors, 1, 16);
    QVector<QRgb> palette;

    // Seed centroids evenly from sorted luminance
    std::sort(activePixels.begin(), activePixels.end(), [](QRgb a, QRgb b) {
        return (0.299 * qRed(a) + 0.587 * qGreen(a) + 0.114 * qBlue(a)) <
               (0.299 * qRed(b) + 0.587 * qGreen(b) + 0.114 * qBlue(b));
    });

    const int step = std::max(1, static_cast<int>(activePixels.size() / targetK));
    for (int i = 0; i < targetK && i * step < activePixels.size(); ++i) {
        palette.push_back(activePixels[i * step]);
    }
    if (palette.isEmpty()) palette.push_back(qRgb(0, 0, 0));

    // K-Means iterations (3 rounds is plenty for embroidery quantization)
    for (int iter = 0; iter < 3; ++iter) {
        QVector<double> sumR(palette.size(), 0.0);
        QVector<double> sumG(palette.size(), 0.0);
        QVector<double> sumB(palette.size(), 0.0);
        QVector<int> count(palette.size(), 0);

        for (QRgb c : activePixels) {
            int bestIdx = 0;
            double bestDist = std::numeric_limits<double>::max();
            for (int k = 0; k < palette.size(); ++k) {
                double d = colorDistSq(c, palette[k]);
                if (d < bestDist) {
                    bestDist = d;
                    bestIdx = k;
                }
            }
            sumR[bestIdx] += qRed(c);
            sumG[bestIdx] += qGreen(c);
            sumB[bestIdx] += qBlue(c);
            count[bestIdx]++;
        }

        for (int k = 0; k < palette.size(); ++k) {
            if (count[k] > 0) {
                palette[k] = qRgb(static_cast<int>(sumR[k] / count[k]),
                                  static_cast<int>(sumG[k] / count[k]),
                                  static_cast<int>(sumB[k] / count[k]));
            }
        }
    }

    // Populate sequence palette
    for (int k = 0; k < palette.size(); ++k) {
        seq.palette.emplace_back(QColor(palette[k]), QString("Kreuzstich Garn %1").arg(k + 1));
    }

    // 2. Classify each cell by palette index
    QVector<QVector<Cell>> colorCells(palette.size());
    for (int y = 0; y < gridH; ++y) {
        const QRgb* scan = reinterpret_cast<const QRgb*>(gridImg.constScanLine(y));
        for (int x = 0; x < gridW; ++x) {
            QRgb c = scan[x];
            if (qAlpha(c) < 64) continue;
            if (p.skipWhite && qRed(c) > 240 && qGreen(c) > 240 && qBlue(c) > 240) continue;

            int bestIdx = 0;
            double bestDist = std::numeric_limits<double>::max();
            for (int k = 0; k < palette.size(); ++k) {
                double d = colorDistSq(c, palette[k]);
                if (d < bestDist) {
                    bestDist = d;
                    bestIdx = k;
                }
            }
            colorCells[bestIdx].push_back({ x, y, bestIdx });
        }
    }

    // 3. Generate Stitches Color by Color
    const double x0 = -(gridW * pitch) / 2.0;
    const double y0 = -(gridH * pitch) / 2.0;

    QPointF currentMachinePos(0, 0);
    bool machineStarted = false;

    for (int cIdx = 0; cIdx < palette.size(); ++cIdx) {
        QVector<Cell>& cells = colorCells[cIdx];
        if (cells.isEmpty()) continue;

        if (machineStarted) {
            // Signal thread color change to machine
            seq.stitches.back().flags |= (SF_ColorChange | SF_Stop | SF_Trim);
        }

        // Sort cells using nearest-neighbor greedy path to minimize thread jumps
        QVector<Cell> orderedCells;
        orderedCells.reserve(cells.size());

        QVector<bool> visited(cells.size(), false);
        int currentIdx = 0; // start at first cell
        orderedCells.push_back(cells[0]);
        visited[0] = true;

        for (int step = 1; step < cells.size(); ++step) {
            const Cell& last = orderedCells.back();
            int bestNext = -1;
            double minDist = std::numeric_limits<double>::max();

            for (int candidate = 0; candidate < cells.size(); ++candidate) {
                if (visited[candidate]) continue;
                double dx = cells[candidate].col - last.col;
                double dy = cells[candidate].row - last.row;
                double d = dx * dx + dy * dy;
                if (d < minDist) {
                    minDist = d;
                    bestNext = candidate;
                }
            }

            if (bestNext >= 0) {
                visited[bestNext] = true;
                orderedCells.push_back(cells[bestNext]);
            }
        }

        // Stitch each cross in standard uniform direction
        for (const Cell& cell : orderedCells) {
            const double cx = x0 + cell.col * pitch;
            const double cy = y0 + cell.row * pitch;

            const QPointF BL(cx, cy + pitch);
            const QPointF TR(cx + pitch, cy);
            const QPointF BR(cx + pitch, cy + pitch);
            const QPointF TL(cx, cy);

            const double distToBL = std::hypot(BL.x() - currentMachinePos.x(), BL.y() - currentMachinePos.y());
            const bool needsJump = !machineStarted || (distToBL > p.maxJumpMm);

            if (needsJump) {
                seq.add(BL.x(), BL.y(), SF_Jump | (machineStarted ? SF_Trim : 0), cIdx);
            } else {
                seq.add(BL.x(), BL.y(), SF_Normal, cIdx);
            }
            machineStarted = true;

            // Pass 1 (Lower leg /): BL -> TR
            seq.add(TR.x(), TR.y(), SF_Normal, cIdx);

            if (p.style != StitchStyle::HalfCross) {
                // Pass 2 (Upper deck-stitch \): BR -> TL
                seq.add(BR.x(), BR.y(), SF_Normal, cIdx);
                seq.add(TL.x(), TL.y(), SF_Normal, cIdx);
            }

            if (p.style == StitchStyle::DoubleCross) {
                // Horizontal bar: ML -> MR
                const QPointF ML(cx, cy + pitch * 0.5);
                const QPointF MR(cx + pitch, cy + pitch * 0.5);
                seq.add(ML.x(), ML.y(), SF_Normal, cIdx);
                seq.add(MR.x(), MR.y(), SF_Normal, cIdx);

                // Vertical bar: TC -> BC
                const QPointF TC(cx + pitch * 0.5, cy);
                const QPointF BC(cx + pitch * 0.5, cy + pitch);
                seq.add(TC.x(), TC.y(), SF_Normal, cIdx);
                seq.add(BC.x(), BC.y(), SF_Normal, cIdx);
                currentMachinePos = BC;
            } else {
                currentMachinePos = (p.style == StitchStyle::HalfCross) ? TR : TL;
            }
        }
    }

    return seq;
}

} // namespace stick
