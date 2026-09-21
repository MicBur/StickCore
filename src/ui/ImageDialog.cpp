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
#include <QTableWidget>
#include <QHeaderView>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QColorDialog>
#include <QTimer>
#include <QPixmap>
#include <QPainter>
#include <cmath>

namespace stick {

namespace {
QLabel* caption(const QString& t) {
    auto* l = new QLabel(t);
    l->setStyleSheet(QStringLiteral("color:#8b93a4;font-size:11px;"));
    l->setAlignment(Qt::AlignCenter);
    return l;
}
QLabel* previewBox() {
    auto* l = new QLabel;
    l->setAlignment(Qt::AlignCenter);
    l->setFixedSize(320, 320);
    l->setStyleSheet(QStringLiteral("background:#0f1116;border:1px solid #2c313d;border-radius:8px;"));
    return l;
}
QIcon colorSwatchIcon(const QColor& c) {
    QPixmap px(22, 22);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(c);
    p.setPen(QPen(QColor(50, 50, 60), 1));
    p.drawRoundedRect(1, 1, 20, 20, 4, 4);
    return QIcon(px);
}
} // namespace

ImageDialog::ImageDialog(const QImage& source, QWidget* parent)
    : QDialog(parent), m_source(source)
{
    setWindowTitle(QStringLiteral("Foto & Motiv digitalisieren – Atelier-Meisterschule"));
    setMinimumSize(1060, 720);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(10);
    root->setContentsMargins(14, 12, 14, 12);

    // --- Top: Mode Selector ------------------------------------------------
    auto* modeRow = new QHBoxLayout;
    auto* modeLbl = new QLabel(QStringLiteral("Digitalisierungs-Modus:"));
    modeLbl->setStyleSheet(QStringLiteral("font-weight:bold; font-size:13px; color:#2dd4bf;"));
    modeRow->addWidget(modeLbl);

    m_mode = new QComboBox;
    m_mode->addItem(QStringLiteral("🎨 Vektor-Tatami (K-Means Farbreduktion, Tatami-Muster & Unterleger)"), int(ImageDigitizer::Mode::Colors));
    m_mode->addItem(QStringLiteral("🌫️ Sfumato Photo-Stitch (Fotorealistischer Wellenstich für Fotos)"), int(ImageDigitizer::Mode::Sfumato));
    m_mode->addItem(QStringLiteral("✖️ Traditioneller Kreuzstich (Aida-Raster 11ct–18ct)"), int(ImageDigitizer::Mode::CrossStitch));
    m_mode->addItem(QStringLiteral("✏️ LineArt & Konturen (Lauf- & Dreifachstich / Bean Stitch)"), int(ImageDigitizer::Mode::LineArt));
    m_mode->addItem(QStringLiteral("👤 Konterfei & Silhouette (Comic & Tonwert-Poster)"), int(ImageDigitizer::Mode::Portrait));
    m_mode->setStyleSheet(QStringLiteral("font-size:13px; font-weight:bold; padding:4px;"));
    modeRow->addWidget(m_mode, 1);
    root->addLayout(modeRow);

    // --- Center: Previews (left) + Controls (right) ------------------------
    auto* mid = new QHBoxLayout;
    mid->setSpacing(18);

    // Left Column: Previews & Live Metrics
    auto* prevCol = new QVBoxLayout;
    auto* prevGrid = new QGridLayout;

    m_before = previewBox();
    m_after  = previewBox();

    // Toolbar over "after" preview: View Mode + Fabric selector
    auto* afterToolbar = new QHBoxLayout;
    m_previewMode = new QComboBox;
    m_previewMode->addItem(QStringLiteral("🧵 Gestickte Fäden (3D)"));
    m_previewMode->addItem(QStringLiteral("✏️ Vektor-Pfade (Bézier wie Inkscape)"));
    m_previewMode->addItem(QStringLiteral("🔍 Bildanalyse / Maske"));
    m_previewMode->setCurrentIndex(0);

    m_fabricCombo = new QComboBox;
    m_fabricCombo->addItem(QStringLiteral("⬛ Dunkelgrau / Schwarz"));
    m_fabricCombo->addItem(QStringLiteral("🟫 Helles Leinen / Beige"));
    m_fabricCombo->addItem(QStringLiteral("⬜ Reines Weiß"));
    m_fabricCombo->addItem(QStringLiteral("🟦 Marineblau"));
    m_fabricCombo->addItem(QStringLiteral("🟩 Loden / Jagdgrün"));
    m_fabricCombo->addItem(QStringLiteral("🔴 Rubinrot"));

    // Auto-detect fabric contrast
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

    // Right Column: Controls & Palette Manager
    auto* ctrlCol = new QVBoxLayout;
    ctrlCol->setSpacing(8);

    m_stack = new QStackedWidget;

    // --- Page 0: Colors (Vector-Tatami) -----------------------------------
    {
        auto* w = new QWidget;
        auto* f = new QFormLayout(w);
        f->setSpacing(6);
        f->setContentsMargins(0, 0, 0, 0);

        m_separation = new QComboBox;
        m_separation->addItem(QStringLiteral("Helligkeitsstufen (Tones: Schatten, Knochen, Glanz)"), int(ImageDigitizer::SeparationMode::Tones));
        m_separation->addItem(QStringLiteral("Farben (K-Means Algorithmus mit Kantenerhalt)"), int(ImageDigitizer::SeparationMode::Colors));
        f->addRow(QStringLiteral("Vektor-Zerlegung:"), m_separation);

        m_colors = new QSpinBox;
        m_colors->setRange(1, 8);
        m_colors->setValue(3);
        f->addRow(QStringLiteral("Anzahl Farben / Töne:"), m_colors);

        m_stencilLabel = new QLabel(QStringLiteral("Schablonen-Schwellwert:"));
        m_stencilSlider = new QSlider(Qt::Horizontal);
        m_stencilSlider->setRange(20, 240);
        m_stencilSlider->setValue(128);
        m_stencilLabel->setVisible(false);
        m_stencilSlider->setVisible(false);
        f->addRow(m_stencilLabel, m_stencilSlider);

        m_tatamiPattern = new QComboBox;
        m_tatamiPattern->addItem(QStringLiteral("Standard-Tatami (1/4 Versatz – Glatt & gleichmäßig)"), int(TatamiFill::PatternType::StandardTatami));
        m_tatamiPattern->addItem(QStringLiteral("Köper / Twill (1/3 Versatz – Seidenglanz & Trachtenoptik)"), int(TatamiFill::PatternType::Twill));
        m_tatamiPattern->addItem(QStringLiteral("Ziegelstein / Brick (1/2 Versatz – Mauerwerksverband)"), int(TatamiFill::PatternType::Brick));
        m_tatamiPattern->addItem(QStringLiteral("Flechtmuster / Basket (Korb- & Panamagewebe)"), int(TatamiFill::PatternType::Basketweave));
        f->addRow(QStringLiteral("Tatami-Muster:"), m_tatamiPattern);

        m_densityMm = new QDoubleSpinBox;
        m_densityMm->setRange(0.25, 0.80);
        m_densityMm->setValue(0.40);
        m_densityMm->setSingleStep(0.05);
        m_densityMm->setSuffix(QStringLiteral(" mm"));
        f->addRow(QStringLiteral("Stichdichte (Abstand):"), m_densityMm);

        auto* angleRow = new QHBoxLayout;
        m_fillAngle = new QDoubleSpinBox;
        m_fillAngle->setRange(0.0, 360.0);
        m_fillAngle->setValue(0.0);
        m_fillAngle->setSingleStep(15.0);
        m_fillAngle->setSuffix(QStringLiteral("°"));
        m_multiAngle = new QCheckBox(QStringLiteral("Multi-Winkel (+45°/Farbe)"));
        m_multiAngle->setChecked(false);
        angleRow->addWidget(m_fillAngle, 1);
        angleRow->addWidget(m_multiAngle, 1);
        f->addRow(QStringLiteral("Füllwinkel:"), angleRow);

        auto* stabRow = new QHBoxLayout;
        m_underlay = new QCheckBox(QStringLiteral("Unterleger (Randlauf + 90°-Gitter)"));
        m_underlay->setChecked(true);
        m_pullComp = new QDoubleSpinBox;
        m_pullComp->setRange(0.0, 0.80);
        m_pullComp->setValue(0.25);
        m_pullComp->setSingleStep(0.05);
        m_pullComp->setSuffix(QStringLiteral(" mm"));
        stabRow->addWidget(m_underlay, 1);
        stabRow->addWidget(new QLabel(QStringLiteral("Zugausgleich:")), 0);
        stabRow->addWidget(m_pullComp, 0);
        f->addRow(QStringLiteral("Stoff-Stabilität:"), stabRow);

        m_smoothing = new QDoubleSpinBox;
        m_smoothing->setRange(0.2, 3.0);
        m_smoothing->setValue(1.0);
        m_smoothing->setSingleStep(0.2);
        f->addRow(QStringLiteral("Vektor-Glättung:"), m_smoothing);

        auto* borderRow = new QHBoxLayout;
        m_satinBorder = new QCheckBox(QStringLiteral("Satin-Kettelrand"));
        m_satinBorder->setChecked(false);
        m_borderWidth = new QDoubleSpinBox;
        m_borderWidth->setRange(0.8, 4.0);
        m_borderWidth->setValue(1.5);
        m_borderWidth->setSingleStep(0.2);
        m_borderWidth->setSuffix(QStringLiteral(" mm"));
        borderRow->addWidget(m_satinBorder, 1);
        borderRow->addWidget(new QLabel(QStringLiteral("Breite:")), 0);
        borderRow->addWidget(m_borderWidth, 0);
        f->addRow(QStringLiteral("Kettelkante:"), borderRow);

        m_stack->addWidget(w);
    }

    // --- Page 1: Sfumato Photo-Stitch -------------------------------------
    {
        auto* w = new QWidget;
        auto* f = new QFormLayout(w);
        f->setSpacing(6);
        f->setContentsMargins(0, 0, 0, 0);

        m_sfumatoLineSpacing = new QDoubleSpinBox;
        m_sfumatoLineSpacing->setRange(0.8, 4.0);
        m_sfumatoLineSpacing->setValue(1.4);
        m_sfumatoLineSpacing->setSingleStep(0.1);
        m_sfumatoLineSpacing->setSuffix(QStringLiteral(" mm"));
        f->addRow(QStringLiteral("Zeilenabstand:"), m_sfumatoLineSpacing);

        m_sfumatoAmplitude = new QDoubleSpinBox;
        m_sfumatoAmplitude->setRange(0.2, 4.0);
        m_sfumatoAmplitude->setValue(0.9);
        m_sfumatoAmplitude->setSingleStep(0.1);
        m_sfumatoAmplitude->setSuffix(QStringLiteral(" mm"));
        f->addRow(QStringLiteral("Wellenamplitude:"), m_sfumatoAmplitude);

        m_sfumatoAngle = new QComboBox;
        m_sfumatoAngle->addItem(QStringLiteral("Horizontal (0° Zeilen-Sweep)"), int(SfumatoGenerator::AngleMode::Horizontal));
        m_sfumatoAngle->addItem(QStringLiteral("Vertikal (90° Spalten-Sweep)"), int(SfumatoGenerator::AngleMode::Vertical));
        m_sfumatoAngle->addItem(QStringLiteral("Kreuzschraffur (0° + 90° Orthogonal-Pass)"), int(SfumatoGenerator::AngleMode::CrossHatch));
        f->addRow(QStringLiteral("Stichrichtung:"), m_sfumatoAngle);

        m_sfumatoColorBtn = new QPushButton(QStringLiteral("Garnfarbe wählen"));
        m_sfumatoColorBtn->setIcon(colorSwatchIcon(QColor(40, 35, 30)));
        connect(m_sfumatoColorBtn, &QPushButton::clicked, this, [this]{
            QColor c = QColorDialog::getColor(m_params.sfumatoColor, this, QStringLiteral("Sfumato Garnfarbe wählen"));
            if (c.isValid()) {
                m_params.sfumatoColor = c;
                m_sfumatoColorBtn->setIcon(colorSwatchIcon(c));
                refreshPreview();
            }
        });
        f->addRow(QStringLiteral("Stickgarn:"), m_sfumatoColorBtn);

        auto* hint = caption(QStringLiteral("Sfumato erzeugt fotorealistische Schattierungen über eine kontinuierliche, dichte-modulierte Sinuslinie."));
        hint->setAlignment(Qt::AlignLeft); hint->setWordWrap(true);
        f->addRow(hint);
        m_stack->addWidget(w);
    }

    // --- Page 2: Cross-Stitch ---------------------------------------------
    {
        auto* w = new QWidget;
        auto* f = new QFormLayout(w);
        f->setSpacing(6);
        f->setContentsMargins(0, 0, 0, 0);

        m_crossAida = new QComboBox;
        m_crossAida->addItem(CrossStitchGenerator::aidaName(CrossStitchGenerator::AidaCount::Count11), int(CrossStitchGenerator::AidaCount::Count11));
        m_crossAida->addItem(CrossStitchGenerator::aidaName(CrossStitchGenerator::AidaCount::Count14), int(CrossStitchGenerator::AidaCount::Count14));
        m_crossAida->addItem(CrossStitchGenerator::aidaName(CrossStitchGenerator::AidaCount::Count16), int(CrossStitchGenerator::AidaCount::Count16));
        m_crossAida->addItem(CrossStitchGenerator::aidaName(CrossStitchGenerator::AidaCount::Count18), int(CrossStitchGenerator::AidaCount::Count18));
        m_crossAida->setCurrentIndex(1); // 14ct default
        f->addRow(QStringLiteral("Aida-Gewebe:"), m_crossAida);

        m_crossStyle = new QComboBox;
        m_crossStyle->addItem(QStringLiteral("Klassisches Vollkreuz (/ dann \\)"), int(CrossStitchGenerator::StitchStyle::FullCross));
        m_crossStyle->addItem(QStringLiteral("Smyrna- / Sternkreuz (X + + für 3D-Relief)"), int(CrossStitchGenerator::StitchStyle::DoubleCross));
        m_crossStyle->addItem(QStringLiteral("Halbes Kreuz (Petit Point /)"), int(CrossStitchGenerator::StitchStyle::HalfCross));
        f->addRow(QStringLiteral("Kreuzstich-Art:"), m_crossStyle);

        auto* hint = caption(QStringLiteral("Traditionelle Zählstickerei auf standardisierten Aida-Rasterzellen für Trachten & historische Motive."));
        hint->setAlignment(Qt::AlignLeft); hint->setWordWrap(true);
        f->addRow(hint);
        m_stack->addWidget(w);
    }

    // --- Page 3: LineArt --------------------------------------------------
    {
        auto* w = new QWidget;
        auto* f = new QFormLayout(w);
        f->setSpacing(6);
        f->setContentsMargins(0, 0, 0, 0);

        m_lineArtStitch = new QComboBox;
        m_lineArtStitch->addItem(QStringLiteral("Einfacher Laufstich (Filigran 1-Pass)"), int(ImageDigitizer::LineArtStitch::SingleRun));
        m_lineArtStitch->addItem(QStringLiteral("Dreifachstich / Bean Stitch (3-Pass Relief)"), int(ImageDigitizer::LineArtStitch::BeanStitch));
        m_lineArtStitch->addItem(QStringLiteral("Fünffachstich (5-Pass schweres Relief)"), int(ImageDigitizer::LineArtStitch::FivePass));
        m_lineArtStitch->setCurrentIndex(1); // Bean stitch default
        f->addRow(QStringLiteral("Stichart:"), m_lineArtStitch);

        m_runMm = new QDoubleSpinBox;
        m_runMm->setRange(1.0, 4.0);
        m_runMm->setValue(2.2);
        m_runMm->setSingleStep(0.2);
        m_runMm->setSuffix(QStringLiteral(" mm"));
        f->addRow(QStringLiteral("Stichlänge:"), m_runMm);

        m_edgeThreshold = new QSlider(Qt::Horizontal);
        m_edgeThreshold->setRange(20, 160);
        m_edgeThreshold->setValue(60);
        f->addRow(QStringLiteral("Kanten-Schärfe:"), m_edgeThreshold);

        auto* hint = caption(QStringLiteral("Zeichnet Kanten als erhabene Handstiche nach – ideal für Skizzen und Comics."));
        hint->setAlignment(Qt::AlignLeft); hint->setWordWrap(true);
        f->addRow(hint);
        m_stack->addWidget(w);
    }

    // --- Page 4: Portrait -------------------------------------------------
    {
        auto* w = new QWidget;
        auto* f = new QFormLayout(w);
        f->setSpacing(6);
        f->setContentsMargins(0, 0, 0, 0);

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
        m_tones = new QSpinBox;
        m_tones->setRange(2, 6);
        m_tones->setValue(3);
        f->addRow(m_tonesLabel, m_tones);

        auto* hint = caption(QStringLiteral("Comic = klares Konterfei. Silhouette = kräftig. Graustufen = Vektor-Tatami in Schichten."));
        hint->setAlignment(Qt::AlignLeft); hint->setWordWrap(true);
        f->addRow(hint);
        m_stack->addWidget(w);
    }

    ctrlCol->addWidget(m_stack);

    // --- Interactive Palette Manager (Colors Mode) -------------------------
    m_paletteContainer = new QWidget;
    auto* palLayout = new QVBoxLayout(m_paletteContainer);
    palLayout->setContentsMargins(0, 4, 0, 4);
    palLayout->setSpacing(4);

    auto* palHeader = new QHBoxLayout;
    auto* palTitle = new QLabel(QStringLiteral("Garnfarben-Manager (K-Means Palette):"));
    palTitle->setStyleSheet(QStringLiteral("font-weight:bold; color:#cbd5e1;"));
    palHeader->addWidget(palTitle);

    m_btnDropBg = new QPushButton(QStringLiteral("Hintergrund abwählen"));
    m_btnDropBg->setStyleSheet(QStringLiteral("padding:2px 8px; font-size:11px;"));
    connect(m_btnDropBg, &QPushButton::clicked, this, &ImageDialog::autoDropBackground);
    palHeader->addWidget(m_btnDropBg);

    m_btnAllColors = new QPushButton(QStringLiteral("Alle an"));
    m_btnAllColors->setStyleSheet(QStringLiteral("padding:2px 8px; font-size:11px;"));
    connect(m_btnAllColors, &QPushButton::clicked, this, [this]{ selectAllColors(true); });
    palHeader->addWidget(m_btnAllColors);

    palLayout->addLayout(palHeader);

    m_paletteTable = new QTableWidget;
    m_paletteTable->setColumnCount(4);
    m_paletteTable->setHorizontalHeaderLabels({ QStringLiteral("Sticken"), QStringLiteral("Farbe"), QStringLiteral("Garn (Madeira)"), QStringLiteral("Fläche") });
    m_paletteTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_paletteTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_paletteTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_paletteTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_paletteTable->verticalHeader()->setVisible(false);
    m_paletteTable->setMaximumHeight(130);
    m_paletteTable->setSelectionMode(QAbstractItemView::NoSelection);
    connect(m_paletteTable, &QTableWidget::cellChanged, this, &ImageDialog::onPaletteItemChanged);
    palLayout->addWidget(m_paletteTable);

    ctrlCol->addWidget(m_paletteContainer);

    // --- Common Image Adjustments ------------------------------------------
    auto* sharedBox = new QWidget;
    auto* sf = new QFormLayout(sharedBox);
    sf->setSpacing(6);
    sf->setContentsMargins(0, 4, 0, 0);

    auto* sliderRow1 = new QHBoxLayout;
    m_contrast = new QSlider(Qt::Horizontal);
    m_contrast->setRange(50, 250);
    m_contrast->setValue(115);
    m_brightness = new QSlider(Qt::Horizontal);
    m_brightness->setRange(-50, 50);
    m_brightness->setValue(0);
    sliderRow1->addWidget(new QLabel(QStringLiteral("Kontrast:")), 0);
    sliderRow1->addWidget(m_contrast, 1);
    sliderRow1->addWidget(new QLabel(QStringLiteral("Helligkeit:")), 0);
    sliderRow1->addWidget(m_brightness, 1);
    sf->addRow(sliderRow1);

    auto* sliderRow2 = new QHBoxLayout;
    m_smooth = new QSlider(Qt::Horizontal);
    m_smooth->setRange(0, 4);
    m_smooth->setValue(0);
    m_invert = new QCheckBox(QStringLiteral("Invertieren"));
    sliderRow2->addWidget(new QLabel(QStringLiteral("Entrauschen:")), 0);
    sliderRow2->addWidget(m_smooth, 1);
    sliderRow2->addWidget(m_invert, 0);
    sf->addRow(sliderRow2);

    auto* geomRow = new QHBoxLayout;
    m_resolution = new QComboBox;
    m_resolution->addItem(QStringLiteral("Standard (260 px)"), 260);
    m_resolution->addItem(QStringLiteral("Fein / Scharf (380 px)"), 380);
    m_resolution->addItem(QStringLiteral("Ultra-Detail (500 px)"), 500);
    m_resolution->setCurrentIndex(1); // 380px default for crisp skull/features

    m_widthMm = new QSpinBox;
    m_widthMm->setRange(20, 250);
    m_widthMm->setValue(100);
    m_widthMm->setSuffix(QStringLiteral(" mm"));

    geomRow->addWidget(new QLabel(QStringLiteral("Detailgrad:")), 0);
    geomRow->addWidget(m_resolution, 1);
    geomRow->addWidget(new QLabel(QStringLiteral("Zielbreite:")), 0);
    geomRow->addWidget(m_widthMm, 0);
    sf->addRow(geomRow);

    m_faceCrop = new QCheckBox(QStringLiteral("Gesichtsbereich automatisch zuschneiden (OpenCV)"));
    if (OpenCvBridge::available()) {
        sf->addRow(m_faceCrop);
    } else {
        m_faceCrop->hide();
    }
    ctrlCol->addWidget(sharedBox);
    ctrlCol->addStretch(1);

    mid->addLayout(ctrlCol, 1);
    root->addLayout(mid, 1);

    // --- Bottom Action Buttons ---------------------------------------------
    auto* btnRow = new QHBoxLayout;
    auto* btnCancel = new QPushButton(QStringLiteral("Abbrechen"));
    btnCancel->setStyleSheet(QStringLiteral("padding:6px 14px;"));
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addWidget(btnCancel);

    btnRow->addStretch(1);

    auto* btnVectorPaths = new QPushButton(QStringLiteral("✏️ Als Vektorpfade in Editor (Bitmap to Path)"));
    btnVectorPaths->setStyleSheet(QStringLiteral(
        "padding:6px 16px; font-weight:bold; background:#0f766e; color:#ffffff; border-radius:4px;"));
    connect(btnVectorPaths, &QPushButton::clicked, this, [this]{
        collectParams();
        m_extractedPaths = ImageDigitizer::traceToPaths(m_source, m_params);
        m_applyMode = ApplyMode::VectorPaths;
        accept();
    });
    btnRow->addWidget(btnVectorPaths);

    auto* btnStitches = new QPushButton(QStringLiteral("🧵 Als Stickmuster übernehmen ▶"));
    btnStitches->setStyleSheet(QStringLiteral(
        "padding:6px 18px; font-weight:bold; background:#2563eb; color:#ffffff; border-radius:4px;"));
    btnStitches->setDefault(true);
    connect(btnStitches, &QPushButton::clicked, this, [this]{
        collectParams();
        m_applyMode = ApplyMode::Stitches;
        accept();
    });
    btnRow->addWidget(btnStitches);

    root->addLayout(btnRow);

    // --- Wiring & Debounce -------------------------------------------------
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(80);
    connect(m_debounce, &QTimer::timeout, this, &ImageDialog::refreshPreview);
    auto kick = [this]{ m_debounce->start(); };

    auto syncModeUi = [this, kick]{
        const int idx = m_mode->currentIndex();
        m_stack->setCurrentIndex(idx);
        m_paletteContainer->setVisible(idx == 0); // Palette only in Colors mode
        if (idx == 4) {
            const bool tones = ImageDigitizer::PortraitStyle(m_portraitStyle->currentData().toInt())
                               == ImageDigitizer::PortraitStyle::Tones;
            m_tonesLabel->setVisible(tones);
            m_tones->setVisible(tones);
        }
        kick();
    };

    connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, syncModeUi);
    connect(m_previewMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ImageDialog::updatePreviewDisplay);
    connect(m_fabricCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ImageDialog::updatePreviewDisplay);

