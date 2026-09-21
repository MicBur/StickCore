// ---------------------------------------------------------------------------
//  StickCore  –  ImageDialog.cpp
// ---------------------------------------------------------------------------
#include "ui/ImageDialog.h"
#include "core/MachineProfile.h"
#include "generators/OpenCvBridge.h"

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
    l->setFixedSize(240, 240);
    l->setStyleSheet(QStringLiteral("background:#0f1116;border:1px solid #2c313d;border-radius:8px;"));
    return l;
}
} // namespace

ImageDialog::ImageDialog(const QImage& source, QWidget* parent)
    : QDialog(parent), m_source(source)
{
    setWindowTitle(QStringLiteral("Foto digitalisieren"));
    setMinimumSize(720, 520);

    auto* root = new QVBoxLayout(this);

    // --- mode selector -----------------------------------------------------
    auto* modeRow = new QHBoxLayout;
    modeRow->addWidget(new QLabel(QStringLiteral("Modus:")));
    m_mode = new QComboBox;
    m_mode->addItem(QStringLiteral("🎨 Farben reduzieren (mehrfarbig)"), int(ImageDigitizer::Mode::Colors));
    m_mode->addItem(QStringLiteral("👤 Konterfei (ein-/wenigfarbig, Comic)"), int(ImageDigitizer::Mode::Portrait));
    m_mode->addItem(QStringLiteral("✏ Umriss / Linien (Kontur-Stiche)"), int(ImageDigitizer::Mode::LineArt));
    m_mode->setCurrentIndex(1);   // Portrait is the headline feature
    modeRow->addWidget(m_mode, 1);
    root->addLayout(modeRow);

    // --- middle: previews + controls --------------------------------------
    auto* mid = new QHBoxLayout;

    // previews
    auto* prev = new QGridLayout;
    m_before = previewBox();
    m_after  = previewBox();
    prev->addWidget(caption(QStringLiteral("Original")),          0, 0);
    prev->addWidget(caption(QStringLiteral("So wird gestickt")),  0, 1);
    prev->addWidget(m_before, 1, 0);
    prev->addWidget(m_after,  1, 1);
    prev->setRowStretch(2, 1);   // keep the boxes at the top, no vertical stretch
    mid->addLayout(prev, 0);

    // controls (stacked by mode + shared block)
    auto* ctrlCol = new QVBoxLayout;
    m_stack = new QStackedWidget;

    // page 0 – Colors
    {
        auto* w = new QWidget; auto* f = new QFormLayout(w);
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

        auto* hint = caption(QStringLiteral("Garnfarben mit wechselndem Stichwinkel gegen Stoffverzug."));
        hint->setAlignment(Qt::AlignLeft);
        f->addRow(hint);
        m_stack->addWidget(w);
    }
    // page 1 – Portrait
    {
        auto* w = new QWidget; auto* f = new QFormLayout(w);
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
        auto* hint = caption(QStringLiteral("Die Umrisse werden als Laufstiche gestickt —\nwie eine Zeichnung."));
        hint->setAlignment(Qt::AlignLeft); hint->setWordWrap(true);
        f->addRow(hint);
        m_stack->addWidget(w);
    }
    ctrlCol->addWidget(m_stack);

    // shared block (threshold / contrast / smooth / invert) — hidden in Colors
    auto* shared = new QWidget;
    auto* sf = new QFormLayout(shared);

    m_autoThresh = new QCheckBox(QStringLiteral("Schwellwert automatisch (Otsu)"));
    m_autoThresh->setChecked(true);
    sf->addRow(m_autoThresh);

    m_thresh = new QSlider(Qt::Horizontal); m_thresh->setRange(0, 255); m_thresh->setValue(128);
    m_thresh->setEnabled(false);
    sf->addRow(QStringLiteral("Schwellwert:"), m_thresh);

    m_contrast = new QSlider(Qt::Horizontal); m_contrast->setRange(50, 300); m_contrast->setValue(140);
    sf->addRow(QStringLiteral("Kontrast:"), m_contrast);

    m_smooth = new QSlider(Qt::Horizontal); m_smooth->setRange(0, 4); m_smooth->setValue(1);
    sf->addRow(QStringLiteral("Glätten:"), m_smooth);

    m_invert = new QCheckBox(QStringLiteral("Umkehren (hell/dunkel tauschen)"));
    sf->addRow(m_invert);

    // OpenCV-only option: automatic face crop
    m_faceCrop = new QCheckBox(QStringLiteral("Gesicht automatisch erkennen und zuschneiden"));
    m_fineDetail = new QCheckBox;   // kept for params; the Portrait "Stil" replaces it
    m_fineDetail->setChecked(true); m_fineDetail->hide();
    m_faceCrop->setChecked(true);   // best default for portraits
    if (OpenCvBridge::available()) {
        sf->addRow(m_faceCrop);
    } else {
        m_faceCrop->setChecked(false);   m_faceCrop->hide();
        auto* note = caption(QStringLiteral("(Foto-KI ohne OpenCV: einfache Schwellwert-Reduktion)"));
        note->setAlignment(Qt::AlignLeft);
        sf->addRow(note);
    }
    ctrlCol->addWidget(shared);

    // width
    auto* wf = new QFormLayout;
    m_widthMm = new QSpinBox; m_widthMm->setRange(20, 200); m_widthMm->setValue(100);
    m_widthMm->setSuffix(QStringLiteral(" mm"));
    wf->addRow(QStringLiteral("Breite:"), m_widthMm);
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
    m_debounce->setInterval(90);
    connect(m_debounce, &QTimer::timeout, this, &ImageDialog::refreshPreview);
    auto kick = [this]{ m_debounce->start(); };

    auto syncStyle = [this]{
        const bool tones = ImageDigitizer::PortraitStyle(m_portraitStyle->currentData().toInt())
                           == ImageDigitizer::PortraitStyle::Tones;
        m_tonesLabel->setVisible(tones);
        m_tones->setVisible(tones);
    };
    auto syncMode = [this, shared, syncStyle]{
        const int idx = m_mode->currentIndex();
        m_stack->setCurrentIndex(idx);
        shared->setVisible(idx != 0);   // hide threshold block for Colors
        if (idx == 1) syncStyle();
    };
    connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, syncMode, kick](int){ syncMode(); kick(); });
    connect(m_portraitStyle, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, syncStyle, kick](int){ syncStyle(); kick(); });
    connect(m_autoThresh, &QCheckBox::toggled, this, [this, kick](bool on){
        m_thresh->setEnabled(!on); kick();
    });
    connect(m_thresh,   &QSlider::valueChanged, this, [kick](int){ kick(); });
    connect(m_contrast, &QSlider::valueChanged, this, [kick](int){ kick(); });
    connect(m_smooth,   &QSlider::valueChanged, this, [kick](int){ kick(); });
    connect(m_colors,   QOverload<int>::of(&QSpinBox::valueChanged), this, [kick](int){ kick(); });
    connect(m_fillAngle, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [kick](double){ kick(); });
    connect(m_multiAngle, &QCheckBox::toggled, this, [kick](bool){ kick(); });
    connect(m_tones,    QOverload<int>::of(&QSpinBox::valueChanged), this, [kick](int){ kick(); });
    connect(m_invert,   &QCheckBox::toggled, this, [kick](bool){ kick(); });
    connect(m_faceCrop, &QCheckBox::toggled, this, [kick](bool){ kick(); });
    connect(m_fineDetail, &QCheckBox::toggled, this, [kick](bool){ kick(); });

    // show original once
    if (!m_source.isNull())
        m_before->setPixmap(QPixmap::fromImage(m_source).scaled(
            m_before->minimumSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation));

    syncMode();
    syncStyle();
    refreshPreview();
}

