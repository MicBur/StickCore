// ---------------------------------------------------------------------------
//  StickCore  –  MultiHoopDialog.h
//
//  Multi-Hooping & Auto-Split Dialog:
//  Splits oversized designs into multiple hoop files with alignment marks.
// ---------------------------------------------------------------------------
#pragma once

#include "generators/MultiHoopSplitter.h"
#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QCheckBox;
class QLabel;

namespace stick {

class MultiHoopDialog : public QDialog {
    Q_OBJECT
public:
    explicit MultiHoopDialog(const QRectF& designBounds, QWidget* parent = nullptr);

    MultiHoopSplitter::Params params() const { return m_params; }

private slots:
    void onHoopPresetChanged(int idx);
    void onAccept();

private:
    QRectF                     m_bounds;
    MultiHoopSplitter::Params  m_params;

    QComboBox*      m_hoopPreset    = nullptr;
    QDoubleSpinBox* m_hoopWSpin     = nullptr;
    QDoubleSpinBox* m_hoopHSpin     = nullptr;
    QComboBox*      m_dirCombo      = nullptr;
    QDoubleSpinBox* m_overlapSpin   = nullptr;
    QCheckBox*      m_crosshairsChk = nullptr;
    QCheckBox*      m_recenterChk   = nullptr;
    QLabel*         m_statusLabel   = nullptr;
};

} // namespace stick
