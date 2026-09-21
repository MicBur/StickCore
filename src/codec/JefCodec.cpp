// ---------------------------------------------------------------------------
//  StickCore  –  JefCodec.cpp
//
//  JEF header layout — VERIFIED byte-for-byte against a real Janome file
//  (aprildreams.jef, 10 colours, 214×182 mm). Little-endian throughout.
//
//    0x00  int32   stitchOffset   = 0x74 + colorCount*8   (start of stitches)
//    0x04  int32   0x14           (format/version marker)
//    0x08  char14  date "YYYYMMDDHHMMSS"
//    0x16  byte    0x64           (constant in the reference; meaning unconfirmed)
//    0x17  byte    0x00
//    0x18  int32   colorCount
//    0x1C  int32   stitchCount    (normal + 2*(jumps+colourChanges) + 1)
//    0x20  int32   hoopCode
//    0x24  int32   distLeft   = -minX   (0.1 mm, from design centre)
//    0x28  int32   distTop    =  maxY
//    0x2C  int32   distRight  =  maxX
//    0x30  int32   distBottom = -minY
//    0x34  int32×16  four spare rectangles, all 0xFFFFFFFF (-1, unused)
//    0x74  int32×colorCount   Janome thread catalogue codes
//    ....  int32×colorCount   per-colour attribute (0x0D in the reference)
//    ....  stitch data
//
//  Stitch encoding (confirmed against the reference):
//    normal      : int8 dx, int8 dy               (0.1 mm, range ±127)
//    colour stop : 0x80 0x01 dx dy
//    jump / trim : 0x80 0x02 dx dy
//    end         : 0x80 0x10 0x00 0x00
//  JEF flips the Y axis and centres the design on the hoop origin.
// ---------------------------------------------------------------------------
#include "codec/JefCodec.h"

#include <QFile>
#include <QByteArray>
#include <QDateTime>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <limits>

namespace stick {

namespace {

constexpr int   kFixedHeader   = 0x74;   // bytes before the two colour tables
constexpr int   kVersion       = 0x14;
constexpr int   kMaxDelta      = 127;    // 0x80 is a control byte -> ±127 only
constexpr quint8 kByte16        = 0x64;  // reference constant at 0x16
constexpr qint32 kColorAttrib   = 0x0D;  // reference per-colour attribute value

void putInt32LE(QByteArray& buf, qint32 v)
{
    char tmp[4];
    qToLittleEndian<qint32>(v, tmp);
    buf.append(tmp, 4);
}

inline qint32 toUnits(double mm) { return static_cast<qint32>(std::llround(mm * 10.0)); }
inline qint32 janomeCode(const ThreadColor& t) { return t.janomeCode >= 0 ? t.janomeCode : 0; }

// Encode one absolute move (0.1 mm units) into 'buf', splitting long moves.
// 'ctrl' is 0 for a normal stitch, or the sub-command byte after 0x80.
// 'countField' accumulates the header's stitchCount using the reference rule
// (+1 per normal pair, +2 per control command).
void encodeMove(QByteArray& buf, int dxU, int dyU, quint8 ctrl, int& countField)
{
    while (std::abs(dxU) > kMaxDelta || std::abs(dyU) > kMaxDelta) {
        const int sx = std::clamp(dxU, -kMaxDelta, kMaxDelta);
        const int sy = std::clamp(dyU, -kMaxDelta, kMaxDelta);
        buf.append(char(0x80));
        buf.append(char(0x02));                       // intermediate = jump
        buf.append(char(static_cast<signed char>(sx)));
        buf.append(char(static_cast<signed char>(sy)));
        dxU -= sx; dyU -= sy;
        countField += 2;
    }
    if (ctrl != 0) {
        buf.append(char(0x80));
        buf.append(char(static_cast<char>(ctrl)));
        buf.append(char(static_cast<signed char>(dxU)));
        buf.append(char(static_cast<signed char>(dyU)));
        countField += 2;
    } else {
        buf.append(char(static_cast<signed char>(dxU)));
        buf.append(char(static_cast<signed char>(dyU)));
        countField += 1;
    }
}

} // namespace

JefCodec::Result JefCodec::exportToFile(const QString& path,
                                        const StitchSequence& seq,
                                        HoopType hoop)
{
    const HoopSpec hs = hoopSpec(hoop);
    return exportToFile(path, seq, static_cast<int>(hoop), hs.widthMm, hs.heightMm);
}

JefCodec::Result JefCodec::exportToFile(const QString& path,
                                        const StitchSequence& seq,
                                        int hoopCode, double hoopWidthMm, double hoopHeightMm)
{
    Result r;
    if (seq.empty()) { r.message = QStringLiteral("Empty design"); return r; }

    // --- 1. centre the design on the hoop origin ---------------------------
    double minX, minY, maxX, maxY;
    if (!seq.bounds(minX, minY, maxX, maxY)) {
        r.message = QStringLiteral("No drawable stitches");
        return r;
    }
    const double cx = 0.5 * (minX + maxX);
    const double cy = 0.5 * (minY + maxY);

    // --- 2. boundary verification ------------------------------------------
    const double designW = maxX - minX;
    const double designH = maxY - minY;
    r.withinHoop = (designW <= hoopWidthMm + 1e-6) && (designH <= hoopHeightMm + 1e-6);

    int colorCount = static_cast<int>(seq.palette.size());
    if (colorCount < 1) colorCount = 1;

    // --- 3. encode the stitch stream (also computes design extents in units) -
    QByteArray body;
    int countField = 0;
    int lastX = 0, lastY = 0;             // 0.1 mm, centred, Y flipped
    bool haveLast = false;
    qint32 uMinX = 0, uMaxX = 0, uMinY = 0, uMaxY = 0;
    bool haveExt = false;

    auto absUnits = [&](const Stitch& s, int& ux, int& uy) {
        ux =  toUnits(s.x - cx);
        uy = -toUnits(s.y - cy);          // JEF Y axis points down
    };

    for (const Stitch& s : seq.stitches) {
        if (s.flags & SF_End) break;
        int ux, uy; absUnits(s, ux, uy);
        if (!haveExt) { uMinX = uMaxX = ux; uMinY = uMaxY = uy; haveExt = true; }
        else {
            uMinX = std::min(uMinX, ux); uMaxX = std::max(uMaxX, ux);
            uMinY = std::min(uMinY, uy); uMaxY = std::max(uMaxY, uy);
        }
        const int dx = haveLast ? (ux - lastX) : ux;
        const int dy = haveLast ? (uy - lastY) : uy;

        quint8 ctrl = 0;
        if (s.flags & (SF_ColorChange | SF_Stop)) ctrl = 0x01;
        else if (s.flags & (SF_Jump | SF_Trim))   ctrl = 0x02;

        encodeMove(body, dx, dy, ctrl, countField);
        lastX = ux; lastY = uy; haveLast = true;
    }
    body.append(char(0x80)); body.append(char(0x10));     // end command
    body.append(char(0x00)); body.append(char(0x00));
    countField += 1;

    // --- 4. build the header ------------------------------------------------
    QByteArray header;
    header.reserve(kFixedHeader + colorCount * 8);

    putInt32LE(header, kFixedHeader + colorCount * 8);       // 0x00 stitch offset
    putInt32LE(header, kVersion);                            // 0x04 version

    const QByteArray date = QDateTime::currentDateTime()
                                .toString(QStringLiteral("yyyyMMddHHmmss")).toLatin1();
    header.append(date.leftJustified(14, '0', true));        // 0x08 date (14)
    header.append(char(kByte16));                            // 0x16
    header.append(char(0x00));                               // 0x17

    putInt32LE(header, colorCount);                          // 0x18 colours
    putInt32LE(header, countField);                          // 0x1C stitch count
    putInt32LE(header, hoopCode);                            // 0x20 hoop code

    // 0x24 design extents from centre: Left, Top, Right, Bottom (0.1 mm).
    putInt32LE(header, -uMinX);
    putInt32LE(header,  uMaxY);
    putInt32LE(header,  uMaxX);
    putInt32LE(header, -uMinY);
    // 0x34: four spare rectangles, unused -> -1.
    for (int i = 0; i < 16; ++i) putInt32LE(header, -1);

    // 0x74: Janome thread codes, then per-colour attribute table.
    for (int i = 0; i < colorCount; ++i)
        putInt32LE(header, i < int(seq.palette.size()) ? janomeCode(seq.palette[i]) : 0);
    for (int i = 0; i < colorCount; ++i)
        putInt32LE(header, kColorAttrib);

    Q_ASSERT(header.size() == kFixedHeader + colorCount * 8);

    // --- 5. write the file --------------------------------------------------
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        r.message = QStringLiteral("Cannot open %1 for writing").arg(path);
        return r;
    }
    if (f.write(header) != header.size() || f.write(body) != body.size()) {
        r.message = QStringLiteral("Write error");
        f.close();
        return r;
    }
    f.close();

