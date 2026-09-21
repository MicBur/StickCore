// ---------------------------------------------------------------------------
//  StickCore  –  PlacementDialog.cpp
// ---------------------------------------------------------------------------
#include "ui/PlacementDialog.h"

#include <QPainter>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QPushButton>
#include <algorithm>
#include <cmath>

namespace stick {

// ---------------------------------------------------------------------------
PlacementCanvas::PlacementCanvas(const StitchSequence& seq, double hoopWmm, double hoopHmm,
                                 QWidget* parent)
    : QWidget(parent), m_seq(seq), m_hoopW(hoopWmm), m_hoopH(hoopHmm)
{
    setMinimumSize(440, 360);
    setMouseTracking(false);
    if (!m_seq.bounds(m_bx0, m_by0, m_bx1, m_by1)) {
        m_bx0 = m_by0 = -10; m_bx1 = m_by1 = 10;
    }
}

double PlacementCanvas::scale() const
{
    const double margin = 1.12;   // a little room around the hoop
    const double sx = width()  / (m_hoopW * margin);
    const double sy = height() / (m_hoopH * margin);
    return std::min(sx, sy);
}

QPointF PlacementCanvas::mmToPx(double xmm, double ymm) const
{
    const double s = scale();
    const double cx = width() * 0.5, cy = height() * 0.5;
    return QPointF(cx + xmm * s, cy - ymm * s);   // +Y up
}

void PlacementCanvas::centerInHoop()
{
    m_offset = QPointF(-0.5 * (m_bx0 + m_bx1), -0.5 * (m_by0 + m_by1));
    update();
    emit moved();
}

bool PlacementCanvas::fits() const
{
    const double x0 = m_bx0 + m_offset.x(), x1 = m_bx1 + m_offset.x();
    const double y0 = m_by0 + m_offset.y(), y1 = m_by1 + m_offset.y();
    return x0 >= -m_hoopW/2 - 1e-6 && x1 <= m_hoopW/2 + 1e-6 &&
           y0 >= -m_hoopH/2 - 1e-6 && y1 <= m_hoopH/2 + 1e-6;
}

void PlacementCanvas::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(0x15, 0x17, 0x1c));

    // hoop rectangle
    const QPointF tl = mmToPx(-m_hoopW/2,  m_hoopH/2);
    const QPointF br = mmToPx( m_hoopW/2, -m_hoopH/2);
    const QRectF hoop(tl, br);
    p.setBrush(QColor(0x0f, 0x11, 0x16));
    p.setPen(QPen(QColor(0x2d, 0xd4, 0xbf), 2));
    p.drawRoundedRect(hoop, 6, 6);

    // centre cross
    p.setPen(QPen(QColor(0x39, 0x40, 0x4d), 1, Qt::DashLine));
    const QPointF c = mmToPx(0, 0);
    p.drawLine(QPointF(hoop.left(), c.y()), QPointF(hoop.right(), c.y()));
    p.drawLine(QPointF(c.x(), hoop.top()), QPointF(c.x(), hoop.bottom()));

    // the design, drawn as thread lines at the current offset
    const double s = scale();
    (void)s;
    int prevColor = -1;
    QColor col(200, 170, 90);
    bool have = false;
    QPointF prev;
    for (const Stitch& st : m_seq.stitches) {
        if (st.flags & SF_End) break;
        if (st.colorIdx != prevColor) {
            prevColor = st.colorIdx;
            col = (st.colorIdx >= 0 && st.colorIdx < int(m_seq.palette.size()))
                ? m_seq.palette[st.colorIdx].color : QColor(200,170,90);
        }
        const QPointF q = mmToPx(st.x + m_offset.x(), st.y + m_offset.y());
        const bool travel = (st.flags & (SF_Jump | SF_Trim | SF_ColorChange | SF_Stop)) != 0;
        if (have && !travel) {
            p.setPen(QPen(col, 1.2));
            p.drawLine(prev, q);
        }
        prev = q; have = true;
    }

    // design bounding box highlight
    const bool ok = fits();
    const QPointF b0 = mmToPx(m_bx0 + m_offset.x(), m_by1 + m_offset.y());
    const QPointF b1 = mmToPx(m_bx1 + m_offset.x(), m_by0 + m_offset.y());
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(ok ? QColor(0x2e, 0xcc, 0x71) : QColor(0xe7, 0x4c, 0x3c), 1, Qt::DotLine));
    p.drawRect(QRectF(b0, b1));
}

void PlacementCanvas::mousePressEvent(QMouseEvent* e)
{
    m_dragging = true;
    m_dragStartPx = e->position();
    m_offsetAtPress = m_offset;
}

void PlacementCanvas::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_dragging) return;
    const double s = scale();
    const QPointF d = e->position() - m_dragStartPx;
    m_offset = QPointF(m_offsetAtPress.x() + d.x() / s,
                       m_offsetAtPress.y() - d.y() / s);   // +Y up
    update();
    emit moved();
}

void PlacementCanvas::mouseReleaseEvent(QMouseEvent*)
{
    m_dragging = false;
}

// ---------------------------------------------------------------------------
PlacementDialog::PlacementDialog(const StitchSequence& seq, double hoopWmm, double hoopHmm,
                                 QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Motiv im Rahmen platzieren"));
    auto* lay = new QVBoxLayout(this);

    lay->addWidget(new QLabel(QStringLiteral(
        "Ziehe das Motiv mit der Maus an die gewünschte Stelle im Rahmen.")));

    m_canvas = new PlacementCanvas(seq, hoopWmm, hoopHmm, this);
    lay->addWidget(m_canvas, 1);

    m_status = new QLabel;
    m_status->setAlignment(Qt::AlignCenter);
    lay->addWidget(m_status);

    auto* row = new QHBoxLayout;
    auto* center = new QPushButton(QStringLiteral("Mittig zentrieren"));
    connect(center, &QPushButton::clicked, this, [this]{ m_canvas->centerInHoop(); });
    row->addWidget(center);
    row->addStretch(1);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Übernehmen"));
    bb->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Abbrechen"));
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    row->addWidget(bb);
    lay->addLayout(row);

    connect(m_canvas, &PlacementCanvas::moved, this, &PlacementDialog::updateStatus);
    updateStatus();
    resize(560, 500);
}

void PlacementDialog::updateStatus()
{
    const bool ok = m_canvas->fits();
    const QPointF o = m_canvas->offsetMm();
    m_status->setText(ok
        ? QStringLiteral("<span style='color:#2ecc71'>✓ passt in den Rahmen</span> "
                         "&nbsp; (Versatz %1 / %2 mm)").arg(o.x(),0,'f',1).arg(o.y(),0,'f',1)
        : QStringLiteral("<span style='color:#e74c3c'>✗ ragt über den Rahmen hinaus</span>"));
}

QPointF PlacementDialog::offsetMm() const { return m_canvas->offsetMm(); }

} // namespace stick
