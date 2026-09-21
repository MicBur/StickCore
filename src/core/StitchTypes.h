// ---------------------------------------------------------------------------
//  StickCore  –  StitchTypes.h
//
//  Fundamental data types shared by generators, codecs and the OpenGL viewer.
//  All internal coordinates are in *millimetres* with the mathematical
//  convention: +X to the right, +Y up.  Codecs are responsible for converting
//  to their own machine units (JEF uses 0.1 mm and a flipped Y axis).
// ---------------------------------------------------------------------------
#pragma once

#include <QColor>
#include <QMetaType>
#include <QString>
#include <QVector>
#include <cmath>
#include <vector>

namespace stick {

// ---------------------------------------------------------------------------
//  Stitch flags – describe the *kind* of penetration / machine command.
//  These are bit flags so they can be OR-combined (e.g. Jump|Trim).
// ---------------------------------------------------------------------------
enum StitchFlag : quint32 {
    SF_Normal      = 0x00,   ///< Ordinary needle penetration.
    SF_Jump        = 0x01,   ///< Move without stitching (feed only).
    SF_Trim        = 0x02,   ///< Cut the thread before the next move.
    SF_Stop        = 0x04,   ///< Machine stop (usually a color change).
    SF_ColorChange = 0x08,   ///< Explicit color change command.
    SF_End         = 0x10    ///< End of design.
};

// ---------------------------------------------------------------------------
//  A single machine coordinate.  Position is absolute (mm), flags describe
//  the command, colorIndex refers into StitchSequence::palette.
// ---------------------------------------------------------------------------
struct Stitch {
    double  x        = 0.0;
    double  y        = 0.0;
    quint32 flags    = SF_Normal;
    int     colorIdx = 0;

    Stitch() = default;
    Stitch(double x_, double y_, quint32 f = SF_Normal, int c = 0)
        : x(x_), y(y_), flags(f), colorIdx(c) {}

    bool is(StitchFlag f) const { return (flags & f) != 0u; }
};

// ---------------------------------------------------------------------------
//  A thread color together with an optional Janome catalogue code (used by
//  the JEF codec's color table).  If janomeCode < 0 the codec picks the
//  nearest catalogue entry.
// ---------------------------------------------------------------------------
struct ThreadColor {
    QColor  color      = QColor(0, 0, 0);
    QString description;
    int     janomeCode = -1;

    ThreadColor() = default;
    explicit ThreadColor(const QColor& c, QString desc = QString(), int code = -1)
        : color(c), description(std::move(desc)), janomeCode(code) {}
};

// ---------------------------------------------------------------------------
//  A complete design: the ordered stitch list plus its color palette.
// ---------------------------------------------------------------------------
struct StitchSequence {
    std::vector<Stitch>      stitches;
    std::vector<ThreadColor> palette;

    void clear() { stitches.clear(); palette.clear(); }
    bool empty()  const { return stitches.empty(); }
    std::size_t size() const { return stitches.size(); }

    void add(const Stitch& s)                         { stitches.push_back(s); }
    void add(double x, double y, quint32 f = SF_Normal, int c = 0)
    {
        stitches.emplace_back(x, y, f, c);
    }

    /// Count real needle penetrations (excludes jumps and pure commands).
    std::size_t realStitchCount() const
    {
        std::size_t n = 0;
        for (const auto& s : stitches)
            if (!(s.flags & (SF_Jump | SF_End | SF_Stop | SF_ColorChange)))
                ++n;
        return n;
    }

    std::size_t colorChangeCount() const
    {
        std::size_t n = 0;
        for (const auto& s : stitches)
            if (s.flags & (SF_ColorChange | SF_Stop)) ++n;
        return n;
    }

    /// Axis-aligned bounding box in mm. Returns false when the design is empty.
    bool bounds(double& minX, double& minY, double& maxX, double& maxY) const
    {
        if (stitches.empty()) return false;
        minX = minY =  std::numeric_limits<double>::max();
        maxX = maxY = -std::numeric_limits<double>::max();
        for (const auto& s : stitches) {
            if (s.flags & SF_End) continue;
            minX = std::min(minX, s.x); maxX = std::max(maxX, s.x);
            minY = std::min(minY, s.y); maxY = std::max(maxY, s.y);
        }
        return true;
    }
};

// ---------------------------------------------------------------------------
//  Hoop definitions.  The enum values ARE the JEF header hoop codes.
//  Classic JEF codes 0-4 are documented (KDE/libembroidery); modern JEF+
//  machines use additional codes (e.g. the reference file used code 28 for a
//  large modern hoop) — for those, use the raw-code export overload.
// ---------------------------------------------------------------------------
enum class HoopType : int {
    HoopA_126x110 = 0,   ///< "A" Standard        (126 x 110 mm)  [MC350E]
    HoopC_50x50   = 1,   ///< "C" Free Arm        ( 50 x  50 mm)
    HoopB_140x200 = 2,   ///< "B" Large           (140 x 200 mm)  [MC350E]
    HoopF_126x110 = 3,   ///< "F" Spring Loaded   (126 x 110 mm)
    HoopD_230x200 = 4,   ///< "D" Giga            (230 x 200 mm)
    HoopSQ14_140  = 15,  ///< "SQ14" square       (140 x 140 mm)  verified vs modflower2.jef
    HoopSQ23_230  = 28   ///< "SQ23" square       (230 x 230 mm)  verified vs aprildreams.jef
};

struct HoopSpec {
    double widthMm;
    double heightMm;
    const char* name;
};

inline HoopSpec hoopSpec(HoopType t)
{
    switch (t) {
    case HoopType::HoopC_50x50:   return {  50.0,  50.0, "C 50x50"   };
    case HoopType::HoopB_140x200: return { 140.0, 200.0, "B 140x200" };
    case HoopType::HoopF_126x110: return { 126.0, 110.0, "F 126x110" };
    case HoopType::HoopD_230x200: return { 230.0, 200.0, "D 230x200" };
    case HoopType::HoopSQ14_140:  return { 140.0, 140.0, "SQ14 140x140" };
    case HoopType::HoopSQ23_230:  return { 230.0, 230.0, "SQ23 230x230" };
    case HoopType::HoopA_126x110:
    default:                      return { 126.0, 110.0, "A 126x110" };
    }
}

// Pick the smallest hoop that fits a design of w×h mm. 'fits' reports whether
// any supported hoop can hold it. Order matches the Janome MC350E: A (126×110)
// then B (140×200), with C and Giga as fallbacks. Rotation is allowed.
inline HoopType fitHoopFor(double w, double h, bool* fits = nullptr)
{
    const HoopType order[] = { HoopType::HoopC_50x50,    HoopType::HoopA_126x110,
                               HoopType::HoopSQ14_140,   HoopType::HoopB_140x200,
                               HoopType::HoopD_230x200,  HoopType::HoopSQ23_230 };
    for (HoopType t : order) {
        const HoopSpec s = hoopSpec(t);
        const bool ok = (w <= s.widthMm + 1e-6 && h <= s.heightMm + 1e-6) ||
                        (h <= s.widthMm + 1e-6 && w <= s.heightMm + 1e-6);
        if (ok) { if (fits) *fits = true; return t; }
    }
    if (fits) *fits = false;
    return HoopType::HoopB_140x200;   // largest MC350E hoop as a last resort
}

} // namespace stick

Q_DECLARE_METATYPE(stick::Stitch)
