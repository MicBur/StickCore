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
#include <QRectF>
#include <QPolygonF>
#include <QVector>
#include <QColor>
#include "core/Geometry.h"
#include <cmath>
#include <algorithm>

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
    int colorIdx = 0;
    QColor color = QColor(100, 160, 240);

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

    /// Exact bounding rectangle in mm.
    QRectF boundingRect() const
    {
        if (nodes.isEmpty()) return QRectF();
        double minX = nodes[0].pos.x(), maxX = minX;
        double minY = nodes[0].pos.y(), maxY = minY;
        for (const auto& n : nodes) {
            minX = std::min({minX, n.pos.x(), n.ctrlIn.x(), n.ctrlOut.x()});
            maxX = std::max({maxX, n.pos.x(), n.ctrlIn.x(), n.ctrlOut.x()});
            minY = std::min({minY, n.pos.y(), n.ctrlIn.y(), n.ctrlOut.y()});
            maxY = std::max({maxY, n.pos.y(), n.ctrlIn.y(), n.ctrlOut.y()});
        }
        return QRectF(minX, minY, std::max(0.0, maxX - minX), std::max(0.0, maxY - minY));
    }

    /// Geometric centroid of anchor nodes (mm).
    QPointF center() const
    {
        if (nodes.isEmpty()) return QPointF();
        double sx = 0.0, sy = 0.0;
        for (const auto& n : nodes) {
            sx += n.pos.x();
            sy += n.pos.y();
        }
        return QPointF(sx / nodes.size(), sy / nodes.size());
    }

    /// Surface area in mm² (0 for unclosed lines).
    double area() const
    {
        if (!closed || nodes.size() < 3) return 0.0;
        const QPolygonF poly = toPolygon();
        return polygonArea(poly);
    }

    /// Translate path in mm.
    void translate(double dx, double dy)
    {
        const QPointF d(dx, dy);
        for (auto& nd : nodes) nd.moveTo(nd.pos + d);
    }

    /// Scale path relative to an origin point.
    void scale(double sx, double sy, const QPointF& origin)
    {
        for (auto& nd : nodes) {
            nd.pos = origin + QPointF((nd.pos.x() - origin.x()) * sx, (nd.pos.y() - origin.y()) * sy);
            nd.ctrlIn = origin + QPointF((nd.ctrlIn.x() - origin.x()) * sx, (nd.ctrlIn.y() - origin.y()) * sy);
            nd.ctrlOut = origin + QPointF((nd.ctrlOut.x() - origin.x()) * sx, (nd.ctrlOut.y() - origin.y()) * sy);
        }
    }

    /// Rotate path in degrees clockwise relative to an origin point (matching +Y up Cartesian embroidery coordinates).
    void rotate(double angleDeg, const QPointF& origin)
    {
        const double rad = -angleDeg * M_PI / 180.0;
        const double cosA = std::cos(rad);
        const double sinA = std::sin(rad);
        auto rot = [&](const QPointF& p) {
            const QPointF d = p - origin;
            return origin + QPointF(d.x() * cosA - d.y() * sinA, d.x() * sinA + d.y() * cosA);
        };
        for (auto& nd : nodes) {
            nd.pos = rot(nd.pos);
            nd.ctrlIn = rot(nd.ctrlIn);
            nd.ctrlOut = rot(nd.ctrlOut);
        }
    }

    /// Mirror horizontally across a central X line.
    void flipHorizontal(double centerX)
    {
        for (auto& nd : nodes) {
            nd.pos.setX(2.0 * centerX - nd.pos.x());
            nd.ctrlIn.setX(2.0 * centerX - nd.ctrlIn.x());
            nd.ctrlOut.setX(2.0 * centerX - nd.ctrlOut.x());
            std::swap(nd.ctrlIn, nd.ctrlOut);
        }
        std::reverse(nodes.begin(), nodes.end());
    }

    /// Mirror vertically across a central Y line.
    void flipVertical(double centerY)
    {
        for (auto& nd : nodes) {
            nd.pos.setY(2.0 * centerY - nd.pos.y());
            nd.ctrlIn.setY(2.0 * centerY - nd.ctrlIn.y());
            nd.ctrlOut.setY(2.0 * centerY - nd.ctrlOut.y());
            std::swap(nd.ctrlIn, nd.ctrlOut);
        }
        std::reverse(nodes.begin(), nodes.end());
    }

    /// Bend a segment between nodes[segIdx] and nodes[segIdx+1] by dragging.
    void bendSegment(int segIdx, const QPointF& deltaMm)
    {
        if (nodes.isEmpty() || segIdx < 0 || segIdx >= nodes.size()) return;
        int i0 = segIdx;
        int i1 = segIdx + 1;
        if (i1 >= nodes.size()) {
            if (!closed) return;
            i1 = 0;
        }
        BezierNode& n0 = nodes[i0];
        BezierNode& n1 = nodes[i1];
        if (n0.ctrlOut == n0.pos) {
            n0.ctrlOut = n0.pos + (n1.pos - n0.pos) * (1.0 / 3.0);
        }
        if (n1.ctrlIn == n1.pos) {
            n1.ctrlIn = n0.pos + (n1.pos - n0.pos) * (2.0 / 3.0);
        }
        n0.ctrlOut += deltaMm;
        n1.ctrlIn  += deltaMm;
        n0.smooth = false;
        n1.smooth = false;
    }
};

} // namespace stick
