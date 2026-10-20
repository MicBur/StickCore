// ---------------------------------------------------------------------------
//  StickCore  –  PathEditorWidget.cpp
// ---------------------------------------------------------------------------
#include "editor/PathEditorWidget.h"
#include "core/MachineProfile.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QTransform>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolButton>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QFrame>
#include <QSet>
#include <QActionGroup>
#include <QButtonGroup>
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
        pushUndo();
        m_paths.push_back(EditPath());
        m_active = m_paths.size() - 1;
        m_selectedPaths.clear();
        m_selectedPaths.insert(m_active);
        m_sel = Hit();
        emit selectionChanged(m_selectedPaths.size());
        update();
    }

    void clearAll()
    {
        pushUndo();
        m_paths.clear();
        m_active = -1;
        m_selectedPaths.clear();
        m_sel = Hit();
        emit geometryChanged();
        emit selectionChanged(0);
        update();
    }

    void loadPaths(const QVector<EditPath>& paths)
    {
        pushUndo();
        m_paths = paths;
        m_active = m_paths.isEmpty() ? -1 : 0;
        m_selectedPaths.clear();
        m_sel = Hit();
        emit geometryChanged();
        emit selectionChanged(0);
        update();
    }

    int pathCount() const { return m_paths.size(); }
    const EditPath& path(int i) const { return m_paths[i]; }
    QVector<EditPath> paths() const { return m_paths; }

    // --- Vector Tool & Selection API ---
    void setVectorTool(VectorTool tool)
    {
        m_tool = tool;
        m_drawingMarquee = false;
        m_drawingLasso = false;
        m_bending = false;
        m_scalingSelection = false;
        m_movingSelection = false;
        emit vectorToolChanged(tool);
        update();
    }
    VectorTool vectorTool() const { return m_tool; }

    void selectAllPaths()
    {
        m_selectedPaths.clear();
        for (int i = 0; i < m_paths.size(); ++i) m_selectedPaths.insert(i);
        if (!m_paths.isEmpty()) m_active = 0;
        emit selectionChanged(m_selectedPaths.size());
        update();
    }

    void clearSelection()
    {
        m_selectedPaths.clear();
        m_sel = Hit();
        emit selectionChanged(0);
        update();
    }

    void selectPath(int index, bool additive = false)
    {
        if (index < 0 || index >= m_paths.size()) return;
        if (!additive) m_selectedPaths.clear();
        if (additive && m_selectedPaths.contains(index)) {
            m_selectedPaths.remove(index);
        } else {
            m_selectedPaths.insert(index);
            m_active = index;
        }
        emit selectionChanged(m_selectedPaths.size());
        update();
    }

    void selectPathsInRect(const QRectF& rectMm, bool additive = false)
    {
        if (!additive) m_selectedPaths.clear();
        for (int i = 0; i < m_paths.size(); ++i) {
            const EditPath& p = m_paths[i];
            bool hit = rectMm.intersects(p.boundingRect());
            if (!hit) {
                for (const auto& nd : p.nodes) {
                    if (rectMm.contains(nd.pos)) { hit = true; break; }
                }
            }
            if (hit) m_selectedPaths.insert(i);
        }
        if (!m_selectedPaths.isEmpty()) m_active = *m_selectedPaths.begin();
        emit selectionChanged(m_selectedPaths.size());
        update();
    }

    void selectPathsInPolygon(const QPolygonF& lassoMm, bool additive = false)
    {
        if (!additive) m_selectedPaths.clear();
        for (int i = 0; i < m_paths.size(); ++i) {
            const EditPath& p = m_paths[i];
            bool hit = lassoMm.containsPoint(p.center(), Qt::OddEvenFill);
            if (!hit) {
                for (const auto& nd : p.nodes) {
                    if (lassoMm.containsPoint(nd.pos, Qt::OddEvenFill)) { hit = true; break; }
                }
            }
            if (hit) m_selectedPaths.insert(i);
        }
        if (!m_selectedPaths.isEmpty()) m_active = *m_selectedPaths.begin();
        emit selectionChanged(m_selectedPaths.size());
        update();
    }

    int selectedPathCount() const { return m_selectedPaths.size(); }
    QSet<int> selectedPaths() const { return m_selectedPaths; }

    QRectF selectedBoundsMm() const
    {
        if (m_paths.isEmpty()) return QRectF();
        auto indices = m_selectedPaths.isEmpty() ? allIndices() : m_selectedPaths;
        double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
        int count = 0;
        for (int idx : indices) {
            if (idx >= 0 && idx < m_paths.size()) {
                const QRectF b = m_paths[idx].boundingRect();
                if (b.isValid()) {
                    minX = std::min(minX, b.left());
                    maxX = std::max(maxX, b.right());
                    minY = std::min(minY, b.top());
                    maxY = std::max(maxY, b.bottom());
                    ++count;
                }
            }
        }
        if (count == 0) return QRectF(0, 0, 0, 0);
        return QRectF(minX, minY, std::max(0.1, maxX - minX), std::max(0.1, maxY - minY));
    }

    QSet<int> allIndices() const
    {
        QSet<int> s;
        for (int i = 0; i < m_paths.size(); ++i) s.insert(i);
        return s;
    }

    void pushUndo()
    {
        m_undoStack.push_back(m_paths);
        if (m_undoStack.size() > 50) m_undoStack.removeFirst();
        m_redoStack.clear();
        emit undoStackChanged();
    }

    void undo()
    {
        if (m_undoStack.isEmpty()) return;
        m_redoStack.push_back(m_paths);
        m_paths = m_undoStack.takeLast();
        m_selectedPaths.clear();
        m_active = m_paths.isEmpty() ? -1 : 0;
        m_sel = Hit();
        emit geometryChanged();
        emit selectionChanged(0);
        emit undoStackChanged();
        update();
    }

    void redo()
    {
        if (m_redoStack.isEmpty()) return;
        m_undoStack.push_back(m_paths);
        m_paths = m_redoStack.takeLast();
        m_selectedPaths.clear();
        m_active = m_paths.isEmpty() ? -1 : 0;
        m_sel = Hit();
        emit geometryChanged();
        emit selectionChanged(0);
        emit undoStackChanged();
        update();
    }

    bool canUndo() const { return !m_undoStack.isEmpty(); }
    bool canRedo() const { return !m_redoStack.isEmpty(); }

    void deleteSelected()
    {
        if (m_paths.isEmpty()) return;
        pushUndo();

        // 1. If anchor node is selected, delete that node
        if (m_sel.part == Part::Anchor && m_sel.path >= 0 && m_sel.path < m_paths.size()) {
            EditPath& ep = m_paths[m_sel.path];
            if (m_sel.node >= 0 && m_sel.node < ep.count()) {
                ep.nodes.remove(m_sel.node);
                if (ep.count() < 3) ep.closed = false;
                if (ep.empty()) {
                    m_paths.remove(m_sel.path);
                    m_selectedPaths.clear();
                    m_active = m_paths.isEmpty() ? -1 : 0;
                }
                m_sel = Hit();
                emit geometryChanged();
                emit selectionChanged(m_selectedPaths.size());
                update();
                return;
            }
        }

        // 2. If multiple paths are selected, delete all selected paths
        if (!m_selectedPaths.isEmpty()) {
            QVector<int> sortedIndices = m_selectedPaths.values();
            std::sort(sortedIndices.begin(), sortedIndices.end(), std::greater<int>());
            for (int idx : sortedIndices) {
                if (idx >= 0 && idx < m_paths.size()) {
                    m_paths.remove(idx);
                }
            }
            m_selectedPaths.clear();
            m_active = m_paths.isEmpty() ? -1 : 0;
            m_sel = Hit();
            emit geometryChanged();
            emit selectionChanged(0);
            update();
            return;
        }

        // 3. Fallback: delete active path
        if (m_active >= 0 && m_active < m_paths.size()) {
            m_paths.remove(m_active);
            m_active = m_paths.isEmpty() ? -1 : std::min(m_active, int(m_paths.size()) - 1);
            m_sel = Hit();
            emit geometryChanged();
            emit selectionChanged(0);
            update();
        }
    }

    void filterSmallIslands(double minAreaMm2 = 4.0)
    {
        if (m_paths.isEmpty()) return;
        pushUndo();
        QVector<EditPath> kept;
        for (const auto& p : m_paths) {
            if (p.closed) {
                if (p.area() >= minAreaMm2) kept.push_back(p);
            } else {
                QRectF b = p.boundingRect();
                if (b.width() * b.height() >= minAreaMm2 || std::max(b.width(), b.height()) >= 3.0) {
                    kept.push_back(p);
                }
            }
        }
        m_paths = kept;
        m_selectedPaths.clear();
        m_active = m_paths.isEmpty() ? -1 : 0;
        m_sel = Hit();
        emit geometryChanged();
        emit selectionChanged(0);
        update();
    }

    void flipHorizontalSelected()
    {
        if (m_paths.isEmpty()) return;
        pushUndo();
        QRectF b = selectedBoundsMm();
        double cx = b.center().x();
        auto targets = m_selectedPaths.isEmpty() ? allIndices() : m_selectedPaths;
        for (int idx : targets) {
            if (idx >= 0 && idx < m_paths.size()) {
                m_paths[idx].flipHorizontal(cx);
            }
        }
        emit geometryChanged();
        update();
    }

    void flipVerticalSelected()
    {
        if (m_paths.isEmpty()) return;
        pushUndo();
        QRectF b = selectedBoundsMm();
        double cy = b.center().y();
        auto targets = m_selectedPaths.isEmpty() ? allIndices() : m_selectedPaths;
        for (int idx : targets) {
            if (idx >= 0 && idx < m_paths.size()) {
                m_paths[idx].flipVertical(cy);
            }
        }
        emit geometryChanged();
        update();
    }

    void rotateSelected(double angleDeg = 90.0)
    {
        if (m_paths.isEmpty()) return;
        pushUndo();
        QRectF b = selectedBoundsMm();
        QPointF center = b.center();
        auto targets = m_selectedPaths.isEmpty() ? allIndices() : m_selectedPaths;
        for (int idx : targets) {
            if (idx >= 0 && idx < m_paths.size()) {
                m_paths[idx].rotate(angleDeg, center);
            }
        }
        emit geometryChanged();
        update();
    }

    void scaleSelected(double factor)
    {
        if (m_paths.isEmpty() || factor <= 1e-4) return;
        pushUndo();
        QRectF b = selectedBoundsMm();
        QPointF center = b.center();
        auto targets = m_selectedPaths.isEmpty() ? allIndices() : m_selectedPaths;
        for (int idx : targets) {
            if (idx >= 0 && idx < m_paths.size()) {
                m_paths[idx].scale(factor, factor, center);
            }
        }
        emit geometryChanged();
        update();
    }

    void translatePaths(double dxMm, double dyMm)
    {
        const QPointF d(dxMm, dyMm);
        if (m_sel.part == Part::Anchor && m_sel.path >= 0 && m_sel.path < m_paths.size() &&
            m_sel.node >= 0 && m_sel.node < m_paths[m_sel.path].count()) {
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

    bool pathsBounds(double& minX, double& minY, double& maxX, double& maxY) const
    {
        if (m_paths.isEmpty()) return false;
        minX = 1e9; maxX = -1e9; minY = 1e9; maxY = -1e9;
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
        return count > 0;
    }

    void scalePaths(double factor, const QPointF& center)
    {
        if (factor <= 1e-6) return;
        for (auto& p : m_paths) {
            for (auto& nd : p.nodes) {
                nd.pos = center + (nd.pos - center) * factor;
                nd.ctrlIn = center + (nd.ctrlIn - center) * factor;
                nd.ctrlOut = center + (nd.ctrlOut - center) * factor;
            }
        }
        emit geometryChanged();
        update();
    }

    void fitPathsToHoop(double marginMm = 5.0)
    {
        double minX, minY, maxX, maxY;
        if (!pathsBounds(minX, minY, maxX, maxY)) return;
        const double w = maxX - minX;
        const double h = maxY - minY;
        if (w <= 1e-3 || h <= 1e-3) return;

        if (m_hoopType == HoopType::HoopC_50x50) {
            const double diag = std::hypot(w, h);
            const double targetDiag = std::max(10.0, 50.0 - 2.0 * marginMm);
            const double factor = targetDiag / diag;
            const QPointF center(0.5 * (minX + maxX), 0.5 * (minY + maxY));
            scalePaths(factor, center);
            centerPaths();
            return;
        }

        const double usableW = std::max(10.0, m_hoopW - 2.0 * marginMm);
        const double usableH = std::max(10.0, m_hoopH - 2.0 * marginMm);
        const double factor = std::min(usableW / w, usableH / h);
        const QPointF center(0.5 * (minX + maxX), 0.5 * (minY + maxY));
        scalePaths(factor, center);
        centerPaths();
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

    void setHoop(double wMm, double hMm, const QString& name, HoopType type = HoopType::HoopB_140x200)
    {
        m_hoopW = wMm;
        m_hoopH = hMm;
        m_hoopName = name;
        m_hoopType = type;
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
    void selectionChanged(int count);
    void undoStackChanged();
    void vectorToolChanged(VectorTool tool);

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
    bool hitSegment(const QPointF& px, int& outPath, int& outSeg) const;
    int  hitBBoxHandle(const QPointF& px) const;

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
    HoopType m_hoopType = HoopType::HoopB_140x200;

    PathEditorWidget::CanvasMode m_canvasMode = PathEditorWidget::CanvasMode::Stitches2D;
    PathEditorWidget::Mode       m_bezierMode = PathEditorWidget::Mode::SatinRails;
    int m_active = -1;

    // Vector tool & selection state
    VectorTool m_tool = VectorTool::Select;
    QSet<int>  m_selectedPaths;
    QVector<QVector<EditPath>> m_undoStack;
    QVector<QVector<EditPath>> m_redoStack;

    // Marquee state
    bool    m_drawingMarquee = false;
    QPointF m_marqueeStart;
    QPointF m_marqueeCurrent;

    // Lasso state
    bool      m_drawingLasso = false;
    QPolygonF m_lassoPolyPx;

    // Segment bending state
    bool    m_bending = false;
    int     m_bendPath = -1;
    int     m_bendSeg = -1;
    QPointF m_bendStartMm;

    // Moving selection state
    bool    m_movingSelection = false;
    QPointF m_moveStartMm;

    // Scaling selection state
    bool    m_scalingSelection = false;
    int     m_scaleHandleIdx = -1;
    QRectF  m_initialSelBoundsMm;
    QPointF m_scaleOriginMm;
    QVector<EditPath> m_pathsBeforeTransform;

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

// --- Hit testing for curve segment bending ---
bool Canvas2DWidget::hitSegment(const QPointF& px, int& outPath, int& outSeg) const
{
    if (m_paths.isEmpty()) return false;
    QVector<int> order;
    if (m_active >= 0 && m_active < m_paths.size()) order.push_back(m_active);
    for (int p : m_selectedPaths) if (p != m_active && p >= 0 && p < m_paths.size()) order.push_back(p);
    for (int p = 0; p < m_paths.size(); ++p) if (!order.contains(p)) order.push_back(p);

    for (int p : order) {
        const EditPath& ep = m_paths[p];
        if (ep.count() < 2) continue;
        const int segCount = ep.closed ? ep.count() : (ep.count() - 1);
        for (int s = 0; s < segCount; ++s) {
            const int next = (s + 1) % ep.count();
            const BezierNode& n0 = ep.nodes[s];
            const BezierNode& n1 = ep.nodes[next];
            const QPointF P0 = n0.pos;
            const QPointF P1 = n0.ctrlOut;
            const QPointF P2 = n1.ctrlIn;
            const QPointF P3 = n1.pos;
            for (int step = 1; step < 12; ++step) {
                const double t = step / 12.0;
                const double u = 1.0 - t;
                const QPointF ptMm = u*u*u*P0 + 3*u*u*t*P1 + 3*u*t*t*P2 + t*t*t*P3;
                if (QLineF(px, toScreen(ptMm)).length() <= 6.5) {
                    outPath = p;
                    outSeg = s;
                    return true;
                }
            }
        }
    }
    return false;
}

// --- Hit testing for transform bounding box handles (0..7) ---
int Canvas2DWidget::hitBBoxHandle(const QPointF& px) const
{
    if (m_selectedPaths.isEmpty() || m_tool != VectorTool::Select) return -1;
    const QRectF bMm = selectedBoundsMm();
    if (bMm.width() < 1.0 && bMm.height() < 1.0) return -1;

    const QPointF tl = toScreen(QPointF(bMm.left(),  bMm.top()));
    const QPointF br = toScreen(QPointF(bMm.right(), bMm.bottom()));
    const QRectF r = QRectF(tl, br).normalized();

    const QPointF pts[8] = {
        r.topLeft(),
        QPointF(r.center().x(), r.top()),
        r.topRight(),
        QPointF(r.right(), r.center().y()),
        r.bottomRight(),
        QPointF(r.center().x(), r.bottom()),
        r.bottomLeft(),
        QPointF(r.left(), r.center().y())
    };

    for (int i = 0; i < 8; ++i) {
        if (QLineF(px, pts[i]).length() <= 8.0) return i;
    }
    return -1;
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

        // 5. Transform Bounding Box around selection (in Select mode)
        if (m_tool == VectorTool::Select && !m_selectedPaths.isEmpty()) {
            const QRectF bMm = selectedBoundsMm();
            if (bMm.width() > 0.1 || bMm.height() > 0.1) {
                const QPointF tl = toScreen(QPointF(bMm.left(),  bMm.top()));
                const QPointF br = toScreen(QPointF(bMm.right(), bMm.bottom()));
                const QRectF r = QRectF(tl, br).normalized();

                g.setPen(QPen(QColor(56, 189, 248, 200), 1.2, Qt::DashLine));
                g.setBrush(Qt::NoBrush);
                g.drawRect(r);

                const QPointF pts[8] = {
                    r.topLeft(),
                    QPointF(r.center().x(), r.top()),
                    r.topRight(),
                    QPointF(r.right(), r.center().y()),
                    r.bottomRight(),
                    QPointF(r.center().x(), r.bottom()),
                    r.bottomLeft(),
                    QPointF(r.left(), r.center().y())
                };
                g.setPen(QPen(QColor(2, 132, 199), 1.2));
                g.setBrush(Qt::white);
                for (int h = 0; h < 8; ++h) {
                    g.drawRect(QRectF(pts[h].x() - 3.5, pts[h].y() - 3.5, 7.0, 7.0));
                }
            }
        }

        // 6. Marquee / Rubberband Selection Box
        if (m_drawingMarquee) {
            const QRectF r = QRectF(m_marqueeStart, m_marqueeCurrent).normalized();
            g.setPen(QPen(QColor(56, 189, 248, 230), 1.2, Qt::DashLine));
            g.setBrush(QColor(56, 189, 248, 45));
            g.drawRect(r);
        }

        // 7. Lasso Selection Trail
        if (m_drawingLasso && m_lassoPolyPx.size() >= 2) {
            g.setPen(QPen(QColor(168, 85, 247, 230), 1.5, Qt::DashLine));
            g.setBrush(QColor(168, 85, 247, 45));
            g.drawPolygon(m_lassoPolyPx);
        }

        // Hint text for path editing
        g.setPen(QColor(150, 155, 165));
        QString hint;
        if (m_tool == VectorTool::Marquee) {
            hint = QStringLiteral("⬚ Rechteck-Auswahl: Rahmen aufziehen, um Pfade zu markieren (Shift für Mehrfachauswahl)");
        } else if (m_tool == VectorTool::Lasso) {
            hint = QStringLiteral("➰ Lasso-Auswahl: Freihand-Schlinge um Wasserzeichen / Pfade ziehen zum Markieren");
        } else if (m_tool == VectorTool::Pen) {
            hint = (m_bezierMode == PathEditorWidget::Mode::SatinRails)
                ? QStringLiteral("✏ Satin: Rail A zeichnen · „Neuer Pfad“ · Rail B · „Erzeugen ▶“")
                : QStringLiteral("✏ Tatami: Polygon zeichnen, ersten Punkt anklicken zum Schließen");
        } else {
            hint = QStringLiteral("🖱 Auswahl (V): Pfade verschieben · Bounding-Box skalieren · Kurven direkt biegen · Entf löscht");
        }
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

        QPainterPath hp;
        if (m_hoopType == HoopType::HoopC_50x50) {
            // Circular Free Arm frame (50x50 mm)
            hp.addEllipse(hoopRect);
        } else {
            // Smooth rounded hoop boundary matching real embroidery frames
            const double cornerPx = std::min(18.0, 12.0 * m_scale);
            hp.addRoundedRect(hoopRect, cornerPx, cornerPx);
        }

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

        // Specialty guides:
        if (m_hoopType == HoopType::HoopHat_100x90) {
            // Cap visor / brim guide at bottom
            QPainterPath brim;
            brim.moveTo(toScreen(QPointF(-hw * 0.85, -hh)));
            brim.quadTo(toScreen(QPointF(0.0, -hh - 22.0)), toScreen(QPointF(hw * 0.85, -hh)));
            g.setPen(QPen(QColor(251, 146, 60, 180), 1.6, Qt::DashDotLine));
            g.drawPath(brim);
            g.setPen(QColor(251, 146, 60, 220));
            QFont fCap = g.font(); fCap.setPointSize(9); fCap.setBold(true); g.setFont(fCap);
            g.drawText(toScreen(QPointF(-34.0, -hh - 8.0)), QStringLiteral("🧢 Mützenschirm"));
        } else if (m_hoopType == HoopType::HoopD_230x200) {
            // Giga Hoop: 2 overlapping 140x200 fields (Feld 1 & Feld 2)
            const double sW = 140.0;
            const QRectF r1(toScreen(QPointF(-hw, hh)), toScreen(QPointF(-hw + sW, -hh)));
            const QRectF r2(toScreen(QPointF(hw - sW, hh)), toScreen(QPointF(hw, -hh)));
            g.setPen(QPen(QColor(56, 189, 248, 120), 1.0, Qt::DashLine));
            g.drawRoundedRect(r1, 10, 10);
            g.setPen(QPen(QColor(168, 85, 247, 120), 1.0, Qt::DashLine));
            g.drawRoundedRect(r2, 10, 10);
            QFont fGiga = g.font(); fGiga.setPointSize(9); fGiga.setBold(true); g.setFont(fGiga);
            g.setPen(QColor(56, 189, 248, 200));
            g.drawText(r1.topLeft() + QPointF(8, 36), QStringLiteral("① Feld 1 (140×200)"));
            g.setPen(QColor(168, 85, 247, 200));
            g.drawText(r2.topRight() + QPointF(-115, 36), QStringLiteral("② Feld 2 (140×200)"));
        } else if (m_hoopType == HoopType::HoopMag_140x200 || m_hoopType == HoopType::HoopMag_100x100) {
            // Magnetic brackets
            g.setPen(QColor(234, 179, 8, 210));
            QFont fMag = g.font(); fMag.setPointSize(9); fMag.setBold(true); g.setFont(fMag);
            g.drawText(hoopRect.topRight() + QPointF(-75, 20), QStringLiteral("🧲 Magnet"));
        }

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

    const bool isSelected = m_selectedPaths.contains(pathIndex);

    QColor line = (m_bezierMode == PathEditorWidget::Mode::TatamiPolygon) ? QColor(90, 150, 240)
                                                                          : QColor(230, 80, 80);
    if (ep.color.isValid()) {
        line = ep.color;
    }

    if (isSelected) {
        // High-contrast vibrant selection color (Amber/Gold)
        line = QColor(250, 204, 21);
        g.setPen(QPen(line, 2.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    } else {
        if (!active) line = line.darker(130);
        g.setPen(QPen(line, active ? 2.2 : 1.6));
    }

    if (ep.closed) {
        QColor fillCol = line;
        fillCol.setAlpha(isSelected ? 75 : (active ? 65 : 40));
        g.setBrush(fillCol);
    } else {
        g.setBrush(Qt::NoBrush);
    }
    g.drawPath(T.map(ep.toPainterPath()));

    // Tangent handles for active path or selected paths
    if (active || isSelected) {
        g.setPen(QPen(isSelected ? QColor(253, 224, 71) : QColor(120, 160, 210), 1));
        for (const BezierNode& n : ep.nodes) {
            if (n.isCorner()) continue;
            g.drawLine(toScreen(n.pos), toScreen(n.ctrlOut));
            g.drawLine(toScreen(n.pos), toScreen(n.ctrlIn));
            g.setBrush(isSelected ? QColor(253, 224, 71) : QColor(120, 160, 210));
            g.drawEllipse(toScreen(n.ctrlOut), 3.2, 3.2);
            g.drawEllipse(toScreen(n.ctrlIn),  3.2, 3.2);
        }
    }

    // Anchor nodes
    for (int n = 0; n < ep.count(); ++n) {
        const QPointF s = toScreen(ep.nodes[n].pos);
        const bool selNode = (m_sel.part == Part::Anchor &&
                              m_sel.path == pathIndex && m_sel.node == n);
        g.setPen(QPen(isSelected ? QColor(250, 204, 21) : Qt::white, 1));
        g.setBrush(selNode ? QColor(255, 210, 90) : (isSelected ? QColor(254, 240, 138) : QColor(230, 230, 235)));
        g.drawRect(QRectF(s.x() - 3.5, s.y() - 3.5, 7, 7));
    }

    // Rail label
    if (m_bezierMode == PathEditorWidget::Mode::SatinRails && !ep.nodes.isEmpty()) {
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
        // Shift + Left Click moves design inside hoop when in Stitches2D
        if ((e->modifiers() & Qt::ShiftModifier) && m_canvasMode == PathEditorWidget::CanvasMode::Stitches2D) {
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

        // --- 1. TOOL: MARQUEE (Rechteck-Auswahl) ---
        if (m_tool == VectorTool::Marquee) {
            m_drawingMarquee = true;
            m_marqueeStart = px;
            m_marqueeCurrent = px;
            update();
            return;
        }

        // --- 2. TOOL: LASSO (Freihand-Schlinge) ---
        if (m_tool == VectorTool::Lasso) {
            m_drawingLasso = true;
            m_lassoPolyPx.clear();
            m_lassoPolyPx << px;
            update();
            return;
        }

        // --- 3. TOOL: PEN (Bézier-Stift) ---
        if (m_tool == VectorTool::Pen) {
            const Hit h = hitTest(px);
            if (h.part != Part::None) {
                pushUndo();
                m_drag = h;
                m_sel  = h;
                m_active = h.path;
                m_dragging = true;
                update();
                return;
            }

            if (m_active < 0 || m_active >= m_paths.size()) newPath();
            pushUndo();
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
            return;
        }

        // --- 4. TOOL: SELECT (Pfeil / Transformieren / Biegen) ---
        if (m_tool == VectorTool::Select) {
            // A. Check Bounding Box Handle (Scaling)
            int hBBox = hitBBoxHandle(px);
            if (hBBox >= 0) {
                pushUndo();
                m_scalingSelection = true;
                m_scaleHandleIdx = hBBox;
                m_initialSelBoundsMm = selectedBoundsMm();
                m_pathsBeforeTransform = m_paths;
                switch (hBBox) {
                    case 0: m_scaleOriginMm = m_initialSelBoundsMm.bottomRight(); break;
                    case 1: m_scaleOriginMm = QPointF(m_initialSelBoundsMm.center().x(), m_initialSelBoundsMm.bottom()); break;
                    case 2: m_scaleOriginMm = m_initialSelBoundsMm.bottomLeft(); break;
                    case 3: m_scaleOriginMm = QPointF(m_initialSelBoundsMm.left(), m_initialSelBoundsMm.center().y()); break;
                    case 4: m_scaleOriginMm = m_initialSelBoundsMm.topLeft(); break;
                    case 5: m_scaleOriginMm = QPointF(m_initialSelBoundsMm.center().x(), m_initialSelBoundsMm.top()); break;
                    case 6: m_scaleOriginMm = m_initialSelBoundsMm.topRight(); break;
                    case 7: m_scaleOriginMm = QPointF(m_initialSelBoundsMm.right(), m_initialSelBoundsMm.center().y()); break;
                }
                return;
            }

            // B. Check Anchor / Tangent Handles
            const Hit h = hitTest(px);
            if (h.part != Part::None) {
                pushUndo();
                m_drag = h;
                m_sel  = h;
                m_active = h.path;
                m_dragging = true;
                if (!m_selectedPaths.contains(h.path)) {
                    if (!(e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier))) {
                        m_selectedPaths.clear();
                    }
                    m_selectedPaths.insert(h.path);
                    emit selectionChanged(m_selectedPaths.size());
                }
                update();
                return;
            }

            // C. Check Curve Segment for BENDING
            int bPath = -1, bSeg = -1;
            if (hitSegment(px, bPath, bSeg)) {
                pushUndo();
                m_bending = true;
                m_bendPath = bPath;
                m_bendSeg = bSeg;
                m_bendStartMm = toMM(px);
                m_pathsBeforeTransform = m_paths;
                m_active = bPath;
                if (!m_selectedPaths.contains(bPath)) {
                    if (!(e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier))) {
                        m_selectedPaths.clear();
                    }
                    m_selectedPaths.insert(bPath);
                    emit selectionChanged(m_selectedPaths.size());
                }
                setCursor(Qt::PointingHandCursor);
                update();
                return;
            }

            // D. Check Click on a Path Body / Fill
            int hitPathIdx = -1;
            const QPointF mmClick = toMM(px);
            for (int i = 0; i < m_paths.size(); ++i) {
                const EditPath& ep = m_paths[i];
                if (ep.toPainterPath().contains(mmClick) ||
                    (ep.boundingRect().contains(mmClick) && ep.boundingRect().width() < 12.0 && ep.boundingRect().height() < 12.0)) {
                    hitPathIdx = i;
                    break;
                }
            }

            if (hitPathIdx >= 0) {
                pushUndo();
                const bool multi = (e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier));
                if (multi) {
                    if (m_selectedPaths.contains(hitPathIdx)) m_selectedPaths.remove(hitPathIdx);
                    else m_selectedPaths.insert(hitPathIdx);
                } else {
                    if (!m_selectedPaths.contains(hitPathIdx)) {
                        m_selectedPaths.clear();
                        m_selectedPaths.insert(hitPathIdx);
                    }
                }
                m_active = hitPathIdx;
                m_movingSelection = true;
                m_moveStartMm = mmClick;
                m_pathsBeforeTransform = m_paths;
                emit selectionChanged(m_selectedPaths.size());
                update();
                return;
            }

            // E. Click on empty space: clear selection and start rubberband marquee
            if (!(e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier))) {
                m_selectedPaths.clear();
                m_sel = Hit();
                emit selectionChanged(0);
            }
            m_drawingMarquee = true;
            m_marqueeStart = px;
            m_marqueeCurrent = px;
            update();
        }
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
        m_sequence.translate(dx, dy);
        emit designMoved(dx, dy);
        update();
        return;
    }

    // --- MARQUEE MOVING ---
    if (m_drawingMarquee) {
        m_marqueeCurrent = px;
        update();
        return;
    }

    // --- LASSO MOVING ---
    if (m_drawingLasso) {
        if (m_lassoPolyPx.isEmpty() || QLineF(m_lassoPolyPx.last(), px).length() > 3.0) {
            m_lassoPolyPx << px;
            update();
        }
        return;
    }

    // --- SEGMENT BENDING ---
    if (m_bending && m_bendPath >= 0 && m_bendPath < m_pathsBeforeTransform.size()) {
        const QPointF curMm = toMM(px);
        const QPointF deltaMm = curMm - m_bendStartMm;
        m_paths = m_pathsBeforeTransform;
        m_paths[m_bendPath].bendSegment(m_bendSeg, deltaMm);
        emit geometryChanged();
        update();
        return;
    }

    // --- SCALING SELECTION VIA BBOX HANDLE ---
    if (m_scalingSelection && !m_selectedPaths.isEmpty() && !m_pathsBeforeTransform.isEmpty()) {
        const QPointF curMm = toMM(px);
        const double initW = std::max(0.1, m_initialSelBoundsMm.width());
        const double initH = std::max(0.1, m_initialSelBoundsMm.height());
        const double curDist = std::hypot(curMm.x() - m_scaleOriginMm.x(), curMm.y() - m_scaleOriginMm.y());
        const double initDist = std::hypot(initW, initH);
        double factor = (initDist > 1e-3) ? (curDist / initDist) : 1.0;
        factor = std::clamp(factor, 0.05, 20.0);
        m_paths = m_pathsBeforeTransform;
        for (int pIdx : m_selectedPaths) {
            if (pIdx >= 0 && pIdx < m_paths.size()) {
                m_paths[pIdx].scale(factor, factor, m_scaleOriginMm);
            }
        }
        emit geometryChanged();
        update();
        return;
    }

    // --- MOVING SELECTION ---
    if (m_movingSelection && !m_selectedPaths.isEmpty() && !m_pathsBeforeTransform.isEmpty()) {
        const QPointF curMm = toMM(px);
        const QPointF deltaMm = curMm - m_moveStartMm;
        m_paths = m_pathsBeforeTransform;
        for (int pIdx : m_selectedPaths) {
            if (pIdx >= 0 && pIdx < m_paths.size()) {
                m_paths[pIdx].translate(deltaMm.x(), deltaMm.y());
            }
        }
        emit geometryChanged();
        update();
        return;
    }

    // --- DRAGGING NODE / HANDLE ---
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

    // --- CURSOR FEEDBACK ---
    if (m_canvasMode == PathEditorWidget::CanvasMode::Stitches2D) {
        setCursor(Qt::ArrowCursor);
    } else {
        if (m_tool == VectorTool::Marquee || m_tool == VectorTool::Lasso) {
            setCursor(Qt::CrossCursor);
        } else if (m_tool == VectorTool::Pen) {
            const Hit h = hitTest(px);
            if (h.part == Part::Anchor) setCursor(Qt::SizeAllCursor);
            else if (h.part == Part::HandleIn || h.part == Part::HandleOut) setCursor(Qt::CrossCursor);
            else setCursor(Qt::CrossCursor);
        } else if (m_tool == VectorTool::Select) {
            int hBBox = hitBBoxHandle(px);
            if (hBBox == 0 || hBBox == 4) setCursor(Qt::SizeFDiagCursor);
            else if (hBBox == 2 || hBBox == 6) setCursor(Qt::SizeBDiagCursor);
            else if (hBBox == 1 || hBBox == 5) setCursor(Qt::SizeVerCursor);
            else if (hBBox == 3 || hBBox == 7) setCursor(Qt::SizeHorCursor);
            else {
                const Hit h = hitTest(px);
                if (h.part == Part::Anchor) setCursor(Qt::SizeAllCursor);
                else if (h.part == Part::HandleIn || h.part == Part::HandleOut) setCursor(Qt::CrossCursor);
                else {
                    int bP = -1, bS = -1;
                    if (hitSegment(px, bP, bS)) setCursor(Qt::PointingHandCursor);
                    else setCursor(Qt::ArrowCursor);
                }
            }
        }
    }
}

void Canvas2DWidget::mouseReleaseEvent(QMouseEvent* e)
{
    m_panning = false;
    m_movingDesign = false;
    m_dragging = false;
    m_drag = Hit();
    m_bending = false;
    m_scalingSelection = false;
    m_movingSelection = false;
    m_pathsBeforeTransform.clear();

    if (m_drawingMarquee) {
        m_drawingMarquee = false;
        const QRectF boxPx = QRectF(m_marqueeStart, m_marqueeCurrent).normalized();
        if (boxPx.width() >= 4 || boxPx.height() >= 4) {
            const QPointF p0 = toMM(boxPx.topLeft());
            const QPointF p1 = toMM(boxPx.bottomRight());
            const QRectF rectMm(std::min(p0.x(), p1.x()), std::min(p0.y(), p1.y()),
                                std::abs(p1.x() - p0.x()), std::abs(p1.y() - p0.y()));
            const bool add = (e && (e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)));
            selectPathsInRect(rectMm, add);
        }
        update();
    }

    if (m_drawingLasso) {
        m_drawingLasso = false;
        if (m_lassoPolyPx.size() >= 3) {
            QPolygonF lassoMm;
            for (const auto& ptPx : m_lassoPolyPx) {
                lassoMm << toMM(ptPx);
            }
            if (lassoMm.first() != lassoMm.last()) lassoMm << lassoMm.first();
            const bool add = (e && (e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)));
            selectPathsInPolygon(lassoMm, add);
        }
        m_lassoPolyPx.clear();
        update();
    }

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
        deleteSelected();
        e->accept();
        return;
    }

    if (e->matches(QKeySequence::SelectAll) || (e->key() == Qt::Key_A && (e->modifiers() & Qt::ControlModifier))) {
        selectAllPaths();
        e->accept();
        return;
    }

    if (e->key() == Qt::Key_Escape) {
        clearSelection();
        e->accept();
        return;
    }

    if (e->matches(QKeySequence::Undo) || (e->key() == Qt::Key_Z && (e->modifiers() & Qt::ControlModifier) && !(e->modifiers() & Qt::ShiftModifier))) {
        undo();
        e->accept();
        return;
    }

    if (e->matches(QKeySequence::Redo) || (e->key() == Qt::Key_Y && (e->modifiers() & Qt::ControlModifier)) ||
        (e->key() == Qt::Key_Z && (e->modifiers() & Qt::ControlModifier) && (e->modifiers() & Qt::ShiftModifier))) {
        redo();
        e->accept();
        return;
    }

    // Shortcuts for tool switching
    if (e->key() == Qt::Key_V) {
        setVectorTool(VectorTool::Select);
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_R) {
        setVectorTool(VectorTool::Marquee);
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_L) {
        setVectorTool(VectorTool::Lasso);
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_P) {
        setVectorTool(VectorTool::Pen);
        e->accept();
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
        if (!m_selectedPaths.isEmpty()) {
            pushUndo();
            for (int idx : m_selectedPaths) {
                if (idx >= 0 && idx < m_paths.size()) {
                    m_paths[idx].translate(dx, dy);
                }
            }
            emit geometryChanged();
            update();
        } else {
            translatePaths(dx, dy);
        }
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
        "QWidget { background:#181b22; border-bottom:1px solid #2c313d; } "
        "QToolButton { background:#20242e; color:#cbd5e1; border:1px solid #2f3545; border-radius:5px; padding:3px 5px; font-size:11px; font-weight:600; } "
        "QToolButton:hover { background:#282e3d; color:#ffffff; border-color:#2dd4bf; } "
        "QToolButton:checked { background:#134e4a; color:#5eead4; border:1px solid #2dd4bf; }"));
    auto* tbLay = new QHBoxLayout(tb);
    tbLay->setContentsMargins(6, 4, 6, 4);
    tbLay->setSpacing(4);

    // Mode Toggle Buttons
    m_btnStitches = new QToolButton(tb);
    m_btnStitches->setText(QStringLiteral("Stiche"));
    m_btnStitches->setMinimumWidth(56);
    m_btnStitches->setCheckable(true);
    m_btnStitches->setChecked(true);
    m_btnStitches->setToolTip(QStringLiteral("2D-Stickbild anzeigen (Fadenstiche, Einstichpunkte, Janome-Rahmen)"));

    m_btnPaths = new QToolButton(tb);
    m_btnPaths->setText(QStringLiteral("Pfade"));
    m_btnPaths->setMinimumWidth(52);
    m_btnPaths->setCheckable(true);
    m_btnPaths->setChecked(false);
    m_btnPaths->setToolTip(QStringLiteral("Bézier-Vektorpuffer bearbeiten (Satin-Rails & Tatami-Polygone zeichnen)"));

    connect(m_btnStitches, &QToolButton::clicked, this, [this]{
        m_btnStitches->setChecked(true);
        m_btnPaths->setChecked(false);
        m_canvas->setCanvasMode(CanvasMode::Stitches2D);
        if (m_vectorBar) m_vectorBar->setVisible(false);
    });
    connect(m_btnPaths, &QToolButton::clicked, this, [this]{
        m_btnPaths->setChecked(true);
        m_btnStitches->setChecked(false);
        m_canvas->setCanvasMode(CanvasMode::BezierPaths);
        if (m_vectorBar) m_vectorBar->setVisible(true);
    });

    tbLay->addWidget(m_btnStitches);
    tbLay->addWidget(m_btnPaths);

    // Toggles: Needle points, Jumps, Hoop
    m_btnPoints = new QToolButton(tb);
    m_btnPoints->setText(QStringLiteral("Punkte"));
    m_btnPoints->setMinimumWidth(56);
    m_btnPoints->setCheckable(true);
    m_btnPoints->setChecked(true);
    m_btnPoints->setToolTip(QStringLiteral("Nadel-Einstichpunkte ein-/ausblenden"));
    connect(m_btnPoints, &QToolButton::toggled, this, [this](bool c){ m_canvas->setShowNeedlePoints(c); });

    m_btnJumps = new QToolButton(tb);
    m_btnJumps->setText(QStringLiteral("Sprünge"));
    m_btnJumps->setMinimumWidth(64);
    m_btnJumps->setCheckable(true);
    m_btnJumps->setChecked(true);
    m_btnJumps->setToolTip(QStringLiteral("Sprungstiche (gestrichelte Linien) ein-/ausblenden"));
    connect(m_btnJumps, &QToolButton::toggled, this, [this](bool c){ m_canvas->setShowJumps(c); });

    m_btnHoop = new QToolButton(tb);
    m_btnHoop->setText(QStringLiteral("⭕ Rahmen"));
    m_btnHoop->setCheckable(true);
    m_btnHoop->setChecked(true);
    m_btnHoop->setVisible(false);
    connect(m_btnHoop, &QToolButton::toggled, this, [this](bool c){ m_canvas->setShowHoop(c); });

    m_hoopCombo = new QComboBox(tb);
    m_hoopCombo->setVisible(false);
    const auto& mp = MachineProfile::current();
    for (HoopType t : mp.hoops) {
        const HoopSpec hs = hoopSpec(t);
        m_hoopCombo->addItem(QStringLiteral("%1 (%2×%3)").arg(QString::fromLatin1(hs.name)).arg(int(hs.widthMm)).arg(int(hs.heightMm)),
                             static_cast<int>(t));
    }
    connect(m_hoopCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int idx){
        if (idx >= 0 && idx < m_hoopCombo->count()) {
            HoopType ht = static_cast<HoopType>(m_hoopCombo->itemData(idx).toInt());
            const HoopSpec hs = hoopSpec(ht);
            m_canvas->setHoop(hs.widthMm, hs.heightMm, QString::fromLatin1(hs.name), ht);
            emit hoopSelected(ht);
        }
    });

    tbLay->addWidget(m_btnPoints);
    tbLay->addWidget(m_btnJumps);

    m_btnFitHoop = new QToolButton(tb);
    m_btnFitHoop->setText(QStringLiteral("📐 Einpassen"));
    m_btnFitHoop->setVisible(false);
    connect(m_btnFitHoop, &QToolButton::clicked, this, &PathEditorWidget::fitToHoopRequested);

    // Zoom buttons
    m_btnFit = new QToolButton(tb);
    m_btnFit->setText(QStringLiteral("Fit"));
    m_btnFit->setMinimumWidth(40);
    m_btnFit->setToolTip(QStringLiteral("Motiv & Rahmen optimal im 2D-Fenster einpassen"));
    connect(m_btnFit, &QToolButton::clicked, this, &PathEditorWidget::fitInView);

    m_btnZoom100 = new QToolButton(tb);
    m_btnZoom100->setText(QStringLiteral("1:1"));
    m_btnZoom100->setMinimumWidth(40);
    m_btnZoom100->setToolTip(QStringLiteral("100% Originalgröße"));
    connect(m_btnZoom100, &QToolButton::clicked, this, &PathEditorWidget::resetZoom100);

    tbLay->addWidget(m_btnFit);
    tbLay->addWidget(m_btnZoom100);

    auto* spacer = new QWidget(tb);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tbLay->addWidget(spacer);

    // Vorlage Dimmer (opacity slider, only visible when background image is loaded)
    m_dimLabel = new QLabel(QStringLiteral("🖼"), tb);
    m_dimLabel->setStyleSheet(QStringLiteral("color:#8b93a4; font-size:11px; border:none;"));
    m_dimLabel->setVisible(false);
    m_dimSlider = new QSlider(Qt::Horizontal, tb);
    m_dimSlider->setRange(0, 100);
    m_dimSlider->setValue(45);
    m_dimSlider->setFixedWidth(60);
    m_dimSlider->setToolTip(QStringLiteral("Helligkeit/Transparenz der Bildvorlage"));
    m_dimSlider->setVisible(false);
    connect(m_dimSlider, &QSlider::valueChanged, this, [this](int val){
        m_canvas->setBackgroundOpacity(val / 100.0);
    });

    tbLay->addWidget(m_dimLabel);
    tbLay->addWidget(m_dimSlider);

    mainLay->addWidget(tb);

    // Vector editing toolbar controls
    m_vectorBar = new QWidget(this);
    m_vectorBar->setStyleSheet(QStringLiteral(
        "background:#141720; border-bottom:1px solid #232733; padding:2px 8px;"));
    auto* vbLay = new QHBoxLayout(m_vectorBar);
    vbLay->setContentsMargins(6, 3, 6, 3);
    vbLay->setSpacing(4);

    auto* toolGroup = new QButtonGroup(this);
    toolGroup->setExclusive(true);

    m_btnToolSelect = new QToolButton(m_vectorBar);
    m_btnToolSelect->setText(QStringLiteral("🖱 Auswahl (V)"));
    m_btnToolSelect->setCheckable(true);
    m_btnToolSelect->setChecked(true);
    m_btnToolSelect->setToolTip(QStringLiteral("Auswahl- & Transformier-Werkzeug (V): Klick wählt Pfad, Bounding-Box skaliert, Kurven direkt biegen"));
    toolGroup->addButton(m_btnToolSelect);
    vbLay->addWidget(m_btnToolSelect);

    m_btnToolMarquee = new QToolButton(m_vectorBar);
    m_btnToolMarquee->setText(QStringLiteral("⬚ Rechteck (R)"));
    m_btnToolMarquee->setCheckable(true);
    m_btnToolMarquee->setToolTip(QStringLiteral("Rechteck-Rahmen aufziehen, um Pfade auszuwählen (R)"));
    toolGroup->addButton(m_btnToolMarquee);
    vbLay->addWidget(m_btnToolMarquee);

    m_btnToolLasso = new QToolButton(m_vectorBar);
    m_btnToolLasso->setText(QStringLiteral("➰ Lasso (L)"));
    m_btnToolLasso->setCheckable(true);
    m_btnToolLasso->setToolTip(QStringLiteral("Freihand-Schlinge um Wasserzeichen oder Pfadgruppen ziehen (L)"));
    toolGroup->addButton(m_btnToolLasso);
    vbLay->addWidget(m_btnToolLasso);

    m_btnToolPen = new QToolButton(m_vectorBar);
    m_btnToolPen->setText(QStringLiteral("✏ Stift (P)"));
    m_btnToolPen->setCheckable(true);
    m_btnToolPen->setToolTip(QStringLiteral("Bézier-Stift: Neue Punkte setzen / Kurven zeichnen (P)"));
    toolGroup->addButton(m_btnToolPen);
    vbLay->addWidget(m_btnToolPen);

    connect(m_btnToolSelect,  &QToolButton::clicked, this, [this]{ m_canvas->setVectorTool(VectorTool::Select); });
    connect(m_btnToolMarquee, &QToolButton::clicked, this, [this]{ m_canvas->setVectorTool(VectorTool::Marquee); });
    connect(m_btnToolLasso,   &QToolButton::clicked, this, [this]{ m_canvas->setVectorTool(VectorTool::Lasso); });
    connect(m_btnToolPen,     &QToolButton::clicked, this, [this]{ m_canvas->setVectorTool(VectorTool::Pen); });

    auto* sepV1 = new QFrame(m_vectorBar);
    sepV1->setFrameShape(QFrame::VLine);
    sepV1->setStyleSheet(QStringLiteral("color:#2c313d;"));
    vbLay->addWidget(sepV1);

    m_btnDeleteSel = new QToolButton(m_vectorBar);
    m_btnDeleteSel->setText(QStringLiteral("🗑 Löschen"));
    m_btnDeleteSel->setStyleSheet(QStringLiteral("QToolButton { color:#f87171; font-weight:bold; } QToolButton:hover { background:#7f1d1d; }"));
    m_btnDeleteSel->setToolTip(QStringLiteral("Ausgewählte Pfade oder Punkte löschen (Entf / Backspace)"));
    connect(m_btnDeleteSel, &QToolButton::clicked, this, &PathEditorWidget::deleteSelected);
    vbLay->addWidget(m_btnDeleteSel);

    m_btnFlipH = new QToolButton(m_vectorBar);
    m_btnFlipH->setText(QStringLiteral("↔ Spieg. H"));
    m_btnFlipH->setToolTip(QStringLiteral("Auswahl horizontal spiegeln"));
    connect(m_btnFlipH, &QToolButton::clicked, this, &PathEditorWidget::flipHorizontalSelected);
    vbLay->addWidget(m_btnFlipH);

    m_btnFlipV = new QToolButton(m_vectorBar);
    m_btnFlipV->setText(QStringLiteral("↕ Spieg. V"));
    m_btnFlipV->setToolTip(QStringLiteral("Auswahl vertikal spiegeln"));
    connect(m_btnFlipV, &QToolButton::clicked, this, &PathEditorWidget::flipVerticalSelected);
    vbLay->addWidget(m_btnFlipV);

    m_btnRotate90 = new QToolButton(m_vectorBar);
    m_btnRotate90->setText(QStringLiteral("↻ 90°"));
    m_btnRotate90->setToolTip(QStringLiteral("Auswahl um 90° im Uhrzeigersinn drehen"));
    connect(m_btnRotate90, &QToolButton::clicked, this, [this]{ rotateSelected(90.0); });
    vbLay->addWidget(m_btnRotate90);

    m_btnScale = new QToolButton(m_vectorBar);
    m_btnScale->setText(QStringLiteral("⤢ +10%"));
    m_btnScale->setToolTip(QStringLiteral("Auswahl um 10% vergrößern"));
    connect(m_btnScale, &QToolButton::clicked, this, [this]{ scaleSelected(1.10); });
    vbLay->addWidget(m_btnScale);

    auto* btnScaleDown = new QToolButton(m_vectorBar);
    btnScaleDown->setText(QStringLiteral("⤡ -10%"));
    btnScaleDown->setToolTip(QStringLiteral("Auswahl um 10% verkleinern"));
    connect(btnScaleDown, &QToolButton::clicked, this, [this]{ scaleSelected(0.90); });
    vbLay->addWidget(btnScaleDown);

    m_btnFilterNoise = new QToolButton(m_vectorBar);
    m_btnFilterNoise->setText(QStringLiteral("🧹 Inseln filtern"));
    m_btnFilterNoise->setStyleSheet(QStringLiteral("QToolButton { color:#38bdf8; }"));
    m_btnFilterNoise->setToolTip(QStringLiteral("Kleine Splitter & Wasserzeichenreste entfernen (Fläche < 4 mm²)"));
    connect(m_btnFilterNoise, &QToolButton::clicked, this, [this]{ filterSmallIslands(4.0); });
    vbLay->addWidget(m_btnFilterNoise);

    auto* sepV2 = new QFrame(m_vectorBar);
    sepV2->setFrameShape(QFrame::VLine);
    sepV2->setStyleSheet(QStringLiteral("color:#2c313d;"));
    vbLay->addWidget(sepV2);

    m_btnUndo = new QToolButton(m_vectorBar);
    m_btnUndo->setText(QStringLiteral("↩"));
    m_btnUndo->setToolTip(QStringLiteral("Rückgängig (Strg+Z)"));
    m_btnUndo->setEnabled(false);
    connect(m_btnUndo, &QToolButton::clicked, this, &PathEditorWidget::undo);
    vbLay->addWidget(m_btnUndo);

    m_btnRedo = new QToolButton(m_vectorBar);
    m_btnRedo->setText(QStringLiteral("↪"));
    m_btnRedo->setToolTip(QStringLiteral("Wiederholen (Strg+Y)"));
    m_btnRedo->setEnabled(false);
    connect(m_btnRedo, &QToolButton::clicked, this, &PathEditorWidget::redo);
    vbLay->addWidget(m_btnRedo);

    auto* vSpacer = new QWidget(m_vectorBar);
    vSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    vbLay->addWidget(vSpacer);

    m_lblSelInfo = new QLabel(m_vectorBar);
    m_lblSelInfo->setStyleSheet(QStringLiteral("color:#94a3b8; font-size:11px; font-weight:bold;"));
    m_lblSelInfo->setText(QStringLiteral("Keine Auswahl"));
    vbLay->addWidget(m_lblSelInfo);

    mainLay->addWidget(m_vectorBar);
    m_vectorBar->setVisible(false);

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
        if (m_vectorBar) {
            m_vectorBar->setVisible(m == CanvasMode::BezierPaths || m == CanvasMode::Combined);
        }
        emit canvasModeChanged(m);
    });
    connect(m_canvas, &Canvas2DWidget::selectionChanged, this, [this](int count){
        if (count == 0) {
            m_lblSelInfo->setText(QStringLiteral("Keine Auswahl"));
        } else if (count == 1) {
            const QRectF b = m_canvas->selectedBoundsMm();
            m_lblSelInfo->setText(QStringLiteral("1 Pfad (%1 × %2 mm)")
                .arg(b.width(), 0, 'f', 1).arg(b.height(), 0, 'f', 1));
        } else {
            const QRectF b = m_canvas->selectedBoundsMm();
            m_lblSelInfo->setText(QStringLiteral("%1 Pfade (%2 × %3 mm)")
                .arg(count).arg(b.width(), 0, 'f', 1).arg(b.height(), 0, 'f', 1));
        }
        emit selectionChanged(count);
    });
    connect(m_canvas, &Canvas2DWidget::undoStackChanged, this, [this]{
        m_btnUndo->setEnabled(m_canvas->canUndo());
        m_btnRedo->setEnabled(m_canvas->canRedo());
        emit undoStackChanged();
    });
    connect(m_canvas, &Canvas2DWidget::vectorToolChanged, this, [this](VectorTool t){
        if (t == VectorTool::Select) m_btnToolSelect->setChecked(true);
        else if (t == VectorTool::Marquee) m_btnToolMarquee->setChecked(true);
        else if (t == VectorTool::Lasso) m_btnToolLasso->setChecked(true);
        else if (t == VectorTool::Pen) m_btnToolPen->setChecked(true);
    });
}

