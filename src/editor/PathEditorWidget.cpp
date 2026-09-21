// ---------------------------------------------------------------------------
//  StickCore  –  PathEditorWidget.cpp
// ---------------------------------------------------------------------------
#include "editor/PathEditorWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QTransform>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolButton>
#include <QSlider>
#include <QLabel>
#include <QFrame>
#include <cmath>
#include <algorithm>

namespace stick {

static constexpr double kHitPx = 9.0;

// ===========================================================================
//  Canvas2DWidget  –  The actual rendering surface for 2D Stitches & Paths
// ===========================================================================
class Canvas2DWidget : public QWidget {
    Q_OBJECT
public:
    explicit Canvas2DWidget(QWidget* parent = nullptr) : QWidget(parent)
    {
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        setAutoFillBackground(false);
    }

    // --- Mode & paths ---
    void setMode(PathEditorWidget::Mode m) { m_bezierMode = m; update(); }
    PathEditorWidget::Mode mode() const { return m_bezierMode; }

    void newPath()
    {
        m_paths.push_back(EditPath());
        m_active = m_paths.size() - 1;
        m_sel = Hit();
        update();
    }

    void clearAll()
    {
        m_paths.clear();
        m_active = -1;
        m_sel = Hit();
        emit geometryChanged();
        update();
    }

    void loadPaths(const QVector<EditPath>& paths)
    {
        m_paths = paths;
        m_active = m_paths.isEmpty() ? -1 : 0;
        m_sel = Hit();
        emit geometryChanged();
        update();
    }

    int pathCount() const { return m_paths.size(); }
    const EditPath& path(int i) const { return m_paths[i]; }
    QVector<EditPath> paths() const { return m_paths; }

    void translatePaths(double dxMm, double dyMm)
    {
        const QPointF d(dxMm, dyMm);
        if (m_sel.part == Part::Anchor && m_sel.path >= 0 && m_sel.path < m_paths.size()) {
            BezierNode& nd = m_paths[m_sel.path].nodes[m_sel.node];
            nd.moveTo(nd.pos + d);
        } else {
            for (auto& p : m_paths) {
                for (auto& nd : p.nodes) {
                    nd.moveTo(nd.pos + d);
                }
            }
        }
        emit geometryChanged();
        update();
    }

    void centerPaths()
    {
        if (m_paths.isEmpty()) return;
        double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
        int count = 0;
        for (const auto& p : m_paths) {
            for (const auto& nd : p.nodes) {
                minX = std::min(minX, nd.pos.x());
                maxX = std::max(maxX, nd.pos.x());
                minY = std::min(minY, nd.pos.y());
                maxY = std::max(maxY, nd.pos.y());
                ++count;
            }
        }
        if (count > 0) {
            double cx = 0.5 * (minX + maxX);
            double cy = 0.5 * (minY + maxY);
            translatePaths(-cx, -cy);
        }
    }

    // --- Stitches API ---
    void setSequence(const StitchSequence& seq)
    {
        m_sequence = seq;
        if (!m_sequence.empty() && m_canvasMode == PathEditorWidget::CanvasMode::BezierPaths && m_paths.isEmpty()) {
            m_canvasMode = PathEditorWidget::CanvasMode::Stitches2D;
            emit canvasModeChanged(m_canvasMode);
        }
        update();
    }

    const StitchSequence& sequence() const { return m_sequence; }

    void setBackgroundImage(const QImage& img, double opacity)
    {
        m_bgImage = img;
        m_bgOpacity = std::clamp(opacity, 0.0, 1.0);
        update();
    }

    void setBackgroundOpacity(double opacity)
    {
        m_bgOpacity = std::clamp(opacity, 0.0, 1.0);
        update();
    }

    void clearBackgroundImage()
    {
        m_bgImage = QImage();
        update();
    }

    void setHoop(double wMm, double hMm, const QString& name)
    {
        m_hoopW = wMm;
        m_hoopH = hMm;
        m_hoopName = name;
        update();
    }

    void setCanvasMode(PathEditorWidget::CanvasMode m)
    {
        m_canvasMode = m;
        update();
    }

    PathEditorWidget::CanvasMode canvasMode() const { return m_canvasMode; }

    void setShowNeedlePoints(bool s) { m_showNeedlePoints = s; update(); }
    void setShowJumps(bool s)        { m_showJumps = s; update(); }
    void setShowHoop(bool s)         { m_showHoop = s; update(); }

