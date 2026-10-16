// ---------------------------------------------------------------------------
//  StickCore  –  ArcTextDialog.h
//
//  Bogen- & College-Schrift Studio (Arc Text & Varsity Studio):
//  - Interactive typography on curved baselines (ArcUp, ArcDown, Circle, Wave, Straight)
//  - Specialized Varsity & College TTF fonts (Varsity, Graduate, Jersey 10/15/25)
//  - "Paddlerin" 3D-Relief: Dual-pass padding underlay (2.4mm) + cover satin (3.5mm @ 0.34mm)
//  - Multi-Hoop Auto-Split for oversized arches with 1.0mm locked registration crosshairs
// ---------------------------------------------------------------------------
#pragma once

#include "core/StitchTypes.h"
#include "core/MachineProfile.h"
#include "generators/TextDigitizer.h"
#include "generators/MultiHoopSplitter.h"

#include <QDialog>
#include <QWidget>
#include <memory>

class QLineEdit;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QSlider;
class QCheckBox;
class QLabel;
class QPushButton;
class QGroupBox;

namespace stick {

class ArcPreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit ArcPreviewWidget(QWidget* parent = nullptr);

    void setSequence(const StitchSequence& seq, const QRectF& hoopRect, const QString& hoopName);
    void setMultiHoop(bool enabled, const std::vector<double>& cutLines, const std::vector<QPointF>& crosshairs);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    StitchSequence       m_seq;
    QRectF               m_hoopRect;
    QString              m_hoopName;
    bool                 m_multiHoopEnabled = false;
    std::vector<double>  m_cutLines;
    std::vector<QPointF> m_crosshairs;
};

class ArcTextDialog : public QDialog {
    Q_OBJECT
public:
    explicit ArcTextDialog(QWidget* parent = nullptr);
    ~ArcTextDialog() override = default;

    /// Generated stitch sequence for the current settings
    StitchSequence resultSequence() const { return m_currentSeq; }

    /// Multi-hoop result if multi-hoop was enabled
    MultiHoopSplitter::MultiResult multiHoopResult() const { return m_multiResult; }
    bool isMultiHoopActive() const;

private slots:
    void updatePreview();
    void onBaselineChanged(int idx);
    void onStyleChanged(int idx);
    void onHoopChanged(int idx);
    void onMultiHoopToggled(bool checked);
    void exportDirectJef();
    void onApplyAndClose();

private:
    void setupUi();
    void populateFonts();
    void regenerateStitches();

    // UI Widgets
    QLineEdit*          m_textEdit        = nullptr;
    QComboBox*          m_fontCombo       = nullptr;
    QCheckBox*          m_boldChk         = nullptr;
    QCheckBox*          m_italicChk       = nullptr;
    QComboBox*          m_baselineCombo   = nullptr;
    QDoubleSpinBox*     m_radiusSpin      = nullptr;
    QSlider*            m_radiusSlider    = nullptr;
    QDoubleSpinBox*     m_heightSpin      = nullptr;
    QDoubleSpinBox*     m_spacingSpin     = nullptr;
    QDoubleSpinBox*     m_slantSpin       = nullptr;

    // Stitch Style & Padding
    QComboBox*          m_styleCombo      = nullptr;
    QDoubleSpinBox*     m_paddingWidth    = nullptr;
    QDoubleSpinBox*     m_coverWidth      = nullptr;
    QDoubleSpinBox*     m_coverPitch      = nullptr;
    QDoubleSpinBox*     m_paddingPitch    = nullptr;
    QGroupBox*          m_paddingGroup    = nullptr;

    // Hoop & Multi-Hoop
    QComboBox*          m_hoopCombo       = nullptr;
    QCheckBox*          m_multiHoopChk    = nullptr;
    QSpinBox*           m_partsSpin       = nullptr;
    QDoubleSpinBox*     m_overlapSpin     = nullptr;
    QCheckBox*          m_crosshairChk    = nullptr;
    QCheckBox*          m_centerPartsChk  = nullptr;

    // Preview & Status
    ArcPreviewWidget*   m_preview         = nullptr;
    QLabel*             m_dimLabel        = nullptr;
    QLabel*             m_statusLabel     = nullptr;
    QPushButton*        m_exportBtn       = nullptr;
    QPushButton*        m_applyBtn        = nullptr;

    // State
    StitchSequence                 m_currentSeq;
    MultiHoopSplitter::MultiResult m_multiResult;
    bool                           m_updating = false;
};

} // namespace stick
