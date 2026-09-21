// ---------------------------------------------------------------------------
//  StickCore  –  ColorSorter.cpp
// ---------------------------------------------------------------------------
#include "generators/ColorSorter.h"
#include <algorithm>
#include <limits>
#include <cmath>

namespace stick {

namespace {

struct Block {
    int id = 0;
    int colorIdx = 0;
    std::vector<Stitch> stitches;
    double minX = 0, minY = 0, maxX = 0, maxY = 0;
    bool validBounds = false;

    void calcBounds()
    {
        validBounds = false;
        minX = minY = std::numeric_limits<double>::max();
        maxX = maxY = -std::numeric_limits<double>::max();
        for (const auto& s : stitches) {
            if (s.flags & (SF_Jump | SF_End)) continue;
            minX = std::min(minX, s.x); maxX = std::max(maxX, s.x);
            minY = std::min(minY, s.y); maxY = std::max(maxY, s.y);
            validBounds = true;
        }
        if (!validBounds) {
            minX = minY = maxX = maxY = 0.0;
        }
    }
};

bool blocksOverlap(const Block& a, const Block& b)
{
    if (!a.validBounds || !b.validBounds) return false;
    // AABB intersection check with small safety padding (0.1 mm)
    if (a.maxX < b.minX - 0.1 || a.minX > b.maxX + 0.1) return false;
    if (a.maxY < b.minY - 0.1 || a.minY > b.maxY + 0.1) return false;

    // For overlapping AABBs, check if stitches actually get close (< 0.4 mm)
    // to avoid false positives when bounding boxes touch but actual geometry does not.
    for (const auto& sa : a.stitches) {
        if (sa.flags & (SF_Jump | SF_End)) continue;
        if (sa.x < b.minX - 0.4 || sa.x > b.maxX + 0.4 ||
            sa.y < b.minY - 0.4 || sa.y > b.maxY + 0.4) continue;

        for (const auto& sb : b.stitches) {
            if (sb.flags & (SF_Jump | SF_End)) continue;
            const double dx = sa.x - sb.x;
            const double dy = sa.y - sb.y;
            if (dx*dx + dy*dy < 0.16) { // distance < 0.4 mm
                return true;
            }
        }
    }
    return false;
}

} // namespace

StitchSequence ColorSorter::sort(const StitchSequence& in, Stats* stats)
{
    if (in.stitches.empty()) {
        if (stats) *stats = Stats();
        return in;
    }

    // 1. Deconstruct into homogeneous color blocks
    std::vector<Block> blocks;
    Block currentBlock;
    int curCol = -1;

    for (const Stitch& s : in.stitches) {
        if (s.flags & SF_End) continue;
        const bool isNewColor = (curCol == -1 || s.colorIdx != curCol || (s.flags & SF_ColorChange) != 0);

        if (isNewColor && !currentBlock.stitches.empty()) {
            currentBlock.calcBounds();
            blocks.push_back(std::move(currentBlock));
            currentBlock = Block();
        }

        curCol = s.colorIdx;
        currentBlock.id = int(blocks.size());
        currentBlock.colorIdx = curCol;
        currentBlock.stitches.push_back(s);
    }
    if (!currentBlock.stitches.empty()) {
        currentBlock.calcBounds();
        blocks.push_back(std::move(currentBlock));
    }

    const int N = int(blocks.size());
    if (N <= 1) {
        if (stats) {
            stats->colorChangesBefore = int(in.colorChangeCount());
            stats->colorChangesAfter  = stats->colorChangesBefore;
            stats->blocksBefore       = N;
            stats->blocksAfter        = N;
            stats->savedPercent       = 0.0;
            stats->summary = QStringLiteral("Bereits optimal: keine Farbwechsel einsparbar.");
        }
        return in;
    }

    // 2. Build dependency DAG: block i must come before block j if i < j and they overlap
    std::vector<std::vector<bool>> dep(N, std::vector<bool>(N, false));
    for (int i = 0; i < N; ++i) {
        for (int j = i + 1; j < N; ++j) {
            if (blocksOverlap(blocks[i], blocks[j])) {
                dep[i][j] = true;
            }
        }
    }

    // Transitive closure (Warshall)
    for (int k = 0; k < N; ++k) {
        for (int i = 0; i < N; ++i) {
            if (dep[i][k]) {
                for (int j = 0; j < N; ++j) {
                    if (dep[k][j]) dep[i][j] = true;
                }
            }
        }
    }

    // 3. Stable greedy topological scheduling
    std::vector<bool> scheduled(N, false);
    std::vector<int> scheduleOrder;
    scheduleOrder.reserve(N);
    int activeCol = -1;

    for (int step = 0; step < N; ++step) {
        // Find all ready blocks (all predecessors scheduled)
        std::vector<int> ready;
        for (int j = 0; j < N; ++j) {
            if (scheduled[j]) continue;
            bool canRun = true;
            for (int i = 0; i < j; ++i) {
                if (dep[i][j] && !scheduled[i]) {
                    canRun = false;
                    break;
                }
            }
            if (canRun) ready.push_back(j);
        }

        if (ready.empty()) break; // safety break

        // Pick block: prefer matching activeCol, else lowest original index
        int bestPick = ready[0];
        if (activeCol != -1) {
            for (int r : ready) {
                if (blocks[r].colorIdx == activeCol) {
                    bestPick = r;
                    break;
                }
            }
        }

        scheduled[bestPick] = true;
        scheduleOrder.push_back(bestPick);
        activeCol = blocks[bestPick].colorIdx;
    }

    // 4. Reconstruct optimized StitchSequence
    StitchSequence out;
    out.palette = in.palette;

    int lastCol = -1;
    for (size_t i = 0; i < scheduleOrder.size(); ++i) {
        const Block& b = blocks[scheduleOrder[i]];
        if (b.stitches.empty()) continue;

        const bool needsColorChange = (lastCol != -1 && b.colorIdx != lastCol);
        const bool needsJump = (lastCol != -1 && !needsColorChange);

        for (size_t sIdx = 0; sIdx < b.stitches.size(); ++sIdx) {
            Stitch copy = b.stitches[sIdx];
            copy.colorIdx = b.colorIdx;
            if (sIdx == 0) {
                if (needsColorChange) {
                    copy.flags |= (SF_Jump | SF_Trim | SF_ColorChange);
                } else if (needsJump) {
                    copy.flags |= (SF_Jump | SF_Trim);
                    copy.flags &= ~SF_ColorChange;
                } else if (lastCol == -1) {
                    copy.flags &= ~SF_ColorChange;
                }
            } else {
                copy.flags &= ~SF_ColorChange;
            }
            out.add(copy);
        }
        lastCol = b.colorIdx;
    }

    // 5. Calculate statistics
    if (stats) {
        stats->colorChangesBefore = int(in.colorChangeCount());
        stats->colorChangesAfter  = int(out.colorChangeCount());
        stats->blocksBefore       = N;
        stats->blocksAfter        = int(scheduleOrder.size());
        if (stats->colorChangesBefore > 0) {
            const int diff = stats->colorChangesBefore - stats->colorChangesAfter;
            stats->savedPercent = std::max(0.0, 100.0 * diff / double(stats->colorChangesBefore));
        } else {
            stats->savedPercent = 0.0;
        }
        stats->summary = QStringLiteral("Farbwechsel von %1 auf %2 reduziert (%3 % eingespart).")
                             .arg(stats->colorChangesBefore)
                             .arg(stats->colorChangesAfter)
                             .arg(QString::number(stats->savedPercent, 'f', 1));
    }

    return out;
}

bool ColorSorter::canOptimize(const StitchSequence& in)
{
    Stats s;
    sort(in, &s);
    return s.colorChangesAfter < s.colorChangesBefore;
}

} // namespace stick