    void fitInView()
    {
        double minX = -m_hoopW * 0.5, maxX = m_hoopW * 0.5;
        double minY = -m_hoopH * 0.5, maxY = m_hoopH * 0.5;

        if (!m_sequence.empty()) {
            double sx0, sy0, sx1, sy1;
            if (m_sequence.bounds(sx0, sy0, sx1, sy1)) {
                minX = std::min(minX, sx0); maxX = std::max(maxX, sx1);
                minY = std::min(minY, sy0); maxY = std::max(maxY, sy1);
            }
        }

        const double wMm = std::max(20.0, maxX - minX);
        const double hMm = std::max(20.0, maxY - minY);
        const double cxMm = (minX + maxX) * 0.5;
        const double cyMm = (minY + maxY) * 0.5;

        const double scaleX = (width() * 0.85) / wMm;
        const double scaleY = (height() * 0.85) / hMm;
        m_scale = std::clamp(std::min(scaleX, scaleY), 0.5, 40.0);
        m_pan = QPointF(width() * 0.5 - cxMm * m_scale, height() * 0.5 + cyMm * m_scale);
        update();
    }

    void resetZoom100()
    {
        m_scale = 3.78; // approx 96 DPI 1:1 scale
        m_pan = QPointF(width() * 0.5, height() * 0.5);
        update();
    }

    void zoomBy(double factor)
    {
        const QPointF center(width() * 0.5, height() * 0.5);
        const QPointF mmCenter = toMM(center);
        m_scale = std::clamp(m_scale * factor, 0.4, 50.0);
        m_pan = center - QPointF(mmCenter.x() * m_scale, -mmCenter.y() * m_scale);
        update();
    }

signals:
    void geometryChanged();
    void designMoved(double dx, double dy);
    void canvasModeChanged(PathEditorWidget::CanvasMode mode);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    enum class Part { None, Anchor, HandleIn, HandleOut };
    struct Hit { int path = -1; int node = -1; Part part = Part::None; };

    QPointF toScreen(const QPointF& mm) const { return QPointF(m_pan.x() + mm.x() * m_scale, m_pan.y() - mm.y() * m_scale); }
    QPointF toMM(const QPointF& px) const { return QPointF((px.x() - m_pan.x()) / m_scale, (m_pan.y() - px.y()) / m_scale); }

    Hit hitTest(const QPointF& px) const;
    void drawGridAndHoop(QPainter&);
    void drawBackgroundArtwork(QPainter&);
    void drawStitches(QPainter&);
    void drawPath(QPainter&, int pathIndex, bool active);

    StitchSequence m_sequence;
    QVector<EditPath> m_paths;
    QImage m_bgImage;
    double m_bgOpacity = 0.45;

    double m_hoopW = 140.0;
    double m_hoopH = 200.0;
    QString m_hoopName = QStringLiteral("Rahmen B 140×200");

    PathEditorWidget::CanvasMode m_canvasMode = PathEditorWidget::CanvasMode::Stitches2D;
    PathEditorWidget::Mode       m_bezierMode = PathEditorWidget::Mode::SatinRails;
    int m_active = -1;

    bool m_showNeedlePoints = true;
    bool m_showJumps        = true;
    bool m_showHoop         = true;

    // View state
    double  m_scale = 2.6;
    QPointF m_pan;
    bool    m_panning = false;
    bool    m_movingDesign = false;
    QPointF m_lastPx;
    bool    m_viewInit = false;

    // Bezier drag state
    Hit  m_drag;
    bool m_dragging = false;
    Hit  m_sel;
};

// --- Hit testing for Bézier nodes ---
Canvas2DWidget::Hit Canvas2DWidget::hitTest(const QPointF& px) const
{
    if (m_active >= 0 && m_active < m_paths.size()) {
        const EditPath& ep = m_paths[m_active];
        for (int n = 0; n < ep.count(); ++n) {
            if (!ep.nodes[n].isCorner()) {
                if (QLineF(px, toScreen(ep.nodes[n].ctrlOut)).length() <= kHitPx)
                    return { m_active, n, Part::HandleOut };
                if (QLineF(px, toScreen(ep.nodes[n].ctrlIn)).length() <= kHitPx)
                    return { m_active, n, Part::HandleIn };
            }
        }
    }
    for (int p = 0; p < m_paths.size(); ++p)
        for (int n = 0; n < m_paths[p].count(); ++n)
            if (QLineF(px, toScreen(m_paths[p].nodes[n].pos)).length() <= kHitPx)
                return { p, n, Part::Anchor };
    return {};
}

// --- Painting ---
void Canvas2DWidget::paintEvent(QPaintEvent*)
{
    if (!m_viewInit) {
        m_pan = QPointF(width() * 0.5, height() * 0.5);
        m_viewInit = true;
    }

    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing, true);
    g.fillRect(rect(), QColor(20, 22, 27));

    // 1. Grid & Janome Hoop
    drawGridAndHoop(g);

