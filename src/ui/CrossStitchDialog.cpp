// ---------------------------------------------------------------------------
//  StickCore  –  CrossStitchDialog.cpp
//
//  Cross-Stitch Assistant Dialog Implementation.
// ---------------------------------------------------------------------------
#include "ui/CrossStitchDialog.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QVBoxLayout>

namespace stick {

CrossStitchDialog::CrossStitchDialog(const QImage& img, QWidget* parent)
    : QDialog(parent)
    , m_source(img)
{
    setWindowTitle(QStringLiteral("Traditioneller Kreuzstich Assistent (Aida Raster)"));
    setMinimumSize(520, 420);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(10);

    auto* header = new QLabel(
        QStringLiteral("<b>Traditioneller Kreuzstich (Counted Cross-Stitch)</b><br>"
                       "Wandelt Pixelkunst, Grafiken und Fotos in echtes Zählmuster auf "
                       "Aida-Stoffbasis um mit exakt ausgerichteter Deckstich-Richtung."), this);
    header->setWordWrap(true);
    root->addWidget(header);

    auto* mainHBox = new QHBoxLayout();

    // Left: Preview
    auto* leftBox = new QVBoxLayout();
    m_preview = new QLabel(this);
    m_preview->setFixedSize(180, 180);
    m_preview->setStyleSheet(QStringLiteral("border: 1px solid #555; background: #222;"));
    m_preview->setAlignment(Qt::AlignCenter);
    if (!m_source.isNull()) {
        m_preview->setPixmap(QPixmap::fromImage(m_source.scaled(m_preview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    } else {
        m_preview->setText(QStringLiteral("Kein Bild gewählt"));
    }
    leftBox->addWidget(m_preview);

    m_browseBtn = new QPushButton(QStringLiteral("Bild wählen..."), this);
    connect(m_browseBtn, &QPushButton::clicked, this, &CrossStitchDialog::chooseImage);
    leftBox->addWidget(m_browseBtn);
    leftBox->addStretch();
    mainHBox->addLayout(leftBox);

    // Right: Controls
    auto* form = new QFormLayout();
    form->setSpacing(8);

    m_aidaCombo = new QComboBox(this);
    m_aidaCombo->addItem(CrossStitchGenerator::aidaName(CrossStitchGenerator::AidaCount::Count11), static_cast<int>(CrossStitchGenerator::AidaCount::Count11));
    m_aidaCombo->addItem(CrossStitchGenerator::aidaName(CrossStitchGenerator::AidaCount::Count14), static_cast<int>(CrossStitchGenerator::AidaCount::Count14));
    m_aidaCombo->addItem(CrossStitchGenerator::aidaName(CrossStitchGenerator::AidaCount::Count16), static_cast<int>(CrossStitchGenerator::AidaCount::Count16));
    m_aidaCombo->addItem(CrossStitchGenerator::aidaName(CrossStitchGenerator::AidaCount::Count18), static_cast<int>(CrossStitchGenerator::AidaCount::Count18));
    m_aidaCombo->addItem(CrossStitchGenerator::aidaName(CrossStitchGenerator::AidaCount::Custom), static_cast<int>(CrossStitchGenerator::AidaCount::Custom));
    m_aidaCombo->setCurrentIndex(1); // 14 ct default
    connect(m_aidaCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CrossStitchDialog::onAidaChanged);
    form->addRow(QStringLiteral("Aida Stoffdichte:"), m_aidaCombo);

    m_customMmSpin = new QDoubleSpinBox(this);
    m_customMmSpin->setRange(0.8, 5.0);
    m_customMmSpin->setValue(1.8);
    m_customMmSpin->setSuffix(QStringLiteral(" mm"));
    m_customMmSpin->setEnabled(false);
    form->addRow(QStringLiteral("Eigener Rasterabstand:"), m_customMmSpin);

    m_styleCombo = new QComboBox(this);
    m_styleCombo->addItem(QStringLiteral("Vollkreuz (Standard: / unterer, \\ oberer Deckstich)"), static_cast<int>(CrossStitchGenerator::StitchStyle::FullCross));
    m_styleCombo->addItem(QStringLiteral("Doppelkreuz / Stern (Smyrna X + + für Hochrelief)"), static_cast<int>(CrossStitchGenerator::StitchStyle::DoubleCross));
    m_styleCombo->addItem(QStringLiteral("Halbkreuz / Petit Point (nur /)"), static_cast<int>(CrossStitchGenerator::StitchStyle::HalfCross));
    form->addRow(QStringLiteral("Stich-Typ:"), m_styleCombo);

    m_colorsSpin = new QSpinBox(this);
    m_colorsSpin->setRange(1, 16);
    m_colorsSpin->setValue(6);
    form->addRow(QStringLiteral("Max. Garnfarben:"), m_colorsSpin);

    m_skipWhiteChk = new QCheckBox(QStringLiteral("Weiß / transparenten Hintergrund ignorieren"), this);
    m_skipWhiteChk->setChecked(true);
    form->addRow(QStringLiteral(""), m_skipWhiteChk);

    m_statsLabel = new QLabel(this);
    m_statsLabel->setStyleSheet(QStringLiteral("color: #88bbff; font-size: 11px;"));
    form->addRow(QStringLiteral(""), m_statsLabel);

    mainHBox->addLayout(form);
    root->addLayout(mainHBox);

    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    btnBox->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Kreuzstich digitalisieren"));
    connect(btnBox, &QDialogButtonBox::accepted, this, &CrossStitchDialog::onAccept);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(btnBox);

    onAidaChanged(m_aidaCombo->currentIndex());
}

void CrossStitchDialog::chooseImage()
{
    const QString fn = QFileDialog::getOpenFileName(this, QStringLiteral("Bild für Kreuzstich wählen"),
                                                    QString(), QStringLiteral("Bilder (*.png *.jpg *.jpeg *.bmp *.webp)"));
    if (!fn.isEmpty()) {
        m_source = QImage(fn);
        if (!m_source.isNull()) {
            m_preview->setPixmap(QPixmap::fromImage(m_source.scaled(m_preview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        }
    }
}

void CrossStitchDialog::onAidaChanged(int idx)
{
    const auto count = static_cast<CrossStitchGenerator::AidaCount>(m_aidaCombo->itemData(idx).toInt());
    m_customMmSpin->setEnabled(count == CrossStitchGenerator::AidaCount::Custom);
    const double pitch = CrossStitchGenerator::aidaToPitchMm(count, m_customMmSpin->value());
    m_statsLabel->setText(QString("Zellgröße: ~%1 mm pro Kreuz").arg(pitch, 0, 'f', 2));
}

void CrossStitchDialog::onAccept()
{
    m_params.aida = static_cast<CrossStitchGenerator::AidaCount>(m_aidaCombo->currentData().toInt());
    m_params.customMm = m_customMmSpin->value();
    m_params.style = static_cast<CrossStitchGenerator::StitchStyle>(m_styleCombo->currentData().toInt());
    m_params.maxColors = m_colorsSpin->value();
    m_params.skipWhite = m_skipWhiteChk->isChecked();
    accept();
}

} // namespace stick