void PathEditorWidget::setMode(Mode m) { m_canvas->setMode(m); }
PathEditorWidget::Mode PathEditorWidget::mode() const { return m_canvas->mode(); }
void PathEditorWidget::newPath()
{
    m_btnPaths->setChecked(true);
    m_btnStitches->setChecked(false);
    m_canvas->setCanvasMode(CanvasMode::BezierPaths);
    if (m_vectorBar) m_vectorBar->setVisible(true);
    m_canvas->newPath();
}
void PathEditorWidget::clearAll() { m_canvas->clearAll(); }
void PathEditorWidget::loadPaths(const QVector<EditPath>& paths)
{
    m_canvas->loadPaths(paths);
    if (!paths.isEmpty() && m_vectorBar) {
        m_vectorBar->setVisible(m_canvas->canvasMode() == CanvasMode::BezierPaths || m_canvas->canvasMode() == CanvasMode::Combined);
    }
}
int  PathEditorWidget::pathCount() const { return m_canvas->pathCount(); }
const EditPath& PathEditorWidget::path(int i) const { return m_canvas->path(i); }
QVector<EditPath> PathEditorWidget::paths() const { return m_canvas->paths(); }
void PathEditorWidget::translatePaths(double dx, double dy) { m_canvas->translatePaths(dx, dy); }
void PathEditorWidget::centerPaths() { m_canvas->centerPaths(); }
bool PathEditorWidget::pathsBounds(double& minX, double& minY, double& maxX, double& maxY) const
{
    return m_canvas->pathsBounds(minX, minY, maxX, maxY);
}
void PathEditorWidget::scalePaths(double factor, const QPointF& center)
{
    m_canvas->scalePaths(factor, center);
}
void PathEditorWidget::fitPathsToHoop(double marginMm)
{
    m_canvas->fitPathsToHoop(marginMm);
}

