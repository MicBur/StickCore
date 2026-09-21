// ---------------------------------------------------------------------------
//  StickCore  –  SvgPathParser.h
//
//  Ultra-fast, robust SVG path parser that converts SVG 'd' path strings and
//  SVG vector files into Qt QPainterPath structures with sub-pixel precision.
// ---------------------------------------------------------------------------
#pragma once

#include <QPainterPath>
#include <QString>
#include <QFile>
#include <QXmlStreamReader>
#include <QPointF>
#include <cmath>
#include <algorithm>

namespace stick {

class SvgPathParser {
public:
    /// Parses an SVG path 'd' string into a QPainterPath with WindingFill.
    static QPainterPath parsePathData(const QString& d)
    {
        QPainterPath path;
        path.setFillRule(Qt::WindingFill);

        QPointF cur(0, 0);
        QPointF start(0, 0);
        QPointF lastCubicCtrl(0, 0);
        QPointF lastQuadCtrl(0, 0);

        int i = 0;
        const int n = d.length();

        auto skipSep = [&]() {
            while (i < n && (d[i].isSpace() || d[i] == ',')) ++i;
        };

        auto nextDouble = [&]() -> double {
            skipSep();
            if (i >= n) return 0.0;
            const int s = i;
            if (d[i] == '+' || d[i] == '-') ++i;
            bool hasDigits = false;
            while (i < n && d[i].isDigit()) { ++i; hasDigits = true; }
            if (i < n && d[i] == '.') {
                ++i;
                while (i < n && d[i].isDigit()) { ++i; hasDigits = true; }
            }
            if (hasDigits && i < n && (d[i] == 'e' || d[i] == 'E')) {
                ++i;
                if (i < n && (d[i] == '+' || d[i] == '-')) ++i;
                while (i < n && d[i].isDigit()) ++i;
            }
            return d.mid(s, i - s).toDouble();
        };

        auto hasNumber = [&]() -> bool {
            skipSep();
            if (i >= n) return false;
            const QChar c = d[i];
            return c.isDigit() || c == '-' || c == '+' || c == '.';
        };

        QChar lastCmd = ' ';

        while (i < n) {
            skipSep();
            if (i >= n) break;

            QChar c = d[i];
            QChar cmd;
            if (c.isLetter() && c != 'e' && c != 'E') {
                cmd = c;
                ++i;
            } else if (lastCmd != ' ') {
                // If no command letter, repeated coordinates repeat the previous command
                if (lastCmd == 'M') cmd = 'L';
                else if (lastCmd == 'm') cmd = 'l';
                else cmd = lastCmd;
            } else {
                break;
            }

            switch (cmd.toLatin1()) {
            case 'M': {
                const double x = nextDouble();
                const double y = nextDouble();
                cur = QPointF(x, y);
                start = cur;
                path.moveTo(cur);
                lastCubicCtrl = cur;
                lastQuadCtrl = cur;
                lastCmd = 'L'; // Subsequent pairs are implicit LineTo
                break;
            }
            case 'm': {
                const double dx = nextDouble();
                const double dy = nextDouble();
                cur += QPointF(dx, dy);
                start = cur;
                path.moveTo(cur);
                lastCubicCtrl = cur;
                lastQuadCtrl = cur;
                lastCmd = 'l'; // Subsequent pairs are implicit lineTo
                break;
            }
            case 'L': {
                while (hasNumber()) {
                    const double x = nextDouble();
                    const double y = nextDouble();
                    cur = QPointF(x, y);
                    path.lineTo(cur);
                }
                lastCubicCtrl = cur;
                lastQuadCtrl = cur;
                lastCmd = 'L';
                break;
            }
            case 'l': {
                while (hasNumber()) {
                    const double dx = nextDouble();
                    const double dy = nextDouble();
                    cur += QPointF(dx, dy);
                    path.lineTo(cur);
                }
                lastCubicCtrl = cur;
                lastQuadCtrl = cur;
                lastCmd = 'l';
                break;
            }
            case 'H': {
                while (hasNumber()) {
                    cur.setX(nextDouble());
                    path.lineTo(cur);
                }
                lastCubicCtrl = cur;
                lastQuadCtrl = cur;
                lastCmd = 'H';
                break;
            }
            case 'h': {
                while (hasNumber()) {
                    cur.setX(cur.x() + nextDouble());
                    path.lineTo(cur);
                }
                lastCubicCtrl = cur;
                lastQuadCtrl = cur;
                lastCmd = 'h';
                break;
            }
            case 'V': {
                while (hasNumber()) {
                    cur.setY(nextDouble());
                    path.lineTo(cur);
                }
                lastCubicCtrl = cur;
                lastQuadCtrl = cur;
                lastCmd = 'V';
                break;
            }
            case 'v': {
                while (hasNumber()) {
                    cur.setY(cur.y() + nextDouble());
                    path.lineTo(cur);
                }
                lastCubicCtrl = cur;
                lastQuadCtrl = cur;
                lastCmd = 'v';
                break;
            }
            case 'C': {
                while (hasNumber()) {
                    const double x1 = nextDouble();
                    const double y1 = nextDouble();
                    const double x2 = nextDouble();
                    const double y2 = nextDouble();
                    const double x  = nextDouble();
                    const double y  = nextDouble();
                    path.cubicTo(x1, y1, x2, y2, x, y);
                    lastCubicCtrl = QPointF(x2, y2);
                    cur = QPointF(x, y);
                }
                lastQuadCtrl = cur;
                lastCmd = 'C';
                break;
            }
            case 'c': {
                while (hasNumber()) {
                    const double dx1 = nextDouble();
                    const double dy1 = nextDouble();
                    const double dx2 = nextDouble();
                    const double dy2 = nextDouble();
                    const double dx  = nextDouble();
                    const double dy  = nextDouble();
                    const QPointF p1 = cur + QPointF(dx1, dy1);
                    const QPointF p2 = cur + QPointF(dx2, dy2);
                    const QPointF p  = cur + QPointF(dx, dy);
                    path.cubicTo(p1, p2, p);
                    lastCubicCtrl = p2;
                    cur = p;
                }
                lastQuadCtrl = cur;
                lastCmd = 'c';
                break;
            }
            case 'S': {
                while (hasNumber()) {
                    QPointF c1 = (lastCmd == 'C' || lastCmd == 'c' || lastCmd == 'S' || lastCmd == 's')
                        ? (2.0 * cur - lastCubicCtrl) : cur;
                    const double x2 = nextDouble();
                    const double y2 = nextDouble();
                    const double x  = nextDouble();
                    const double y  = nextDouble();
                    path.cubicTo(c1.x(), c1.y(), x2, y2, x, y);
                    lastCubicCtrl = QPointF(x2, y2);
                    cur = QPointF(x, y);
                }
                lastQuadCtrl = cur;
                lastCmd = 'S';
                break;
            }
            case 's': {
                while (hasNumber()) {
                    QPointF c1 = (lastCmd == 'C' || lastCmd == 'c' || lastCmd == 'S' || lastCmd == 's')
                        ? (2.0 * cur - lastCubicCtrl) : cur;
                    const double dx2 = nextDouble();
                    const double dy2 = nextDouble();
                    const double dx  = nextDouble();
                    const double dy  = nextDouble();
                    const QPointF p2 = cur + QPointF(dx2, dy2);
                    const QPointF p  = cur + QPointF(dx, dy);
                    path.cubicTo(c1, p2, p);
                    lastCubicCtrl = p2;
                    cur = p;
                }
                lastQuadCtrl = cur;
                lastCmd = 's';
                break;
            }
            case 'Q': {
                while (hasNumber()) {
                    const double x1 = nextDouble();
                    const double y1 = nextDouble();
                    const double x  = nextDouble();
                    const double y  = nextDouble();
                    path.quadTo(x1, y1, x, y);
                    lastQuadCtrl = QPointF(x1, y1);
                    cur = QPointF(x, y);
                }
                lastCubicCtrl = cur;
                lastCmd = 'Q';
                break;
            }
            case 'q': {
                while (hasNumber()) {
                    const double dx1 = nextDouble();
                    const double dy1 = nextDouble();
                    const double dx  = nextDouble();
                    const double dy  = nextDouble();
                    const QPointF p1 = cur + QPointF(dx1, dy1);
                    const QPointF p  = cur + QPointF(dx, dy);
                    path.quadTo(p1, p);
                    lastQuadCtrl = p1;
                    cur = p;
                }
                lastCubicCtrl = cur;
                lastCmd = 'q';
                break;
            }
            case 'Z':
            case 'z': {
                path.closeSubpath();
                cur = start;
                lastCubicCtrl = cur;
                lastQuadCtrl = cur;
                lastCmd = 'Z';
                break;
            }
            default:
                break;
            }
        }

        return path;
    }

    /// Reads an SVG file (from disk or Qt resource :/...) and extracts all <path d="..."> elements.
    static QPainterPath parseSvgFile(const QString& filePath, const QString& targetPathId = QString())
    {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return QPainterPath();
        }

        QXmlStreamReader xml(&file);
        QPainterPath combinedPath;
        combinedPath.setFillRule(Qt::WindingFill);

        while (!xml.atEnd() && !xml.hasError()) {
            QXmlStreamReader::TokenType token = xml.readNext();
            if (token == QXmlStreamReader::StartElement) {
                if (xml.name().toString() == QStringLiteral("path")) {
                    const QXmlStreamAttributes attrs = xml.attributes();
                    const QString id = attrs.value(QStringLiteral("id")).toString();
                    if (targetPathId.isEmpty() || id == targetPathId) {
                        const QString d = attrs.value(QStringLiteral("d")).toString();
                        if (!d.isEmpty()) {
                            combinedPath.addPath(parsePathData(d));
                        }
                    }
                }
            }
        }

        return combinedPath;
    }
};

} // namespace stick
