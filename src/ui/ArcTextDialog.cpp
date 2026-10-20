// ---------------------------------------------------------------------------
//  StickCore  –  ArcTextDialog.cpp
//
//  Bogen- & College-Schrift Studio (Arc Text & Varsity Studio) Implementation.
// ---------------------------------------------------------------------------
#include "ui/ArcTextDialog.h"
#include "codec/JefCodec.h"
#include "core/ThreadCatalog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QSlider>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QScrollArea>
#include <QPainter>
#include <QFontDatabase>
#include <QDir>
#include <cmath>

namespace stick {

// ---------------------------------------------------------------------------
// Helper: Ensure custom TTF application fonts are registered in QFontDatabase
// ---------------------------------------------------------------------------
static void ensureStudioFonts()
{
    static bool loaded = false;
    if (loaded) return;
    loaded = true;

    const QStringList fontResources = {
        QStringLiteral(":/fonts/varsity_regular.ttf"),
        QStringLiteral(":/fonts/Graduate-Regular.ttf"),
        QStringLiteral(":/fonts/Jersey10-Regular.ttf"),
        QStringLiteral(":/fonts/Jersey15-Regular.ttf"),
        QStringLiteral(":/fonts/Jersey25-Regular.ttf"),
        QStringLiteral(":/fonts/GreatVibes-Regular.ttf"),
        QStringLiteral(":/fonts/Tangerine-Bold.ttf"),
        QStringLiteral(":/fonts/PinyonScript-Regular.ttf"),
        QStringLiteral(":/fonts/Parisienne-Regular.ttf"),
        QStringLiteral(":/fonts/Allura-Regular.ttf"),
        QStringLiteral(":/fonts/PetitFormalScript-Regular.ttf"),
        QStringLiteral(":/fonts/HerrVonMuellerhoff-Regular.ttf"),
        QStringLiteral(":/fonts/AlexBrush-Regular.ttf")
    };

    for (const auto& path : fontResources) {
        if (QFile::exists(path)) {
            QFontDatabase::addApplicationFont(path);
        }
    }
}

// ---------------------------------------------------------------------------
// ArcPreviewWidget Implementation
// ---------------------------------------------------------------------------
ArcPreviewWidget::ArcPreviewWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(460, 380);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void ArcPreviewWidget::setSequence(const StitchSequence& seq, const QRectF& hoopRect, const QString& hoopName)
{
    m_seq = seq;
    m_hoopRect = hoopRect;
    m_hoopName = hoopName;
    update();
}

void ArcPreviewWidget::setMultiHoop(bool enabled, const std::vector<double>& cutLines, const std::vector<QPointF>& crosshairs)
{
    m_multiHoopEnabled = enabled;
    m_cutLines = cutLines;
    m_crosshairs = crosshairs;
    update();
}

void ArcPreviewWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Dark canvas background
    p.fillRect(rect(), QColor(20, 22, 28));

    const QRectF bb = m_seq.boundingRect();
    QRectF viewBounds = m_hoopRect;
    if (!bb.isEmpty()) {
        viewBounds = viewBounds.united(bb);
    }
    // Add margin around bounding box
    const double pad = std::max(15.0, std::max(viewBounds.width(), viewBounds.height()) * 0.12);
    viewBounds.adjust(-pad, -pad, pad, pad);

    const double sx = (width() - 20) / std::max(10.0, viewBounds.width());
    const double sy = (height() - 20) / std::max(10.0, viewBounds.height());
    const double s  = std::min(sx, sy);

    const double cx = width() * 0.5;
    const double cy = height() * 0.5;
    const double vcx = viewBounds.center().x();
    const double vcy = viewBounds.center().y();

    auto toScreen = [&](double mmX, double mmY) -> QPointF {
        return QPointF(cx + (mmX - vcx) * s, cy - (mmY - vcy) * s);
    };

    // Draw coordinate axes
    const QPointF originScreen = toScreen(0, 0);
    p.setPen(QPen(QColor(45, 52, 65), 1.0, Qt::DashLine));
    p.drawLine(QPointF(0, originScreen.y()), QPointF(width(), originScreen.y()));
    p.drawLine(QPointF(originScreen.x(), 0), QPointF(originScreen.x(), height()));

    // Draw physical hoop boundary
    const QPointF hTopLeft = toScreen(m_hoopRect.left(), m_hoopRect.top());
    const QPointF hBottomRight = toScreen(m_hoopRect.right(), m_hoopRect.bottom());
    const QRectF hoopScreenRect(hTopLeft, hBottomRight);

    const bool exceedsHoop = (!bb.isEmpty() && (bb.width() > m_hoopRect.width() || bb.height() > m_hoopRect.height()));

