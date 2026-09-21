// ---------------------------------------------------------------------------
//  StickCore  –  StitchGLWidget.h
//
//  Modern Core-Profile (3.3) OpenGL viewer that renders a stitch list as
//  shaded 3-D thread. Stitches become instanced cylinder segments with
//  Blinn-Phong highlights over a normal-mapped fabric plane, and a timer
//  drives an animated "machine playback".
// ---------------------------------------------------------------------------
#pragma once

#include "core/StitchTypes.h"

#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QMatrix4x4>
#include <QTimer>
#include <QVector3D>
#include <vector>

class QOpenGLShaderProgram;

namespace stick {

class StitchGLWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    enum class MouseMode {
        Orbit,       ///< Left drag orbits camera, Shift+drag or right drag moves design
        MoveDesign   ///< Left drag moves design, right drag orbits camera
    };

    struct HoopVisual {
        double  widthMm  = 140.0;
        double  heightMm = 200.0;
        QString name     = QStringLiteral("Rahmen B (140×200)");
        bool    visible  = true;
    };

    explicit StitchGLWidget(QWidget* parent = nullptr);
    ~StitchGLWidget() override;

    void setSequence(const StitchSequence& seq);
    void setThreadRadiusMm(float r);

    int  visibleSegmentCount() const { return m_visible; }
    int  totalSegmentCount() const { return static_cast<int>(m_segments.size()); }
    bool isPlaying() const { return m_timer.isActive(); }

    void setVisibleSegments(int count);

    // Hoop & visual guides
    void setHoop(double widthMm, double heightMm, const QString& name = QString());
    void setHoopVisible(bool visible);
    bool isHoopVisible() const { return m_hoop.visible; }
    HoopVisual hoop() const { return m_hoop; }

    // Mouse & tool modes
    void setMouseMode(MouseMode mode);
    MouseMode mouseMode() const { return m_mouseMode; }

    // Camera view presets
    void setTopDownView();
    void setPerspectiveView();
    bool isTopDown() const { return m_pitch >= 88.0f; }

    // Design movement (offset in mm)
    void moveDesign(double dxMm, double dyMm);
    void centerDesign();
    QPointF designCenter() const;
    bool isOverflowing() const;

    const StitchSequence& sequence() const { return m_seq; }

public slots:
    void startPlayback();
    void pausePlayback();
    void resetPlayback();
    void showAll();
    void setPlaybackSpeed(int stitchesPerTick);

signals:
    void playbackProgress(int current, int total);
    void playbackStateChanged(bool playing);
    void designMoved(double deltaXmm, double deltaYmm);
    void designPositionChanged(double centerXmm, double centerYmm);
    void hoopOverflowChanged(bool overflows);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private slots:
    void onTick();

private:
    struct Segment { QMatrix4x4 model; QVector3D color; };

    void buildCylinderMesh(int sides);
    void buildFabric();
    void buildHoopGeometry();
    void rebuildInstances();     // CPU: build segment instance data from m_seq
    void uploadInstances();      // GPU: upload instance data (context must be current)

    QMatrix4x4 currentViewMatrix() const;
    QPointF    unprojectToPlane(const QPoint& pos) const;

    // GPU objects -----------------------------------------------------------
    QOpenGLShaderProgram* m_threadProg = nullptr;
    QOpenGLShaderProgram* m_fabricProg = nullptr;
    QOpenGLShaderProgram* m_hoopProg   = nullptr;

    unsigned int m_vao = 0, m_vbo = 0, m_ebo = 0, m_instVbo = 0;
    unsigned int m_fabVao = 0, m_fabVbo = 0;
    unsigned int m_hoopVao = 0, m_hoopVbo = 0;
    unsigned int m_gridVao = 0, m_gridVbo = 0;
    int  m_indexCount = 0;
    int  m_hoopVertexCount = 0;
    int  m_gridVertexCount = 0;

    // Scene data ------------------------------------------------------------
    StitchSequence         m_seq;
    std::vector<Segment>   m_segments;
    std::vector<float>     m_instanceData;   // 19 floats per segment
    float                  m_threadRadius = 0.55f;   // mm (exaggerated for on-screen clarity)
    HoopVisual             m_hoop;
    MouseMode              m_mouseMode = MouseMode::Orbit;

    // Camera ----------------------------------------------------------------
    QMatrix4x4 m_proj;
    float m_yaw = 0.0f, m_pitch = 28.0f, m_dist = 220.0f;
    QVector3D m_center{0.0f, 0.0f, 0.0f};  // centered on hoop origin by default
    QPoint    m_lastMouse;
    float m_designW = 0.0f, m_designH = 0.0f;   // mm, for auto-framing
    bool  m_autoFrame = true;                    // recompute m_dist next paint

    // Drag interaction
    bool   m_draggingDesign = false;
    bool   m_orbiting       = false;
    bool   m_panning        = false;
    QPointF m_planeDragStart;

    // Playback --------------------------------------------------------------
    QTimer m_timer;
    int    m_visible = 0;          // number of visible segments
    int    m_speed   = 12;         // segments revealed per tick
    bool   m_ready   = false;
};

} // namespace stick
