// ---------------------------------------------------------------------------
//  StickCore  –  SfumatoDialog.cpp
//
//  Sfumato Photo-Stitch Assistant Dialog Implementation.
// ---------------------------------------------------------------------------
#include "ui/SfumatoDialog.h"
#include "library/StitchThumbnail.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QVBoxLayout>

namespace stick {

SfumatoDialog::SfumatoDialog(const QImage& img, QWidget* parent)
    : QDialog(parent)
    , m_source(img)
{
    setWindowTitle(QStringLiteral("Sfumato Photo-Stitch Assistent (Embird-Stil)"));
    setMinimumSize(540, 500);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(10);

    auto* header = new QLabel(
        QStringLiteral("<b>Sfumato Photo-Stitch</b> erzeugt fotorealistische Stickerei durch "
                       "wellenförmige, dichte-modulierte Mäanderlinien ohne grobe Farbgrenzen."), this);
    header->setWordWrap(true);
    root->addWidget(header);

    auto* mainHBox = new QHBoxLayout();

    // Left: Preview & Browse
    auto* leftBox = new QVBoxLayout();
    m_imgPreview = new QLabel(this);
    m_imgPreview->setFixedSize(200, 200);
    m_imgPreview->setStyleSheet(QStringLiteral("border: 1px solid #555; background: #222;"));
    m_imgPreview->setAlignment(Qt::AlignCenter);
    leftBox->addWidget(m_imgPreview);

    m_browseBtn = new QPushButton(QStringLiteral("Bild wählen..."), this);
    connect(m_browseBtn, &QPushButton::clicked, this, &SfumatoDialog::chooseImage);
    leftBox->addWidget(m_browseBtn);

    m_colorBtn = new QPushButton(QStringLiteral("Garnfarbe"), this);
    m_colorBtn->setStyleSheet(QStringLiteral("background-color: #222222; color: #ffffff; font-weight: bold;"));
    connect(m_colorBtn, &QPushButton::clicked, this, &SfumatoDialog::chooseColor);
    leftBox->addWidget(m_colorBtn);
    leftBox->addStretch();

    mainHBox->addLayout(leftBox);

    // Right: Parameters
    auto* form = new QFormLayout();
    form->setSpacing(6);

    m_widthSpin = new QDoubleSpinBox(this);
    m_widthSpin->setRange(20.0, 300.0);
    m_widthSpin->setValue(m_params.widthMm);
    m_widthSpin->setSuffix(QStringLiteral(" mm"));
    form->addRow(QStringLiteral("Breite:"), m_widthSpin);

    m_heightSpin = new QDoubleSpinBox(this);
    m_heightSpin->setRange(20.0, 300.0);
    m_heightSpin->setValue(m_params.heightMm);
    m_heightSpin->setSuffix(QStringLiteral(" mm"));
    form->addRow(QStringLiteral("Höhe:"), m_heightSpin);

    m_spacingSpin = new QDoubleSpinBox(this);
    m_spacingSpin->setRange(0.6, 3.5);
    m_spacingSpin->setSingleStep(0.1);
    m_spacingSpin->setValue(m_params.lineSpacingMm);
    m_spacingSpin->setSuffix(QStringLiteral(" mm"));
    form->addRow(QStringLiteral("Zeilenabstand:"), m_spacingSpin);

    m_amplitudeSpin = new QDoubleSpinBox(this);
    m_amplitudeSpin->setRange(0.2, 2.5);
    m_amplitudeSpin->setSingleStep(0.1);
    m_amplitudeSpin->setValue(m_params.maxAmplitudeMm);
    m_amplitudeSpin->setSuffix(QStringLiteral(" mm"));
    form->addRow(QStringLiteral("Wellen-Amplitude:"), m_amplitudeSpin);

    m_contrastSlider = new QSlider(Qt::Horizontal, this);
    m_contrastSlider->setRange(50, 200);
    m_contrastSlider->setValue(115);
    connect(m_contrastSlider, &QSlider::valueChanged, this, &SfumatoDialog::refreshPreview);
    form->addRow(QStringLiteral("Kontrast:"), m_contrastSlider);

    m_gammaSlider = new QSlider(Qt::Horizontal, this);
    m_gammaSlider->setRange(50, 200);
    m_gammaSlider->setValue(100);
    connect(m_gammaSlider, &QSlider::valueChanged, this, &SfumatoDialog::refreshPreview);
    form->addRow(QStringLiteral("Gamma:"), m_gammaSlider);

    m_invertChk = new QCheckBox(QStringLiteral("Helles Garn auf dunklem Stoff (Invertieren)"), this);
    connect(m_invertChk, &QCheckBox::toggled, this, &SfumatoDialog::refreshPreview);
    form->addRow(QStringLiteral(""), m_invertChk);

    m_angleCombo = new QComboBox(this);
    m_angleCombo->addItem(QStringLiteral("Horizontal (0° Zeilen)"), static_cast<int>(SfumatoGenerator::AngleMode::Horizontal));
    m_angleCombo->addItem(QStringLiteral("Vertikal (90° Spalten)"), static_cast<int>(SfumatoGenerator::AngleMode::Vertical));
    m_angleCombo->addItem(QStringLiteral("Kreuzschraffur (0° + 90° Doppelpass)"), static_cast<int>(SfumatoGenerator::AngleMode::CrossHatch));
    connect(m_angleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SfumatoDialog::refreshPreview);
    connect(m_spacingSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SfumatoDialog::refreshPreview);
    connect(m_amplitudeSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SfumatoDialog::refreshPreview);
    connect(m_widthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SfumatoDialog::refreshPreview);
    connect(m_heightSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SfumatoDialog::refreshPreview);
    form->addRow(QStringLiteral("Stichrichtung:"), m_angleCombo);

    mainHBox->addLayout(form);
    root->addLayout(mainHBox);

    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    btnBox->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Sfumato Muster berechnen"));
    connect(btnBox, &QDialogButtonBox::accepted, this, &SfumatoDialog::onAccept);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(btnBox);

    refreshPreview();
}

void SfumatoDialog::chooseImage()
{
    const QString fn = QFileDialog::getOpenFileName(this, QStringLiteral("Bild für Sfumato laden"),
                                                    QString(), QStringLiteral("Bilder (*.png *.jpg *.jpeg *.bmp *.webp)"));
    if (!fn.isEmpty()) {
        m_source = QImage(fn);
        if (!m_source.isNull()) {
            // Adapt default aspect ratio
            const double aspect = static_cast<double>(m_source.height()) / std::max(1, m_source.width());
            m_heightSpin->setValue(std::round(m_widthSpin->value() * aspect));
            refreshPreview();
        }
    }
}

void SfumatoDialog::chooseColor()
{
    const QColor col = QColorDialog::getColor(m_params.threadColor, this, QStringLiteral("Garnfarbe wählen"));
    if (col.isValid()) {
        m_params.threadColor = col;
        m_colorBtn->setStyleSheet(QString("background-color: %1; color: %2; font-weight: bold;")
                                      .arg(col.name())
                                      .arg(col.lightness() > 128 ? "#000000" : "#ffffff"));
        refreshPreview();
    }
}

void SfumatoDialog::refreshPreview()
{
    if (m_source.isNull()) {
        m_imgPreview->setText(QStringLiteral("Kein Bild geladen"));
        return;
    }

    SfumatoGenerator::Params p;
    p.widthMm = m_widthSpin->value();
    p.heightMm = m_heightSpin->value();
    p.lineSpacingMm = m_spacingSpin->value();
    p.maxAmplitudeMm = m_amplitudeSpin->value();
    p.contrast = m_contrastSlider->value() / 100.0;
    p.gamma = m_gammaSlider->value() / 100.0;
    p.invertLuminance = m_invertChk->isChecked();
    p.angleMode = static_cast<SfumatoGenerator::AngleMode>(m_angleCombo->currentData().toInt());
    p.threadColor = m_params.threadColor;

    // Fast stitch preview
    SfumatoGenerator::Params fastP = p;
    fastP.lineSpacingMm = std::max(p.lineSpacingMm, 1.2);
    StitchSequence seq = SfumatoGenerator::generate(m_source, fastP);
    if (!seq.empty()) {
        const QColor fabric = p.invertLuminance ? QColor(0x14, 0x17, 0x1C) : QColor(0xEF, 0xEA, 0xDE);
        QImage thumb = StitchThumbnail::render(seq, m_imgPreview->width(), fabric, true);
        m_imgPreview->setPixmap(QPixmap::fromImage(thumb));
    } else {
        QImage prep = SfumatoGenerator::preprocessImage(m_source, p);
        if (!prep.isNull()) {
            m_imgPreview->setPixmap(QPixmap::fromImage(prep.scaled(m_imgPreview->size(),
                                                                   Qt::KeepAspectRatio,
                                                                   Qt::SmoothTransformation)));
        }
    }
}

void SfumatoDialog::onAccept()
{
    m_params.widthMm = m_widthSpin->value();
    m_params.heightMm = m_heightSpin->value();
    m_params.lineSpacingMm = m_spacingSpin->value();
    m_params.maxAmplitudeMm = m_amplitudeSpin->value();
    m_params.contrast = m_contrastSlider->value() / 100.0;
    m_params.gamma = m_gammaSlider->value() / 100.0;
    m_params.invertLuminance = m_invertChk->isChecked();
    m_params.angleMode = static_cast<SfumatoGenerator::AngleMode>(m_angleCombo->currentData().toInt());
    accept();
}

} // namespace stick