    p.setPen(QPen(exceedsHoop ? QColor(230, 70, 70, 220) : QColor(45, 212, 191, 160), 2.0));
    p.setBrush(QColor(25, 30, 40, 100));
    p.drawRoundedRect(hoopScreenRect.normalized(), 8.0, 8.0);

    // Hoop dimension label
    p.setPen(exceedsHoop ? QColor(255, 100, 100) : QColor(140, 155, 175));
    p.setFont(QFont(QStringLiteral("Segoe UI"), 9));
    const QString hLabel = QString("%1 (%2 x %3 mm)%4")
                               .arg(m_hoopName)
                               .arg(m_hoopRect.width(), 0, 'f', 0)
                               .arg(m_hoopRect.height(), 0, 'f', 0)
                               .arg(exceedsHoop ? QStringLiteral(" [ÜBERSCHRITTEN]") : QString());
    p.drawText(hoopScreenRect.left() + 8, hoopScreenRect.top() + 18, hLabel);

    // Draw Multi-Hoop cut lines if active
    if (m_multiHoopEnabled) {
        p.setPen(QPen(QColor(255, 170, 0, 220), 1.5, Qt::DashDotLine));
        for (double cutX : m_cutLines) {
            const QPointF pTop = toScreen(cutX, viewBounds.top());
            const QPointF pBot = toScreen(cutX, viewBounds.bottom());
            p.drawLine(pTop, pBot);
        }

        // Draw registration crosshairs (+)
        for (const QPointF& ch : m_crosshairs) {
            const QPointF chScr = toScreen(ch.x(), ch.y());
            p.setPen(QPen(QColor(255, 60, 60), 2.0));
            const double r = 7.0;
            p.drawLine(QPointF(chScr.x() - r, chScr.y()), QPointF(chScr.x() + r, chScr.y()));
            p.drawLine(QPointF(chScr.x(), chScr.y() - r), QPointF(chScr.x(), chScr.y() + r));
            p.setPen(QPen(QColor(255, 60, 60, 160), 1.0));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(chScr, r + 2.0, r + 2.0);
        }
    }

    // Draw stitches
    if (m_seq.stitches.size() >= 2) {
        QColor curColor(220, 220, 220);
        p.setPen(QPen(curColor, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

        QPointF lastPt;
        bool hasLast = false;

        for (const Stitch& st : m_seq.stitches) {
            const QPointF pt = toScreen(st.x, st.y);
            if (st.flags & (SF_Jump | SF_Trim)) {
                hasLast = false;
            } else {
                if (hasLast) {
                    if (st.colorIdx >= 0 && static_cast<size_t>(st.colorIdx) < m_seq.palette.size()) {
                        curColor = m_seq.palette[st.colorIdx].color;
                        p.setPen(QPen(curColor, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                    }
                    p.drawLine(lastPt, pt);
                }
            }
            lastPt = pt;
            hasLast = true;
        }
    }

    // Bottom info badge
    if (!bb.isEmpty()) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(15, 17, 22, 210));
        p.drawRoundedRect(QRectF(10, height() - 32, width() - 20, 24), 6, 6);

        p.setPen(QColor(220, 230, 240));
        p.setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::DemiBold));
        const QString badgeText = QString("Breite: %1 mm  •  Höhe: %2 mm  •  %3 Stiche")
                                      .arg(bb.width(), 0, 'f', 1)
                                      .arg(bb.height(), 0, 'f', 1)
                                      .arg(m_seq.stitches.size());
        p.drawText(QRectF(14, height() - 32, width() - 28, 24), Qt::AlignVCenter | Qt::AlignLeft, badgeText);
    }
}

// ---------------------------------------------------------------------------
// ArcTextDialog Implementation
// ---------------------------------------------------------------------------
ArcTextDialog::ArcTextDialog(QWidget* parent)
    : QDialog(parent)
{
    ensureStudioFonts();
    setWindowTitle(QStringLiteral("Bogen- & College-Schrift Studio (Arc & Varsity Studio)"));
    resize(980, 620);
    setMinimumSize(780, 500);

    setupUi();
    populateFonts();

    // Default values for Varsity arch
    m_textEdit->setText(QStringLiteral("BERUFSFEUERWEHR"));
    m_baselineCombo->setCurrentIndex(1); // ArcUp
    m_radiusSpin->setValue(160.0);
    m_heightSpin->setValue(28.0);
    m_spacingSpin->setValue(1.5);
    m_styleCombo->setCurrentIndex(0); // 3D-Relief Polsterraupe (Paddlerin)

    regenerateStitches();
}