    // 2. Background Image (if loaded)
    drawBackgroundArtwork(g);

    // 3. Stitches (in 2D Stickbild or Combined mode)
    if (m_canvasMode == PathEditorWidget::CanvasMode::Stitches2D ||
        m_canvasMode == PathEditorWidget::CanvasMode::Combined) {
        drawStitches(g);
    }

    // 4. Bézier Paths (in BezierPaths or Combined mode)
    if (m_canvasMode == PathEditorWidget::CanvasMode::BezierPaths ||
        m_canvasMode == PathEditorWidget::CanvasMode::Combined) {
        for (int i = 0; i < m_paths.size(); ++i)
            drawPath(g, i, i == m_active);

        // Hint text for path editing
        g.setPen(QColor(150, 155, 165));
        const QString hint = (m_bezierMode == PathEditorWidget::Mode::SatinRails)
            ? QStringLiteral("Satin: Rail A zeichnen · „Neuer Pfad“ · Rail B · „Erzeugen ▶“")
            : QStringLiteral("Tatami: Polygon zeichnen, ersten Punkt anklicken zum Schließen");
        g.drawText(12, height() - 14, hint);
    }
}

void Canvas2DWidget::drawGridAndHoop(QPainter& g)
{
    // 10 mm grid lines
    g.setPen(QPen(QColor(32, 36, 44), 1));
    const QPointF o0 = toMM(QPointF(0, 0));
    const QPointF o1 = toMM(QPointF(width(), height()));
    const int x0 = int(std::floor(std::min(o0.x(), o1.x()) / 10.0)) * 10;
    const int x1 = int(std::ceil (std::max(o0.x(), o1.x()) / 10.0)) * 10;
    const int y0 = int(std::floor(std::min(o0.y(), o1.y()) / 10.0)) * 10;
    const int y1 = int(std::ceil (std::max(o0.y(), o1.y()) / 10.0)) * 10;

    for (int x = x0; x <= x1; x += 10) {
        if (x % 50 == 0) g.setPen(QPen(QColor(42, 47, 58), 1));
        else             g.setPen(QPen(QColor(30, 33, 40), 1));
        g.drawLine(toScreen(QPointF(x, y0)), toScreen(QPointF(x, y1)));
    }
    for (int y = y0; y <= y1; y += 10) {
        if (y % 50 == 0) g.setPen(QPen(QColor(42, 47, 58), 1));
        else             g.setPen(QPen(QColor(30, 33, 40), 1));
        g.drawLine(toScreen(QPointF(x0, y)), toScreen(QPointF(x1, y)));
    }

    // Origin crosshair
    g.setPen(QPen(QColor(80, 88, 102), 1.2));
    g.drawLine(toScreen(QPointF(-8, 0)), toScreen(QPointF(8, 0)));
    g.drawLine(toScreen(QPointF(0, -8)), toScreen(QPointF(0, 8)));

    // Active Janome Hoop
    if (m_showHoop && m_hoopW > 0 && m_hoopH > 0) {
        const double hw = m_hoopW * 0.5;
        const double hh = m_hoopH * 0.5;
        const QPointF tl = toScreen(QPointF(-hw, hh));
        const QPointF br = toScreen(QPointF(hw, -hh));
        const QRectF hoopRect(tl, br);

        // Smooth rounded hoop boundary matching real embroidery frames
        const double cornerPx = std::min(18.0, 12.0 * m_scale);
        QPainterPath hp;
        hp.addRoundedRect(hoopRect, cornerPx, cornerPx);

        // Outer subtle glow / shadow
        g.setPen(QPen(QColor(45, 212, 191, 40), 3.0));
        g.drawPath(hp);

        // Main hoop line (dashed cyan)
        g.setPen(QPen(QColor(45, 212, 191, 190), 1.4, Qt::DashLine));
        g.drawPath(hp);

        // Center crosshair axis lines inside hoop
        g.setPen(QPen(QColor(45, 212, 191, 55), 1.0, Qt::DotLine));
        g.drawLine(toScreen(QPointF(-hw, 0)), toScreen(QPointF(hw, 0)));
        g.drawLine(toScreen(QPointF(0, -hh)), toScreen(QPointF(0, hh)));

        // Hoop Label
        g.setPen(QColor(45, 212, 191));
        QFont f = g.font(); f.setPointSize(10); f.setBold(true); g.setFont(f);
        g.drawText(hoopRect.topLeft() + QPointF(10, 20),
                   QStringLiteral("⭕ %1 (%2 × %3 mm)").arg(m_hoopName).arg(int(m_hoopW)).arg(int(m_hoopH)));
    }
}

