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
#include <QImage>

class QToolButton;
class QSlider;
class QLabel;
class QComboBox;

namespace stick {

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
};

} // namespace stick