void ArcTextDialog::setupUi()
{
    auto* mainHBox = new QHBoxLayout(this);
    mainHBox->setSpacing(12);
    mainHBox->setContentsMargins(12, 12, 12, 12);

    // Left Column: Scrollable Controls for any laptop resolution
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setFixedWidth(460);

    auto* leftContainer = new QWidget(scroll);
    auto* leftVBox = new QVBoxLayout(leftContainer);
    leftVBox->setSpacing(10);
    leftVBox->setContentsMargins(0, 0, 8, 0);

    // Group 1: Text & Schriftart
    auto* gText = new QGroupBox(QStringLiteral("1. Text & College-Schriftart"), leftContainer);
    auto* fText = new QFormLayout(gText);
    fText->setSpacing(8);

    m_textEdit = new QLineEdit(gText);
    m_textEdit->setPlaceholderText(QStringLiteral("Schriftzug eingeben (z.B. BERUFSFEUERWEHR)..."));
    connect(m_textEdit, &QLineEdit::textChanged, this, &ArcTextDialog::updatePreview);
    fText->addRow(QStringLiteral("<b>Text:</b>"), m_textEdit);

    m_fontCombo = new QComboBox(gText);
    connect(m_fontCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ArcTextDialog::updatePreview);
    fText->addRow(QStringLiteral("Schriftart:"), m_fontCombo);

    auto* styleHBox = new QHBoxLayout();
    m_boldChk = new QCheckBox(QStringLiteral("Fett (Bold)"), gText);
    m_boldChk->setChecked(true);
    connect(m_boldChk, &QCheckBox::toggled, this, &ArcTextDialog::updatePreview);
    styleHBox->addWidget(m_boldChk);

    m_italicChk = new QCheckBox(QStringLiteral("Kursiv (Italic)"), gText);
    connect(m_italicChk, &QCheckBox::toggled, this, &ArcTextDialog::updatePreview);
    styleHBox->addWidget(m_italicChk);
    fText->addRow(QStringLiteral("Schnitt:"), styleHBox);

    leftVBox->addWidget(gText);

    // Group 2: Bogen-Geometrie & Krümmung
    auto* gArc = new QGroupBox(QStringLiteral("2. Bogen-Geometrie & Krümmung"), leftContainer);
    auto* fArc = new QFormLayout(gArc);
    fArc->setSpacing(8);

    m_baselineCombo = new QComboBox(gArc);
    m_baselineCombo->addItem(QStringLiteral("━ Gerade (Horizontal)"), int(TextDigitizer::Baseline::Straight));
    m_baselineCombo->addItem(QStringLiteral("⌒ Bogen Oben (Arc Up / Konvex)"), int(TextDigitizer::Baseline::ArcUp));
    m_baselineCombo->addItem(QStringLiteral("⌣ Bogen Unten (Arc Down / Konkav)"), int(TextDigitizer::Baseline::ArcDown));
    m_baselineCombo->addItem(QStringLiteral("〰 Welle (Wave / S-Kurve)"), int(TextDigitizer::Baseline::Wave));
    m_baselineCombo->addItem(QStringLiteral("⭕ Vollkreis (Circle 360°)"), int(TextDigitizer::Baseline::Circle));
    connect(m_baselineCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ArcTextDialog::onBaselineChanged);
    fArc->addRow(QStringLiteral("Bogenform:"), m_baselineCombo);

    auto* radHBox = new QHBoxLayout();
    m_radiusSpin = new QDoubleSpinBox(gArc);
    m_radiusSpin->setRange(20.0, 500.0);
    m_radiusSpin->setValue(160.0);
    m_radiusSpin->setSuffix(QStringLiteral(" mm"));
    connect(m_radiusSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double val){
        m_radiusSlider->blockSignals(true);
        m_radiusSlider->setValue(static_cast<int>(val));
        m_radiusSlider->blockSignals(false);
        updatePreview();
    });
    radHBox->addWidget(m_radiusSpin);

    m_radiusSlider = new QSlider(Qt::Horizontal, gArc);
    m_radiusSlider->setRange(20, 500);
    m_radiusSlider->setValue(160);
    connect(m_radiusSlider, &QSlider::valueChanged, this, [this](int val){
        m_radiusSpin->blockSignals(true);
        m_radiusSpin->setValue(val);
        m_radiusSpin->blockSignals(false);
        updatePreview();
    });
    radHBox->addWidget(m_radiusSlider);
    fArc->addRow(QStringLiteral("Bogenradius:"), radHBox);

    m_heightSpin = new QDoubleSpinBox(gArc);
    m_heightSpin->setRange(8.0, 150.0);
    m_heightSpin->setValue(28.0);
    m_heightSpin->setSuffix(QStringLiteral(" mm"));
    connect(m_heightSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ArcTextDialog::updatePreview);
    fArc->addRow(QStringLiteral("Schrifthöhe:"), m_heightSpin);

    m_spacingSpin = new QDoubleSpinBox(gArc);
    m_spacingSpin->setRange(-4.0, 25.0);
    m_spacingSpin->setValue(1.5);
    m_spacingSpin->setSingleStep(0.5);
    m_spacingSpin->setSuffix(QStringLiteral(" mm"));
    connect(m_spacingSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ArcTextDialog::updatePreview);
    fArc->addRow(QStringLiteral("Zeichenabstand:"), m_spacingSpin);

    m_slantSpin = new QDoubleSpinBox(gArc);
    m_slantSpin->setRange(-30.0, 30.0);
    m_slantSpin->setValue(0.0);
    m_slantSpin->setSuffix(QStringLiteral("°"));
    connect(m_slantSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ArcTextDialog::updatePreview);
    fArc->addRow(QStringLiteral("Neigung (Slant):"), m_slantSpin);

    leftVBox->addWidget(gArc);

    // Group 3: Stick-Stil & 3D-Relief ("Paddlerin")
    m_paddingGroup = new QGroupBox(QStringLiteral("3. Stick-Stil & 3D-Relief (\"Paddlerin\")"), leftContainer);
    auto* fStyle = new QFormLayout(m_paddingGroup);
    fStyle->setSpacing(8);

    m_styleCombo = new QComboBox(m_paddingGroup);
    m_styleCombo->addItem(QStringLiteral("⭐ 3D-Relief Polsterraupe (Paddlerin Dual-Pass)"), int(TextDigitizer::StitchStyle::PaddedSatin));
    m_styleCombo->addItem(QStringLiteral("🧵 Reine Satin-Kontur (Satin Outline)"), int(TextDigitizer::StitchStyle::SatinOutline));
    m_styleCombo->addItem(QStringLiteral("✦ Erhabener Satin-Rand auf Tatami"), int(TextDigitizer::StitchStyle::Raised));
    m_styleCombo->addItem(QStringLiteral("▦ Dichte Tatami-Webung (Tatami Only)"), int(TextDigitizer::StitchStyle::TatamiOnly));
    m_styleCombo->addItem(QStringLiteral("🪡 Dreifachstich (Bean Stitch)"), int(TextDigitizer::StitchStyle::BeanStitch));
    m_styleCombo->addItem(QStringLiteral("✒ Feiner Steppstich (Run Stitch)"), int(TextDigitizer::StitchStyle::RunStitch));
    connect(m_styleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ArcTextDialog::onStyleChanged);
    fStyle->addRow(QStringLiteral("Stick-Stil:"), m_styleCombo);

    m_paddingWidth = new QDoubleSpinBox(m_paddingGroup);
    m_paddingWidth->setRange(1.0, 8.0);
    m_paddingWidth->setValue(2.4);
    m_paddingWidth->setSingleStep(0.2);
    m_paddingWidth->setSuffix(QStringLiteral(" mm"));
    connect(m_paddingWidth, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ArcTextDialog::updatePreview);
    fStyle->addRow(QStringLiteral("Polster-Breite (Pass 1):"), m_paddingWidth);

    m_coverWidth = new QDoubleSpinBox(m_paddingGroup);
    m_coverWidth->setRange(1.5, 12.0);
    m_coverWidth->setValue(3.5);
    m_coverWidth->setSingleStep(0.2);
    m_coverWidth->setSuffix(QStringLiteral(" mm"));
    connect(m_coverWidth, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ArcTextDialog::updatePreview);
    fStyle->addRow(QStringLiteral("Deck-Raupe (Pass 2):"), m_coverWidth);

    m_coverPitch = new QDoubleSpinBox(m_paddingGroup);
    m_coverPitch->setRange(0.20, 0.80);
    m_coverPitch->setValue(0.34);
    m_coverPitch->setSingleStep(0.02);
    m_coverPitch->setSuffix(QStringLiteral(" mm"));
    connect(m_coverPitch, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ArcTextDialog::updatePreview);
    fStyle->addRow(QStringLiteral("Deck-Stichabstand:"), m_coverPitch);

    m_paddingPitch = new QDoubleSpinBox(m_paddingGroup);
    m_paddingPitch->setRange(0.30, 1.20);
    m_paddingPitch->setValue(0.48);
    m_paddingPitch->setSingleStep(0.02);
    m_paddingPitch->setSuffix(QStringLiteral(" mm"));
    connect(m_paddingPitch, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ArcTextDialog::updatePreview);
    fStyle->addRow(QStringLiteral("Polster-Abstand:"), m_paddingPitch);

    leftVBox->addWidget(m_paddingGroup);

    // Group 4: Stickrahmen & Übergrößen-Mehrfachrahmung (Multi-Hoop Auto-Split)
    auto* gHoop = new QGroupBox(QStringLiteral("4. Zielrahmen & Übergröße (Multi-Hoop Auto-Split)"), leftContainer);
    auto* fHoop = new QFormLayout(gHoop);
    fHoop->setSpacing(8);

    m_hoopCombo = new QComboBox(gHoop);
    m_hoopCombo->addItem(QStringLiteral("Janome A (126 x 110 mm) - Standard"), QSizeF(126, 110));
    m_hoopCombo->addItem(QStringLiteral("Janome B (140 x 200 mm) - Groß"), QSizeF(140, 200));
    m_hoopCombo->addItem(QStringLiteral("Janome C (50 x 50 mm) - Freiarm"), QSizeF(50, 50));
    m_hoopCombo->addItem(QStringLiteral("Janome F (110 x 110 mm) - Federklemme"), QSizeF(110, 110));
    m_hoopCombo->addItem(QStringLiteral("Kappenrahmen (100 x 90 mm)"), QSizeF(100, 90));
    connect(m_hoopCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ArcTextDialog::onHoopChanged);
    fHoop->addRow(QStringLiteral("Maschinen-Rahmen:"), m_hoopCombo);

    m_multiHoopChk = new QCheckBox(QStringLiteral("Übergröße: In Teil-Rahmen aufteilen (Auto-Split)"), gHoop);
    connect(m_multiHoopChk, &QCheckBox::toggled, this, &ArcTextDialog::onMultiHoopToggled);
    fHoop->addRow(QStringLiteral(""), m_multiHoopChk);

    auto* partsHBox = new QHBoxLayout();
    m_partsSpin = new QSpinBox(gHoop);
    m_partsSpin->setRange(2, 6);
    m_partsSpin->setValue(3);
    m_partsSpin->setEnabled(false);
    connect(m_partsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ArcTextDialog::updatePreview);
    partsHBox->addWidget(new QLabel(QStringLiteral("Teile:")));
    partsHBox->addWidget(m_partsSpin);

    m_overlapSpin = new QDoubleSpinBox(gHoop);
    m_overlapSpin->setRange(4.0, 25.0);
    m_overlapSpin->setValue(8.0);
    m_overlapSpin->setSuffix(QStringLiteral(" mm"));
    m_overlapSpin->setEnabled(false);
    connect(m_overlapSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ArcTextDialog::updatePreview);
    partsHBox->addWidget(new QLabel(QStringLiteral("Überlappung:")));
    partsHBox->addWidget(m_overlapSpin);
    fHoop->addRow(QStringLiteral("Aufteilung:"), partsHBox);

    m_crosshairChk = new QCheckBox(QStringLiteral("Sichere 1,0 mm Passkreuze einfügen (fadenbruch-sicher)"), gHoop);
    m_crosshairChk->setChecked(true);
    m_crosshairChk->setEnabled(false);
    connect(m_crosshairChk, &QCheckBox::toggled, this, &ArcTextDialog::updatePreview);
    fHoop->addRow(QStringLiteral(""), m_crosshairChk);

    m_centerPartsChk = new QCheckBox(QStringLiteral("Jeden Teilrahmen im Ursprung (0,0) zentrieren"), gHoop);
    m_centerPartsChk->setChecked(true);
    m_centerPartsChk->setEnabled(false);
    connect(m_centerPartsChk, &QCheckBox::toggled, this, &ArcTextDialog::updatePreview);
    fHoop->addRow(QStringLiteral(""), m_centerPartsChk);

    leftVBox->addWidget(gHoop);
    leftVBox->addStretch(1);

    scroll->setWidget(leftContainer);
    mainHBox->addWidget(scroll);

    // Right Column: Live 2D Preview & Action Buttons
    auto* rightVBox = new QVBoxLayout();
    rightVBox->setSpacing(10);

    m_preview = new ArcPreviewWidget(this);
    rightVBox->addWidget(m_preview, 1);

    // Status / Warning message box
    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet(QStringLiteral("background: #1b1e26; border: 1px solid #2c313d; border-radius: 6px; padding: 8px; color: #37d67a; font-weight: bold;"));
    rightVBox->addWidget(m_statusLabel);

    // Action button bar
    auto* btnHBox = new QHBoxLayout();
    m_exportBtn = new QPushButton(QStringLiteral("💾 Direkt als JEF exportieren…"), this);
    m_exportBtn->setStyleSheet(QStringLiteral("background:#232732; color:#dbe0e9; border:1px solid #2c313d; border-radius:6px; padding:8px 14px; font-weight:bold;"));
    connect(m_exportBtn, &QPushButton::clicked, this, &ArcTextDialog::exportDirectJef);
    btnHBox->addWidget(m_exportBtn);

    btnHBox->addStretch(1);

    auto* cancelBtn = new QPushButton(QStringLiteral("Abbrechen"), this);
    cancelBtn->setStyleSheet(QStringLiteral("background:#232732; color:#8b93a4; border:1px solid #2c313d; border-radius:6px; padding:8px 14px;"));
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnHBox->addWidget(cancelBtn);

    m_applyBtn = new QPushButton(QStringLiteral("▶ In StickCore Studio übernehmen"), this);
    m_applyBtn->setStyleSheet(QStringLiteral("background:#2dd4bf; color:#04201c; border:0; border-radius:6px; padding:8px 18px; font-weight:bold; font-size:14px;"));
    connect(m_applyBtn, &QPushButton::clicked, this, &ArcTextDialog::onApplyAndClose);
    btnHBox->addWidget(m_applyBtn);

    rightVBox->addLayout(btnHBox);

    mainHBox->addLayout(rightVBox, 1);
}