void PathEditorWidget::setVectorTool(VectorTool tool)
{
    m_canvas->setVectorTool(tool);
    if (tool == VectorTool::Select && m_btnToolSelect) m_btnToolSelect->setChecked(true);
    else if (tool == VectorTool::Marquee && m_btnToolMarquee) m_btnToolMarquee->setChecked(true);
    else if (tool == VectorTool::Lasso && m_btnToolLasso) m_btnToolLasso->setChecked(true);
    else if (tool == VectorTool::Pen && m_btnToolPen) m_btnToolPen->setChecked(true);
}

VectorTool PathEditorWidget::vectorTool() const
{
    return m_canvas->vectorTool();
}

void PathEditorWidget::selectAllPaths()
{
    m_canvas->selectAllPaths();
}

void PathEditorWidget::clearSelection()
{
    m_canvas->clearSelection();
}

void PathEditorWidget::selectPath(int index, bool additive)
{
    m_canvas->selectPath(index, additive);
}

QSet<int> PathEditorWidget::selectedPaths() const
{
    return m_canvas->selectedPaths();
}

void PathEditorWidget::deleteSelected()
{
    m_canvas->deleteSelected();
}

void PathEditorWidget::filterSmallIslands(double minAreaMm2)
{
    m_canvas->filterSmallIslands(minAreaMm2);
}

