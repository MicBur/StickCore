// ---------------------------------------------------------------------------
//  StickCore  –  ImageDialog.h
//
//  Comprehensive High-End Image Digitizer Dialog:
//  5 digitizing modes (Vector-Tatami, Sfumato Photo-Stitch, Counted Cross-Stitch,
//  LineArt Bean Stitch, Konterfei Portrait), interactive palette manager,
//  underlay/pull compensation controls, and real-time live preview.
// ---------------------------------------------------------------------------
#pragma once

#include "generators/ImageDigitizer.h"
#include "editor/BezierPath.h"
#include <QDialog>
#include <QImage>
#include <vector>

class QLabel;
class QSlider;
class QSpinBox;
class QDoubleSpinBox;
class QComboBox;
class QCheckBox;
class QTimer;
class QStackedWidget;
class QTableWidget;
class QPushButton;

namespace stick {

class ImageDialog : public QDialog {
    Q_OBJECT
public:
    enum class ApplyMode { Stitches, VectorPaths };

    explicit ImageDialog(const QImage& source, QWidget* parent = nullptr);

    ImageDigitizer::Params params() const { return m_params; }
    ApplyMode applyMode() const { return m_applyMode; }
    QVector<EditPath> extractedPaths() const { return m_extractedPaths; }

signals:
    void livePreviewGenerated(const StitchSequence& previewSeq);

private slots:
    void refreshPreview();
    void updatePreviewDisplay();
    void onPaletteItemChanged(int row, int column);
    void autoDropBackground();
    void selectAllColors(bool checked);

private:
    void collectParams();
    void rebuildPaletteTable();
    QColor currentFabricColor() const;

    QImage  m_source;
    ImageDigitizer::Params m_params;
    StitchSequence m_latestPreviewSeq;
    QImage  m_latestAnalysisImg;
    std::vector<ImageDigitizer::QuantizedColor> m_currentPalette;
    bool    m_updatingPaletteUi = false;

    ApplyMode m_applyMode = ApplyMode::Stitches;
    QVector<EditPath> m_extractedPaths;

    QComboBox* m_mode          = nullptr;
    QComboBox* m_previewMode   = nullptr;
    QComboBox* m_fabricCombo   = nullptr;
    QLabel*    m_before        = nullptr;
    QLabel*    m_after         = nullptr;
    QLabel*    m_metricsLabel  = nullptr;

    QStackedWidget* m_stack    = nullptr;

    // Page 0 – Colors (Vector-Tatami & Inkscape Bitmap to Path)
    QComboBox*      m_separation    = nullptr;
    QSpinBox*       m_colors        = nullptr;
    QLabel*         m_colorHintLabel= nullptr;
    QLabel*         m_stencilLabel  = nullptr;
    QSlider*        m_stencilSlider = nullptr;
    QComboBox*      m_tatamiPattern = nullptr;
    QDoubleSpinBox* m_densityMm     = nullptr;
    QDoubleSpinBox* m_fillAngle     = nullptr;
    QCheckBox*      m_multiAngle    = nullptr;
    QCheckBox*      m_underlay      = nullptr;
    QDoubleSpinBox* m_pullComp      = nullptr;
    QDoubleSpinBox* m_smoothing     = nullptr;
    QCheckBox*      m_satinBorder   = nullptr;
    QDoubleSpinBox* m_borderWidth   = nullptr;

    // Palette Manager
    QWidget*        m_paletteContainer = nullptr;
    QTableWidget*   m_paletteTable     = nullptr;
    QPushButton*    m_btnDropBg        = nullptr;
    QPushButton*    m_btnAllColors     = nullptr;

    // Page 1 – Sfumato Photo-Stitch
    QDoubleSpinBox* m_sfumatoLineSpacing = nullptr;
    QDoubleSpinBox* m_sfumatoAmplitude   = nullptr;
    QComboBox*      m_sfumatoAngle       = nullptr;
    QPushButton*    m_sfumatoColorBtn    = nullptr;

    // Page 2 – Cross-Stitch
    QComboBox*      m_crossAida          = nullptr;
    QComboBox*      m_crossStyle         = nullptr;
    QSpinBox*       m_crossColors        = nullptr;

    // Page 3 – LineArt
    QComboBox*      m_lineArtStitch      = nullptr;
    QDoubleSpinBox* m_runMm              = nullptr;
    QSlider*        m_edgeThreshold      = nullptr;

    // Page 4 – Portrait
    QComboBox*      m_portraitStyle      = nullptr;
    QLabel*         m_tonesLabel         = nullptr;
    QSpinBox*       m_tones              = nullptr;

    // Common adjustment controls
    QSlider*        m_contrast           = nullptr;
    QSlider*        m_brightness         = nullptr;
    QSlider*        m_smooth             = nullptr;
    QCheckBox*      m_invert             = nullptr;
    QComboBox*      m_resolution         = nullptr;
    QSpinBox*       m_widthMm            = nullptr;
    QCheckBox*      m_faceCrop           = nullptr;

    QTimer*         m_debounce           = nullptr;
};

} // namespace stick