void ArcTextDialog::populateFonts()
{
    m_fontCombo->clear();
    // 1. Acquired Varsity & Athletic Fonts
    m_fontCombo->addItem(QStringLiteral("🏈 Varsity (Authentic College Athletics)"), QStringLiteral("Varsity"));
    m_fontCombo->addItem(QStringLiteral("🎓 Graduate (US Ivy League Serif Varsity)"), QStringLiteral("Graduate"));
    m_fontCombo->addItem(QStringLiteral("🔢 Jersey 10 (Chunky Block Athletics)"), QStringLiteral("Jersey 10"));
    m_fontCombo->addItem(QStringLiteral("🔢 Jersey 15 (Standard Varsity Athleisure)"), QStringLiteral("Jersey 15"));
    m_fontCombo->addItem(QStringLiteral("🔢 Jersey 25 (Bold Stadium Numbers & Text)"), QStringLiteral("Jersey 25"));

    // 2. Calligraphy & Script Fonts
    m_fontCombo->addItem(QStringLiteral("✒ Great Vibes (Elegante Kalligraphie)"), QStringLiteral("Great Vibes"));
    m_fontCombo->addItem(QStringLiteral("✒ Alex Brush (Schwungvolle Schreibschrift)"), QStringLiteral("Alex Brush"));
    m_fontCombo->addItem(QStringLiteral("✒ Tangerine (Klassische Feder)"), QStringLiteral("Tangerine"));

    // 3. System Defaults
    m_fontCombo->addItem(QStringLiteral("🔤 DejaVu Sans (Klar & Modern)"), QStringLiteral("DejaVu Sans"));
    m_fontCombo->addItem(QStringLiteral("🔤 Serif (Traditionelle Antiqua)"), QStringLiteral("Serif"));
    m_fontCombo->addItem(QStringLiteral("🔤 Arial / Sans-Serif"), QStringLiteral("Arial"));
}