void Canvas2DWidget::drawBackgroundArtwork(QPainter& g)
{
    if (m_bgImage.isNull()) return;

    g.save();
    g.setOpacity(m_bgOpacity);

    // Fit image inside hoop or central 120 mm
    double maxDimMm = std::min(m_hoopW > 0 ? m_hoopW : 120.0, m_hoopH > 0 ? m_hoopH : 120.0);
    double imgW = m_bgImage.width();
    double imgH = m_bgImage.height();
    double aspect = imgW / std::max(1.0, imgH);
    double wMm = maxDimMm, hMm = maxDimMm;
    if (aspect >= 1.0) hMm = wMm / aspect;
    else               wMm = hMm * aspect;

    const QPointF tl = toScreen(QPointF(-wMm * 0.5, hMm * 0.5));
    const QPointF br = toScreen(QPointF(wMm * 0.5, -hMm * 0.5));
    const QRectF dstRect(tl, br);

    g.drawImage(dstRect, m_bgImage);
    g.restore();
}

void Canvas2DWidget::drawStitches(QPainter& g)
{
    if (m_sequence.empty()) {
        // Helpful friendly banner when canvas is empty
        g.setPen(QColor(110, 120, 138));
        QFont f = g.font(); f.setPointSize(12); f.setBold(true); g.setFont(f);
        const QString title = QStringLiteral("🧵 2D Stickbild-Entwurf");
        const QString sub = QStringLiteral("Wähle oben Text, Monogramm, MEF-Logo oder Bild,\nbevor die 3D-Simulation rechts abgespielt wird.");
        QFontMetrics fm(f);
        int ty = height() * 0.5 - 20;
        g.drawText((width() - fm.horizontalAdvance(title)) / 2, ty, title);
        f.setPointSize(10); f.setBold(false); g.setFont(f);
        QRect r(0, ty + 12, width(), 60);
        g.drawText(r, Qt::AlignHCenter | Qt::AlignTop, sub);
        return;
    }

    const auto& stitches = m_sequence.stitches;
    const auto& palette  = m_sequence.palette;
    if (stitches.empty()) return;

    // Draw connecting stitch lines
    const double threadWidthPx = std::clamp(0.35 * m_scale, 1.0, 6.0);
    QPointF prevScreen = toScreen(QPointF(stitches[0].x, stitches[0].y));

    for (std::size_t i = 0; i < stitches.size(); ++i) {
        const auto& s = stitches[i];
        const QPointF curScreen = toScreen(QPointF(s.x, s.y));

        if (s.flags & SF_Jump) {
            if (m_showJumps) {
                g.setPen(QPen(QColor(96, 165, 250, 130), 1.0, Qt::DashLine));
                g.drawLine(prevScreen, curScreen);
            }
        } else {
            QColor col(220, 220, 225);
            if (s.colorIdx >= 0 && s.colorIdx < int(palette.size())) {
                col = palette[s.colorIdx].color;
            }
            g.setPen(QPen(col, threadWidthPx, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            g.drawLine(prevScreen, curScreen);
        }
        prevScreen = curScreen;
    }

    // Needle penetration points
    if (m_showNeedlePoints) {
        const double dotRadius = (m_scale > 4.0) ? 2.2 : 1.4;
        g.setPen(QPen(QColor(15, 23, 42, 200), 0.8));
        g.setBrush(QColor(248, 250, 252, 220));

        for (const auto& s : stitches) {
            if (s.flags & (SF_Jump | SF_End | SF_Stop | SF_ColorChange)) continue;
            const QPointF pt = toScreen(QPointF(s.x, s.y));
            g.drawEllipse(pt, dotRadius, dotRadius);
        }
    }

    // Start & End markers
    if (!stitches.empty()) {
        const QPointF startPt = toScreen(QPointF(stitches.front().x, stitches.front().y));
        const QPointF endPt   = toScreen(QPointF(stitches.back().x,  stitches.back().y));

        // Start (Green circle 'S')
        g.setPen(QPen(Qt::white, 1.2));
        g.setBrush(QColor(34, 197, 94));
        g.drawEllipse(startPt, 5.0, 5.0);

        // End (Red circle 'E')
        g.setPen(QPen(Qt::white, 1.2));
        g.setBrush(QColor(239, 68, 68));
        g.drawEllipse(endPt, 5.0, 5.0);
    }

    // Bounding box & stats badge (bottom left)
    double minX, minY, maxX, maxY;
    if (m_sequence.bounds(minX, minY, maxX, maxY)) {
        const double wMm = maxX - minX;
        const double hMm = maxY - minY;
        const QString info = QStringLiteral("%1 × %2 mm  ·  %3 Stiche  ·  %4 Farben")
            .arg(wMm, 0, 'f', 1).arg(hMm, 0, 'f', 1)
            .arg(m_sequence.realStitchCount())
            .arg(palette.size());

        QFont bf = g.font(); bf.setPointSize(9); bf.setBold(true); g.setFont(bf);
        QFontMetrics bfm(bf);
        const int badgeW = bfm.horizontalAdvance(info) + 20;
        const QRectF badgeRect(12, height() - 36, badgeW, 24);

        g.setPen(QPen(QColor(44, 49, 61), 1.0));
        g.setBrush(QColor(27, 30, 38, 225));
        g.drawRoundedRect(badgeRect, 6, 6);

        g.setPen(QColor(45, 212, 191));
        g.drawText(badgeRect, Qt::AlignCenter, info);
    }
}

void Canvas2DWidget::drawPath(QPainter& g, int pathIndex, bool active)
{
    const EditPath& ep = m_paths[pathIndex];
    if (ep.empty()) return;

    QTransform T;
    T.translate(m_pan.x(), m_pan.y());
    T.scale(m_scale, -m_scale);

    QColor line = (m_bezierMode == PathEditorWidget::Mode::TatamiPolygon) ? QColor(90, 150, 240)
                                                                          : QColor(230, 80, 80);
    if (!active) line = line.darker(150);
    g.setPen(QPen(line, active ? 2.2 : 1.6));
    g.setBrush(Qt::NoBrush);
    g.drawPath(T.map(ep.toPainterPath()));

    // Tangent handles for active path
    if (active) {
        g.setPen(QPen(QColor(120, 160, 210), 1));
        for (const BezierNode& n : ep.nodes) {
            if (n.isCorner()) continue;
            g.drawLine(toScreen(n.pos), toScreen(n.ctrlOut));
            g.drawLine(toScreen(n.pos), toScreen(n.ctrlIn));
            g.setBrush(QColor(120, 160, 210));
            g.drawEllipse(toScreen(n.ctrlOut), 3.2, 3.2);
            g.drawEllipse(toScreen(n.ctrlIn),  3.2, 3.2);
        }
    }

    // Anchor nodes
    for (int n = 0; n < ep.count(); ++n) {
        const QPointF s = toScreen(ep.nodes[n].pos);
        const bool sel = (m_sel.part == Part::Anchor &&
                          m_sel.path == pathIndex && m_sel.node == n);
        g.setPen(QPen(Qt::white, 1));
        g.setBrush(sel ? QColor(255, 210, 90) : QColor(230, 230, 235));
        g.drawRect(QRectF(s.x() - 3.5, s.y() - 3.5, 7, 7));
    }

    // Rail label
    if (m_bezierMode == PathEditorWidget::Mode::SatinRails) {
        const QString lbl = (pathIndex == 0) ? QStringLiteral("Rail A")
                          : (pathIndex == 1) ? QStringLiteral("Rail B")
                                             : QString::number(pathIndex);
        g.setPen(line);
        g.drawText(toScreen(ep.nodes.first().pos) + QPointF(6, -6), lbl);
    }
}

// --- Mouse Events ---
void Canvas2DWidget::mousePressEvent(QMouseEvent* e)
{
    const QPointF px = e->position();
    m_lastPx = px;

    if (e->button() == Qt::MiddleButton) {
        m_panning = true;
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    if (e->button() == Qt::LeftButton) {
        // Shift + Left Click moves design inside hoop
        if (e->modifiers() & Qt::ShiftModifier) {
            m_movingDesign = true;
            setCursor(Qt::SizeAllCursor);
            return;
        }

        // In 2D Stitches mode, left drag pans
        if (m_canvasMode == PathEditorWidget::CanvasMode::Stitches2D) {
            m_panning = true;
            setCursor(Qt::ClosedHandCursor);
            return;
        }

        // In Bézier mode, Illustrator-style pen interaction
        const Hit h = hitTest(px);
        if (h.part != Part::None) {
            m_drag = h;
            m_sel  = h;
            m_active = h.path;
            m_dragging = true;
            update();
            return;
        }

        if (m_active < 0 || m_active >= m_paths.size()) newPath();
        EditPath& ep = m_paths[m_active];

        if (ep.count() >= 3 && !ep.closed) {
            if (QLineF(px, toScreen(ep.nodes.first().pos)).length() <= kHitPx) {
                ep.closed = true;
                emit geometryChanged();
                update();
                return;
            }
        }

        const QPointF mm = toMM(px);
        ep.nodes.push_back(BezierNode(mm));
        m_drag = { m_active, ep.count() - 1, Part::HandleOut };
        m_sel  = { m_active, ep.count() - 1, Part::Anchor };
        m_dragging = true;
        emit geometryChanged();
        update();
    }
}

void Canvas2DWidget::mouseMoveEvent(QMouseEvent* e)
{
    const QPointF px = e->position();
    const QPointF delta = px - m_lastPx;
    m_lastPx = px;

    if (m_panning) {
        m_pan += delta;
        update();
        return;
    }

    if (m_movingDesign) {
        const double dx = delta.x() / m_scale;
        const double dy = -delta.y() / m_scale;
        for (Stitch& s : m_sequence.stitches) {
            s.x += dx;
            s.y += dy;
        }
        emit designMoved(dx, dy);
        update();
        return;
    }

    if (m_dragging && m_drag.path >= 0 && m_drag.path < m_paths.size()) {
        EditPath& ep = m_paths[m_drag.path];
        if (m_drag.node < 0 || m_drag.node >= ep.count()) return;
        BezierNode& nd = ep.nodes[m_drag.node];
        const QPointF mm = toMM(px);

        if (m_drag.part == Part::Anchor) {
            nd.moveTo(mm);
        } else if (m_drag.part == Part::HandleOut) {
            nd.ctrlOut = mm;
            if (nd.smooth) nd.ctrlIn = nd.pos - (nd.ctrlOut - nd.pos);
        } else if (m_drag.part == Part::HandleIn) {
            nd.ctrlIn = mm;
            if (nd.smooth) nd.ctrlOut = nd.pos - (nd.ctrlIn - nd.pos);
        }
        emit geometryChanged();
        update();
        return;
    }

    // Cursor feedback
    if (m_canvasMode == PathEditorWidget::CanvasMode::Stitches2D) {
        setCursor(Qt::ArrowCursor);
    } else {
        const Hit h = hitTest(px);
        if (h.part == Part::Anchor) setCursor(Qt::SizeAllCursor);
        else if (h.part == Part::HandleIn || h.part == Part::HandleOut) setCursor(Qt::CrossCursor);
        else setCursor(Qt::ArrowCursor);
    }
}

void Canvas2DWidget::mouseReleaseEvent(QMouseEvent*)
{
    m_panning = false;
    m_movingDesign = false;
    m_dragging = false;
    m_drag = Hit();
    setCursor(Qt::ArrowCursor);
}

void Canvas2DWidget::wheelEvent(QWheelEvent* e)
{
    const QPointF mmBefore = toMM(e->position());
    const double factor = (e->angleDelta().y() > 0) ? 1.15 : (1.0 / 1.15);
    m_scale = std::clamp(m_scale * factor, 0.4, 50.0);
    m_pan = e->position() - QPointF(mmBefore.x() * m_scale, -mmBefore.y() * m_scale);
    update();
}

void Canvas2DWidget::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
        if (m_sel.path >= 0 && m_sel.path < m_paths.size()) {
            EditPath& ep = m_paths[m_sel.path];
            if (m_sel.node >= 0 && m_sel.node < ep.count()) {
                ep.nodes.remove(m_sel.node);
                if (ep.count() < 3) ep.closed = false;
                if (ep.empty()) {
                    m_paths.remove(m_sel.path);
                    m_active = m_paths.isEmpty() ? -1 : 0;
                }
                m_sel = Hit();
                emit geometryChanged();
                update();
            }
        }
        return;
    }

    if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Right ||
        e->key() == Qt::Key_Up   || e->key() == Qt::Key_Down) {
        double step = 1.0;
        if (e->modifiers() & Qt::ShiftModifier) step = 5.0;
        else if (e->modifiers() & Qt::AltModifier) step = 0.1;
        double dx = 0.0, dy = 0.0;
        if (e->key() == Qt::Key_Left)  dx = -step;
        if (e->key() == Qt::Key_Right) dx = +step;
        if (e->key() == Qt::Key_Up)    dy = +step;
        if (e->key() == Qt::Key_Down)  dy = -step;
        translatePaths(dx, dy);
        e->accept();
        return;
    }
}

