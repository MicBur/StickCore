// ---------------------------------------------------------------------------
//  StickCore  –  CarvingPattern.cpp
// ---------------------------------------------------------------------------
#include "generators/CarvingPattern.h"
#include <cmath>
#include <algorithm>

namespace stick {

QString CarvingPattern::presetName(Preset p)
{
    switch (p) {
        case Preset::None:        return QStringLiteral("Keine Prägung (Glatt)");
        case Preset::Star:        return QStringLiteral("Stern (5-zackig)");
        case Preset::Heart:       return QStringLiteral("Zierherz");
        case Preset::OakLeaf:     return QStringLiteral("Eichenblatt (Tradition)");
        case Preset::DiamondGrid: return QStringLiteral("Rauten-Gitter (Jacquard)");
        case Preset::WaveLines:   return QStringLiteral("Wellenlinien (Kräuselung)");
        case Preset::Custom:      return QStringLiteral("Eigener Vektorstempel");
    }
    return QStringLiteral("Keine");
}

QPainterPath CarvingPattern::createPath(Preset preset, double widthMm, double heightMm,
                                       const QPointF& center)
{
    QPainterPath path;
    const double hw = widthMm * 0.5;
    const double hh = heightMm * 0.5;

    switch (preset) {
        case Preset::None:
            break;

        case Preset::Star: {
            const int points = 5;
            const double rOut = std::min(hw, hh);
            const double rIn = rOut * 0.40;
            const double step = 3.14159265358979323846 / points;
            for (int i = 0; i < 2 * points; ++i) {
                const double r = (i % 2 == 0) ? rOut : rIn;
                const double angle = i * step - 3.14159265358979323846 * 0.5;
                const double x = center.x() + r * std::cos(angle);
                const double y = center.y() + r * std::sin(angle);
                if (i == 0) path.moveTo(x, y);
                else path.lineTo(x, y);
            }
            path.closeSubpath();
            break;
        }

        case Preset::Heart: {
            // Symmetrical cubic Bézier heart
            const double cx = center.x();
            const double cy = center.y();
            path.moveTo(cx, cy - hh * 0.7);
            path.cubicTo(cx + hw * 0.9, cy - hh * 1.1,
                         cx + hw * 1.1, cy + hh * 0.1,
                         cx, cy + hh);
            path.cubicTo(cx - hw * 1.1, cy + hh * 0.1,
                         cx - hw * 0.9, cy - hh * 1.1,
                         cx, cy - hh * 0.7);
            path.closeSubpath();
            break;
        }

        case Preset::OakLeaf: {
            // Traditional 3-lobed oak leaf silhouette
            const double cx = center.x();
            const double cy = center.y();
            path.moveTo(cx, cy + hh); // base stem
            // Right lobe 1
            path.cubicTo(cx + hw * 0.4, cy + hh * 0.8, cx + hw * 0.9, cy + hh * 0.6, cx + hw * 0.7, cy + hh * 0.4);
            // Right lobe 2
            path.cubicTo(cx + hw * 1.0, cy + hh * 0.3, cx + hw * 0.9, cy - hh * 0.2, cx + hw * 0.6, cy - hh * 0.4);
            // Right top lobe & apex
            path.cubicTo(cx + hw * 0.7, cy - hh * 0.7, cx + hw * 0.3, cy - hh * 0.95, cx, cy - hh);
            // Left top lobe
            path.cubicTo(cx - hw * 0.3, cy - hh * 0.95, cx - hw * 0.7, cy - hh * 0.7, cx - hw * 0.6, cy - hh * 0.4);
            // Left lobe 2
            path.cubicTo(cx - hw * 0.9, cy - hh * 0.2, cx - hw * 1.0, cy + hh * 0.3, cx - hw * 0.7, cy + hh * 0.4);
            // Left lobe 1 & return to stem
            path.cubicTo(cx - hw * 0.9, cy + hh * 0.6, cx - hw * 0.4, cy + hh * 0.8, cx, cy + hh);
            path.closeSubpath();
            break;
        }

        case Preset::DiamondGrid: {
            // Cross-hatched diamond pattern
            const double cx = center.x();
            const double cy = center.y();
            for (double d = -hw; d <= hw; d += hw * 0.4) {
                path.moveTo(cx + d, cy - hh);
                path.lineTo(cx + d + hw * 0.5, cy + hh);
                path.moveTo(cx + d, cy - hh);
                path.lineTo(cx + d - hw * 0.5, cy + hh);
            }
            break;
        }

        case Preset::WaveLines: {
            // Multiple gentle wave ripples
            const double cx = center.x();
            const double cy = center.y();
            for (double rowY = cy - hh * 0.8; rowY <= cy + hh * 0.8; rowY += hh * 0.35) {
                path.moveTo(cx - hw, rowY);
                for (double x = cx - hw; x <= cx + hw; x += hw * 0.2) {
                    const double phase = (x - (cx - hw)) / widthMm * 6.2831853;
                    path.lineTo(x, rowY + hh * 0.08 * std::sin(phase));
                }
            }
            break;
        }

        case Preset::Custom:
            break;
    }

    return path;
}

QVector<double> CarvingPattern::findIntersections(const QPainterPath& path, double scanY)
{
    QVector<double> xs;
    if (path.isEmpty()) return xs;

    const QList<QPolygonF> polys = path.toSubpathPolygons();
    for (const QPolygonF& poly : polys) {
        const int n = poly.size();
        for (int i = 0; i < n - 1; ++i) {
            const QPointF& p1 = poly[i];
            const QPointF& p2 = poly[i + 1];

            if ((p1.y() <= scanY && p2.y() > scanY) || (p2.y() <= scanY && p1.y() > scanY)) {
                const double dy = p2.y() - p1.y();
                if (std::abs(dy) > 1e-9) {
                    const double t = (scanY - p1.y()) / dy;
                    xs.push_back(p1.x() + t * (p2.x() - p1.x()));
                }
            }
        }
    }

    std::sort(xs.begin(), xs.end());
    // Remove duplicates closer than 0.1 mm
    QVector<double> uniqueXs;
    for (double x : xs) {
        if (uniqueXs.isEmpty() || std::abs(x - uniqueXs.back()) > 0.1) {
            uniqueXs.push_back(x);
        }
    }
    return uniqueXs;
}

} // namespace stick