void ArcTextDialog::onBaselineChanged(int idx)
{
    const auto b = TextDigitizer::Baseline(m_baselineCombo->itemData(idx).toInt());
    const bool isCurved = (b != TextDigitizer::Baseline::Straight);
    m_radiusSpin->setEnabled(isCurved);
    m_radiusSlider->setEnabled(isCurved);
    updatePreview();
}

void ArcTextDialog::onStyleChanged(int idx)
{
    const auto st = TextDigitizer::StitchStyle(m_styleCombo->itemData(idx).toInt());
    const bool isPadded = (st == TextDigitizer::StitchStyle::PaddedSatin);
    m_paddingWidth->setEnabled(isPadded);
    m_paddingPitch->setEnabled(isPadded);
    m_coverWidth->setEnabled(isPadded || st == TextDigitizer::StitchStyle::SatinOutline || st == TextDigitizer::StitchStyle::Raised);
    m_coverPitch->setEnabled(isPadded || st == TextDigitizer::StitchStyle::SatinOutline || st == TextDigitizer::StitchStyle::Raised);
    updatePreview();
}

void ArcTextDialog::onHoopChanged(int /*idx*/)
{
    updatePreview();
}

void ArcTextDialog::onMultiHoopToggled(bool checked)
{
    m_partsSpin->setEnabled(checked);
    m_overlapSpin->setEnabled(checked);
    m_crosshairChk->setEnabled(checked);
    m_centerPartsChk->setEnabled(checked);
    updatePreview();
}