void Canvas2DWidget::resizeEvent(QResizeEvent*)
{
    if (!m_viewInit && width() > 10 && height() > 10) {
        m_pan = QPointF(width() * 0.5, height() * 0.5);
        m_viewInit = true;
    }
}

// ===========================================================================
//  PathEditorWidget Implementation (Container with Toolbar & Canvas)
// ===========================================================================
PathEditorWidget::PathEditorWidget(QWidget* parent) : QWidget(parent)
{
    setupUi();
}

void PathEditorWidget::setupUi()
{
    auto* mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(0, 0, 0, 0);
    mainLay->setSpacing(0);

    // Top control bar
    auto* tb = new QWidget(this);
    tb->setStyleSheet(QStringLiteral(
        "background:#181b22; border-bottom:1px solid #2c313d; padding:4px 8px;"));
    auto* tbLay = new QHBoxLayout(tb);
    tbLay->setContentsMargins(6, 4, 6, 4);
    tbLay->setSpacing(6);

    // Mode Toggle Buttons
    m_btnStitches = new QToolButton(tb);
    m_btnStitches->setText(QStringLiteral("🧵 2D Stickbild"));
    m_btnStitches->setCheckable(true);
    m_btnStitches->setChecked(true);
    m_btnStitches->setToolTip(QStringLiteral("2D-Stickbild anzeigen (Fadenstiche, Einstichpunkte, Janome-Rahmen)"));

    m_btnPaths = new QToolButton(tb);
    m_btnPaths->setText(QStringLiteral("✏ Bézier Pfade"));
    m_btnPaths->setCheckable(true);
    m_btnPaths->setChecked(false);
    m_btnPaths->setToolTip(QStringLiteral("Bézier-Vektorpuffer bearbeiten (Satin-Rails & Tatami-Polygone zeichnen)"));

    connect(m_btnStitches, &QToolButton::clicked, this, [this]{
        m_btnStitches->setChecked(true);
        m_btnPaths->setChecked(false);
        m_canvas->setCanvasMode(CanvasMode::Stitches2D);
    });
    connect(m_btnPaths, &QToolButton::clicked, this, [this]{
        m_btnPaths->setChecked(true);
        m_btnStitches->setChecked(false);
        m_canvas->setCanvasMode(CanvasMode::BezierPaths);
    });

    tbLay->addWidget(m_btnStitches);
    tbLay->addWidget(m_btnPaths);

    // Separator line
    auto* sep1 = new QFrame(tb);
    sep1->setFrameShape(QFrame::VLine);
    sep1->setStyleSheet(QStringLiteral("color:#2c313d;"));
    tbLay->addWidget(sep1);

    // Toggles: Needle points, Jumps, Hoop
    m_btnPoints = new QToolButton(tb);
    m_btnPoints->setText(QStringLiteral("• Punkte"));
    m_btnPoints->setCheckable(true);
    m_btnPoints->setChecked(true);
    m_btnPoints->setToolTip(QStringLiteral("Nadel-Einstichpunkte ein-/ausblenden"));
    connect(m_btnPoints, &QToolButton::toggled, this, [this](bool c){ m_canvas->setShowNeedlePoints(c); });

    m_btnJumps = new QToolButton(tb);
    m_btnJumps->setText(QStringLiteral("⤹ Sprünge"));
    m_btnJumps->setCheckable(true);
    m_btnJumps->setChecked(true);
    m_btnJumps->setToolTip(QStringLiteral("Sprungstiche (gestrichelte Linien) ein-/ausblenden"));
    connect(m_btnJumps, &QToolButton::toggled, this, [this](bool c){ m_canvas->setShowJumps(c); });

    m_btnHoop = new QToolButton(tb);
    m_btnHoop->setText(QStringLiteral("⭕ Rahmen"));
    m_btnHoop->setCheckable(true);
    m_btnHoop->setChecked(true);
    m_btnHoop->setToolTip(QStringLiteral("Janome-Stickrahmen in 2D ein-/ausblenden"));
    connect(m_btnHoop, &QToolButton::toggled, this, [this](bool c){ m_canvas->setShowHoop(c); });

    tbLay->addWidget(m_btnPoints);
    tbLay->addWidget(m_btnJumps);
    tbLay->addWidget(m_btnHoop);

    // Separator line
    auto* sep2 = new QFrame(tb);
    sep2->setFrameShape(QFrame::VLine);
    sep2->setStyleSheet(QStringLiteral("color:#2c313d;"));
    tbLay->addWidget(sep2);

    // Zoom buttons
    m_btnFit = new QToolButton(tb);
    m_btnFit->setText(QStringLiteral("⌖ Einpassen"));
    m_btnFit->setToolTip(QStringLiteral("Motiv & Rahmen optimal im Fenster einpassen"));
    connect(m_btnFit, &QToolButton::clicked, this, &PathEditorWidget::fitInView);

    m_btnZoom100 = new QToolButton(tb);
    m_btnZoom100->setText(QStringLiteral("1:1"));
    m_btnZoom100->setToolTip(QStringLiteral("100% Originalgröße"));
    connect(m_btnZoom100, &QToolButton::clicked, this, &PathEditorWidget::resetZoom100);

    tbLay->addWidget(m_btnFit);
    tbLay->addWidget(m_btnZoom100);

    auto* spacer = new QWidget(tb);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tbLay->addWidget(spacer);

    // Vorlage Dimmer (opacity slider)
    m_dimLabel = new QLabel(QStringLiteral("🖼 Vorlage:"), tb);
    m_dimLabel->setStyleSheet(QStringLiteral("color:#8b93a4; font-size:11px;"));
    m_dimSlider = new QSlider(Qt::Horizontal, tb);
    m_dimSlider->setRange(0, 100);
    m_dimSlider->setValue(45);
    m_dimSlider->setFixedWidth(80);
    m_dimSlider->setToolTip(QStringLiteral("Helligkeit/Transparenz der Bildvorlage"));
    connect(m_dimSlider, &QSlider::valueChanged, this, [this](int val){
        m_canvas->setBackgroundOpacity(val / 100.0);
    });

    tbLay->addWidget(m_dimLabel);
    tbLay->addWidget(m_dimSlider);

    mainLay->addWidget(tb);

    // Canvas below toolbar
    m_canvas = new Canvas2DWidget(this);
    mainLay->addWidget(m_canvas, 1);

    // Connect canvas signals
    connect(m_canvas, &Canvas2DWidget::geometryChanged, this, &PathEditorWidget::geometryChanged);
    connect(m_canvas, &Canvas2DWidget::designMoved, this, &PathEditorWidget::designMoved);
    connect(m_canvas, &Canvas2DWidget::canvasModeChanged, this, [this](CanvasMode m){
        if (m == CanvasMode::Stitches2D) {
            m_btnStitches->setChecked(true);
            m_btnPaths->setChecked(false);
        } else if (m == CanvasMode::BezierPaths) {
            m_btnPaths->setChecked(true);
            m_btnStitches->setChecked(false);
        }
        emit canvasModeChanged(m);
    });
}

