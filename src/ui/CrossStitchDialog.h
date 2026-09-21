// ---------------------------------------------------------------------------
//  StickCore  –  CrossStitchDialog.h
//
//  Cross-Stitch Assistant Dialog:
//  Configures authentic counted cross-stitch conversion from images.
// ---------------------------------------------------------------------------
#pragma once

#include "generators/CrossStitchGenerator.h"
#include <QDialog>
#include <QImage>

class QLabel;
class QSpinBox;
class QDoubleSpinBox;
class QComboBox;
class QCheckBox;
class QPushButton;

namespace stick {

class CrossStitchDialog : public QDialog {
    Q_OBJECT
public:
    explicit CrossStitchDialog(const QImage& img, QWidget* parent = nullptr);

    CrossStitchGenerator::Params params() const { return m_params; }
    QImage sourceImage() const { return m_source; }

private slots:
    void chooseImage();
    void onAidaChanged(int idx);
    void onAccept();

private:
    QImage                       m_source;
    CrossStitchGenerator::Params m_params;

    QLabel*         m_preview       = nullptr;
    QPushButton*    m_browseBtn     = nullptr;
    QComboBox*      m_aidaCombo     = nullptr;
    QDoubleSpinBox* m_customMmSpin  = nullptr;
    QComboBox*      m_styleCombo    = nullptr;
    QSpinBox*       m_colorsSpin    = nullptr;
    QCheckBox*      m_skipWhiteChk  = nullptr;
    QLabel*         m_statsLabel    = nullptr;
};

} // namespace stick