bool ArcTextDialog::isMultiHoopActive() const
{
    return m_multiHoopChk->isChecked();
}

void ArcTextDialog::updatePreview()
{
    if (m_updating) return;
    m_updating = true;
    regenerateStitches();
    m_updating = false;
}

void ArcTextDialog::regenerateStitches()
{
    const QString txt = m_textEdit->text().trimmed();
    if (txt.isEmpty()) {
        m_currentSeq.clear();
        m_preview->setSequence(m_currentSeq, QRectF(-63, -55, 126, 110), QStringLiteral("Rahmen"));
        m_statusLabel->setText(QStringLiteral("Bitte Text eingeben."));
        return;
    }

    TextDigitizer::Params tp;
    tp.text            = txt;
    tp.family          = m_fontCombo->currentData().toString();
    tp.bold            = m_boldChk->isChecked();
    tp.italic          = m_italicChk->isChecked();
    tp.heightMm        = m_heightSpin->value();
    tp.baseline        = TextDigitizer::Baseline(m_baselineCombo->currentData().toInt());
    tp.arcRadiusMm     = m_radiusSpin->value();
    tp.letterSpacingMm = m_spacingSpin->value();
    tp.slantDeg        = m_slantSpin->value();
    tp.style           = TextDigitizer::StitchStyle(m_styleCombo->currentData().toInt());
    tp.borderWidthMm   = m_coverWidth->value();
    tp.borderPitchMm   = m_coverPitch->value();
    tp.paddingWidthMm  = m_paddingWidth->value();
    tp.paddingPitchMm  = m_paddingPitch->value();
    tp.raised          = (tp.style == TextDigitizer::StitchStyle::Raised);

    m_currentParams = tp;
    m_currentSeq = TextDigitizer::generate(tp);
    m_currentSeq.palette = { ThreadCatalog::snap(QColor(180, 30, 45)) }; // Cardinal Red / Gold

    // Target Hoop Bounds
    const QSizeF hSz = m_hoopCombo->currentData().toSizeF();
    const QRectF hoopRect(-hSz.width() * 0.5, -hSz.height() * 0.5, hSz.width(), hSz.height());
    const QString hoopName = m_hoopCombo->currentText();

    const QRectF bb = m_currentSeq.boundingRect();
    const bool exceeds = (bb.width() > hSz.width() || bb.height() > hSz.height());

    std::vector<double> cutLines;
    std::vector<QPointF> crosshairs;

    if (m_multiHoopChk->isChecked()) {
        MultiHoopSplitter::Params mp;
        mp.hoopWidthMm     = hSz.width();
        mp.hoopHeightMm    = hSz.height();
        mp.overlapMm       = m_overlapSpin->value();
        mp.addCrosshairs   = m_crosshairChk->isChecked();
        mp.centerEachHoop  = m_centerPartsChk->isChecked();
        mp.direction       = MultiHoopSplitter::SplitDirection::Vertical;

        m_multiResult = MultiHoopSplitter::splitMulti(m_currentSeq, mp, m_partsSpin->value());
        crosshairs = m_multiResult.crosshairs;

        // Cut positions along width
        const double step = bb.width() / static_cast<double>(m_partsSpin->value());
        for (int i = 1; i < m_partsSpin->value(); ++i) {
            cutLines.push_back(bb.left() + step * i);
        }

        m_statusLabel->setStyleSheet(QStringLiteral("background: #2a1a1f; border: 1px solid #e5484d; border-radius: 6px; padding: 8px; color: #ffb4b4; font-weight: bold;"));
        m_statusLabel->setText(QString("⚠️ Übergröße (%1 x %2 mm) überschreitet %3!<br>"
                                      "✓ Auto-Split AKTIV: Muster wird in <b>%4 Teil-Rahmen</b> geteilt mit 1,0 mm gesicherten Passkreuzen.")
                                  .arg(bb.width(), 0, 'f', 1)
                                  .arg(bb.height(), 0, 'f', 1)
                                  .arg(hoopName)
                                  .arg(m_partsSpin->value()));
    } else {
        if (exceeds) {
            m_statusLabel->setStyleSheet(QStringLiteral("background: #2a1a1f; border: 1px solid #e5484d; border-radius: 6px; padding: 8px; color: #ff9999; font-weight: bold;"));
            m_statusLabel->setText(QString("⚠️ Achtung: Motivgröße (%1 x %2 mm) überschreitet den gewählten Rahmen (%3 x %4 mm)!<br>"
                                          "Aktiviere oben die Checkbox 'Übergröße: In Teil-Rahmen aufteilen' für automatische Mehrfachrahmung.")
                                      .arg(bb.width(), 0, 'f', 1)
                                      .arg(bb.height(), 0, 'f', 1)
                                      .arg(hSz.width(), 0, 'f', 0)
                                      .arg(hSz.height(), 0, 'f', 0));
        } else {
            m_statusLabel->setStyleSheet(QStringLiteral("background: #152723; border: 1px solid #2dd4bf; border-radius: 6px; padding: 8px; color: #3ee0cc; font-weight: bold;"));
            m_statusLabel->setText(QString("✓ Motiv passt optimal in %1 (%2 x %3 mm) • %4 Stiche")
                                      .arg(hoopName)
                                      .arg(bb.width(), 0, 'f', 1)
                                      .arg(bb.height(), 0, 'f', 1)
                                      .arg(m_currentSeq.stitches.size()));
        }
    }

    m_preview->setSequence(m_currentSeq, hoopRect, hoopName);
    m_preview->setMultiHoop(m_multiHoopChk->isChecked(), cutLines, crosshairs);
}

