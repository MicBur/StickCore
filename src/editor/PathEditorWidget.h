// ---------------------------------------------------------------------------
//  StickCore  –  PathEditorWidget.h
//
//  2D Design & Stitch Canvas.
//  Displays the 2D technical stitch design (needle points, thread runs,
//  jumps, Janome hoop, background artwork) before and while 3D simulation runs,
//  and provides cubic-Bézier path editing for manual digitizing.
// ---------------------------------------------------------------------------
#pragma once

#include "editor/BezierPath.h"
#include "core/StitchTypes.h"
#include <QWidget>
#include <QVector>
#include <QSet>
#include <QImage>

class QToolButton;
class QSlider;
class QLabel;
class QComboBox;

namespace stick {

enum class VectorTool {
    Select,   ///< Pointer / Transform: select, move, scale via bbox handles, bend curves
    Marquee,  ///< Rectangle box selection
    Lasso,    ///< Freehand lasso selection
    Pen       ///< Bézier pen tool (add points, drag tangent handles, close path)
};

class Canvas2DWidget;

class PathEditorWidget : public QWidget {
    Q_OBJECT
public:
    enum class Mode { SatinRails, TatamiPolygon };
    enum class CanvasMode { Stitches2D, BezierPaths, Combined };

    explicit PathEditorWidget(QWidget* parent = nullptr);

    // Mode & Bezier editing API
    void setMode(Mode m);
    Mode mode() const;

    void newPath();
    void clearAll();
    void loadPaths(const QVector<EditPath>& paths);

    int             pathCount() const;
    const EditPath& path(int i) const;
    QVector<EditPath> paths() const;

    void translatePaths(double dxMm, double dyMm);
    void centerPaths();
    bool pathsBounds(double& minX, double& minY, double& maxX, double& maxY) const;
    void scalePaths(double factor, const QPointF& center);
    void fitPathsToHoop(double marginMm = 5.0);

    // Vector Tool & Selection API
    void setVectorTool(VectorTool tool);
    VectorTool vectorTool() const;

    void selectAllPaths();
    void clearSelection();
    void selectPath(int index, bool additive = false);
    void selectPathsInRect(const QRectF& rectMm, bool additive = false);
    void selectPathsInPolygon(const QPolygonF& lassoMm, bool additive = false);
    int  selectedPathCount() const;
    QSet<int> selectedPaths() const;

    void deleteSelected();
    void filterSmallIslands(double minAreaMm2 = 4.0);

    void flipHorizontalSelected();
    void flipVerticalSelected();
    void rotateSelected(double angleDeg = 90.0);
    void scaleSelected(double factor);

    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;

    // Stitches API
    void setSequence(const StitchSequence& seq);
    const StitchSequence& sequence() const;

    // Background artwork & dimming API
    void setBackgroundImage(const QImage& img, double opacity = 0.45);
    void setBackgroundOpacity(double opacity);
    void clearBackgroundImage();

    // Hoop & view display
    void setHoop(double wMm, double hMm, const QString& name, HoopType type = HoopType::HoopB_140x200);
    void setSelectedHoop(HoopType type);
    HoopType selectedHoop() const;
    void setCanvasMode(CanvasMode mode);
    CanvasMode canvasMode() const;

    void setShowNeedlePoints(bool s);
    void setShowJumps(bool s);
    void setShowHoop(bool s);

    void fitInView();
    void resetZoom100();

    QSize sizeHint() const override { return QSize(560, 760); }

signals:
    void geometryChanged();
    void designMoved(double dx, double dy);
    void canvasModeChanged(CanvasMode mode);
    void hoopSelected(HoopType type);
    void fitToHoopRequested();
    void selectionChanged(int count);
    void undoStackChanged();

private:
    void setupUi();

    Canvas2DWidget* m_canvas = nullptr;

    // Toolbar controls
    QToolButton* m_btnStitches = nullptr;
    QToolButton* m_btnPaths    = nullptr;
    QToolButton* m_btnPoints   = nullptr;
    QToolButton* m_btnJumps    = nullptr;
    QToolButton* m_btnHoop     = nullptr;
    QComboBox*   m_hoopCombo   = nullptr;
    QToolButton* m_btnFitHoop  = nullptr;
    QToolButton* m_btnFit      = nullptr;
    QToolButton* m_btnZoomIn   = nullptr;
    QToolButton* m_btnZoomOut  = nullptr;
    QToolButton* m_btnZoom100  = nullptr;
    QLabel*      m_dimLabel    = nullptr;
    QSlider*     m_dimSlider   = nullptr;

    // Vector editing toolbar controls
    QWidget*     m_vectorBar      = nullptr;
    QToolButton* m_btnToolSelect  = nullptr;
    QToolButton* m_btnToolMarquee = nullptr;
    QToolButton* m_btnToolLasso   = nullptr;
    QToolButton* m_btnToolPen     = nullptr;
    QToolButton* m_btnDeleteSel   = nullptr;
    QToolButton* m_btnFlipH       = nullptr;
    QToolButton* m_btnFlipV       = nullptr;
    QToolButton* m_btnRotate90    = nullptr;
    QToolButton* m_btnScale       = nullptr;
    QToolButton* m_btnFilterNoise = nullptr;
    QToolButton* m_btnUndo        = nullptr;
    QToolButton* m_btnRedo        = nullptr;
    QLabel*      m_lblSelInfo     = nullptr;
};

} // namespace stick
