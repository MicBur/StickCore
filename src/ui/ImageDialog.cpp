// ---------------------------------------------------------------------------
//  StickCore  –  ImageDialog.cpp
// ---------------------------------------------------------------------------
#include "ui/ImageDialog.h"
#include "core/MachineProfile.h"
#include "generators/OpenCvBridge.h"
#include "library/StitchThumbnail.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QStackedWidget>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QTimer>
#include <QPixmap>
#include <cmath>

namespace stick {

namespace {
QLabel* caption(const QString& t) {
    auto* l = new QLabel(t);
    l->setStyleSheet(QStringLiteral("color:#8b93a4;font-size:12px;"));
    l->setAlignment(Qt::AlignCenter);
    return l;
}
QLabel* previewBox() {
    auto* l = new QLabel;
    l->setAlignment(Qt::AlignCenter);
    l->setFixedSize(340, 340);
    l->setStyleSheet(QStringLiteral("background:#0f1116;border:1px solid #2c313d;border-radius:8px;"));
    return l;
}
} // namespace

ImageDialog::ImageDialog(const QImage& source, QWidget* parent)
    : QDialog(parent), m_source(source)
{
    setWindowTitle(QStringLiteral("Foto digitalisieren – Echtzeit-Vorschau"));
    setMinimumSize(920, 620);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(10);

    // --- top: mode selector & preview toolbar ------------------------------
    auto* modeRow = new QHBoxLayout;
    modeRow->addWidget(new QLabel(QStringLiteral("Modus:")));
    m_mode = new QComboBox;
    m_mode->addItem(QStringLiteral("🎨 Farben reduzieren (mehrfarbig)"), int(ImageDigitizer::Mode::Colors));
    m_mode->addItem(QStringLiteral("👤 Konterfei (ein-/wenigfarbig, Comic)"), int(ImageDigitizer::Mode::Portrait));
    m_mode->addItem(QStringLiteral("✏ Umriss / Linien (Kontur-Stiche)"), int(ImageDigitizer::Mode::LineArt));
    m_mode->setCurrentIndex(0); // Colors mode default for photos
    modeRow->addWidget(m_mode, 1);
    root->addLayout(modeRow);

    // --- middle: previews + controls --------------------------------------
    auto* mid = new QHBoxLayout;
    mid->setSpacing(16);

    // previews column
    auto* prevCol = new QVBoxLayout;
    auto* prevGrid = new QGridLayout;

    m_before = previewBox();
    m_after  = previewBox();

    // Toolbar over "after" preview: View Mode + Fabric selector
    auto* afterToolbar = new QHBoxLayout;
    m_previewMode = new QComboBox;
    m_previewMode->addItem(QStringLiteral("🧵 Gestickte Fäden"));
    m_previewMode->addItem(QStringLiteral("🔍 Bildanalyse / Maske"));
    m_previewMode->setCurrentIndex(0);

    m_fabricCombo = new QComboBox;
    m_fabricCombo->addItem(QStringLiteral("⬛ Dunkelgrau / Schwarz"));
    m_fabricCombo->addItem(QStringLiteral("🟫 Helles Leinen / Beige"));
    m_fabricCombo->addItem(QStringLiteral("⬜ Reines Weiß"));
    m_fabricCombo->addItem(QStringLiteral("🟦 Marineblau"));
    m_fabricCombo->addItem(QStringLiteral("🟩 Loden / Jagdgrün"));
    m_fabricCombo->addItem(QStringLiteral("🔴 Rubinrot"));

    // Auto-detect fabric: dark motifs/images look best on dark fabric
    bool darkTheme = false;
    if (!m_source.isNull()) {
        const int w = m_source.width(), h = m_source.height();
        long sumLum = 0; int samples = 0;
        for (int y : {0, h/4, h/2, 3*h/4, h-1}) {
            for (int x : {0, w/4, w/2, 3*w/4, w-1}) {
                QRgb p = m_source.pixel(x, y);
                sumLum += (qRed(p)*299 + qGreen(p)*587 + qBlue(p)*114) / 1000;
                samples++;
            }
        }
        if (samples > 0 && (sumLum / samples) < 110) darkTheme = true;
    }
    m_fabricCombo->setCurrentIndex(darkTheme ? 0 : 1);

    afterToolbar->addWidget(m_previewMode, 1);
    afterToolbar->addWidget(m_fabricCombo, 1);

    prevGrid->addWidget(caption(QStringLiteral("Original-Vorlage")), 0, 0);
    prevGrid->addLayout(afterToolbar, 0, 1);
    prevGrid->addWidget(m_before, 1, 0);
    prevGrid->addWidget(m_after,  1, 1);
    prevCol->addLayout(prevGrid);

    // Live Metrics Badge
    m_metricsLabel = new QLabel;
    m_metricsLabel->setStyleSheet(QStringLiteral(
        "background:#141720; border:1px solid #2a3042; border-radius:6px; "
        "padding:8px 12px; color:#e0e6ed; font-size:12px; font-weight:bold;"));
    m_metricsLabel->setAlignment(Qt::AlignCenter);
    prevCol->addWidget(m_metricsLabel);
    prevCol->addStretch(1);

    mid->addLayout(prevCol, 0);

    // controls (stacked by mode + shared block)
    auto* ctrlCol = new QVBoxLayout;
    ctrlCol->setSpacing(8);
    m_stack = new QStackedWidget;

    // page 0 – Colors
    {
        auto* w = new QWidget; auto* f = new QFormLayout(w);
        f->setSpacing(6);
        m_colors = new QSpinBox; m_colors->setRange(2, 12); m_colors->setValue(6);
        f->addRow(QStringLiteral("Anzahl Farben:"), m_colors);

        m_fillAngle = new QDoubleSpinBox;
        m_fillAngle->setRange(0.0, 360.0);
        m_fillAngle->setValue(45.0);
        m_fillAngle->setSingleStep(15.0);
        m_fillAngle->setSuffix(QStringLiteral("°"));
        f->addRow(QStringLiteral("Füllwinkel:"), m_fillAngle);

        m_multiAngle = new QCheckBox(QStringLiteral("Multi-Winkel Schutz (+45° je Farbe)"));
        m_multiAngle->setChecked(true);
        f->addRow(m_multiAngle);

        m_dropBg = new QCheckBox(QStringLiteral("Hintergrund entfernen (Ecken / Kontur)"));
        m_dropBg->setChecked(true);
        f->addRow(m_dropBg);

        m_satinBorder = new QCheckBox(QStringLiteral("Erhabener Satin-Kettelrand um Motiv"));
        m_satinBorder->setChecked(false);
        f->addRow(m_satinBorder);

        m_borderWidth = new QDoubleSpinBox;
        m_borderWidth->setRange(0.8, 4.0);
        m_borderWidth->setValue(1.5);
        m_borderWidth->setSingleStep(0.2);
        m_borderWidth->setSuffix(QStringLiteral(" mm"));
        f->addRow(QStringLiteral("Kettelrand-Breite:"), m_borderWidth);

        auto* hint = caption(QStringLiteral("Garnfarben mit wechselndem Stichwinkel gegen Stoffverzug."));
        hint->setAlignment(Qt::AlignLeft);
        f->addRow(hint);
        m_stack->addWidget(w);
    }
    // page 1 – Portrait
    {
        auto* w = new QWidget; auto* f = new QFormLayout(w);
        f->setSpacing(6);
        m_portraitStyle = new QComboBox;
        for (auto s : { ImageDigitizer::PortraitStyle::Comic,
                        ImageDigitizer::PortraitStyle::Silhouette,
                        ImageDigitizer::PortraitStyle::Sketch,
                        ImageDigitizer::PortraitStyle::Detailed,
                        ImageDigitizer::PortraitStyle::Stylized,
                        ImageDigitizer::PortraitStyle::Tones })
            m_portraitStyle->addItem(ImageDigitizer::portraitStyleName(s), int(s));
        f->addRow(QStringLiteral("Stil:"), m_portraitStyle);
        m_tonesLabel = new QLabel(QStringLiteral("Tonstufen:"));
        m_tones = new QSpinBox; m_tones->setRange(2, 5); m_tones->setValue(3);
        f->addRow(m_tonesLabel, m_tones);
        auto* hint = caption(QStringLiteral("Comic = klares Konterfei (empfohlen). Silhouette = kräftig. "
            "Zeichnung/Detailliert/Stilisiert = verschiedene Looks. Graustufen = mehrfarbig."));
        hint->setAlignment(Qt::AlignLeft); hint->setWordWrap(true);
        f->addRow(hint);
        m_stack->addWidget(w);
    }
    // page 2 – LineArt
    {
        auto* w = new QWidget; auto* f = new QFormLayout(w);
        f->setSpacing(6);
        auto* hint = caption(QStringLiteral("Die Umrisse werden als Laufstiche gestickt —\nwie eine Zeichnung."));
        hint->setAlignment(Qt::AlignLeft); hint->setWordWrap(true);
        f->addRow(hint);
        m_stack->addWidget(w);
    }
    ctrlCol->addWidget(m_stack);

    // common adjustment controls
    auto* shared = new QWidget;
    auto* sf = new QFormLayout(shared);
    sf->setSpacing(6);

    // Threshold row (only for Portrait/LineArt)
    m_threshRow = new QWidget;
    auto* trf = new QFormLayout(m_threshRow);
    trf->setContentsMargins(0, 0, 0, 0);
    trf->setSpacing(6);
    m_autoThresh = new QCheckBox(QStringLiteral("Schwellwert automatisch (Otsu)"));
    m_autoThresh->setChecked(true);
    trf->addRow(m_autoThresh);
    m_thresh = new QSlider(Qt::Horizontal); m_thresh->setRange(0, 255); m_thresh->setValue(128);
    m_thresh->setEnabled(false);
    trf->addRow(QStringLiteral("Schwellwert:"), m_thresh);
    sf->addRow(m_threshRow);

    m_contrast = new QSlider(Qt::Horizontal); m_contrast->setRange(50, 300); m_contrast->setValue(140);
    sf->addRow(QStringLiteral("Kontrast:"), m_contrast);

    m_smooth = new QSlider(Qt::Horizontal); m_smooth->setRange(0, 4); m_smooth->setValue(1);
    sf->addRow(QStringLiteral("Glätten / Filter:"), m_smooth);

    m_invert = new QCheckBox(QStringLiteral("Invertieren (hell/dunkel tauschen)"));
    sf->addRow(m_invert);

    // OpenCV-only option: automatic face crop
    m_faceCrop = new QCheckBox(QStringLiteral("Gesicht automatisch zuschneiden"));
    m_fineDetail = new QCheckBox;
    m_fineDetail->setChecked(true); m_fineDetail->hide();
    m_faceCrop->setChecked(true);
    if (OpenCvBridge::available()) {
        sf->addRow(m_faceCrop);
    } else {
        m_faceCrop->setChecked(false);   m_faceCrop->hide();
    }
    ctrlCol->addWidget(shared);

    // Target width
    auto* wf = new QFormLayout;
    m_widthMm = new QSpinBox; m_widthMm->setRange(20, 200); m_widthMm->setValue(100);
    m_widthMm->setSuffix(QStringLiteral(" mm"));
    wf->addRow(QStringLiteral("Zielbreite:"), m_widthMm);
    ctrlCol->addLayout(wf);
    ctrlCol->addStretch(1);

    mid->addLayout(ctrlCol, 1);
    root->addLayout(mid, 1);

    // --- buttons -----------------------------------------------------------
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Digitalisieren ▶"));
    bb->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Abbrechen"));
    connect(bb, &QDialogButtonBox::accepted, this, [this]{ collectParams(); accept(); });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(bb);

    // --- wiring ------------------------------------------------------------
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(80);
    connect(m_debounce, &QTimer::timeout, this, &ImageDialog::refreshPreview);
    auto kick = [this]{ m_debounce->start(); };

    auto syncStyle = [this]{
        const bool tones = ImageDigitizer::PortraitStyle(m_portraitStyle->currentData().toInt())
                           == ImageDigitizer::PortraitStyle::Tones;
        m_tonesLabel->setVisible(tones);
        m_tones->setVisible(tones);
    };
    auto syncMode = [this, syncStyle]{
        const int idx = m_mode->currentIndex();
        m_stack->setCurrentIndex(idx);
        m_threshRow->setVisible(idx != 0); // threshold only needed for binary modes
        if (idx == 1) syncStyle();
    };

    connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, syncMode, kick](int){ syncMode(); kick(); });
    connect(m_portraitStyle, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, syncStyle, kick](int){ syncStyle(); kick(); });
    connect(m_previewMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &ImageDialog::updatePreviewDisplay);
    connect(m_fabricCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &ImageDialog::updatePreviewDisplay);

    connect(m_autoThresh, &QCheckBox::toggled, this, [this, kick](bool on){
        m_thresh->setEnabled(!on); kick();
    });
    connect(m_thresh,      &QSlider::valueChanged, this, [kick](int){ kick(); });
    connect(m_contrast,    &QSlider::valueChanged, this, [kick](int){ kick(); });
    connect(m_smooth,      &QSlider::valueChanged, this, [kick](int){ kick(); });
    connect(m_colors,      QOverload<int>::of(&QSpinBox::valueChanged), this, [kick](int){ kick(); });
    connect(m_fillAngle,   QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [kick](double){ kick(); });
    connect(m_multiAngle,  &QCheckBox::toggled, this, [kick](bool){ kick(); });
    connect(m_dropBg,      &QCheckBox::toggled, this, [kick](bool){ kick(); });
    connect(m_satinBorder, &QCheckBox::toggled, this, [kick](bool){ kick(); });
    connect(m_borderWidth, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [kick](double){ kick(); });
    connect(m_tones,       QOverload<int>::of(&QSpinBox::valueChanged), this, [kick](int){ kick(); });
    connect(m_invert,      &QCheckBox::toggled, this, [kick](bool){ kick(); });
    connect(m_faceCrop,    &QCheckBox::toggled, this, [kick](bool){ kick(); });
    connect(m_widthMm,     QOverload<int>::of(&QSpinBox::valueChanged), this, [kick](int){ kick(); });

    // Show original
    if (!m_source.isNull()) {
        m_before->setPixmap(QPixmap::fromImage(m_source).scaled(
            m_before->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    syncMode();
    syncStyle();
    refreshPreview();
}

QColor ImageDialog::currentFabricColor() const
{
    if (!m_fabricCombo) return QColor(0x14, 0x17, 0x1C);
    switch (m_fabricCombo->currentIndex()) {
    case 0: return QColor(0x14, 0x17, 0x1C); // Dark/Black
    case 1: return QColor(0xEF, 0xEA, 0xDE); // Light Linen/Beige
    case 2: return QColor(0xFF, 0xFF, 0xFF); // Pure White
    case 3: return QColor(0x1A, 0x23, 0x3A); // Navy
    case 4: return QColor(0x24, 0x30, 0x26); // Loden/Hunter Green
    case 5: return QColor(0x55, 0x14, 0x19); // Ruby Red
    default: return QColor(0x14, 0x17, 0x1C);
    }
}

void ImageDialog::collectParams()
{
    ImageDigitizer::Params p;
    p.mode          = ImageDigitizer::Mode(m_mode->currentData().toInt());
    p.widthMm       = m_widthMm->value();
    p.brand         = MachineProfile::current().defaultBrand;
    p.colors        = m_colors->value();
    p.fillAngleDeg  = m_fillAngle ? m_fillAngle->value() : 45.0;
    p.multiAngle    = m_multiAngle ? m_multiAngle->isChecked() : true;
    p.dropBackground= m_dropBg ? m_dropBg->isChecked() : true;
    p.satinBorder   = m_satinBorder ? m_satinBorder->isChecked() : false;
    p.borderWidthMm = m_borderWidth ? m_borderWidth->value() : 1.5;
    p.portraitStyle = ImageDigitizer::PortraitStyle(m_portraitStyle->currentData().toInt());
    p.tones         = m_tones->value();
    p.contrast      = m_contrast->value() / 100.0;
    p.blur          = m_smooth->value();
    p.invert        = m_invert->isChecked();
    p.threshold     = m_autoThresh->isChecked() ? -1 : m_thresh->value();
    p.autoFaceCrop  = m_faceCrop->isChecked();
    p.fineDetail    = m_fineDetail->isChecked();
    m_params = p;
}

void ImageDialog::refreshPreview()
{
    if (m_source.isNull()) return;
    collectParams();

    // Fast stitch calculation for silky responsive preview
    m_latestPreviewSeq = ImageDigitizer::generateFastPreview(m_source, m_params);

    // Analysis mask / cluster image
    m_latestAnalysisImg = ImageDigitizer::preview(m_source, m_params);

    updatePreviewDisplay();

    emit livePreviewGenerated(m_latestPreviewSeq);
}

void ImageDialog::updatePreviewDisplay()
{
    if (m_after == nullptr) return;

    if (m_previewMode->currentIndex() == 0 && !m_latestPreviewSeq.empty()) {
        // Stitch simulation on chosen fabric
        QImage thumb = StitchThumbnail::render(m_latestPreviewSeq, m_after->width(), currentFabricColor(), true);
        m_after->setPixmap(QPixmap::fromImage(thumb));
    } else if (!m_latestAnalysisImg.isNull()) {
        // Image analysis / color clusters
        m_after->setPixmap(QPixmap::fromImage(m_latestAnalysisImg).scaled(
            m_after->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
    }

    // Update live metrics badge
    if (m_metricsLabel) {
        if (m_latestPreviewSeq.empty()) {
            m_metricsLabel->setText(QStringLiteral("⚠️ Keine Stiche erzeugt – Bitte Kontrast oder Schwellwert anpassen"));
        } else {
            const size_t stCount = m_latestPreviewSeq.realStitchCount();
            const size_t colCount = m_latestPreviewSeq.palette.size();
            double x0, y0, x1, y1;
            m_latestPreviewSeq.bounds(x0, y0, x1, y1);
            const double w = std::max(0.0, x1 - x0);
            const double h = std::max(0.0, y1 - y0);
            const int estMinutes = std::max(1, int(std::ceil(stCount / 680.0 + colCount * 0.7)));

            QString hoopFit;
            if (w <= 42.0 && h <= 42.0) {
                hoopFit = QStringLiteral("✅ Passt in Janome Hoop C (50×50 mm)");
            } else if (w <= 130.0 && h <= 130.0) {
                hoopFit = QStringLiteral("✅ Passt in Janome Hoop SQ14 (140×140 mm)");
            } else if (w <= 130.0 && h <= 190.0) {
                hoopFit = QStringLiteral("✅ Passt in Janome Standard-Rahmen B (140×200 mm)");
            } else if (w <= 220.0 && h <= 190.0) {
                hoopFit = QStringLiteral("✅ Passt in Janome Giga-Hoop D (230×200 mm)");
            } else {
                hoopFit = QStringLiteral("⚠️ Größer als Standard-Rahmen (Multi-Hooping nötig)");
            }

            m_metricsLabel->setText(QStringLiteral(
                "🧵 %1 Stiche  |  🎨 %2 Farben  |  📐 %3 × %4 mm  |  ⏱️ ~%5 Min.  |  %6")
                .arg(QLocale().toString(quint64(stCount)))
                .arg(colCount)
                .arg(w, 0, 'f', 1)
                .arg(h, 0, 'f', 1)
                .arg(estMinutes)
                .arg(hoopFit));
        }
    }
}

} // namespace stick
