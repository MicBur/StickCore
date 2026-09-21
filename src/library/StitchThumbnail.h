// ---------------------------------------------------------------------------
//  StickCore  –  StitchThumbnail.h
//
//  Renders a StitchSequence to a small preview image (thread on fabric) for
//  the design library grid. Pure QPainter — no OpenGL, works headless.
// ---------------------------------------------------------------------------
#pragma once

#include "core/StitchTypes.h"
#include <QImage>
#include <QPainter>
#include <QColor>
#include <algorithm>

namespace stick {

class StitchThumbnail {
public:
    static QImage render(const StitchSequence& seq, int size = 240,
                         const QColor& fabric = QColor(0xEF, 0xEA, 0xDE),
                         bool realistic = false)
    {
        QImage img(size, size, QImage::Format_ARGB32);
        img.fill(fabric);
        if (seq.stitches.size() < 2) return img;

        double minX, minY, maxX, maxY;
        if (!seq.bounds(minX, minY, maxX, maxY)) return img;

        const double w = std::max(1e-3, maxX - minX);
        const double h = std::max(1e-3, maxY - minY);
        const double pad = size * 0.08;
        const double s = std::min((size - 2*pad) / w, (size - 2*pad) / h);
        const double ox = (size - w * s) * 0.5;
        const double oy = (size - h * s) * 0.5;

        // map mm -> pixel, flip Y so +Y is up
        auto X = [&](double x){ return ox + (x - minX) * s; };
        auto Y = [&](double y){ return size - (oy + (y - minY) * s); };

        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);

        if (realistic) {
            // Subtle fabric weave texture
            const QColor weaveCol = (fabric.lightness() < 80)
                ? QColor(255, 255, 255, 8)
                : QColor(0, 0, 0, 10);
            p.setPen(QPen(weaveCol, 1.0));
            for (int y = 0; y < size; y += 6) p.drawLine(0, y, size, y);
            for (int x = 0; x < size; x += 6) p.drawLine(x, 0, x, size);
        }

        int prevColor = -1;
        QColor col(30, 30, 30);
        bool have = false;
        double px = 0, py = 0;
        const double strokeW = std::max(1.2, size / 160.0);

        for (const Stitch& st : seq.stitches) {
            if (st.flags & SF_End) break;
            if (st.colorIdx != prevColor) {
                prevColor = st.colorIdx;
                col = (st.colorIdx >= 0 && st.colorIdx < int(seq.palette.size()))
                    ? seq.palette[st.colorIdx].color : QColor(30,30,30);
            }
            const double cx = X(st.x), cy = Y(st.y);
            const bool travel = (st.flags & (SF_Jump | SF_Trim | SF_ColorChange | SF_Stop)) != 0;
            if (have && !travel) {
                if (realistic) {
                    // Under-thread shadow
                    p.setPen(QPen(col.darker(170), strokeW * 1.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                    p.drawLine(QPointF(px + 0.5, py + 0.5), QPointF(cx + 0.5, cy + 0.5));

                    // Base thread
                    p.setPen(QPen(col, strokeW, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                    p.drawLine(QPointF(px, py), QPointF(cx, cy));

                    // Subtle sheen highlight
                    p.setPen(QPen(col.lighter(130), strokeW * 0.35, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                    p.drawLine(QPointF(px - 0.2, py - 0.2), QPointF(cx - 0.2, cy - 0.2));
                } else {
                    p.setPen(QPen(col, strokeW, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                    p.drawLine(QPointF(px, py), QPointF(cx, cy));
                }
            }
            px = cx; py = cy; have = true;
        }
        p.end();
        return img;
    }
};

} // namespace stick