    // Colors page connections
    connect(m_separation, QOverload<int>::of(&QComboBox::currentIndexChanged), this, kick);
    connect(m_stencilSlider, &QSlider::valueChanged, this, kick);
    connect(m_colors, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, kick](int val){
        const bool is1 = (val == 1);
        m_stencilLabel->setVisible(is1);
        m_stencilSlider->setVisible(is1);
        m_separation->setEnabled(!is1);
        m_params.activeColors.clear();
        kick();
    });
    connect(m_tatamiPattern, QOverload<int>::of(&QComboBox::currentIndexChanged), this, kick);
    connect(m_densityMm, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, kick);
    connect(m_fillAngle, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, kick);
    connect(m_multiAngle, &QCheckBox::toggled, this, kick);
    connect(m_underlay, &QCheckBox::toggled, this, kick);
    connect(m_pullComp, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, kick);
    connect(m_smoothing, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, kick);
    connect(m_satinBorder, &QCheckBox::toggled, this, kick);
    connect(m_borderWidth, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, kick);

    // Sfumato connections
    connect(m_sfumatoLineSpacing, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, kick);
    connect(m_sfumatoAmplitude, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, kick);
    connect(m_sfumatoAngle, QOverload<int>::of(&QComboBox::currentIndexChanged), this, kick);

    // CrossStitch connections
    connect(m_crossAida, QOverload<int>::of(&QComboBox::currentIndexChanged), this, kick);
    connect(m_crossStyle, QOverload<int>::of(&QComboBox::currentIndexChanged), this, kick);

    // LineArt connections
    connect(m_lineArtStitch, QOverload<int>::of(&QComboBox::currentIndexChanged), this, kick);
    connect(m_runMm, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, kick);
    connect(m_edgeThreshold, &QSlider::valueChanged, this, kick);

    // Portrait connections
    connect(m_portraitStyle, QOverload<int>::of(&QComboBox::currentIndexChanged), this, syncModeUi);
    connect(m_tones, QOverload<int>::of(&QSpinBox::valueChanged), this, kick);

    // Shared adjustments connections
    connect(m_contrast, &QSlider::valueChanged, this, kick);
    connect(m_brightness, &QSlider::valueChanged, this, kick);
    connect(m_smooth, &QSlider::valueChanged, this, kick);
    connect(m_invert, &QCheckBox::toggled, this, kick);
    connect(m_resolution, QOverload<int>::of(&QComboBox::currentIndexChanged), this, kick);
    connect(m_widthMm, QOverload<int>::of(&QSpinBox::valueChanged), this, kick);
    connect(m_faceCrop, &QCheckBox::toggled, this, kick);

    // Setup initial view
    if (!m_source.isNull()) {
        m_before->setPixmap(QPixmap::fromImage(m_source).scaled(
            m_before->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    syncModeUi();
    refreshPreview();
}

QColor ImageDialog::currentFabricColor() const
{
    switch (m_fabricCombo ? m_fabricCombo->currentIndex() : 0) {
    case 0: return QColor(24, 26, 32);     // Dunkelgrau / Schwarz
    case 1: return QColor(228, 220, 204);  // Helles Leinen / Beige
    case 2: return QColor(250, 250, 252);  // Reines Weiß
    case 3: return QColor(18, 30, 52);     // Marineblau
    case 4: return QColor(36, 54, 38);     // Loden / Jagdgrün
    case 5: return QColor(88, 16, 26);     // Rubinrot
    default: return QColor(24, 26, 32);
    }
}

void ImageDialog::collectParams()
{
    ImageDigitizer::Params p;
    p.mode          = ImageDigitizer::Mode(m_mode->currentData().toInt());
    p.widthMm       = m_widthMm->value();
    p.maxProcessPx  = m_resolution->currentData().toInt();

    // Shared adjustments
    p.contrast      = m_contrast->value() / 100.0;
    p.brightness    = m_brightness->value() / 100.0;
    p.blur          = m_smooth->value();
    p.invert        = m_invert->isChecked();
    p.autoFaceCrop  = m_faceCrop->isChecked();

    // Colors (Vector-Tatami & Bitmap to Path)
    p.separation    = ImageDigitizer::SeparationMode(m_separation->currentData().toInt());
    p.colors        = m_colors->value();
    p.stencilThreshold = (p.colors == 1 && m_stencilSlider) ? m_stencilSlider->value() : -1;
    p.tatamiPattern = TatamiFill::PatternType(m_tatamiPattern->currentData().toInt());
    p.densityMm     = m_densityMm->value();
    p.fillAngleDeg  = m_fillAngle->value();
    p.multiAngle    = m_multiAngle->isChecked();
    p.underlay      = m_underlay->isChecked();
    p.pullCompMm    = m_pullComp->value();
    p.smoothing     = m_smoothing->value();
    p.satinBorder   = m_satinBorder->isChecked();
    p.borderWidthMm = m_borderWidth->value();

    // Preserve active colors from palette table
    if (!m_currentPalette.empty()) {
        p.activeColors.resize(m_currentPalette.size());
        for (size_t i = 0; i < m_currentPalette.size(); ++i) {
            p.activeColors[i] = m_currentPalette[i].active;
        }
    }

    // Sfumato
    p.sfumatoLineSpacingMm = m_sfumatoLineSpacing->value();
    p.sfumatoAmplitudeMm   = m_sfumatoAmplitude->value();
    p.sfumatoAngle         = SfumatoGenerator::AngleMode(m_sfumatoAngle->currentData().toInt());
    p.sfumatoColor         = m_params.sfumatoColor;

    // Cross-Stitch
    p.crossStitchAida  = CrossStitchGenerator::AidaCount(m_crossAida->currentData().toInt());
    p.crossStitchStyle = CrossStitchGenerator::StitchStyle(m_crossStyle->currentData().toInt());

    // LineArt
    p.lineArtStitch = ImageDigitizer::LineArtStitch(m_lineArtStitch->currentData().toInt());
    p.runMm         = m_runMm->value();
    p.edgeThreshold = m_edgeThreshold->value();

    // Portrait
    p.portraitStyle = ImageDigitizer::PortraitStyle(m_portraitStyle->currentData().toInt());
    p.tones         = m_tones->value();

    m_params = p;
}

void ImageDialog::rebuildPaletteTable()
{
    if (!m_paletteTable || m_params.mode != ImageDigitizer::Mode::Colors) return;

    m_updatingPaletteUi = true;
    m_currentPalette = ImageDigitizer::extractPalette(m_source, m_params);

    m_paletteTable->setRowCount(int(m_currentPalette.size()));
    for (int i = 0; i < int(m_currentPalette.size()); ++i) {
        const auto& qc = m_currentPalette[size_t(i)];

        // Checkbox "Sticken"
        auto* chkItem = new QTableWidgetItem(qc.active ? QStringLiteral("Ja") : QStringLiteral("Nein"));
        chkItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        chkItem->setCheckState(qc.active ? Qt::Checked : Qt::Unchecked);
        m_paletteTable->setItem(i, 0, chkItem);

        // Color Swatch
        auto* colItem = new QTableWidgetItem(colorSwatchIcon(qc.rgb), QString());
        colItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_paletteTable->setItem(i, 1, colItem);

        // Madeira Thread Name
        QString thName = qc.thread.description.isEmpty() ? qc.rgb.name().toUpper() : qc.thread.description;
        auto* nameItem = new QTableWidgetItem(thName);
        nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        if (qc.isBackground) nameItem->setText(nameItem->text() + QStringLiteral(" (Hintergrund)"));
        m_paletteTable->setItem(i, 2, nameItem);

        // Coverage %
        auto* pctItem = new QTableWidgetItem(QString::number(qc.percentage, 'f', 1) + QStringLiteral(" %"));
        pctItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        pctItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_paletteTable->setItem(i, 3, pctItem);
    }
    m_updatingPaletteUi = false;
}

void ImageDialog::onPaletteItemChanged(int row, int column)
{
    if (m_updatingPaletteUi || column != 0) return;
    if (row < 0 || size_t(row) >= m_currentPalette.size()) return;

    auto* item = m_paletteTable->item(row, column);
    if (!item) return;

    const bool checked = (item->checkState() == Qt::Checked);
    item->setText(checked ? QStringLiteral("Ja") : QStringLiteral("Nein"));
    m_currentPalette[size_t(row)].active = checked;

    m_params.activeColors.resize(m_currentPalette.size());
    for (size_t i = 0; i < m_currentPalette.size(); ++i) {
        m_params.activeColors[i] = m_currentPalette[i].active;
    }

    refreshPreview();
}

void ImageDialog::autoDropBackground()
{
    if (m_currentPalette.empty()) return;
    m_updatingPaletteUi = true;

    // Find background index
    int bgIdx = -1;
    double maxBorder = -1.0;
    for (int i = 0; i < int(m_currentPalette.size()); ++i) {
        if (m_currentPalette[size_t(i)].isBackground) {
            bgIdx = i;
            break;
        }
        if (m_currentPalette[size_t(i)].percentage > maxBorder) {
            maxBorder = m_currentPalette[size_t(i)].percentage;
            bgIdx = i;
        }
    }

    for (int i = 0; i < int(m_currentPalette.size()); ++i) {
        bool act = (i != bgIdx);
        m_currentPalette[size_t(i)].active = act;
        auto* item = m_paletteTable->item(i, 0);
        if (item) {
            item->setCheckState(act ? Qt::Checked : Qt::Unchecked);
            item->setText(act ? QStringLiteral("Ja") : QStringLiteral("Nein"));
        }
    }

    m_params.activeColors.resize(m_currentPalette.size());
    for (size_t i = 0; i < m_currentPalette.size(); ++i) {
        m_params.activeColors[i] = m_currentPalette[i].active;
    }

    m_updatingPaletteUi = false;
    refreshPreview();
}

void ImageDialog::selectAllColors(bool checked)
{
    if (m_currentPalette.empty()) return;
    m_updatingPaletteUi = true;

    for (int i = 0; i < int(m_currentPalette.size()); ++i) {
        m_currentPalette[size_t(i)].active = checked;
        auto* item = m_paletteTable->item(i, 0);
        if (item) {
            item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
            item->setText(checked ? QStringLiteral("Ja") : QStringLiteral("Nein"));
        }
    }

    m_params.activeColors.resize(m_currentPalette.size());
    for (size_t i = 0; i < m_currentPalette.size(); ++i) {
        m_params.activeColors[i] = m_currentPalette[i].active;
    }

    m_updatingPaletteUi = false;
    refreshPreview();
}

void ImageDialog::refreshPreview()
{
    if (m_source.isNull()) return;
    collectParams();

    if (m_params.mode == ImageDigitizer::Mode::Colors &&
        (m_currentPalette.empty() || m_currentPalette.size() != size_t(m_params.colors))) {
        rebuildPaletteTable();
    }

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
        const int previewW = std::max(120, m_after->width());
        QImage thumb = StitchThumbnail::render(m_latestPreviewSeq, previewW, currentFabricColor(), true);
        m_after->setPixmap(QPixmap::fromImage(thumb));
    } else if (m_previewMode->currentIndex() == 1) {
        // Vector Bézier paths (Bitmap to Path wie Inkscape)
        m_extractedPaths = ImageDigitizer::traceToPaths(m_source, m_params);
        const QSize targetSize = (m_after->width() >= 32 && m_after->height() >= 32)
            ? m_after->size() : QSize(400, 400);
        QImage vecImg(targetSize, QImage::Format_ARGB32_Premultiplied);
        vecImg.fill(currentFabricColor());
        QPainter p(&vecImg);
        p.setRenderHint(QPainter::Antialiasing, true);

        double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
        for (const auto& ep : m_extractedPaths) {
            for (const auto& node : ep.nodes) {
                minX = std::min(minX, node.pos.x());
                maxX = std::max(maxX, node.pos.x());
                minY = std::min(minY, node.pos.y());
                maxY = std::max(maxY, node.pos.y());
            }
        }
        if (maxX > minX && maxY > minY) {
            double wMm = maxX - minX;
            double hMm = maxY - minY;
            double scale = std::min((vecImg.width() * 0.85) / wMm, (vecImg.height() * 0.85) / hMm);
            double cxPx = vecImg.width() * 0.5;
            double cyPx = vecImg.height() * 0.5;
            double midXMm = (minX + maxX) * 0.5;
            double midYMm = (minY + maxY) * 0.5;

            for (const auto& ep : m_extractedPaths) {
                QPainterPath pp = ep.toPainterPath();
                QTransform xf;
                xf.translate(cxPx, cyPx);
                xf.scale(scale, -scale);
                xf.translate(-midXMm, -midYMm);

                QPainterPath screenPath = xf.map(pp);

                QColor fillCol = ep.color;
                fillCol.setAlpha(70);
                p.setBrush(fillCol);
                p.setPen(QPen(ep.color.lighter(130), 1.8));
                p.drawPath(screenPath);

                p.setPen(Qt::NoPen);
                p.setBrush(QColor(255, 255, 255, 220));
                for (const auto& node : ep.nodes) {
                    QPointF sp = xf.map(node.pos);
                    p.drawRect(QRectF(sp.x() - 1.5, sp.y() - 1.5, 3.0, 3.0));
                }
            }
        }
        p.end();
        m_after->setPixmap(QPixmap::fromImage(vecImg));
    } else if (!m_latestAnalysisImg.isNull()) {
        // Image analysis / color clusters
        m_after->setPixmap(QPixmap::fromImage(m_latestAnalysisImg).scaled(
            m_after->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
    }

    // Update live metrics badge
    if (m_metricsLabel) {
        if (m_previewMode->currentIndex() == 1) {
            double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            if (!m_extractedPaths.isEmpty()) {
                m_latestPreviewSeq.bounds(x0, y0, x1, y1);
            }
            m_metricsLabel->setText(QStringLiteral(
                "✏️ %1 Vektorpfade extrahiert  |  🎨 %2 Farben/Töne  |  📐 %3 × %4 mm  |  Bézier-Splines für Editor")
                .arg(m_extractedPaths.size())
                .arg(m_params.colors)
                .arg(std::max(0.0, x1 - x0), 0, 'f', 1)
                .arg(std::max(0.0, y1 - y0), 0, 'f', 1));
        } else if (m_latestPreviewSeq.empty()) {
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
