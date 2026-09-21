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
                         const QColor& fabric = QColor(0xEF, 0xEA, 0xDE))
    {
        QImage img(size, size, QImage::Format_ARGB32);
        img.fill(fabric);
        if (seq.stitches.size() < 2) return img;

        double minX, minY, maxX, maxY;
        if (!seq.bounds(minX, minY, maxX, maxY)) return img;

        const double w = std::max(1e-3, maxX - minX);
        const double h = std::max(1e-3, maxY - minY);
        const double pad = size * 0.10;
        const double s = std::min((size - 2*pad) / w, (size - 2*pad) / h);
        const double ox = (size - w * s) * 0.5;
        const double oy = (size - h * s) * 0.5;

        // map mm -> pixel, flip Y so +Y is up
        auto X = [&](double x){ return ox + (x - minX) * s; };
        auto Y = [&](double y){ return size - (oy + (y - minY) * s); };

        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);

        int prevColor = -1;
        QColor col(30, 30, 30);
        bool have = false;
        double px = 0, py = 0;

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
                p.setPen(QPen(col, std::max(1.0, size/170.0), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p.drawLine(QPointF(px, py), QPointF(cx, cy));
            }
            px = cx; py = cy; have = true;
        }
        p.end();
        return img;
    }
};

} // namespace stick
