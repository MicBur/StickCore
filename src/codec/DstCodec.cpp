// ---------------------------------------------------------------------------
//  StickCore  –  DstCodec.cpp
// ---------------------------------------------------------------------------
#include "codec/DstCodec.h"

#include <QFile>
#include <QByteArray>
#include <cmath>
#include <algorithm>

namespace stick {

namespace {

constexpr int kMaxDelta = 121;  // ±12.1 mm maximum single move in DST

void encodeDstMove(int dx, int dy, bool jump, bool colorChange, QByteArray& out)
{
    unsigned char b0 = 0, b1 = 0, b2 = 0x03;
    if (jump)        b2 |= 0x80;
    if (colorChange) b2 |= 0x40;

    if (dx > 40)  { b2 |= 0x04; dx -= 81; }
    if (dx < -40) { b2 |= 0x08; dx += 81; }
    if (dx > 13)  { b1 |= 0x04; dx -= 27; }
    if (dx < -13) { b1 |= 0x08; dx += 27; }
    if (dx > 4)   { b0 |= 0x04; dx -= 9; }
    if (dx < -4)  { b0 |= 0x08; dx += 9; }
    if (dx > 1)   { b1 |= 0x01; dx -= 3; }
    if (dx < -1)  { b1 |= 0x02; dx += 3; }
    if (dx > 0)   { b0 |= 0x01; dx -= 1; }
    if (dx < 0)   { b0 |= 0x02; dx += 1; }

    if (dy > 40)  { b2 |= 0x20; dy -= 81; }
    if (dy < -40) { b2 |= 0x10; dy += 81; }
    if (dy > 13)  { b1 |= 0x20; dy -= 27; }
    if (dy < -13) { b1 |= 0x10; dy += 27; }
    if (dy > 4)   { b0 |= 0x20; dy -= 9; }
    if (dy < -4)  { b0 |= 0x10; dy += 9; }
    if (dy > 1)   { b1 |= 0x80; dy -= 3; }
    if (dy < -1)  { b1 |= 0x40; dy += 3; }
    if (dy > 0)   { b0 |= 0x80; dy -= 1; }
    if (dy < 0)   { b0 |= 0x40; dy += 1; }

    out.append(char(b0));
    out.append(char(b1));
    out.append(char(b2));
}

void splitAndEncode(int dx, int dy, bool isJump, bool isColorChange, QByteArray& out, int& count)
{
    while (std::abs(dx) > kMaxDelta || std::abs(dy) > kMaxDelta) {
        int sx = std::clamp(dx, -kMaxDelta, kMaxDelta);
        int sy = std::clamp(dy, -kMaxDelta, kMaxDelta);
        encodeDstMove(sx, sy, true, false, out);
        dx -= sx;
        dy -= sy;
        count++;
    }
    encodeDstMove(dx, dy, isJump, isColorChange, out);
    count++;
}

void decodeDstMove(unsigned char b0, unsigned char b1, unsigned char b2, int& dx, int& dy, quint32& flags)
{
    dx = 0; dy = 0; flags = SF_Normal;
    if (b2 == 0xF3) { flags = SF_End; return; }
    if (b2 & 0x80) flags |= SF_Jump;
    if (b2 & 0x40) flags |= SF_ColorChange;

    if (b2 & 0x04) dx += 81;
    if (b2 & 0x08) dx -= 81;
    if (b1 & 0x04) dx += 27;
    if (b1 & 0x08) dx -= 27;
    if (b0 & 0x04) dx += 9;
    if (b0 & 0x08) dx -= 9;
    if (b1 & 0x01) dx += 3;
    if (b1 & 0x02) dx -= 3;
    if (b0 & 0x01) dx += 1;
    if (b0 & 0x02) dx -= 1;

    if (b2 & 0x20) dy += 81;
    if (b2 & 0x10) dy -= 81;
    if (b1 & 0x20) dy += 27;
    if (b1 & 0x10) dy -= 27;
    if (b0 & 0x20) dy += 9;
    if (b0 & 0x10) dy -= 9;
    if (b1 & 0x80) dy += 3;
    if (b1 & 0x40) dy -= 3;
    if (b0 & 0x80) dy += 1;
    if (b0 & 0x40) dy -= 1;
}

inline int toUnits(double mm) { return static_cast<int>(std::llround(mm * 10.0)); }

} // namespace

DstCodec::Result DstCodec::exportToFile(const QString& path, const StitchSequence& seq)
{
    Result r;
    if (seq.empty()) { r.message = QStringLiteral("Keine Stiche zum Exportieren."); return r; }

    double minX, minY, maxX, maxY;
    if (!seq.bounds(minX, minY, maxX, maxY)) {
        r.message = QStringLiteral("Ungültige Stichkoordinaten.");
        return r;
    }

    const double cx = 0.5 * (minX + maxX);
    const double cy = 0.5 * (minY + maxY);

    QByteArray body;
    int stitchCount = 0;
    int lastX = 0, lastY = 0;
    bool haveLast = false;
    int colorChanges = 0;

    int uMinX = 0, uMaxX = 0, uMinY = 0, uMaxY = 0;
    bool haveExt = false;

    for (const Stitch& s : seq.stitches) {
        if (s.flags & SF_End) break;
        int ux = toUnits(s.x - cx);
        int uy = toUnits(s.y - cy);

        if (!haveExt) { uMinX = uMaxX = ux; uMinY = uMaxY = uy; haveExt = true; }
        else {
            uMinX = std::min(uMinX, ux); uMaxX = std::max(uMaxX, ux);
            uMinY = std::min(uMinY, uy); uMaxY = std::max(uMaxY, uy);
        }

        int dx = haveLast ? (ux - lastX) : ux;
        int dy = haveLast ? (uy - lastY) : uy;

        const bool isColorChange = (s.flags & (SF_ColorChange | SF_Stop)) != 0;
        const bool isJump        = (s.flags & (SF_Jump | SF_Trim)) != 0;
        if (isColorChange) colorChanges++;

        splitAndEncode(dx, dy, isJump, isColorChange, body, stitchCount);
        lastX = ux; lastY = uy; haveLast = true;
    }

    // End code 0x00 0x00 0xF3
    body.append(char(0x00));
    body.append(char(0x00));
    body.append(char(0xF3));
    stitchCount++;

    // 512-byte Tajima header
    QByteArray header(512, ' ');
    auto setField = [&](const char* tag, const QString& val, int pos) {
        QByteArray f = QByteArray(tag) + val.toLatin1();
        for (int i = 0; i < f.size() && pos + i < 512; ++i)
            header[pos + i] = f[i];
    };

    setField("LA:", "StickCore", 0);
    setField("ST:", QStringLiteral("%1").arg(stitchCount, 7, 10, QChar(' ')), 16);
    setField("CO:", QStringLiteral("%1").arg(colorChanges, 3, 10, QChar(' ')), 27);
    setField("+X:", QStringLiteral("%1").arg(std::max(0, uMaxX), 5, 10, QChar(' ')), 35);
    setField("-X:", QStringLiteral("%1").arg(std::abs(std::min(0, uMinX)), 5, 10, QChar(' ')), 44);
    setField("+Y:", QStringLiteral("%1").arg(std::max(0, uMaxY), 5, 10, QChar(' ')), 53);
    setField("-Y:", QStringLiteral("%1").arg(std::abs(std::min(0, uMinY)), 5, 10, QChar(' ')), 62);
    setField("AX:", QStringLiteral("+%1").arg(0, 5, 10, QChar(' ')), 71);
    setField("AY:", QStringLiteral("+%1").arg(0, 5, 10, QChar(' ')), 80);
    setField("MX:", QStringLiteral("+%1").arg(0, 5, 10, QChar(' ')), 89);
    setField("MY:", QStringLiteral("+%1").arg(0, 5, 10, QChar(' ')), 98);
    setField("PD:", "*******", 107);
    header[511] = char(0x1A);   // EOF

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        r.message = QStringLiteral("Konnte Datei %1 nicht schreiben").arg(path);
        return r;
    }
    f.write(header);
    f.write(body);
    f.close();