void PathEditorWidget::setMode(Mode m) { m_canvas->setMode(m); }
PathEditorWidget::Mode PathEditorWidget::mode() const { return m_canvas->mode(); }
void PathEditorWidget::newPath()
{
    m_btnPaths->setChecked(true);
    m_btnStitches->setChecked(false);
    m_canvas->setCanvasMode(CanvasMode::BezierPaths);
    m_canvas->newPath();
}
void PathEditorWidget::clearAll() { m_canvas->clearAll(); }
void PathEditorWidget::loadPaths(const QVector<EditPath>& paths) { m_canvas->loadPaths(paths); }
int  PathEditorWidget::pathCount() const { return m_canvas->pathCount(); }
const EditPath& PathEditorWidget::path(int i) const { return m_canvas->path(i); }
QVector<EditPath> PathEditorWidget::paths() const { return m_canvas->paths(); }
void PathEditorWidget::translatePaths(double dx, double dy) { m_canvas->translatePaths(dx, dy); }
void PathEditorWidget::centerPaths() { m_canvas->centerPaths(); }

void PathEditorWidget::setSequence(const StitchSequence& seq)
{
    m_canvas->setSequence(seq);
}

const StitchSequence& PathEditorWidget::sequence() const
{
    return m_canvas->sequence();
}

