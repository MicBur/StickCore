// ---------------------------------------------------------------------------
//  StickCore  –  ImageDialog.h
//
//  A settings dialog for turning a photo into stitches. Lets the user pick a
//  mode (colours / Konterfei-portrait / line-art), adjust the reduction
//  (colours, threshold, contrast, tones, invert) and see a live preview of
//  what the machine will "see" before generating.
// ---------------------------------------------------------------------------
#pragma once

#include "generators/ImageDigitizer.h"
#include <QDialog>
#include <QImage>

class QLabel;
class QSlider;
class QSpinBox;
class QDoubleSpinBox;
class QComboBox;
class QCheckBox;
class QTimer;
class QStackedWidget;

namespace stick {

class ImageDialog : public QDialog {
    Q_OBJECT
public:
    explicit ImageDialog(const QImage& source, QWidget* parent = nullptr);

    ImageDigitizer::Params params() const { return m_params; }

signals:
    void livePreviewGenerated(const StitchSequence& previewSeq);

private slots:
    void refreshPreview();
    void updatePreviewDisplay();

private:
    void collectParams();
    QColor currentFabricColor() const;

    QImage  m_source;
    ImageDigitizer::Params m_params;
    StitchSequence m_latestPreviewSeq;
    QImage  m_latestAnalysisImg;

    QComboBox* m_mode          = nullptr;
    QComboBox* m_previewMode   = nullptr;
    QComboBox* m_fabricCombo   = nullptr;
    QLabel*    m_before        = nullptr;
    QLabel*    m_after         = nullptr;
    QLabel*    m_metricsLabel  = nullptr;

    QStackedWidget* m_stack = nullptr;

    // Colors page
    QSpinBox*       m_colors     = nullptr;
    QDoubleSpinBox* m_fillAngle  = nullptr;
    QCheckBox*      m_multiAngle = nullptr;
    QCheckBox*      m_dropBg     = nullptr;
    QCheckBox*      m_satinBorder = nullptr;
    QDoubleSpinBox* m_borderWidth = nullptr;

    // Portrait page
    QComboBox* m_portraitStyle = nullptr;
    QLabel*    m_tonesLabel = nullptr;
    QSpinBox*  m_tones    = nullptr;

    // Common adjustment controls
    QWidget*   m_threshRow  = nullptr;
    QSlider*   m_thresh   = nullptr;
    QCheckBox* m_autoThresh = nullptr;
    QSlider*   m_contrast = nullptr;
    QSlider*   m_smooth   = nullptr;
    QCheckBox* m_invert   = nullptr;
    QCheckBox* m_faceCrop = nullptr;   // OpenCV: auto-detect & crop to face
    QCheckBox* m_fineDetail = nullptr; // OpenCV: adaptive threshold (portrait)
    QSpinBox*  m_widthMm  = nullptr;

    QTimer*    m_debounce = nullptr;
};

} // namespace stick