    r.ok = true;
    r.stitchesWritten = stitchCount;
    r.message = QStringLiteral("DST exportiert (%1 Stiche, %2 Farbwechsel)").arg(stitchCount).arg(colorChanges);
    return r;
}

bool DstCodec::importFromFile(const QString& path, StitchSequence& out)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray data = f.readAll();
    f.close();

    if (data.size() < 512 + 3) return false;

    out.clear();
    out.palette.emplace_back(QColor(40, 40, 40), QStringLiteral("Tajima 1"));

    int curColor = 0;
    double curX = 0.0, curY = 0.0;

    for (int i = 512; i + 2 < data.size(); i += 3) {
        const auto b0 = static_cast<unsigned char>(data[i]);
        const auto b1 = static_cast<unsigned char>(data[i + 1]);
        const auto b2 = static_cast<unsigned char>(data[i + 2]);

        int dx = 0, dy = 0;
        quint32 flags = SF_Normal;
        decodeDstMove(b0, b1, b2, dx, dy, flags);

        if (flags & SF_End) break;

        curX += dx * 0.1;
        curY += dy * 0.1;

        if (flags & SF_ColorChange) {
            curColor++;
            out.palette.emplace_back(QColor(40, 40, 40), QStringLiteral("Farbe %1").arg(curColor + 1));
        }

        out.add(curX, curY, flags, curColor);
    }
    return !out.empty();
}

} // namespace stick