void PathEditorWidget::setBackgroundImage(const QImage& img, double opacity)
{
    m_canvas->setBackgroundImage(img, opacity);
    m_dimSlider->setValue(int(opacity * 100));
}

void PathEditorWidget::setBackgroundOpacity(double opacity)
{
    m_canvas->setBackgroundOpacity(opacity);
    m_dimSlider->setValue(int(opacity * 100));
}

void PathEditorWidget::clearBackgroundImage()
{
    m_canvas->clearBackgroundImage();
}

void PathEditorWidget::setHoop(double wMm, double hMm, const QString& name)
{
    m_canvas->setHoop(wMm, hMm, name);
}

void PathEditorWidget::setCanvasMode(CanvasMode mode)
{
    if (mode == CanvasMode::Stitches2D) {
        m_btnStitches->setChecked(true);
        m_btnPaths->setChecked(false);
    } else if (mode == CanvasMode::BezierPaths) {
        m_btnPaths->setChecked(true);
        m_btnStitches->setChecked(false);
    }
    m_canvas->setCanvasMode(mode);
}

PathEditorWidget::CanvasMode PathEditorWidget::canvasMode() const
{
    return m_canvas->canvasMode();
}

void PathEditorWidget::setShowNeedlePoints(bool s)
{
    m_btnPoints->setChecked(s);
    m_canvas->setShowNeedlePoints(s);
}

void PathEditorWidget::setShowJumps(bool s)
{
    m_btnJumps->setChecked(s);
    m_canvas->setShowJumps(s);
}

void PathEditorWidget::setShowHoop(bool s)
{
    m_btnHoop->setChecked(s);
    m_canvas->setShowHoop(s);
}

void PathEditorWidget::fitInView()
{
    m_canvas->fitInView();
}

void PathEditorWidget::resetZoom100()
{
    m_canvas->resetZoom100();
}

} // namespace stick

#include "PathEditorWidget.moc"
