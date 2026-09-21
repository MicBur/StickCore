// ---------------------------------------------------------------------------
//  StickCore  –  ThreadCatalog.cpp
// ---------------------------------------------------------------------------
#include "core/ThreadCatalog.h"
#include <cmath>
#include <limits>

namespace stick {

const std::vector<CatalogThread>& ThreadCatalog::all()
{
    // Janome codes marked (✓) were confirmed from real .jef files; the rest are
    // plausible catalogue entries. RGBs are approximate starter values.
    static const std::vector<CatalogThread> data = {
        // --- Janome (code = JEF thread code) ---
        {"Janome","Black",           1,  QColor(  0,  0,  0)},
        {"Janome","White",           2,  QColor(245,245,245)},
        {"Janome","Yellow",          3,  QColor(247,214, 45)}, // ✓
        {"Janome","Gold",            4,  QColor(216,170, 40)},
        {"Janome","Olive Green",     5,  QColor(107,116, 51)}, // ✓
        {"Janome","Emerald",         7,  QColor( 32,140, 92)},
        {"Janome","Royal Blue",      9,  QColor( 40, 70,150)},
        {"Janome","Sky Blue",       11,  QColor( 96,166,214)},
        {"Janome","Brown",          13,  QColor(107, 79, 42)}, // ✓
        {"Janome","Pale Yellow",    16,  QColor(238,228,150)},
        {"Janome","Pale Pink",      17,  QColor(236,196,200)}, // ✓
        {"Janome","Yellow Green",   22,  QColor( 63,155, 86)}, // ✓
        {"Janome","Teal",          26,  QColor( 30,120,120)},
        {"Janome","Salmon Pink",    31,  QColor(230,120,110)}, // ✓
        {"Janome","Grey",          34,  QColor(140,144,150)},
        {"Janome","Orchid Pink",    38,  QColor(224,106,134)}, // ✓
        {"Janome","Peony Purple",   39,  QColor(150, 60,120)},
        {"Janome","Burgundy",       40,  QColor(150, 40, 60)}, // ✓
        {"Janome","Navy",          45,  QColor( 30, 40, 80)},
        {"Janome","Vermilion",      74,  QColor(220, 70, 45)}, // ✓
        {"Janome","Deep Red",       76,  QColor(170, 30, 40)},

        // --- Madeira Polyneon (representative subset) ---
        {"Madeira","Snow White",  1801, QColor(250,250,250)},
        {"Madeira","Lemon",       1624, QColor(245,222, 70)},
        {"Madeira","Rust",        1678, QColor(178, 78, 42)},
        {"Madeira","Grass Green", 1701, QColor( 74,150, 66)},
        {"Madeira","Royal",       1733, QColor( 40, 66,150)},
        {"Madeira","Black",       1800, QColor( 12, 12, 12)},

        // --- Robison-Anton (subset) ---
        {"Robison-Anton","Ecru",   2201, QColor(232,220,190)},
        {"Robison-Anton","Cardinal",2311,QColor(170, 34, 46)},
        {"Robison-Anton","Kelly",  2273, QColor( 38,140, 74)},
        {"Robison-Anton","Marine", 2354, QColor( 28, 72,140)},

        // --- Mettler (subset) ---
        {"Mettler","Snow",         1000, QColor(248,248,248)},
        {"Mettler","Poppy",        1305, QColor(206, 46, 50)},
        {"Mettler","Fern",         1099, QColor( 66,132, 60)},
        {"Mettler","Sapphire",     1076, QColor( 36, 78,146)},
    };
    return data;
}

// Perceptual "redmean" RGB distance – cheap and noticeably better than plain
// Euclidean for matching thread colours.
static double dist2(const QColor& a, const QColor& b)
{
    const double rm = 0.5 * (a.red() + b.red());
    const double dr = a.red()   - b.red();
    const double dg = a.green() - b.green();
    const double db = a.blue()  - b.blue();
    return (2 + rm/256.0)*dr*dr + 4*dg*dg + (2 + (255-rm)/256.0)*db*db;
}

CatalogThread ThreadCatalog::nearest(const QColor& c, const QString& brand)
{
    const auto& list = all();
    const CatalogThread* best = nullptr;
    double bestD = std::numeric_limits<double>::max();
    for (const auto& t : list) {
        if (!brand.isEmpty() && t.brand != brand) continue;
        const double d = dist2(c, t.color);
        if (d < bestD) { bestD = d; best = &t; }
    }
    if (best) return *best;
    return { QStringLiteral("Janome"), QStringLiteral("Black"), 1, QColor(0,0,0) };
}

} // namespace stick