void ArcTextDialog::exportDirectJef()
{
    if (m_currentSeq.empty()) {
        QMessageBox::warning(this, QStringLiteral("Keine Stiche"), QStringLiteral("Bitte zuerst einen Text eingeben."));
        return;
    }

    // Default target directory: USB drive F:/EmbF5/MyDesign or Documents
    QString defaultDir = QStringLiteral("F:/EmbF5/MyDesign");
    if (!QDir(defaultDir).exists()) {
        defaultDir = QDir::homePath();
    }

    const HoopType hoopType = (m_hoopCombo->currentIndex() == 1) ? HoopType::HoopB_140x200 : HoopType::HoopA_126x110;

    if (m_multiHoopChk->isChecked() && m_multiResult.success && !m_multiResult.parts.empty()) {
        const QString outBase = QFileDialog::getSaveFileName(
            this, QStringLiteral("Mehrteiligen Schriftbogen exportieren (JEF)"),
            defaultDir + QStringLiteral("/BOGEN.JEF"),
            QStringLiteral("Janome Stickdateien (*.jef)"));
        if (outBase.isEmpty()) return;

        const QFileInfo fi(outBase);
        const QString dir = fi.absolutePath();
        const QString baseName = fi.completeBaseName().left(5).toUpper(); // Max 8.3 filename

        QStringList exportedFiles;
        for (size_t i = 0; i < m_multiResult.parts.size(); ++i) {
            const QString partFile = QString("%1/%2_%3.JEF").arg(dir, baseName).arg(i + 1);
            auto res = JefCodec::exportToFile(partFile, m_multiResult.parts[i], hoopType);
            if (res.ok) {
                exportedFiles.append(QFileInfo(partFile).fileName());
            }
        }

        QMessageBox::information(this, QStringLiteral("Export Erfolgreich"),
            QString("Der Schriftbogen wurde erfolgreich in %1 Teil-Dateien exportiert:<br><b>%2</b><br><br>"
                    "Alle Teile enthalten präzise 1,0 mm Passkreuze mit automatischer Fadenabschnitt-Sicherung.")
                .arg(exportedFiles.size())
                .arg(exportedFiles.join(QStringLiteral(", "))));
    } else {
        const QString outFile = QFileDialog::getSaveFileName(
            this, QStringLiteral("Schriftbogen exportieren (JEF)"),
            defaultDir + QStringLiteral("/BOGEN_1.JEF"),
            QStringLiteral("Janome Stickdateien (*.jef)"));
        if (outFile.isEmpty()) return;

        auto res = JefCodec::exportToFile(outFile, m_currentSeq, hoopType);
        if (res.ok) {
            QMessageBox::information(this, QStringLiteral("Export Erfolgreich"),
                QString("Stickdatei erfolgreich gespeichert:<br><b>%1</b> (%2 Stiche)")
                    .arg(QFileInfo(outFile).fileName())
                    .arg(m_currentSeq.stitches.size()));
        } else {
            QMessageBox::critical(this, QStringLiteral("Fehler beim Export"), res.message);
        }
    }
}

void ArcTextDialog::onApplyAndClose()
{
    if (m_currentSeq.empty()) {
        QMessageBox::warning(this, QStringLiteral("Keine Stiche"), QStringLiteral("Bitte zuerst einen Text eingeben."));
        return;
    }
    accept();
}

} // namespace stick
