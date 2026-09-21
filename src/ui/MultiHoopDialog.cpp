// ---------------------------------------------------------------------------
//  StickCore  –  MultiHoopDialog.cpp
//
//  Multi-Hooping & Auto-Split Dialog Implementation.
// ---------------------------------------------------------------------------
#include "ui/MultiHoopDialog.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace stick {

MultiHoopDialog::MultiHoopDialog(const QRectF& designBounds, QWidget* parent)
    : QDialog(parent)
    , m_bounds(designBounds)
{
    setWindowTitle(QStringLiteral("Mehrfach-Rahmung (Auto-Split & Multi-Hooping)"));
    setMinimumWidth(440);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(10);

    auto* header = new QLabel(
        QStringLiteral("<b>Muster aufteilen für große Stickprojekte</b><br>"
                       "Teilt Stickmuster, die den physischen Stickrahmen überschreiten, "
                       "automatisch in zwei Teilmuster (Rahmen 1 & Rahmen 2) auf und erzeugt "
                       "präzise Passkreuze (Registrierstiche) zur exakten Neupositionierung."), this);
    header->setWordWrap(true);
    root->addWidget(header);

    auto* form = new QFormLayout();
    form->setSpacing(8);

    m_hoopPreset = new QComboBox(this);
    m_hoopPreset->addItem(QStringLiteral("Janome Rahmen B (140 x 200 mm) - Standard"), QSizeF(140, 200));
    m_hoopPreset->addItem(QStringLiteral("Janome Rahmen A (126 x 110 mm)"), QSizeF(126, 110));
    m_hoopPreset->addItem(QStringLiteral("Freiarm-Rahmen C (50 x 50 mm)"), QSizeF(50, 50));
    m_hoopPreset->addItem(QStringLiteral("Quadratisch SQ14 (140 x 140 mm)"), QSizeF(140, 140));
    m_hoopPreset->addItem(QStringLiteral("Großrahmen (200 x 280 mm)"), QSizeF(200, 280));
    m_hoopPreset->addItem(QStringLiteral("Benutzerdefiniert..."), QSizeF(0, 0));
    connect(m_hoopPreset, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MultiHoopDialog::onHoopPresetChanged);
    form->addRow(QStringLiteral("Ziel-Stickrahmen:"), m_hoopPreset);

    m_hoopWSpin = new QDoubleSpinBox(this);
    m_hoopWSpin->setRange(20.0, 500.0);
    m_hoopWSpin->setValue(140.0);
    m_hoopWSpin->setSuffix(QStringLiteral(" mm"));
    m_hoopWSpin->setEnabled(false);
    form->addRow(QStringLiteral("Rahmenbreite:"), m_hoopWSpin);

    m_hoopHSpin = new QDoubleSpinBox(this);
    m_hoopHSpin->setRange(20.0, 500.0);
    m_hoopHSpin->setValue(200.0);
    m_hoopHSpin->setSuffix(QStringLiteral(" mm"));
    m_hoopHSpin->setEnabled(false);
    form->addRow(QStringLiteral("Rahmenhöhe:"), m_hoopHSpin);

    m_dirCombo = new QComboBox(this);
    m_dirCombo->addItem(QStringLiteral("Automatisch (entlang der längsten Seite)"), static_cast<int>(MultiHoopSplitter::SplitDirection::Auto));
    m_dirCombo->addItem(QStringLiteral("Horizontal (Obere & untere Rahmenhälfte)"), static_cast<int>(MultiHoopSplitter::SplitDirection::Horizontal));
    m_dirCombo->addItem(QStringLiteral("Vertikal (Linke & rechte Rahmenhälfte)"), static_cast<int>(MultiHoopSplitter::SplitDirection::Vertical));
    form->addRow(QStringLiteral("Schnittrichtung:"), m_dirCombo);

    m_overlapSpin = new QDoubleSpinBox(this);
    m_overlapSpin->setRange(2.0, 30.0);
    m_overlapSpin->setValue(8.0);
    m_overlapSpin->setSuffix(QStringLiteral(" mm"));
    form->addRow(QStringLiteral("Überlappung / Nahtzugabe:"), m_overlapSpin);

    m_crosshairsChk = new QCheckBox(QStringLiteral("Passkreuze (+) für Neupositionierung einfügen"), this);
    m_crosshairsChk->setChecked(true);
    form->addRow(QStringLiteral(""), m_crosshairsChk);

    m_recenterChk = new QCheckBox(QStringLiteral("Beide Teilmuster auf Rahmenzentrum (0,0) zentrieren"), this);
    m_recenterChk->setChecked(false);
    form->addRow(QStringLiteral(""), m_recenterChk);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet(QStringLiteral("color: #88bbff; font-size: 11px;"));
    form->addRow(QStringLiteral(""), m_statusLabel);

    root->addLayout(form);

    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    btnBox->button(QDialogButtonBox::Ok)->setText(QStringLiteral("In 2 Rahmen teilen"));
    connect(btnBox, &QDialogButtonBox::accepted, this, &MultiHoopDialog::onAccept);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(btnBox);

    // Initial label check
    const QString motifInfo = QString("Aktuelle Motivgröße: %1 x %2 mm")
                                  .arg(m_bounds.width(), 0, 'f', 1)
                                  .arg(m_bounds.height(), 0, 'f', 1);
    m_statusLabel->setText(motifInfo);
}

void MultiHoopDialog::onHoopPresetChanged(int idx)
{
    const QSizeF sz = m_hoopPreset->itemData(idx).toSizeF();
    const bool isCustom = (sz.width() <= 0.0);
    m_hoopWSpin->setEnabled(isCustom);
    m_hoopHSpin->setEnabled(isCustom);
    if (!isCustom) {
        m_hoopWSpin->setValue(sz.width());
        m_hoopHSpin->setValue(sz.height());
    }
}

void MultiHoopDialog::onAccept()
{
    m_params.hoopWidthMm = m_hoopWSpin->value();
    m_params.hoopHeightMm = m_hoopHSpin->value();
    m_params.direction = static_cast<MultiHoopSplitter::SplitDirection>(m_dirCombo->currentData().toInt());
    m_params.overlapMm = m_overlapSpin->value();
    m_params.addCrosshairs = m_crosshairsChk->isChecked();
    m_params.centerEachHoop = m_recenterChk->isChecked();
    accept();
}

} // namespace stick