    r.ok = true;
    r.stitchesWritten = countField;
    r.message = r.withinHoop
        ? QStringLiteral("OK – %1 stitches, %2 colours").arg(countField).arg(colorCount)
        : QStringLiteral("WARNING – design %1×%2 mm exceeds %3×%4 mm hoop")
              .arg(designW, 0, 'f', 1).arg(designH, 0, 'f', 1)
              .arg(hoopWidthMm, 0, 'f', 0).arg(hoopHeightMm, 0, 'f', 0);
    return r;
}

bool JefCodec::importFromFile(const QString& path, StitchSequence& out)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray data = f.readAll();
    f.close();
    if (data.size() < kFixedHeader) return false;

    const uchar* p = reinterpret_cast<const uchar*>(data.constData());
    const qint32 stitchOffset = qFromLittleEndian<qint32>(p + 0x00);
    const qint32 colorCount   = qFromLittleEndian<qint32>(p + 0x18);
    if (stitchOffset < kFixedHeader || stitchOffset > data.size()) return false;

    out.clear();
    for (int i = 0; i < colorCount && (0x74 + i * 4 + 4) <= stitchOffset; ++i) {
        const qint32 code = qFromLittleEndian<qint32>(p + 0x74 + i * 4);
        ThreadColor tc(QColor(0, 0, 0));
        tc.janomeCode = code;
        out.palette.push_back(tc);
    }

    int idx = stitchOffset;
    double x = 0.0, y = 0.0;
    int color = 0;
    while (idx + 1 < data.size()) {
        const uchar b0 = p[idx];
        const uchar b1 = p[idx + 1];
        if (b0 == 0x80) {
            if (b1 == 0x10) break;                    // end
            if (idx + 3 >= data.size()) break;
            const int dx = static_cast<signed char>(p[idx + 2]);
            const int dy = static_cast<signed char>(p[idx + 3]);
            x += dx * 0.1; y -= dy * 0.1;
            if (b1 == 0x01) { ++color; out.add(x, y, SF_ColorChange, color); }
            else            {          out.add(x, y, SF_Jump,        color); }
            idx += 4;
        } else {
            const int dx = static_cast<signed char>(b0);
            const int dy = static_cast<signed char>(b1);
            x += dx * 0.1; y -= dy * 0.1;
            out.add(x, y, SF_Normal, color);
            idx += 2;
        }
    }
    return true;
}

} // namespace stick
