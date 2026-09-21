// ---------------------------------------------------------------------------
//  StickCore  –  PlacementDialog.h
//
//  Lets the user position a finished motif inside the hoop by dragging it with
//  the mouse. Shows the hoop to scale with a centre cross; live-reports whether
//  the motif still fits. Returns the chosen offset in millimetres.
// ---------------------------------------------------------------------------
#pragma once

#include "core/StitchTypes.h"
#include <QDialog>
#include <QPointF>
#include <QWidget>

class QLabel;

namespace stick {

// The draggable hoop canvas.
class PlacementCanvas : public QWidget {
    Q_OBJECT
public:
    PlacementCanvas(const StitchSequence& seq, double hoopWmm, double hoopHmm,
                    QWidget* parent = nullptr);

    QPointF offsetMm() const { return m_offset; }
    bool    fits() const;
    void    centerInHoop();   // put the design's centre at the hoop centre

signals:
    void moved();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    double scale() const;                 // px per mm
    QPointF mmToPx(double xmm, double ymm) const;

    const StitchSequence& m_seq;
    double  m_hoopW, m_hoopH;             // mm
    double  m_bx0, m_by0, m_bx1, m_by1;   // design bounds (mm, no offset)
    QPointF m_offset;                     // mm
    bool    m_dragging = false;
    QPointF m_dragStartPx;
    QPointF m_offsetAtPress;
};

class PlacementDialog : public QDialog {
    Q_OBJECT
public:
    PlacementDialog(const StitchSequence& seq, double hoopWmm, double hoopHmm,
                    QWidget* parent = nullptr);

    QPointF offsetMm() const;

private:
    PlacementCanvas* m_canvas = nullptr;
    QLabel*          m_status = nullptr;
    void updateStatus();
};

} // namespace stick
