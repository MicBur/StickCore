// ---------------------------------------------------------------------------
//  StickCore  –  SfumatoDialog.h
//
//  Sfumato Photo-Stitch Assistant Dialog:
//  Interactive setup for photorealistic continuous-stipple embroidery.
// ---------------------------------------------------------------------------
#pragma once

#include "generators/SfumatoGenerator.h"
#include <QDialog>
#include <QImage>

class QLabel;
class QSlider;
class QDoubleSpinBox;
class QComboBox;
class QCheckBox;
class QPushButton;

namespace stick {

class SfumatoDialog : public QDialog {
    Q_OBJECT
public:
    explicit SfumatoDialog(const QImage& img, QWidget* parent = nullptr);

    SfumatoGenerator::Params params() const { return m_params; }
    QImage sourceImage() const { return m_source; }

private slots:
    void refreshPreview();
    void chooseImage();
    void chooseColor();
    void onAccept();

private:
    QImage                   m_source;
    SfumatoGenerator::Params m_params;

    QLabel*         m_imgPreview     = nullptr;
    QPushButton*    m_browseBtn      = nullptr;
    QDoubleSpinBox* m_widthSpin      = nullptr;
    QDoubleSpinBox* m_heightSpin     = nullptr;
    QDoubleSpinBox* m_spacingSpin    = nullptr;
    QDoubleSpinBox* m_amplitudeSpin  = nullptr;
    QSlider*        m_contrastSlider = nullptr;
    QSlider*        m_gammaSlider    = nullptr;
    QCheckBox*      m_invertChk      = nullptr;
    QComboBox*      m_angleCombo     = nullptr;
    QPushButton*    m_colorBtn       = nullptr;
};

} // namespace stick