void PathEditorWidget::flipHorizontalSelected()
{
    m_canvas->flipHorizontalSelected();
}

void PathEditorWidget::flipVerticalSelected()
{
    m_canvas->flipVerticalSelected();
}

void PathEditorWidget::rotateSelected(double angleDeg)
{
    m_canvas->rotateSelected(angleDeg);
}

void PathEditorWidget::scaleSelected(double factor)
{
    m_canvas->scaleSelected(factor);
}

void PathEditorWidget::undo()
{
    m_canvas->undo();
}

void PathEditorWidget::redo()
{
    m_canvas->redo();
}

bool PathEditorWidget::canUndo() const
{
    return m_canvas->canUndo();
}

bool PathEditorWidget::canRedo() const
{
    return m_canvas->canRedo();
}

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

void PathEditorWidget::setHoop(double wMm, double hMm, const QString& name, HoopType type)
{
    m_canvas->setHoop(wMm, hMm, name, type);
    if (m_hoopCombo) {
        for (int i = 0; i < m_hoopCombo->count(); ++i) {
            if (m_hoopCombo->itemData(i).toInt() == static_cast<int>(type)) {
                QSignalBlocker b(m_hoopCombo);
                m_hoopCombo->setCurrentIndex(i);
                break;
            }
        }
    }
}

void PathEditorWidget::setSelectedHoop(HoopType type)
{
    const HoopSpec hs = hoopSpec(type);
    setHoop(hs.widthMm, hs.heightMm, QString::fromLatin1(hs.name), type);
}

HoopType PathEditorWidget::selectedHoop() const
{
    if (m_hoopCombo && m_hoopCombo->currentIndex() >= 0) {
        return static_cast<HoopType>(m_hoopCombo->currentData().toInt());
    }
    return HoopType::HoopB_140x200;
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
    if (m_vectorBar) {
        m_vectorBar->setVisible(mode == CanvasMode::BezierPaths || mode == CanvasMode::Combined);
    }
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