void ImageDialog::collectParams()
{
    ImageDigitizer::Params p;
    p.mode      = ImageDigitizer::Mode(m_mode->currentData().toInt());
    p.widthMm   = m_widthMm->value();
    p.brand     = MachineProfile::current().defaultBrand;
    p.colors    = m_colors->value();
    p.fillAngleDeg = m_fillAngle ? m_fillAngle->value() : 45.0;
    p.multiAngle   = m_multiAngle ? m_multiAngle->isChecked() : true;
    p.portraitStyle = ImageDigitizer::PortraitStyle(m_portraitStyle->currentData().toInt());
    p.tones     = m_tones->value();
    p.contrast  = m_contrast->value() / 100.0;
    p.blur      = m_smooth->value();
    p.invert    = m_invert->isChecked();
    p.threshold = m_autoThresh->isChecked() ? -1 : m_thresh->value();
    p.autoFaceCrop = m_faceCrop->isChecked();
    p.fineDetail   = m_fineDetail->isChecked();
    m_params = p;
}

void ImageDialog::refreshPreview()
{
    if (m_source.isNull()) return;
    collectParams();
    QImage pv = ImageDigitizer::preview(m_source, m_params);
    if (pv.isNull()) return;
    m_after->setPixmap(QPixmap::fromImage(pv).scaled(
        m_after->minimumSize(), Qt::KeepAspectRatio, Qt::FastTransformation));
}

} // namespace stick
