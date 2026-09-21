// ---------------------------------------------------------------------------
//  StickCore  –  BezierPath.h
//
//  Editable cubic-Bézier path model used by the 2-D editor. Positions are in
//  millimetres, +Y up (same convention as the generators). Each node keeps its
//  two control handles as ABSOLUTE positions; a "corner" node simply has both
//  handles sitting on the anchor.
// ---------------------------------------------------------------------------
#pragma once

#include <QPainterPath>
#include <QPointF>
#include <QPolygonF>
#include <QVector>

namespace stick {

struct BezierNode {
    QPointF pos;        ///< anchor (mm)
    QPointF ctrlIn;     ///< incoming handle (mm, absolute)
    QPointF ctrlOut;    ///< outgoing handle (mm, absolute)
    bool    smooth = true;

    BezierNode() = default;
    explicit BezierNode(const QPointF& p)
        : pos(p), ctrlIn(p), ctrlOut(p) {}

    void moveTo(const QPointF& p) {           // move anchor, drag handles along
        const QPointF d = p - pos;
        pos += d; ctrlIn += d; ctrlOut += d;
    }
    bool isCorner() const {
        return ctrlIn == pos && ctrlOut == pos;
    }
};

struct EditPath {
    QVector<BezierNode> nodes;
    bool closed = false;

    int  count() const { return nodes.size(); }
    bool empty() const { return nodes.isEmpty(); }

    /// Build a QPainterPath (mm space) for the generators / rendering.
    QPainterPath toPainterPath() const
    {
        QPainterPath p;
        if (nodes.isEmpty()) return p;
        p.moveTo(nodes[0].pos);
        for (int i = 1; i < nodes.size(); ++i)
            p.cubicTo(nodes[i-1].ctrlOut, nodes[i].ctrlIn, nodes[i].pos);
        if (closed && nodes.size() >= 2) {
            p.cubicTo(nodes.back().ctrlOut, nodes[0].ctrlIn, nodes[0].pos);
            p.closeSubpath();
        }
        return p;
    }

    /// Flattened polygon (mm) — for closed shapes used by the tatami fill.
    QPolygonF toPolygon() const
    {
        const QPainterPath pp = toPainterPath();
        QPolygonF poly = pp.toFillPolygon();
        if (!poly.isEmpty() && poly.first() == poly.last())
            poly.removeLast();
        return poly;
    }
};

} // namespace stick
