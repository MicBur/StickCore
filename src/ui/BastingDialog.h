// ---------------------------------------------------------------------------
//  StickCore  –  BastingDialog.h
//
//  Heftrahmen / Basting Box Assistant Dialog:
//  Configures automatic basting perimeter stitches around motif or hoop.
// ---------------------------------------------------------------------------
#pragma once

#include "generators/BastingGenerator.h"
#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QCheckBox;

namespace stick {

class BastingDialog : public QDialog {
    Q_OBJECT
public:
    explicit BastingDialog(QWidget* parent = nullptr);

    BastingGenerator::Params params() const { return m_params; }

private slots:
    void onAccept();

private:
    BastingGenerator::Params m_params;
    QComboBox*      m_targetCombo   = nullptr;
    QDoubleSpinBox* m_marginSpin    = nullptr;
    QDoubleSpinBox* m_stitchLenSpin = nullptr;
    QCheckBox*      m_doublePassChk = nullptr;
};

} // namespace stick
