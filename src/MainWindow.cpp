// ---------------------------------------------------------------------------
//  StickCore  –  MainWindow.cpp
//
//  StickCore Studio: 2-D Bézier Editor (left), 3-D Thread Preview & Playback
//  Transport (center), and Machine / Properties / Metrics Panel (right).
// ---------------------------------------------------------------------------
#include "MainWindow.h"
#include "gl/StitchGLWidget.h"
#include "editor/PathEditorWidget.h"
#include "generators/SatinGenerator.h"
#include "generators/TatamiFill.h"
#include "generators/TextDigitizer.h"
#include "generators/MonogramGenerator.h"
#include "generators/HuntingMotifs.h"
#include "generators/LogoGenerator.h"
#include "generators/SvgDigitizer.h"
#include "core/SvgPathParser.h"
#include "generators/ImageDigitizer.h"
#include "generators/AppliqueGenerator.h"
#include "core/ThreadCatalog.h"
#include "core/MachineProfile.h"
#include "codec/JefCodec.h"
#include "codec/DstCodec.h"
#include "net/QrCode.h"
#include "net/QrUploadServer.h"
#include "library/DesignLibrary.h"
#include "library/LibraryDialog.h"
#include "library/DesignFactory.h"
#include "library/StitchThumbnail.h"
#include "ui/WelcomeWizard.h"
#include "ui/ImageDialog.h"
#include "ui/PlacementDialog.h"
#include "ui/ThreadPickDialog.h"
#include "ui/BastingDialog.h"
#include "ui/SfumatoDialog.h"
#include "ui/CrossStitchDialog.h"
#include "ui/MultiHoopDialog.h"
#include "generators/ColorSorter.h"
#include "generators/BastingGenerator.h"
#include "generators/CarvingPattern.h"
#include "generators/SfumatoGenerator.h"
#include "generators/CrossStitchGenerator.h"
#include "generators/MultiHoopSplitter.h"

#include <QApplication>
#include <QTimer>
#include <QSettings>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QToolBar>
#include <QComboBox>
#include <QSplitter>
#include <QFileDialog>
#include <QStatusBar>
#include <QMessageBox>
#include <QLabel>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QImage>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QPainter>
#include <QPixmap>
#include <QListWidget>
#include <QScrollArea>
#include <QFrame>
#include <QStyle>
#include <QIcon>
#include <QSlider>
#include <QPushButton>
#include <QBuffer>
#include <QKeyEvent>
#include <cmath>

namespace {

QPixmap qrPixmap(const std::vector<std::vector<bool>>& m, int scale, int quiet)
{
    if (m.empty()) return QPixmap();
    const int n = int(m.size());
    const int S = (n + 2 * quiet) * scale;
    QImage img(S, S, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    for (int r = 0; r < n; ++r)
        for (int c = 0; c < n; ++c)
            if (m[r][c]) p.fillRect((c + quiet) * scale, (r + quiet) * scale, scale, scale, Qt::black);
    p.end();
    return QPixmap::fromImage(img);
}

QPixmap swatch(const QColor& c, int s = 16)
{
    QPixmap pm(s, s); pm.fill(Qt::transparent);
    QPainter p(&pm); p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(0, 0, 0, 60))); p.setBrush(c);
    p.drawRoundedRect(0, 0, s - 1, s - 1, 4, 4); p.end();
    return pm;
}

QLabel* sectionTitle(const QString& t)
{
    auto* l = new QLabel(t);
    l->setProperty("role", "section");
    return l;
}

void calculateMetrics(const stick::StitchSequence& seq, double& topThreadMeters, double& bobbinMeters, int& estMinutes)
{
    topThreadMeters = 0.0;
    bobbinMeters    = 0.0;
    estMinutes      = 0;
    if (seq.empty()) return;

    double distMm = 0.0;
    const auto& st = seq.stitches;
    for (size_t i = 1; i < st.size(); ++i) {
        if (st[i].flags & (stick::SF_Jump | stick::SF_End)) continue;
        const double dx = st[i].x - st[i - 1].x;
        const double dy = st[i].y - st[i - 1].y;
        distMm += std::hypot(dx, dy);
    }
    // Upper thread: stitch distance + fabric pass-through (~1.3x + 2mm per needle penetration)
    topThreadMeters = (distMm * 1.30 + seq.realStitchCount() * 2.0) / 1000.0;
    bobbinMeters    = topThreadMeters * 0.40;

    // Time: average ~500 stitches/min + 45 sec per color change stop
    const double spm = 500.0;
    double minutes = double(seq.realStitchCount()) / spm;
    minutes += seq.colorChangeCount() * 0.75;
    estMinutes = std::max(1, static_cast<int>(std::ceil(minutes)));
}

} // namespace

namespace stick {

// ---------------------------------------------------------------------------
QString MainWindow::styleSheet()
{
    return QStringLiteral(R"QSS(
QMainWindow, QWidget { background:#12141a; color:#dbe0e9; font-size:13px; }
QMenuBar { background:#181b22; color:#dbe0e9; border-bottom:1px solid #2c313d; }
QMenuBar::item { background:transparent; padding:6px 10px; }
QMenuBar::item:selected { background:#232732; border-radius:4px; color:#2dd4bf; }
QMenu { background:#1b1e26; color:#dbe0e9; border:1px solid #2c313d; padding:4px; }
QMenu::item { padding:6px 24px; border-radius:4px; }
QMenu::item:selected { background:#2dd4bf; color:#04201c; }
QMenu::separator { height:1px; background:#2c313d; margin:4px 8px; }
QToolBar { background:#1b1e26; border:0; border-bottom:1px solid #2c313d; spacing:4px; padding:5px 8px; }
QToolButton { color:#dbe0e9; background:#232732; border:1px solid #2c313d; border-radius:7px; padding:6px 11px; }
QToolButton:hover { background:#2b3040; border-color:#2dd4bf; }
QToolButton:pressed { background:#333a4c; }
QToolBar QLabel { color:#8b93a4; padding:0 4px; }
QComboBox, QDoubleSpinBox, QSpinBox, QLineEdit {
    background:#232732; color:#dbe0e9; border:1px solid #2c313d; border-radius:7px; padding:4px 8px; }
QComboBox::drop-down { border:0; width:18px; }
QComboBox QAbstractItemView { background:#1b1e26; color:#dbe0e9; selection-background-color:#2dd4bf;
    selection-color:#04201c; border:1px solid #2c313d; }
QStatusBar { background:#1b1e26; color:#8b93a4; border-top:1px solid #2c313d; }
QStatusBar::item { border:0; }
QLabel[role="section"] { color:#8b93a4; font-size:11px; font-weight:600; text-transform:uppercase;
    letter-spacing:1px; padding:2px 0; }
QLabel[role="machine"] { color:#2dd4bf; font-size:15px; font-weight:700; }
QLabel[role="ok"]  { color:#37d67a; font-weight:600; }
QLabel[role="bad"] { color:#e5484d; font-weight:600; }
QListWidget { background:#181b22; border:1px solid #2c313d; border-radius:8px; padding:4px; }
QListWidget::item { padding:5px 4px; border-radius:6px; }
QListWidget::item:selected { background:#232732; color:#dbe0e9; }
#InfoPanel { background:#1b1e26; border-left:1px solid #2c313d; }
#Card { background:#171a21; border:1px solid #2c313d; border-radius:10px; }
QScrollArea { border:0; background:transparent; }
QDialog { background:#1b1e26; }
QMessageBox { background:#1b1e26; }
QPushButton { background:#2dd4bf; color:#04201c; border:0; border-radius:7px; padding:7px 13px; font-weight:600; }
QPushButton:hover { background:#3ee0cc; }
#TransportBar { background:#171a21; border-top:1px solid #2c313d; padding:6px 12px; }
#TransportBtn { background:#232732; color:#dbe0e9; border:1px solid #2c313d; border-radius:6px; padding:5px 10px; font-weight:600; min-width:28px; }
#TransportBtn:hover { background:#2b3040; border-color:#2dd4bf; }
#TransportBtn:pressed { background:#333a4c; }
#PlayBtn { background:#2dd4bf; color:#04201c; border:0; border-radius:6px; padding:5px 14px; font-weight:700; min-width:95px; }
#PlayBtn:hover { background:#3ee0cc; }
QSlider::groove:horizontal { height:6px; background:#232732; border-radius:3px; }
QSlider::sub-page:horizontal { background:#2dd4bf; border-radius:3px; }
QSlider::handle:horizontal { background:#dbe0e9; border:1px solid #2c313d; width:14px; margin-top:-4px; margin-bottom:-4px; border-radius:7px; }
QSlider::handle:horizontal:hover { background:#3ee0cc; }
)QSS");
}

// ---------------------------------------------------------------------------
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    const auto& mp = MachineProfile::current();
    setWindowTitle(QStringLiteral("StickCore Studio — %1").arg(mp.name));
    resize(1440, 880);

    m_library = std::make_unique<DesignLibrary>();
    m_library->ensurePopulated();

    m_editor = new PathEditorWidget(this);
    m_view   = new StitchGLWidget(this);
    const HoopSpec initHs = hoopSpec(HoopType::HoopB_140x200);
    m_view->setHoop(initHs.widthMm, initHs.heightMm, QString::fromLatin1(initHs.name));
    m_editor->setHoop(initHs.widthMm, initHs.heightMm, QString::fromLatin1(initHs.name), HoopType::HoopB_140x200);

    connect(m_editor, &PathEditorWidget::designMoved, this, [this](double dx, double dy){
        m_view->setSequence(m_editor->sequence());
        m_current = m_editor->sequence();
        updateInfoPanel();
    });
    connect(m_editor, &PathEditorWidget::hoopSelected, this, &MainWindow::selectHoop);

    QWidget* centerView = buildCenterView();
    QWidget* panel = buildInfoPanel();

    auto* split = new QSplitter(Qt::Horizontal, this);
    split->addWidget(m_editor);
    split->addWidget(centerView);
    split->addWidget(panel);
    split->setStretchFactor(0, 4);
    split->setStretchFactor(1, 6);
    split->setStretchFactor(2, 0);
    split->setSizes({ 480, 660, 300 });
    setCentralWidget(split);

    setupMenus();

    // Studio Quick Toolbar
    QToolBar* tb = addToolBar(QStringLiteral("StudioToolbar"));
    tb->setMovable(false);
    tb->addWidget(new QLabel(QStringLiteral(" Modus ")));
    m_mode = new QComboBox(this);
    m_mode->addItem(QStringLiteral("Satin (2 Rails)"));
    m_mode->addItem(QStringLiteral("Tatami (Polygon)"));
    connect(m_mode, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::modeChanged);
    tb->addWidget(m_mode);

    tb->addAction(QStringLiteral("Neuer Pfad"), m_editor, &PathEditorWidget::newPath);
    tb->addAction(QStringLiteral("Löschen"),    m_editor, &PathEditorWidget::clearAll);
    tb->addAction(QStringLiteral("⚡ Berechnen ▶"), this, &MainWindow::generate);
    tb->addSeparator();

    auto* moveGroup = new QActionGroup(this);
    m_actOrbitMode = tb->addAction(QStringLiteral("🔄 Drehen"), this, [this]{
        m_view->setMouseMode(StitchGLWidget::MouseMode::Orbit);
        statusBar()->showMessage(QStringLiteral("Mausmodus: 3D Orbit-Kamera (Linksklick dreht, Shift+Klick verschiebt Motiv)."), 4000);
    });
    m_actOrbitMode->setCheckable(true);
    m_actOrbitMode->setChecked(true);
    m_actOrbitMode->setToolTip(QStringLiteral("Kamera mit Maus frei drehen (O)"));
    moveGroup->addAction(m_actOrbitMode);

    m_actMoveMode = tb->addAction(QStringLiteral("✥ Bewegen"), this, [this]{
        m_view->setMouseMode(StitchGLWidget::MouseMode::MoveDesign);
        statusBar()->showMessage(QStringLiteral("Mausmodus: Motiv verschieben (Linksklick zieht das Motiv im Rahmen)."), 4000);
    });
    m_actMoveMode->setCheckable(true);
    m_actMoveMode->setToolTip(QStringLiteral("Motiv mit Maus im Rahmen verschieben (V)"));
    moveGroup->addAction(m_actMoveMode);

    tb->addSeparator();
    m_actHoop = tb->addAction(QStringLiteral("⭕ Rahmen"), this, &MainWindow::toggleHoopVisible);
    m_actHoop->setCheckable(true);
    m_actHoop->setChecked(true);
    m_actHoop->setToolTip(QStringLiteral("Stickrahmen und Zentrier-Fadenkreuz ein-/ausblenden (H)"));

    m_actTopDown = tb->addAction(QStringLiteral("👁 2D/3D"), this, &MainWindow::toggleViewPreset);
    m_actTopDown->setToolTip(QStringLiteral("Zwischen 2D-Draufsicht und 3D-Perspektive umschalten (Strg+D)"));

    auto* centerAct = tb->addAction(QStringLiteral("⌖ Zentrieren"), this, &MainWindow::centerCurrentDesign);
    centerAct->setToolTip(QStringLiteral("Motiv genau im Rahmen zentrieren (Strg+0)"));

    tb->addSeparator();
    tb->addAction(QStringLiteral("📚 Bibliothek"), this, &MainWindow::openLibrary);
    tb->addAction(QStringLiteral("★ Speichern…"), this, &MainWindow::saveToLibrary);
    tb->addSeparator();
    tb->addAction(QStringLiteral("Text…"),        this, &MainWindow::makeText);
    tb->addAction(QStringLiteral("✦ Monogramm…"), this, &MainWindow::makeMonogram);
    tb->addAction(QStringLiteral("✂ Applikation…"), this, &MainWindow::makeApplique);
    tb->addAction(QStringLiteral("👑 MEF Original"), this, [this]{ loadMefOriginal(0); });
    tb->addAction(QStringLiteral("❦ Logo"),        this, &MainWindow::makeLogo);
    tb->addAction(QStringLiteral("Bild…"),        this, &MainWindow::digitizeImage);
    tb->addAction(QStringLiteral("📱 Handy (QR)"), this, &MainWindow::phoneUpload);
    tb->addSeparator();
    tb->addAction(QStringLiteral("Größe…"),       this, &MainWindow::resizeDesign);
    tb->addAction(QStringLiteral("⤢ Platzieren…"), this, &MainWindow::placeDesign);
    tb->addAction(QStringLiteral("↔ Spieg. H"),   this, &MainWindow::mirrorHorizontal);
    tb->addAction(QStringLiteral("↕ Spieg. V"),   this, &MainWindow::mirrorVertical);
    tb->addAction(QStringLiteral("↻ 90°"),        this, [this]{ rotate90(true); });
    tb->addAction(QStringLiteral("⟳ Drehen…"),    this, &MainWindow::rotateFree);

    auto* spacer = new QWidget; spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);
    tb->addAction(QStringLiteral("📄 Druckblatt…"), this, &MainWindow::exportWorksheet);
    tb->addAction(QStringLiteral("DST Export…"),   this, &MainWindow::exportDst);
    tb->addAction(QStringLiteral("JEF Export…"),   this, &MainWindow::exportJef);

    statusBar()->showMessage(QStringLiteral("StickCore Studio bereit — für %1 eingerichtet. Rahmen eingeblendet.").arg(mp.name));
    loadSatinDemo();

    // Command-line demo hooks
    const QStringList a = qApp->arguments();
    const int di = a.indexOf(QStringLiteral("--demo"));
    if (di >= 0 && di + 1 < a.size()) {
        const QString which = a[di + 1];
        if (which == QLatin1String("text")) {
            TextDigitizer::Params tp; tp.text = QStringLiteral("StickCore"); tp.heightMm = 22; tp.raised = true;
            m_current = TextDigitizer::generate(tp);
            m_current.palette = { ThreadCatalog::snap(QColor(150, 40, 60)) };
            m_editor->clearAll(); m_view->setSequence(m_current); m_view->showAll();
            setStatusForSequence(QStringLiteral("Text"));
        } else if (which == QLatin1String("image") && di + 2 < a.size()) {
            QImage img(a[di + 2]);
            if (!img.isNull()) digitizeFrom(img, QStringLiteral("Bild"));
        } else if (which == QLatin1String("logo")) {
            makeLogo();
            if (di + 2 < a.size())
                JefCodec::exportToFile(a[di + 2], m_current, HoopType::HoopB_140x200);
        } else if (which == QLatin1String("tatami")) {
            loadTatamiDemo();
        } else if (which == QLatin1String("monogram")) {
            makeMonogram();
        }
    } else if (!a.contains(QStringLiteral("--manual-shots")) && !a.contains(QStringLiteral("--shot"))) {
        QTimer::singleShot(0, this, &MainWindow::maybeShowWizardOnStart);
    }
}

MainWindow::~MainWindow() = default;

// ---------------------------------------------------------------------------
void MainWindow::setupMenus()
{
    // --- Datei ---
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("&Datei"));
    auto* newAct = fileMenu->addAction(QStringLiteral("&Neues Motiv"), this, [this]{
        m_editor->clearAll();
        m_current.clear();
        m_view->setSequence(m_current);
        updateInfoPanel();
        statusBar()->showMessage(QStringLiteral("Neues leeres Motiv angelegt."), 4000);
    }, QKeySequence::New);
    auto* openImgAct = fileMenu->addAction(QStringLiteral("&Bild öffnen & digitalisieren…"), this, &MainWindow::digitizeImage, QKeySequence::Open);
    auto* openSvgAct = fileMenu->addAction(QStringLiteral("&SVG-Vektordatei importieren & sticken…"), this, &MainWindow::importSvg, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    fileMenu->addSeparator();
    auto* libOpenAct = fileMenu->addAction(QStringLiteral("Aus &Bibliothek laden…"), this, &MainWindow::openLibrary, QKeySequence(Qt::CTRL | Qt::Key_L));
    auto* libSaveAct = fileMenu->addAction(QStringLiteral("In Bibliothek &speichern…"), this, &MainWindow::saveToLibrary, QKeySequence::Save);
    fileMenu->addSeparator();
    auto* expJefAct  = fileMenu->addAction(QStringLiteral("&JEF exportieren (Janome)…"), this, &MainWindow::exportJef, QKeySequence(Qt::CTRL | Qt::Key_E));
    auto* expDstAct  = fileMenu->addAction(QStringLiteral("&DST exportieren (Tajima Universal)…"), this, &MainWindow::exportDst);
    auto* expVarAct  = fileMenu->addAction(QStringLiteral("&Größen-Varianten exportieren…"), this, &MainWindow::exportSizeVariants);
    auto* expSheetAct = fileMenu->addAction(QStringLiteral("&Produktionsdatenblatt (HTML/Druck)…"), this, &MainWindow::exportWorksheet, QKeySequence::Print);
    fileMenu->addSeparator();
    auto* quitAct    = fileMenu->addAction(QStringLiteral("&Beenden"), this, &QWidget::close, QKeySequence::Quit);

    Q_UNUSED(newAct); Q_UNUSED(openImgAct); Q_UNUSED(openSvgAct); Q_UNUSED(libOpenAct); Q_UNUSED(libSaveAct);
    Q_UNUSED(expJefAct); Q_UNUSED(expDstAct); Q_UNUSED(expVarAct); Q_UNUSED(expSheetAct); Q_UNUSED(quitAct);

    // --- Bearbeiten ---
    QMenu* editMenu = menuBar()->addMenu(QStringLiteral("&Bearbeiten"));
    editMenu->addAction(QStringLiteral("&Größe ändern…"), this, &MainWindow::resizeDesign, QKeySequence(Qt::CTRL | Qt::Key_R));
    editMenu->addAction(QStringLiteral("Im &Rahmen platzieren…"), this, &MainWindow::placeDesign);
    editMenu->addSeparator();
    editMenu->addAction(QStringLiteral("↔ Horizontal &spiegeln"), this, &MainWindow::mirrorHorizontal, QKeySequence(Qt::Key_F7));
    editMenu->addAction(QStringLiteral("↕ Vertikal sp&iegeln"), this, &MainWindow::mirrorVertical, QKeySequence(Qt::Key_F8));
    editMenu->addAction(QStringLiteral("↻ &90° im Uhrzeigersinn"), this, [this]{ rotate90(true); }, QKeySequence(Qt::Key_F9));
    editMenu->addAction(QStringLiteral("↺ 90° &gegen Uhrzeigersinn"), this, [this]{ rotate90(false); }, QKeySequence(Qt::SHIFT | Qt::Key_F9));
    editMenu->addAction(QStringLiteral("⟳ &Freier Drehwinkel…"), this, &MainWindow::rotateFree, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R));
    editMenu->addSeparator();
    editMenu->addAction(QStringLiteral("🎨 Intelligente Farbsortierung (Smart Color Sort)…"), this, &MainWindow::smartColorSort);
    editMenu->addAction(QStringLiteral("🔲 Heftrahmen hinzufügen (Basting Box)…"), this, &MainWindow::addBastingBox);
    editMenu->addAction(QStringLiteral("✂ Mehrfach-Rahmung (Auto-Split & Passkreuze)…"), this, &MainWindow::multiHoopSplit);
    editMenu->addSeparator();
    editMenu->addAction(QStringLiteral("&Alle Pfade löschen"), m_editor, &PathEditorWidget::clearAll);

    // --- Motive & Assistenten ---
    QMenu* motMenu = menuBar()->addMenu(QStringLiteral("&Motive & Assistenten"));
    motMenu->addAction(QStringLiteral("Stiche &berechnen ▶"), this, &MainWindow::generate, QKeySequence(Qt::Key_F5));
    motMenu->addSeparator();
    motMenu->addAction(QStringLiteral("✂ 3-Stufen-&Applikation (Aufnäher)…"), this, &MainWindow::makeApplique);
    motMenu->addAction(QStringLiteral("&Text sticken…"), this, &MainWindow::makeText, QKeySequence(Qt::CTRL | Qt::Key_T));
    motMenu->addAction(QStringLiteral("✦ Kunstvolles &Monogramm…"), this, &MainWindow::makeMonogram, QKeySequence(Qt::CTRL | Qt::Key_M));
    motMenu->addAction(QStringLiteral("🌫 Sfumato Photo-Stitch (Fotorealistisch)…"), this, &MainWindow::digitizeSfumato);
    motMenu->addAction(QStringLiteral("✖ Traditioneller Kreuzstich (Aida Raster)…"), this, &MainWindow::digitizeCrossStitch);
    motMenu->addAction(QStringLiteral("⚜ Tatami Prägemuster (Carving / Eichenlaub / Stern)…"), this, &MainWindow::applyTatamiCarving);

    QMenu* mefMenu = motMenu->addMenu(QStringLiteral("👑 Modewerkstatt Knüppel (Original Vektor-Logo)"));
    mefMenu->addAction(QStringLiteral("👑 Meister-Aufnäher (Foto-Original: Kettelrand & Satinschrift)"), this, [this]{ loadMefOriginal(0); });
    mefMenu->addAction(QStringLiteral("✦ Atelier Kontur-Glanz (Florentiner Schimmer)"), this, [this]{ loadMefOriginal(4); });
    mefMenu->addAction(QStringLiteral("✨ Zwei-Ton-Relief Gold (Madeira 1083 & 1070)"), this, [this]{ loadMefOriginal(2); });
    mefMenu->addAction(QStringLiteral("🧵 Königliche Meister-Tatami (Dichte Webung)"), this, [this]{ loadMefOriginal(1); });
    mefMenu->addAction(QStringLiteral("✒ Feiner Gold-Steppstich (Linien-Art)"), this, [this]{ loadMefOriginal(3); });
    mefMenu->addSeparator();
    mefMenu->addAction(QStringLiteral("❦ Kalligraphie-Variante (Schriftarten-Badge)"), this, &MainWindow::makeLogo);

    QMenu* huntMenu = motMenu->addMenu(QStringLiteral("🌿 Jagd, Natur & Tradition (Eichenlaub, Hirsch, Keiler)"));
    auto* oakMenu = huntMenu->addMenu(QStringLiteral("🌿 Eichenlaub mit Eicheln (Trachten & Schützen)"));
    oakMenu->addAction(QStringLiteral("▶ Als fertige Stickerei laden"), this, [this]{ loadHuntingMotif(0, false); });
    oakMenu->addAction(QStringLiteral("✏ Im 2D-Editor bearbeiten (Bézier-Pfade)"), this, [this]{ loadHuntingMotif(0, true); });

    auto* stagMenu = huntMenu->addMenu(QStringLiteral("🦌 Kapitaler 12-Ender Hirschkopf"));
    stagMenu->addAction(QStringLiteral("▶ Als fertige Stickerei laden"), this, [this]{ loadHuntingMotif(1, false); });
    stagMenu->addAction(QStringLiteral("✏ Im 2D-Editor bearbeiten (Bézier-Pfade)"), this, [this]{ loadHuntingMotif(1, true); });

    auto* boarMenu = huntMenu->addMenu(QStringLiteral("🐗 Keiler / Schwarzwild mit Hauer"));
    boarMenu->addAction(QStringLiteral("▶ Als fertige Stickerei laden"), this, [this]{ loadHuntingMotif(2, false); });
    boarMenu->addAction(QStringLiteral("✏ Im 2D-Editor bearbeiten (Bézier-Pfade)"), this, [this]{ loadHuntingMotif(2, true); });

    auto* crestMenu = huntMenu->addMenu(QStringLiteral("🎖 Waidmannsheil-Medaillon (Kranz & Flinten)"));
    crestMenu->addAction(QStringLiteral("▶ Als fertige Stickerei laden"), this, [this]{ loadHuntingMotif(3, false); });
    crestMenu->addAction(QStringLiteral("✏ Im 2D-Editor bearbeiten (Bézier-Pfade)"), this, [this]{ loadHuntingMotif(3, true); });

    motMenu->addAction(QStringLiteral("📱 &Smartphone-Foto Upload (QR)…"), this, &MainWindow::phoneUpload, QKeySequence(Qt::CTRL | Qt::Key_U));
    motMenu->addSeparator();
    motMenu->addAction(QStringLiteral("Demo: Satin-Welle"), this, &MainWindow::loadSatinDemo);
    motMenu->addAction(QStringLiteral("Demo: Tatami-Kreis"), this, &MainWindow::loadTatamiDemo);
    motMenu->addSeparator();
    motMenu->addAction(QStringLiteral("❔ Ersteinrichtungs-&Assistent…"), this, &MainWindow::showWizard);

    // --- Simulation & Ansicht ---
    QMenu* simMenu = menuBar()->addMenu(QStringLiteral("&Simulation & Ansicht"));
    simMenu->addAction(QStringLiteral("&Wiedergabe starten/pausieren"), this, &MainWindow::togglePlayback, QKeySequence(Qt::Key_Space));
    simMenu->addAction(QStringLiteral("&Zurückspulen"), m_view, &StitchGLWidget::resetPlayback, QKeySequence(Qt::Key_Home));
    simMenu->addAction(QStringLiteral("&Alles anzeigen"), m_view, &StitchGLWidget::showAll, QKeySequence(Qt::Key_End));
    simMenu->addAction(QStringLiteral("&Kamera zentrieren"), m_view, &StitchGLWidget::showAll);
    simMenu->addSeparator();
    if (m_actOrbitMode) simMenu->addAction(m_actOrbitMode);
    if (m_actMoveMode)  simMenu->addAction(m_actMoveMode);
    simMenu->addSeparator();
    if (m_actHoop)      simMenu->addAction(m_actHoop);

    m_hoopMenu = simMenu->addMenu(QStringLiteral("⭕ &Stickrahmen auswählen"));
    m_hoopActionGroup = new QActionGroup(this);

    auto* actAuto = m_hoopMenu->addAction(QStringLiteral("✨ Automatisch (beste Passform)"), this, [this]{
        m_hoopCombo->setCurrentIndex(0);
    });
    actAuto->setCheckable(true);
    actAuto->setChecked(true);
    m_hoopActionGroup->addAction(actAuto);

    m_hoopMenu->addSeparator();
    auto* secStd = m_hoopMenu->addAction(QStringLiteral("── Janome MC350E Original & Zubehör ──"));
    secStd->setEnabled(false);

    const auto& mp = MachineProfile::current();
    for (HoopType t : mp.hoops) {
        const HoopSpec hs = hoopSpec(t);
        if (t == HoopType::HoopHat_100x90) {
            m_hoopMenu->addSeparator();
            auto* secSpec = m_hoopMenu->addAction(QStringLiteral("── Spezial- & Magnetrahmen ──"));
            secSpec->setEnabled(false);
        } else if (t == HoopType::HoopSQ23_230) {
            m_hoopMenu->addSeparator();
            auto* secBig = m_hoopMenu->addAction(QStringLiteral("── Großmaschinen-Referenz ──"));
            secBig->setEnabled(false);
        }

        auto* act = m_hoopMenu->addAction(QString::fromLatin1(hs.displayName), this, [this, t]{
            selectHoop(t);
        });
        act->setCheckable(true);
        act->setToolTip(QString::fromLatin1(hs.description));
        m_hoopActionGroup->addAction(act);
    }

    simMenu->addSeparator();
    if (m_actTopDown)   simMenu->addAction(m_actTopDown);
    simMenu->addAction(QStringLiteral("&Im Rahmen zentrieren"), this, &MainWindow::centerCurrentDesign, QKeySequence(Qt::CTRL | Qt::Key_0));

    // --- Hilfe ---
    QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("&Hilfe"));
    helpMenu->addAction(QStringLiteral("&Tastenkürzel & Anleitung"), this, [this]{
        QMessageBox::information(this, QStringLiteral("Tastenkürzel — StickCore Studio"),
            QStringLiteral("<b>Navigation, Ansicht & Verschieben:</b><br>"
                           "• <b>Pfeiltasten (←, →, ↑, ↓):</b> Motiv im Rahmen verschieben (1 mm, Shift: 5 mm, Alt: 0,1 mm)<br>"
                           "• <b>Maus (Modus Bewegen / Shift-Drag):</b> Motiv frei im Rahmen verschieben<br>"
                           "• <b>Maus (Modus Drehen / Rechtsklick):</b> 3D-Kamera rotieren<br>"
                           "• <b>Mausrad / Mittlere Maustaste:</b> Zoomen / Kamera verschieben (Pan)<br>"
                           "• <b>H:</b> Stickrahmen ein-/ausblenden<br>"
                           "• <b>Strg+D:</b> 2D-Draufsicht / 3D-Perspektive umschalten<br>"
                           "• <b>Strg+0:</b> Motiv im Rahmen zentrieren<br>"
                           "• <b>Leertaste:</b> 3D-Simulation starten / pausieren<br>"
                           "• <b>F5:</b> Stiche aus Pfaden berechnen<br>"
                           "• <b>Strg+N:</b> Neues Motiv<br>"
                           "• <b>Strg+O:</b> Bild digitalisieren<br>"
                           "• <b>Strg+E:</b> JEF Exportieren<br>"
                           "• <b>Strg+P:</b> Produktionsblatt drucken<br>"
                           "• <b>Doppelklick in Garnliste:</b> Garnfarbe anpassen"));
    });
    helpMenu->addAction(QStringLiteral("&Über StickCore Studio"), this, [this]{
        QMessageBox::about(this, QStringLiteral("Über StickCore Studio"),
            QStringLiteral("<h3>StickCore Studio 1.0</h3>"
                           "<p>High-End Stickdatei-Studio für <b>Janome MC350E</b> und universelle <b>Tajima DST</b> Maschinen.</p>"
                           "<p>Mit interaktiver 3D-Simulation, Rahmen-Visualisierung, Drag & Drop-Verschiebung und Garnkatalogen.</p>"
                           "<p>Entwickelt mit Qt 6 & modernem C++.</p>"));
    });
}

// ---------------------------------------------------------------------------
QWidget* MainWindow::buildCenterView()
{
    auto* container = new QWidget(this);
    auto* lay = new QVBoxLayout(container);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    lay->addWidget(m_view, 1);

    // Transport Bar under 3D Viewport
    auto* tb = new QWidget(container);
    tb->setObjectName(QStringLiteral("TransportBar"));
    auto* tlay = new QHBoxLayout(tb);
    tlay->setContentsMargins(8, 6, 8, 6);
    tlay->setSpacing(8);

    m_resetBtn = new QPushButton(QStringLiteral("⏮"), tb);
    m_resetBtn->setObjectName(QStringLiteral("TransportBtn"));
    m_resetBtn->setToolTip(QStringLiteral("Zum Anfang zurückspulen (Pos1)"));
    connect(m_resetBtn, &QPushButton::clicked, m_view, &StitchGLWidget::resetPlayback);

    m_stepBackBtn = new QPushButton(QStringLiteral("◀"), tb);
    m_stepBackBtn->setObjectName(QStringLiteral("TransportBtn"));
    m_stepBackBtn->setToolTip(QStringLiteral("50 Stiche zurück"));
    connect(m_stepBackBtn, &QPushButton::clicked, this, [this]{
        m_view->setVisibleSegments(m_view->visibleSegmentCount() - 50);
    });

    m_playBtn = new QPushButton(QStringLiteral("▶ Abspielen"), tb);
    m_playBtn->setObjectName(QStringLiteral("PlayBtn"));
    m_playBtn->setToolTip(QStringLiteral("Simulation starten / anhalten (Leertaste)"));
    connect(m_playBtn, &QPushButton::clicked, this, &MainWindow::togglePlayback);

    m_stepFwdBtn = new QPushButton(QStringLiteral("▶"), tb);
    m_stepFwdBtn->setObjectName(QStringLiteral("TransportBtn"));
    m_stepFwdBtn->setToolTip(QStringLiteral("50 Stiche vor"));
    connect(m_stepFwdBtn, &QPushButton::clicked, this, [this]{
        m_view->setVisibleSegments(m_view->visibleSegmentCount() + 50);
    });

    m_scrubSlider = new QSlider(Qt::Horizontal, tb);
    m_scrubSlider->setRange(0, 0);
    m_scrubSlider->setToolTip(QStringLiteral("Timeline: Durch das Motiv scrubben"));
    connect(m_scrubSlider, &QSlider::sliderMoved, this, &MainWindow::onScrubSliderMoved);

    m_scrubLabel = new QLabel(QStringLiteral("0 / 0 Stiche"), tb);
    m_scrubLabel->setStyleSheet(QStringLiteral("font-family:monospace; font-weight:600; min-width:120px;"));

    auto* speedLbl = new QLabel(QStringLiteral("Tempo:"), tb);
    m_speedCombo = new QComboBox(tb);
    m_speedCombo->addItem(QStringLiteral("1x (Ruhig)"), 4);
    m_speedCombo->addItem(QStringLiteral("3x (Normal)"), 12);
    m_speedCombo->addItem(QStringLiteral("8x (Flott)"), 32);
    m_speedCombo->addItem(QStringLiteral("25x (Turbo)"), 100);
    m_speedCombo->setCurrentIndex(1);
    connect(m_speedCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int idx){
        m_view->setPlaybackSpeed(m_speedCombo->itemData(idx).toInt());
    });

    tlay->addWidget(m_resetBtn);
    tlay->addWidget(m_stepBackBtn);
    tlay->addWidget(m_playBtn);
    tlay->addWidget(m_stepFwdBtn);
    tlay->addWidget(m_scrubSlider, 1);
    tlay->addWidget(m_scrubLabel);
    tlay->addWidget(speedLbl);
    tlay->addWidget(m_speedCombo);

    lay->addWidget(tb);

    connect(m_view, &StitchGLWidget::playbackProgress, this, &MainWindow::onPlayerProgress);
    connect(m_view, &StitchGLWidget::playbackStateChanged, this, &MainWindow::onPlayerStateChanged);

    connect(m_view, &StitchGLWidget::designMoved, this, [this](double dx, double dy){
        for (Stitch& s : m_current.stitches) {
            s.x += dx;
            s.y += dy;
        }
        updateInfoPanel();
    });

    connect(m_view, &StitchGLWidget::designPositionChanged, this, [this](double cx, double cy){
        if (m_posXSpin && m_posYSpin) {
            m_posXSpin->blockSignals(true);
            m_posYSpin->blockSignals(true);
            m_posXSpin->setValue(cx);
            m_posYSpin->setValue(cy);
            m_posXSpin->blockSignals(false);
            m_posYSpin->blockSignals(false);
        }
    });

    connect(m_view, &StitchGLWidget::hoopOverflowChanged, this, [this](bool overflow){
        if (overflow) {
            m_fitsLabel->setText(QStringLiteral("⚠ Außerhalb des Rahmens!"));
            m_fitsLabel->setStyleSheet(QStringLiteral("color: #f43f5e; font-weight: bold;"));
        } else {
            m_fitsLabel->setStyleSheet(QStringLiteral("color: #10b981; font-weight: bold;"));
        }
    });

    return container;
}

// ---------------------------------------------------------------------------
void MainWindow::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Space) {
        togglePlayback();
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right ||
        event->key() == Qt::Key_Up   || event->key() == Qt::Key_Down) {
        QWidget* f = focusWidget();
        if (!f || !qobject_cast<QLineEdit*>(f)) {
            double step = 1.0;
            if (event->modifiers() & Qt::ShiftModifier) step = 5.0;
            else if (event->modifiers() & Qt::AltModifier) step = 0.1;
            double dx = 0.0, dy = 0.0;
            if (event->key() == Qt::Key_Left)  dx = -step;
            if (event->key() == Qt::Key_Right) dx = +step;
            if (event->key() == Qt::Key_Up)    dy = +step;
            if (event->key() == Qt::Key_Down)  dy = -step;
            m_view->moveDesign(dx, dy);
            statusBar()->showMessage(
                QStringLiteral("Motiv verschoben (Versatz ΔX: %1 mm / ΔY: %2 mm)")
                    .arg(dx, 0, 'f', 1).arg(dy, 0, 'f', 1), 2000);
            event->accept();
            return;
        }
    }

    QMainWindow::keyPressEvent(event);
}

// ---------------------------------------------------------------------------
void MainWindow::togglePlayback()
{
    if (m_view->isPlaying())
        m_view->pausePlayback();
    else
        m_view->startPlayback();
}

void MainWindow::onPlayerProgress(int current, int total)
{
    m_scrubSlider->blockSignals(true);
    m_scrubSlider->setRange(0, total);
    m_scrubSlider->setValue(current);
    m_scrubSlider->blockSignals(false);
    m_scrubLabel->setText(QStringLiteral("%1 / %2 Stiche").arg(current).arg(total));
}

void MainWindow::onPlayerStateChanged(bool playing)
{
    m_playBtn->setText(playing ? QStringLiteral("⏸ Pause") : QStringLiteral("▶ Abspielen"));
}

void MainWindow::onScrubSliderMoved(int val)
{
    m_view->setVisibleSegments(val);
}

// ---------------------------------------------------------------------------
void MainWindow::onThreadItemDoubleClicked(QListWidgetItem* item)
{
    if (!item) return;
    const int idx = m_threadList->row(item);
    if (idx < 0 || idx >= int(m_current.palette.size())) return;

    ThreadPickDialog dlg(m_current.palette[idx], this);
    if (dlg.exec() == QDialog::Accepted) {
        m_current.palette[idx] = dlg.selectedThread();
        m_view->setSequence(m_current);
        updateInfoPanel();
        statusBar()->showMessage(QStringLiteral("Garnfarbe %1 geändert zu: %2")
            .arg(idx + 1).arg(m_current.palette[idx].description), 5000);
    }
}

// ---------------------------------------------------------------------------
void MainWindow::makeApplique()
{
    const auto paths = m_editor->paths();
    int chosen = -1;
    for (int i = 0; i < paths.size(); ++i) {
        if (paths[i].count() >= 3) { chosen = i; break; }
    }
    if (chosen < 0) {
        QMessageBox::information(this, windowTitle(),
            QStringLiteral("Bitte zuerst im Editor ein geschlossenes Polygon mit ≥ 3 Punkten zeichnen (oder Tatami-Demo laden)."));
        return;
    }
    const QPolygonF poly = paths[chosen].toPolygon();
    AppliqueGenerator::Params ap;
    ap.satinWidthMm = 3.2;
    ap.satinPitchMm = m_density->value();
    m_current = AppliqueGenerator::generate(poly, ap);
    setCurrentSequence(m_current, QStringLiteral("Applikation"));
    statusBar()->showMessage(
        QStringLiteral("3-Stufen-Applikation erzeugt: 1. Positionieren -> 2. Heften -> 3. Satinkante."), 8000);
}

// ---------------------------------------------------------------------------
void MainWindow::exportDst()
{
    if (m_current.empty()) {
        QMessageBox::information(this, windowTitle(), QStringLiteral("Nichts erzeugt – bitte zuerst ein Motiv erstellen."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Als Tajima DST speichern"),
        QStringLiteral("design.dst"), QStringLiteral("Tajima DST (*.dst)"));
    if (path.isEmpty()) return;

    const DstCodec::Result res = DstCodec::exportToFile(path, m_current);
    if (res.ok)
        statusBar()->showMessage(QStringLiteral("DST-Export: %1").arg(res.message), 8000);
    else
        QMessageBox::warning(this, windowTitle(), QStringLiteral("Export fehlgeschlagen: %1").arg(res.message));
}

// ---------------------------------------------------------------------------
void MainWindow::exportWorksheet()
{
    if (m_current.empty()) {
        QMessageBox::information(this, windowTitle(), QStringLiteral("Zuerst ein Motiv erzeugen."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Produktionsblatt speichern"),
        QStringLiteral("stickplan.html"), QStringLiteral("HTML-Produktionsblatt (*.html)"));
    if (path.isEmpty()) return;

    double x0, y0, x1, y1; m_current.bounds(x0, y0, x1, y1);
    const double w = x1 - x0, h = y1 - y0;
    const auto& mp = MachineProfile::current();
    const HoopSpec hs = hoopSpec(mp.hoopFor(w, h));

    double topMeters = 0.0, bobbinMeters = 0.0;
    int estMinutes = 0;
    calculateMetrics(m_current, topMeters, bobbinMeters, estMinutes);

    QImage thumb = StitchThumbnail::render(m_current, 400);
    QByteArray ba;
    QBuffer buf(&ba);
    thumb.save(&buf, "PNG");
    const QString b64 = QString::fromLatin1(ba.toBase64());

    QString html = QStringLiteral(R"(<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>StickCore Studio — Produktionsblatt</title>
<style>
body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; background:#f4f6f8; color:#1e293b; padding:24px; }
.card { background:white; border-radius:12px; box-shadow:0 2px 8px rgba(0,0,0,0.08); max-width:800px; margin:auto; padding:28px; }
h1 { margin-top:0; color:#0f172a; font-size:24px; border-bottom:2px solid #e2e8f0; padding-bottom:12px; }
.grid { display:grid; grid-template-columns: 1fr 1fr; gap:20px; margin-bottom:24px; }
.thumb { text-align:center; background:#f8fafc; border-radius:8px; padding:12px; border:1px solid #e2e8f0; }
.thumb img { max-width:100%; height:auto; border-radius:4px; }
table { width:100%; border-collapse:collapse; margin-top:12px; }
th, td { text-align:left; padding:8px 12px; border-bottom:1px solid #e2e8f0; font-size:14px; }
th { background:#f1f5f9; color:#475569; }
.badge { display:inline-block; width:16px; height:16px; border-radius:3px; vertical-align:middle; margin-right:8px; border:1px solid rgba(0,0,0,0.2); }
</style>
</head>
<body>
<div class="card">
  <h1>StickCore Studio — Produktions- & Stickplan</h1>
  <div class="grid">
    <div class="thumb">
      <img src="data:image/png;base64,%1" alt="Vorschau">
    </div>
    <div>
      <h3>Kenndaten</h3>
      <p><b>Abmessungen:</b> %2 × %3 mm</p>
      <p><b>Stiche gesamt:</b> %4</p>
      <p><b>Farben / Stopps:</b> %5</p>
      <p><b>Empfohlener Rahmen:</b> %6</p>
      <p><b>Geschätzte Stickzeit:</b> ~%7 Minuten (@ 500 SPM)</p>
      <p><b>Oberfaden-Bedarf:</b> ca. %8 m</p>
      <p><b>Unterfaden-Bedarf:</b> ca. %9 m</p>
    </div>
  </div>
  <h3>Farbreihenfolge & Garnzuordnung</h3>
  <table>
    <tr><th>#</th><th>Garnfarbe</th><th>Beschreibung / Katalog-Code</th></tr>
)").arg(b64).arg(w, 0, 'f', 1).arg(h, 0, 'f', 1).arg(m_current.realStitchCount())
   .arg(m_current.palette.size()).arg(QString::fromLatin1(hs.name)).arg(estMinutes)
   .arg(topMeters, 0, 'f', 1).arg(bobbinMeters, 0, 'f', 1);

    int step = 1;
    for (const ThreadColor& tc : m_current.palette) {
        QString codeInfo = tc.janomeCode >= 0 ? QStringLiteral("Janome #%1").arg(tc.janomeCode) : QStringLiteral("Standard");
        html += QStringLiteral("<tr><td>%1</td><td><span class='badge' style='background:%2;'></span>%3</td><td>%4</td></tr>")
                    .arg(step++)
                    .arg(tc.color.name())
                    .arg(tc.description.isEmpty() ? QStringLiteral("Garn") : tc.description)
                    .arg(codeInfo);
    }

    html += QStringLiteral(R"(
  </table>
  <p style="margin-top:24px; font-size:12px; color:#94a3b8; text-align:center;">
    Erstellt mit StickCore Studio für Janome MC350E & Universal Tajima DST.
  </p>
</div>
</body>
</html>)");

    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        f.write(html.toUtf8());
        f.close();
        statusBar()->showMessage(QStringLiteral("Produktionsblatt gespeichert: %1").arg(path), 8000);
    }
}

// ---------------------------------------------------------------------------
void MainWindow::maybeShowWizardOnStart()
{
    QSettings s;
    if (s.value(QStringLiteral("showWelcome"), true).toBool())
        showWizard();
}

// ---------------------------------------------------------------------------
void MainWindow::showWizard()
{
    WelcomeWizard wiz(this);
    const int r = wiz.exec();

    QSettings s;
    if (wiz.dontShowAgain()) s.setValue(QStringLiteral("showWelcome"), false);

    if (r != QDialog::Accepted) return;

    switch (wiz.choice()) {
    case WelcomeWizard::Choice::Library: openLibrary(); break;
    case WelcomeWizard::Choice::Text:    makeText(); break;
    case WelcomeWizard::Choice::Image:   digitizeImage(); break;
    case WelcomeWizard::Choice::Draw:
        m_mode->setCurrentIndex(0);
        m_editor->setMode(PathEditorWidget::Mode::SatinRails);
        m_editor->clearAll();
        m_editor->newPath();
        statusBar()->showMessage(
            QStringLiteral("Zeichne Rail A: Punkte klicken → „Neuer Pfad“ → Rail B → „Erzeugen ▶“."), 0);
        break;
    default: break;
    }
}

// ---------------------------------------------------------------------------
void MainWindow::openLibrary()
{
    LibraryDialog dlg(m_library.get(), this);
    if (dlg.exec() == QDialog::Accepted && dlg.hasChoice()) {
        m_current = dlg.chosenSequence();
        setCurrentSequence(m_current, QStringLiteral("Bibliothek: %1").arg(dlg.chosenName()));
    }
}

// ---------------------------------------------------------------------------
void MainWindow::saveToLibrary()
{
    if (m_current.empty()) {
        QMessageBox::information(this, windowTitle(),
            QStringLiteral("Nichts zum Speichern – zuerst ein Motiv erzeugen."));
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("In Bibliothek speichern"),
        QStringLiteral("Name des Motivs:"), QLineEdit::Normal, QStringLiteral("Mein Motiv"), &ok);
    if (!ok || name.isEmpty()) return;

    QStringList cats; cats << QStringLiteral("Eigene") << DesignFactory::categories();
    const QString cat = QInputDialog::getItem(this, QStringLiteral("Kategorie"),
        QStringLiteral("Kategorie wählen (oder eigene eingeben):"), cats, 0, true, &ok);
    if (!ok) return;

    const QString id = m_library->add(name, cat.isEmpty() ? QStringLiteral("Eigene") : cat, {}, m_current);
    if (!id.isEmpty())
        statusBar()->showMessage(QStringLiteral("„%1“ in der Bibliothek gespeichert.").arg(name), 6000);
    else
        QMessageBox::warning(this, windowTitle(), QStringLiteral("Speichern fehlgeschlagen."));
}

// ---------------------------------------------------------------------------
QWidget* MainWindow::buildInfoPanel()
{
    const auto& mp = MachineProfile::current();

    auto* panel = new QWidget;
    panel->setObjectName(QStringLiteral("InfoPanel"));
    panel->setMinimumWidth(280);
    auto* outer = new QVBoxLayout(panel);
    outer->setContentsMargins(14, 14, 14, 14);
    outer->setSpacing(14);

    // --- Machine card ---
    auto* mach = new QFrame; mach->setObjectName(QStringLiteral("Card"));
    auto* ml = new QVBoxLayout(mach); ml->setContentsMargins(14,12,14,12); ml->setSpacing(3);
    auto* mn = new QLabel(mp.name); mn->setProperty("role","machine");
    ml->addWidget(mn);
    ml->addWidget(new QLabel(QStringLiteral("%1 · max %2 × %3 mm · %4 St./min")
        .arg(mp.format).arg(mp.maxWidthMm,0,'f',0).arg(mp.maxHeightMm,0,'f',0).arg(mp.maxSpeedSpm)));
    outer->addWidget(mach);

    // --- Hoop ---
    outer->addWidget(sectionTitle(QStringLiteral("Stickrahmen")));
    m_hoopCombo = new QComboBox;
    m_hoopCombo->addItem(QStringLiteral("✨ Automatisch (optimale Passform)"), -1);
    for (HoopType t : mp.hoops) {
        const HoopSpec hs = hoopSpec(t);
        m_hoopCombo->addItem(QString::fromLatin1(hs.displayName), static_cast<int>(t));
        const int curIdx = m_hoopCombo->count() - 1;
        m_hoopCombo->setItemData(curIdx, QString::fromLatin1(hs.description), Qt::ToolTipRole);
    }
    connect(m_hoopCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::onHoopComboChanged);
    outer->addWidget(m_hoopCombo);

    // --- Position im Rahmen ---
    outer->addWidget(sectionTitle(QStringLiteral("Position im Rahmen")));
    auto* posLayout = new QGridLayout;
    posLayout->setSpacing(6);
    posLayout->addWidget(new QLabel(QStringLiteral("X-Mitte:")), 0, 0);
    m_posXSpin = new QDoubleSpinBox;
    m_posXSpin->setRange(-400.0, 400.0);
    m_posXSpin->setSingleStep(1.0);
    m_posXSpin->setSuffix(QStringLiteral(" mm"));
    connect(m_posXSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double){ onPositionSpinChanged(); });
    posLayout->addWidget(m_posXSpin, 0, 1);

    posLayout->addWidget(new QLabel(QStringLiteral("Y-Mitte:")), 1, 0);
    m_posYSpin = new QDoubleSpinBox;
    m_posYSpin->setRange(-400.0, 400.0);
    m_posYSpin->setSingleStep(1.0);
    m_posYSpin->setSuffix(QStringLiteral(" mm"));
    connect(m_posYSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double){ onPositionSpinChanged(); });
    posLayout->addWidget(m_posYSpin, 1, 1);

    auto* nudgeBar = new QHBoxLayout;
    nudgeBar->setSpacing(4);
    auto* btnLeft = new QPushButton(QStringLiteral("◄"));
    btnLeft->setToolTip(QStringLiteral("1mm nach links (←)"));
    connect(btnLeft, &QPushButton::clicked, this, [this]{ m_view->moveDesign(-1.0, 0.0); });
    auto* btnRight = new QPushButton(QStringLiteral("►"));
    btnRight->setToolTip(QStringLiteral("1mm nach rechts (→)"));
    connect(btnRight, &QPushButton::clicked, this, [this]{ m_view->moveDesign(+1.0, 0.0); });
    auto* btnUp = new QPushButton(QStringLiteral("▲"));
    btnUp->setToolTip(QStringLiteral("1mm nach oben (↑)"));
    connect(btnUp, &QPushButton::clicked, this, [this]{ m_view->moveDesign(0.0, +1.0); });
    auto* btnDown = new QPushButton(QStringLiteral("▼"));
    btnDown->setToolTip(QStringLiteral("1mm nach unten (↓)"));
    connect(btnDown, &QPushButton::clicked, this, [this]{ m_view->moveDesign(0.0, -1.0); });
    auto* btnCenter = new QPushButton(QStringLiteral("⌖ Zentrieren"));
    btnCenter->setToolTip(QStringLiteral("Motiv genau im Rahmen zentrieren (Strg+0)"));
    connect(btnCenter, &QPushButton::clicked, this, &MainWindow::centerCurrentDesign);

    nudgeBar->addWidget(btnLeft);
    nudgeBar->addWidget(btnRight);
    nudgeBar->addWidget(btnUp);
    nudgeBar->addWidget(btnDown);
    nudgeBar->addWidget(btnCenter);

    outer->addLayout(posLayout);
    outer->addLayout(nudgeBar);

    // --- Design size / fit ---
    outer->addWidget(sectionTitle(QStringLiteral("Design & Metriken")));
    m_sizeLabel = new QLabel(QStringLiteral("—"));
    m_fitsLabel = new QLabel(QStringLiteral(" "));
    m_threadUsageLabel = new QLabel(QStringLiteral(" "));
    m_threadUsageLabel->setStyleSheet(QStringLiteral("color:#8b93a4; font-size:12px;"));
    m_timeEstLabel = new QLabel(QStringLiteral(" "));
    m_timeEstLabel->setStyleSheet(QStringLiteral("color:#2dd4bf; font-weight:600; font-size:12px;"));

    outer->addWidget(m_sizeLabel);
    outer->addWidget(m_fitsLabel);
    outer->addWidget(m_threadUsageLabel);
    outer->addWidget(m_timeEstLabel);

    // --- Threads ---
    outer->addWidget(sectionTitle(QStringLiteral("Garne (Doppelklick zum Ändern)")));
    m_threadList = new QListWidget;
    m_threadList->setMinimumHeight(120);
    connect(m_threadList, &QListWidget::itemDoubleClicked, this, &MainWindow::onThreadItemDoubleClicked);
    outer->addWidget(m_threadList, 1);

    // --- Stitch properties ---
    outer->addWidget(sectionTitle(QStringLiteral("Sticheigenschaften")));
    auto* form = new QFormLayout; form->setLabelAlignment(Qt::AlignLeft);
    m_density = new QDoubleSpinBox; m_density->setRange(0.20, 1.20); m_density->setSingleStep(0.05);
    m_density->setValue(mp.satinDensityMm); m_density->setSuffix(QStringLiteral(" mm"));
    m_density->setToolTip(QStringLiteral("Abstand zwischen den Stichreihen. Kleiner = dichter, "
        "voller — braucht aber mehr Garn und Zeit."));
    m_pull = new QDoubleSpinBox; m_pull->setRange(0.00, 0.60); m_pull->setSingleStep(0.05);
    m_pull->setValue(mp.pullCompMm); m_pull->setSuffix(QStringLiteral(" mm"));
    m_pull->setToolTip(QStringLiteral("Gleicht das Zusammenziehen des Stoffes aus, indem jede "
        "Reihe seitlich etwas verbreitert wird."));
    m_maxStitch = new QDoubleSpinBox; m_maxStitch->setRange(1.5, 7.0); m_maxStitch->setSingleStep(0.5);
    m_maxStitch->setValue(4.0); m_maxStitch->setSuffix(QStringLiteral(" mm"));
    m_maxStitch->setToolTip(QStringLiteral("Längster Einzelstich. Lange Stiche werden automatisch "
        "in kürzere aufgeteilt, damit sie nicht hängen bleiben."));
    m_underlay = new QCheckBox(QStringLiteral("Unterlage sticken (empfohlen)"));
    m_underlay->setChecked(true);
    m_underlay->setToolTip(QStringLiteral("Stickt zuerst Kontur & Unterlage, dann die Deckstiche — "
        "so wird die Stickerei fest und sauber, ohne Wellen."));
    form->addRow(QStringLiteral("Stichdichte"), m_density);
    m_fillAngleSpin = new QDoubleSpinBox; m_fillAngleSpin->setRange(0.0, 360.0); m_fillAngleSpin->setSingleStep(15.0);
    m_fillAngleSpin->setValue(45.0); m_fillAngleSpin->setSuffix(QStringLiteral("°"));
    m_fillAngleSpin->setToolTip(QStringLiteral("Richtung der Stichreihen (0°-360°). Bricht das Licht auf dem Garn unterschiedlich."));

    m_patternCombo = new QComboBox;
    for (auto pt : { TatamiFill::PatternType::StandardTatami, TatamiFill::PatternType::Brick,
                     TatamiFill::PatternType::Twill, TatamiFill::PatternType::Basketweave,
                     TatamiFill::PatternType::Honeycomb, TatamiFill::PatternType::ContourEcho }) {
        m_patternCombo->addItem(TatamiFill::patternName(pt), int(pt));
    }
    form->addRow(QStringLiteral("Füllwinkel"), m_fillAngleSpin);
    form->addRow(QStringLiteral("Füllmuster"), m_patternCombo);
    form->addRow(QStringLiteral("Zugausgleich"), m_pull);
    form->addRow(QStringLiteral("Max. Stichlänge"), m_maxStitch);
    outer->addLayout(form);
    outer->addWidget(m_underlay);
    auto* hint = new QLabel(QStringLiteral("Tipp: Doppelklick auf ein Garn in der Liste oben öffnet die Farbauswahl."));
    hint->setWordWrap(true); hint->setStyleSheet(QStringLiteral("color:#8b93a4;font-size:11px;"));
    outer->addWidget(hint);

    return panel;
}

// ---------------------------------------------------------------------------
void MainWindow::updateInfoPanel()
{
    const auto& mp = MachineProfile::current();
    double x0, y0, x1, y1;
    if (m_current.bounds(x0, y0, x1, y1)) {
        const double w = x1 - x0, h = y1 - y0;
        const double cx = 0.5 * (x0 + x1), cy = 0.5 * (y0 + y1);
        m_sizeLabel->setText(QStringLiteral("%1 × %2 mm · %3 Stiche · %4 Farben")
            .arg(w, 0, 'f', 1).arg(h, 0, 'f', 1).arg(m_current.realStitchCount()).arg(m_current.palette.size()));

        if (m_posXSpin && m_posYSpin) {
            m_posXSpin->blockSignals(true);
            m_posYSpin->blockSignals(true);
            m_posXSpin->setValue(cx);
            m_posYSpin->setValue(cy);
            m_posXSpin->blockSignals(false);
            m_posYSpin->blockSignals(false);
        }

        const bool overflowing = m_view->isOverflowing();
        m_fitsLabel->setProperty("role", overflowing ? "bad" : "ok");
        if (overflowing) {
            m_fitsLabel->setText(QStringLiteral("⚠ Außerhalb von %1 (Zentrum %2 / %3 mm)")
                .arg(m_view->hoop().name).arg(cx, 0, 'f', 1).arg(cy, 0, 'f', 1));
            m_fitsLabel->setStyleSheet(QStringLiteral("color:#f43f5e; font-weight:700;"));
        } else {
            m_fitsLabel->setText(QStringLiteral("✓ passt in %1 (Zentrum %2 / %3 mm)")
                .arg(m_view->hoop().name).arg(cx, 0, 'f', 1).arg(cy, 0, 'f', 1));
            m_fitsLabel->setStyleSheet(QStringLiteral("color:#10b981; font-weight:600;"));
        }
        m_fitsLabel->style()->unpolish(m_fitsLabel); m_fitsLabel->style()->polish(m_fitsLabel);

        double topM = 0, bobM = 0; int estMin = 0;
        calculateMetrics(m_current, topM, bobM, estMin);
        m_threadUsageLabel->setText(QStringLiteral("🧵 Garn: ~%1 m Oberfaden · ~%2 m Unterfaden")
            .arg(topM, 0, 'f', 1).arg(bobM, 0, 'f', 1));
        m_timeEstLabel->setText(QStringLiteral("⏱ Stickzeit: ca. %1 Min. (@ 500 SPM)").arg(estMin));
    } else {
        m_sizeLabel->setText(QStringLiteral("—"));
        m_fitsLabel->setText(QString());
        m_threadUsageLabel->setText(QString());
        m_timeEstLabel->setText(QString());
        if (m_posXSpin && m_posYSpin) {
            m_posXSpin->blockSignals(true);
            m_posYSpin->blockSignals(true);
            m_posXSpin->setValue(0.0);
            m_posYSpin->setValue(0.0);
            m_posXSpin->blockSignals(false);
            m_posYSpin->blockSignals(false);
        }
    }

    m_threadList->clear();
    int i = 1;
    for (const ThreadColor& t : m_current.palette) {
        const QString code = t.janomeCode >= 0 ? QStringLiteral("  ·  Janome %1").arg(t.janomeCode) : QString();
        auto* it = new QListWidgetItem(QIcon(swatch(t.color)),
            QStringLiteral("%1. %2%3").arg(i++).arg(t.description.isEmpty() ? QStringLiteral("Farbe") : t.description, code));
        m_threadList->addItem(it);
    }
}

// ---------------------------------------------------------------------------
void MainWindow::toggleHoopVisible()
{
    m_view->setHoopVisible(!m_view->isHoopVisible());
    if (m_actHoop) m_actHoop->setChecked(m_view->isHoopVisible());
    statusBar()->showMessage(m_view->isHoopVisible()
        ? QStringLiteral("Stickrahmen eingeblendet.")
        : QStringLiteral("Stickrahmen ausgeblendet."), 3000);
}

void MainWindow::toggleViewPreset()
{
    if (m_view->isTopDown()) {
        m_view->setPerspectiveView();
        if (m_actTopDown) m_actTopDown->setText(QStringLiteral("👁 2D Draufsicht"));
        statusBar()->showMessage(QStringLiteral("3D-Perspektive aktiv."), 3000);
    } else {
        m_view->setTopDownView();
        if (m_actTopDown) m_actTopDown->setText(QStringLiteral("👁 3D Perspektive"));
        statusBar()->showMessage(QStringLiteral("2D-Draufsicht aktiv."), 3000);
    }
}

void MainWindow::centerCurrentDesign()
{
    m_view->centerDesign();
    statusBar()->showMessage(QStringLiteral("Motiv im Rahmen zentriert (0, 0)."), 4000);
}

void MainWindow::onPositionSpinChanged()
{
    if (m_current.empty()) return;
    double x0, y0, x1, y1;
    if (!m_current.bounds(x0, y0, x1, y1)) return;

    const double curX = 0.5 * (x0 + x1);
    const double curY = 0.5 * (y0 + y1);
    const double targetX = m_posXSpin->value();
    const double targetY = m_posYSpin->value();
    const double dx = targetX - curX;
    const double dy = targetY - curY;

    if (std::abs(dx) > 1e-4 || std::abs(dy) > 1e-4) {
        m_view->moveDesign(dx, dy);
    }
}

void MainWindow::onHoopComboChanged(int idx)
{
    const auto& mp = MachineProfile::current();
    HoopType ht;
    if (idx > 0 && idx < m_hoopCombo->count()) {
        ht = static_cast<HoopType>(m_hoopCombo->itemData(idx).toInt());
    } else {
        double x0, y0, x1, y1;
        if (m_current.bounds(x0, y0, x1, y1)) {
            ht = mp.hoopFor(x1 - x0, y1 - y0);
        } else {
            ht = HoopType::HoopB_140x200;
        }
    }
    const HoopSpec hs = hoopSpec(ht);
    m_view->setHoop(hs.widthMm, hs.heightMm, QString::fromLatin1(hs.name));
    m_editor->setHoop(hs.widthMm, hs.heightMm, QString::fromLatin1(hs.name), ht);

    if (m_hoopActionGroup) {
        const auto acts = m_hoopActionGroup->actions();
        if (idx <= 0 && !acts.isEmpty()) {
            acts.first()->setChecked(true);
        } else {
            for (auto* act : acts) {
                if (act->text() == QString::fromLatin1(hs.displayName)) {
                    act->setChecked(true);
                    break;
                }
            }
        }
    }

    updateInfoPanel();
}

void MainWindow::selectHoop(HoopType type)
{
    for (int i = 0; i < m_hoopCombo->count(); ++i) {
        if (m_hoopCombo->itemData(i).toInt() == static_cast<int>(type)) {
            m_hoopCombo->setCurrentIndex(i);
            return;
        }
    }
    const HoopSpec hs = hoopSpec(type);
    m_view->setHoop(hs.widthMm, hs.heightMm, QString::fromLatin1(hs.name));
    m_editor->setHoop(hs.widthMm, hs.heightMm, QString::fromLatin1(hs.name), type);
    updateInfoPanel();
}

// ---------------------------------------------------------------------------
void MainWindow::setCurrentSequence(const StitchSequence& s, const QString& label)
{
    m_current = s;
    m_editor->setSequence(m_current);
    m_view->setSequence(m_current);
    m_view->showAll();
    updateInfoPanel();
    setStatusForSequence(label);
}

// ---------------------------------------------------------------------------
void MainWindow::mirrorHorizontal()
{
    if (m_current.empty()) return;
    double minX, minY, maxX, maxY;
    if (!m_current.bounds(minX, minY, maxX, maxY)) return;
    const double cx = 0.5 * (minX + maxX);
    for (Stitch& s : m_current.stitches) {
        s.x = 2.0 * cx - s.x;
    }
    for (EditPath& p : m_editor->paths()) {
        for (BezierNode& n : p.nodes) {
            n.pos.setX(2.0 * cx - n.pos.x());
            n.ctrlIn.setX(2.0 * cx - n.ctrlIn.x());
            n.ctrlOut.setX(2.0 * cx - n.ctrlOut.x());
        }
    }
    m_editor->setSequence(m_current);
    m_view->setSequence(m_current);
    updateInfoPanel();
    statusBar()->showMessage(QStringLiteral("Motiv horizontal gespiegelt (↔)."), 3000);
}

void MainWindow::mirrorVertical()
{
    if (m_current.empty()) return;
    double minX, minY, maxX, maxY;
    if (!m_current.bounds(minX, minY, maxX, maxY)) return;
    const double cy = 0.5 * (minY + maxY);
    for (Stitch& s : m_current.stitches) {
        s.y = 2.0 * cy - s.y;
    }
    for (EditPath& p : m_editor->paths()) {
        for (BezierNode& n : p.nodes) {
            n.pos.setY(2.0 * cy - n.pos.y());
            n.ctrlIn.setY(2.0 * cy - n.ctrlIn.y());
            n.ctrlOut.setY(2.0 * cy - n.ctrlOut.y());
        }
    }
    m_editor->setSequence(m_current);
    m_view->setSequence(m_current);
    updateInfoPanel();
    statusBar()->showMessage(QStringLiteral("Motiv vertikal gespiegelt (↕)."), 3000);
}

void MainWindow::rotate90(bool clockwise)
{
    if (m_current.empty()) return;
    double minX, minY, maxX, maxY;
    if (!m_current.bounds(minX, minY, maxX, maxY)) return;
    const double cx = 0.5 * (minX + maxX);
    const double cy = 0.5 * (minY + maxY);
    for (Stitch& s : m_current.stitches) {
        const double dx = s.x - cx;
        const double dy = s.y - cy;
        if (clockwise) {
            s.x = cx + dy;
            s.y = cy - dx;
        } else {
            s.x = cx - dy;
            s.y = cy + dx;
        }
    }
    m_editor->setSequence(m_current);
    m_view->setSequence(m_current);
    updateInfoPanel();
    statusBar()->showMessage(clockwise
        ? QStringLiteral("Motiv 90° im Uhrzeigersinn gedreht (↻).")
        : QStringLiteral("Motiv 90° gegen den Uhrzeigersinn gedreht (↺)."), 3000);
}

void MainWindow::rotateFree()
{
    if (m_current.empty()) {
        QMessageBox::information(this, windowTitle(), QStringLiteral("Zuerst ein Motiv erzeugen oder laden."));
        return;
    }
    bool ok = false;
    const double deg = QInputDialog::getDouble(this, QStringLiteral("Motiv frei drehen"),
        QStringLiteral("Drehwinkel in Grad (-180° bis +180°):"), 45.0, -180.0, 180.0, 1, &ok);
    if (!ok || std::abs(deg) < 1e-4) return;

    double minX, minY, maxX, maxY;
    if (!m_current.bounds(minX, minY, maxX, maxY)) return;
    const double cx = 0.5 * (minX + maxX);
    const double cy = 0.5 * (minY + maxY);
    const double rad = -deg * M_PI / 180.0;
    const double cosA = std::cos(rad);
    const double sinA = std::sin(rad);

    for (Stitch& s : m_current.stitches) {
        const double dx = s.x - cx;
        const double dy = s.y - cy;
        s.x = cx + dx * cosA - dy * sinA;
        s.y = cy + dx * sinA + dy * cosA;
    }
    m_editor->setSequence(m_current);
    m_view->setSequence(m_current);
    updateInfoPanel();
    statusBar()->showMessage(QStringLiteral("Motiv um %1° gedreht (⟳).").arg(deg, 0, 'f', 1), 3000);
}

// ---------------------------------------------------------------------------
void MainWindow::modeChanged(int index)
{
    m_editor->setMode(index == 1 ? PathEditorWidget::Mode::TatamiPolygon
                                  : PathEditorWidget::Mode::SatinRails);
}

// ---------------------------------------------------------------------------
void MainWindow::generate()
{
    const auto paths = m_editor->paths();
    const double density  = m_density->value();
    const double pull     = m_pull->value();
    const double maxStit  = m_maxStitch->value();
    const bool   underlay = m_underlay->isChecked();

    if (m_editor->mode() == PathEditorWidget::Mode::SatinRails) {
        int usable = 0;
        for (const auto& p : paths) if (p.count() >= 2) ++usable;
        if (usable < 2) {
            statusBar()->showMessage(QStringLiteral("Satin braucht zwei Rails mit je ≥2 Punkten."), 6000);
            return;
        }
        QVector<QPainterPath> rails;
        for (const auto& p : paths) if (p.count() >= 2) rails.push_back(p.toPainterPath());
        SatinGenerator::Params sp; sp.densityMm = density; sp.pullCompMm = pull;
        sp.maxRungMm = maxStit; sp.underlay = underlay;
        m_current = SatinGenerator::generate(rails[0], rails[1], sp);
        m_current.palette.clear();
        m_current.palette.push_back(ThreadCatalog::snap(QColor(210, 40, 50)));
    } else {
        int outer = -1;
        for (int i = 0; i < paths.size(); ++i) if (paths[i].count() >= 3) { outer = i; break; }
        if (outer < 0) {
            statusBar()->showMessage(QStringLiteral("Tatami braucht ein Polygon mit ≥3 Punkten."), 6000);
            return;
        }
        QVector<QPolygonF> region;
        region.push_back(paths[outer].toPolygon());
        for (int i = 0; i < paths.size(); ++i)
            if (i != outer && paths[i].count() >= 3) region.push_back(paths[i].toPolygon());
        TatamiFill::Params tp; tp.rowSpacingMm = density;
        tp.maxStitchMm = maxStit; tp.underlay = underlay;
        tp.fillAngleDeg = m_fillAngleSpin ? m_fillAngleSpin->value() : 45.0;
        tp.pattern = m_patternCombo ? TatamiFill::PatternType(m_patternCombo->currentData().toInt()) : TatamiFill::PatternType::StandardTatami;
        m_current = TatamiFill::generate(region, tp);
        m_current.palette.clear();
        m_current.palette.push_back(ThreadCatalog::snap(QColor(40, 90, 200)));
    }

    setCurrentSequence(m_current, QStringLiteral("Erzeugt"));
}

// ---------------------------------------------------------------------------
void MainWindow::loadSatinDemo()
{
    m_mode->setCurrentIndex(0);
    m_editor->setMode(PathEditorWidget::Mode::SatinRails);
    EditPath railA;
    { BezierNode a(QPointF(-40, -20)); a.ctrlOut = QPointF(-20, 20);
      BezierNode b(QPointF( 40, -20)); b.ctrlIn  = QPointF( 20, 20); railA.nodes << a << b; }
    EditPath railB;
    { BezierNode a(QPointF(-40, -12)); a.ctrlOut = QPointF(-20, 32);
      BezierNode b(QPointF( 40,  -8)); b.ctrlIn  = QPointF( 20, 32); railB.nodes << a << b; }
    QVector<EditPath> ps; ps << railA << railB;
    m_editor->loadPaths(ps);
    generate();
}

// ---------------------------------------------------------------------------
void MainWindow::loadTatamiDemo()
{
    m_mode->setCurrentIndex(1);
    m_editor->setMode(PathEditorWidget::Mode::TatamiPolygon);
    const double r = 34.0, k = r * 0.5522847498;
    EditPath ring; ring.closed = true;
    auto nd = [&](QPointF p, QPointF i, QPointF o){ BezierNode n(p); n.ctrlIn = i; n.ctrlOut = o; return n; };
    ring.nodes << nd(QPointF(r, 0),  QPointF(r, -k), QPointF(r, k))
               << nd(QPointF(0, r),  QPointF(k, r),  QPointF(-k, r))
               << nd(QPointF(-r, 0), QPointF(-r, k), QPointF(-r, -k))
               << nd(QPointF(0, -r), QPointF(-k, -r), QPointF(k, -r));
    QVector<EditPath> ps; ps << ring;
    m_editor->loadPaths(ps);
    generate();
}

// ---------------------------------------------------------------------------
void MainWindow::makeText()
{
    QDialog d(this);
    d.setWindowTitle(QStringLiteral("Janome Digitizer Jr — Schriftzug & Lettering"));
    d.resize(480, 520);
    auto* mainLay = new QVBoxLayout(&d);

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft);

    auto* textEdit = new QLineEdit(QStringLiteral("StickCore"), &d);
    textEdit->setPlaceholderText(QStringLiteral("Text hier eingeben…"));
    form->addRow(QStringLiteral("<b>Text:</b>"), textEdit);

    // Baseline Form (Gerade, Bogen Oben, Bogen Unten, Kreis)
    auto* baseCombo = new QComboBox(&d);
    baseCombo->addItem(QStringLiteral("━ Gerade (Horizontal)"), int(TextDigitizer::Baseline::Straight));
    baseCombo->addItem(QStringLiteral("⌒ Bogen Oben (Arc Up)"), int(TextDigitizer::Baseline::ArcUp));
    baseCombo->addItem(QStringLiteral("⌣ Bogen Unten (Arc Down)"), int(TextDigitizer::Baseline::ArcDown));
    baseCombo->addItem(QStringLiteral("⭕ Kreis (Circle)"), int(TextDigitizer::Baseline::Circle));
    form->addRow(QStringLiteral("Bogenform:"), baseCombo);

    // Bogenradius
    auto* radSpin = new QDoubleSpinBox(&d);
    radSpin->setRange(15.0, 300.0);
    radSpin->setValue(50.0);
    radSpin->setSuffix(QStringLiteral(" mm"));
    radSpin->setEnabled(false);
    form->addRow(QStringLiteral("Bogenradius:"), radSpin);

    connect(baseCombo, qOverload<int>(&QComboBox::currentIndexChanged), [baseCombo, radSpin]{
        const auto b = TextDigitizer::Baseline(baseCombo->currentData().toInt());
        radSpin->setEnabled(b != TextDigitizer::Baseline::Straight);
    });

    // Font Family
    auto* fontCombo = new QComboBox(&d);
    fontCombo->addItem(QStringLiteral("DejaVu Sans (Klar & Modern)"), QStringLiteral("DejaVu Sans"));
    fontCombo->addItem(QStringLiteral("Serif (Klassisch Elegant)"), QStringLiteral("Serif"));
    fontCombo->addItem(QStringLiteral("Brush Script / Schreibschrift"), QStringLiteral("Brush Script MT"));
    form->addRow(QStringLiteral("Schriftart:"), fontCombo);

    auto* boldCheck = new QCheckBox(QStringLiteral("Fett (Bold)"), &d);
    boldCheck->setChecked(true);
    auto* italicCheck = new QCheckBox(QStringLiteral("Kursiv (Italic)"), &d);
    auto* styleBox = new QHBoxLayout;
    styleBox->addWidget(boldCheck);
    styleBox->addWidget(italicCheck);
    form->addRow(QStringLiteral("Schriftschnitt:"), styleBox);

    // Height
    auto* heightSpin = new QDoubleSpinBox(&d);
    heightSpin->setRange(6.0, 120.0);
    heightSpin->setValue(20.0);
    heightSpin->setSuffix(QStringLiteral(" mm"));
    form->addRow(QStringLiteral("Schrifthöhe:"), heightSpin);

    // Letter Spacing (Kerning)
    auto* spaceSpin = new QDoubleSpinBox(&d);
    spaceSpin->setRange(-2.0, 15.0);
    spaceSpin->setSingleStep(0.5);
    spaceSpin->setValue(0.0);
    spaceSpin->setSuffix(QStringLiteral(" mm"));
    form->addRow(QStringLiteral("Zeichenabstand:"), spaceSpin);

    // Slant
    auto* slantSpin = new QDoubleSpinBox(&d);
    slantSpin->setRange(-30.0, 30.0);
    slantSpin->setSingleStep(2.0);
    slantSpin->setValue(0.0);
    slantSpin->setSuffix(QStringLiteral("°"));
    form->addRow(QStringLiteral("Neigung (Slant):"), slantSpin);

    // Fill Angle
    auto* angleSpin = new QDoubleSpinBox(&d);
    angleSpin->setRange(0.0, 360.0);
    angleSpin->setSingleStep(15.0);
    angleSpin->setValue(45.0);
    angleSpin->setSuffix(QStringLiteral("°"));
    form->addRow(QStringLiteral("Füllwinkel:"), angleSpin);

    // Fill Pattern
    auto* patCombo = new QComboBox(&d);
    for (auto pt : { TatamiFill::PatternType::StandardTatami, TatamiFill::PatternType::Brick,
                     TatamiFill::PatternType::Twill, TatamiFill::PatternType::Basketweave,
                     TatamiFill::PatternType::Honeycomb, TatamiFill::PatternType::ContourEcho }) {
        patCombo->addItem(TatamiFill::patternName(pt), int(pt));
    }
    form->addRow(QStringLiteral("Füllmuster:"), patCombo);

    // Stitch Style
    auto* stitchCombo = new QComboBox(&d);
    stitchCombo->addItem(QStringLiteral("✦ Erhabener Satin-Rand auf Füllung (Janome Jr)"), int(TextDigitizer::StitchStyle::Raised));
    stitchCombo->addItem(QStringLiteral("🧵 Reine Satin-Kontur (Satin Outline)"), int(TextDigitizer::StitchStyle::SatinOutline));
    stitchCombo->addItem(QStringLiteral("▦ Dichte Tatami-Webung (Tatami Only)"), int(TextDigitizer::StitchStyle::TatamiOnly));
    stitchCombo->addItem(QStringLiteral("✒ Feiner Steppstich (Run Stitch)"), int(TextDigitizer::StitchStyle::RunStitch));
    form->addRow(QStringLiteral("Stick-Stil:"), stitchCombo);

    mainLay->addLayout(form);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    mainLay->addWidget(bb);

    if (d.exec() != QDialog::Accepted || textEdit->text().trimmed().isEmpty()) return;

    TextDigitizer::Params tp;
    tp.text            = textEdit->text().trimmed();
    tp.family          = fontCombo->currentData().toString();
    tp.bold            = boldCheck->isChecked();
    tp.italic          = italicCheck->isChecked();
    tp.heightMm        = heightSpin->value();
    tp.baseline        = TextDigitizer::Baseline(baseCombo->currentData().toInt());
    tp.arcRadiusMm     = radSpin->value();
    tp.letterSpacingMm = spaceSpin->value();
    tp.slantDeg        = slantSpin->value();
    tp.fillAngleDeg    = angleSpin->value();
    tp.pattern         = TatamiFill::PatternType(patCombo->currentData().toInt());
    tp.style           = TextDigitizer::StitchStyle(stitchCombo->currentData().toInt());
    tp.raised          = (tp.style == TextDigitizer::StitchStyle::Raised);
    tp.densityMm       = m_density->value();
    tp.maxStitchMm     = m_maxStitch->value();
    tp.underlay        = m_underlay->isChecked();

    m_current = TextDigitizer::generate(tp);
    m_current.palette = { ThreadCatalog::snap(QColor(150, 40, 60)) };
    setCurrentSequence(m_current, QStringLiteral("Text: %1").arg(tp.text));
}

// ---------------------------------------------------------------------------
void MainWindow::makeMonogram()
{
    QDialog d(this);
    d.setWindowTitle(QStringLiteral("Kunstvolles Monogramm"));
    auto* form = new QFormLayout(&d);
    auto* letters = new QLineEdit(QStringLiteral("ABC")); letters->setMaxLength(3);
    letters->setToolTip(QStringLiteral("Bis zu 3 Initialen in Anzeige-Reihenfolge.\n"
        "Klassische Etikette: Vorname – NACHNAME (Mitte, groß) – 2. Vorname."));
    auto* big = new QCheckBox(QStringLiteral("Mittleren Buchstaben größer (klassischer Stil)"));
    big->setChecked(true);
    auto* style = new QComboBox;
    for (auto s : { MonogramGenerator::Style::Serif, MonogramGenerator::Style::Sans,
                    MonogramGenerator::Style::Script, MonogramGenerator::Style::SlabBold,
                    MonogramGenerator::Style::Elegant,
                    MonogramGenerator::Style::GreatVibes, MonogramGenerator::Style::Tangerine,
                    MonogramGenerator::Style::Pinyon, MonogramGenerator::Style::Parisienne,
                    MonogramGenerator::Style::Allura, MonogramGenerator::Style::PetitFormal,
                    MonogramGenerator::Style::Muellerhoff, MonogramGenerator::Style::AlexBrush })
        style->addItem(MonogramGenerator::styleName(s), int(s));
    auto* fill = new QComboBox;
    for (auto fst : { MonogramGenerator::Fill::Raised, MonogramGenerator::Fill::Contour })
        fill->addItem(MonogramGenerator::fillName(fst), int(fst));
    auto* frame = new QComboBox;
    for (auto fr : { MonogramGenerator::Frame::OakWreath,
                     MonogramGenerator::Frame::LaurelWreath,
                     MonogramGenerator::Frame::ShieldCrest,
                     MonogramGenerator::Frame::BaroqueCartouche,
                     MonogramGenerator::Frame::Oval,
                     MonogramGenerator::Frame::Circle,
                     MonogramGenerator::Frame::None }) {
        frame->addItem(MonogramGenerator::frameName(fr), int(fr));
    }

    auto* angleSpin = new QDoubleSpinBox(&d);
    angleSpin->setRange(0.0, 360.0);
    angleSpin->setSingleStep(15.0);
    angleSpin->setValue(45.0);
    angleSpin->setSuffix(QStringLiteral("°"));

    QObject::connect(style, QOverload<int>::of(&QComboBox::currentIndexChanged), &d,
        [style, fill]{
            const auto s = MonogramGenerator::Style(style->currentData().toInt());
            const bool calli = s == MonogramGenerator::Style::GreatVibes
                            || s == MonogramGenerator::Style::Tangerine
                            || s == MonogramGenerator::Style::Pinyon
                            || s == MonogramGenerator::Style::Parisienne
                            || s == MonogramGenerator::Style::Allura
                            || s == MonogramGenerator::Style::PetitFormal
                            || s == MonogramGenerator::Style::Muellerhoff
                            || s == MonogramGenerator::Style::AlexBrush;
            fill->setCurrentIndex(calli ? 1 : 0);
        });
    form->addRow(QStringLiteral("Initialen (1–3):"), letters);
    form->addRow(QString(), big);
    form->addRow(QStringLiteral("Schriftstil:"), style);
    form->addRow(QStringLiteral("Füllung:"), fill);
    form->addRow(QStringLiteral("Füllwinkel:"), angleSpin);
    form->addRow(QStringLiteral("Schmuckrahmen:"), frame);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted || letters->text().trimmed().isEmpty()) return;

    MonogramGenerator::Params mp;
    mp.letters = letters->text();
    mp.centerLarge = big->isChecked();
    mp.frame = MonogramGenerator::Frame(frame->currentData().toInt());
    mp.style = MonogramGenerator::Style(style->currentData().toInt());
    mp.fill  = MonogramGenerator::Fill(fill->currentData().toInt());
    mp.fillAngleDeg = angleSpin->value();
    mp.densityMm = m_density->value();
    mp.maxStitchMm = m_maxStitch->value();
    mp.underlay = m_underlay->isChecked();
    mp.heightMm = 34.0;
    mp.color = QColor(40, 55, 90);
    mp.frameColor = QColor(185, 140, 45); // Gold / Bronze
    m_current = MonogramGenerator::generate(mp);
    setCurrentSequence(m_current, QStringLiteral("Monogramm: %1").arg(mp.letters));
}

// ---------------------------------------------------------------------------
void MainWindow::resizeDesign()
{
    if (m_current.empty()) {
        QMessageBox::information(this, windowTitle(), QStringLiteral("Zuerst ein Motiv erzeugen."));
        return;
    }
    double x0, y0, x1, y1; m_current.bounds(x0, y0, x1, y1);
    const double curW = x1 - x0;
    bool ok = false;
    const double target = QInputDialog::getDouble(this, QStringLiteral("Größe ändern"),
        QStringLiteral("Neue Breite (mm):"), curW, 5.0, 300.0, 1, &ok);
    if (!ok || curW < 1e-6) return;

    const double f = target / curW;
    const double cx = 0.5 * (x0 + x1), cy = 0.5 * (y0 + y1);
    for (Stitch& s : m_current.stitches) { s.x = cx + (s.x - cx) * f; s.y = cy + (s.y - cy) * f; }

    setCurrentSequence(m_current, QStringLiteral("Skaliert"));
    statusBar()->showMessage(
        QStringLiteral("Auf %1 mm Breite skaliert. Tipp: bei großen Änderungen das Motiv neu erzeugen (Dichte).")
            .arg(target, 0, 'f', 1), 8000);
}

// ---------------------------------------------------------------------------
void MainWindow::makeLogo()
{
    loadMefOriginal(0);
}

void MainWindow::loadMefOriginal(int style)
{
    m_current = LogoGenerator::mefOriginalBadge(style, 150.0);
    if (m_current.empty()) {
        m_current = LogoGenerator::knueppelBadge();
    }

    QString styleName;
    switch (style) {
    case 0:
    default: styleName = QStringLiteral("Atelier Meister-Aufnäher (Foto-Original: Kettelrand & Satinschrift)"); break;
    case 1: styleName = QStringLiteral("Königliche Meister-Tatami (Dichte Webung)"); break;
    case 2: styleName = QStringLiteral("Zwei-Ton-Relief Gold (Madeira 1083 / 1070)"); break;
    case 3: styleName = QStringLiteral("Feiner Gold-Steppstich (Linien-Art)"); break;
    case 4: styleName = QStringLiteral("Atelier Kontur-Glanz (Florentiner Schimmer)"); break;
    }

    setCurrentSequence(m_current, QStringLiteral("👑 %1").arg(styleName));
    statusBar()->showMessage(
        QStringLiteral("👑 Modewerkstatt Knüppel — %1 gestickt (%2 Stiche · Janome Hoop B).")
            .arg(styleName).arg(m_current.stitches.size()), 8000);
}

void MainWindow::importSvg()
{
    const QString fn = QFileDialog::getOpenFileName(this,
        QStringLiteral("SVG-Vektordatei auswählen"),
        QString(),
        QStringLiteral("SVG-Vektorgrafiken (*.svg);;Alle Dateien (*.*)"));
    if (fn.isEmpty()) return;

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("SVG Vektor-Stick Assistent"));
    auto* lay = new QVBoxLayout(&dlg);

    lay->addWidget(new QLabel(QStringLiteral("<b>SVG-Vektorgrafik kunstvoll digitalisieren:</b>"), &dlg));
    auto* nameLbl = new QLabel(QFileInfo(fn).fileName(), &dlg);
    nameLbl->setStyleSheet(QStringLiteral("color:#2dd4bf; font-weight:600; font-size:13px;"));
    lay->addWidget(nameLbl);

    auto* form = new QFormLayout;
    auto* styleCombo = new QComboBox(&dlg);
    styleCombo->addItem(QStringLiteral("👑 Meister-Aufnäher (Foto-Original: Kettelrand & Satinschrift)"), static_cast<int>(SvgDigitizer::StitchStyle::AuthenticPatchSatin));
    styleCombo->addItem(QStringLiteral("✦ Atelier Kontur-Glanz (Florentiner Schimmer)"), static_cast<int>(SvgDigitizer::StitchStyle::ContourEcho));
    styleCombo->addItem(QStringLiteral("🧵 Königliche Tatami-Webung (Dichter Stich)"), static_cast<int>(SvgDigitizer::StitchStyle::TatamiWeave));
    styleCombo->addItem(QStringLiteral("✨ Zwei-Ton-Relief Gold (Madeira Duotone)"), static_cast<int>(SvgDigitizer::StitchStyle::RoyalDuotoneGold));
    styleCombo->addItem(QStringLiteral("✒ Feiner Steppstich (Doppel-Linie)"), static_cast<int>(SvgDigitizer::StitchStyle::FineOutlineRun));
    form->addRow(QStringLiteral("Künstlerischer Stickstil:"), styleCombo);

    auto* widthSpin = new QDoubleSpinBox(&dlg);
    widthSpin->setRange(20.0, 300.0);
    widthSpin->setValue(150.0);
    widthSpin->setSuffix(QStringLiteral(" mm"));
    widthSpin->setToolTip(QStringLiteral("Passende Rahmen: Janome Hoop B (140x200mm) oder Hoop A (126x110mm)"));
    form->addRow(QStringLiteral("Zielbreite:"), widthSpin);

    auto* spacingSpin = new QDoubleSpinBox(&dlg);
    spacingSpin->setRange(0.20, 1.50);
    spacingSpin->setSingleStep(0.05);
    spacingSpin->setValue(0.40);
    spacingSpin->setSuffix(QStringLiteral(" mm"));
    form->addRow(QStringLiteral("Stich-/Reihenabstand:"), spacingSpin);

    lay->addLayout(form);

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(btns);

    if (dlg.exec() != QDialog::Accepted) return;

    SvgDigitizer::Params params;
    params.style = static_cast<SvgDigitizer::StitchStyle>(styleCombo->currentData().toInt());
    params.targetWidthMm = widthSpin->value();
    params.spacingMm = spacingSpin->value();

    m_current = SvgDigitizer::digitizeSvgFile(fn, params);
    if (m_current.empty()) {
        QMessageBox::warning(this, windowTitle(),
            QStringLiteral("Aus der SVG-Datei konnten keine Stiche erzeugt werden."));
        return;
    }

    setCurrentSequence(m_current, QStringLiteral("SVG: %1").arg(QFileInfo(fn).fileName()));
    statusBar()->showMessage(QStringLiteral("SVG-Vektordatei '%1' erfolgreich digitalisiert (%2 Stiche).")
                             .arg(QFileInfo(fn).fileName()).arg(m_current.stitches.size()), 8000);
}

// ---------------------------------------------------------------------------
void MainWindow::placeDesign()
{
    if (m_current.empty()) {
        QMessageBox::information(this, windowTitle(), QStringLiteral("Zuerst ein Motiv erzeugen."));
        return;
    }
    const auto& mp = MachineProfile::current();
    double x0, y0, x1, y1; m_current.bounds(x0, y0, x1, y1);
    const double w = x1 - x0, h = y1 - y0;

    HoopType ht;
    const int idx = m_hoopCombo ? m_hoopCombo->currentIndex() : 0;
    if (idx > 0 && idx < m_hoopCombo->count()) ht = static_cast<HoopType>(m_hoopCombo->itemData(idx).toInt());
    else ht = mp.hoopFor(w, h);
    const HoopSpec hs = hoopSpec(ht);

    if (w > hs.widthMm + 0.01 || h > hs.heightMm + 0.01) {
        QMessageBox::information(this, windowTitle(),
            QStringLiteral("Das Motiv ist größer als der Rahmen %1. Erst über „Größe…“ verkleinern.")
                .arg(QString::fromLatin1(hs.name)));
        return;
    }

    PlacementDialog dlg(m_current, hs.widthMm, hs.heightMm, this);
    if (dlg.exec() != QDialog::Accepted) return;
    const QPointF off = dlg.offsetMm();
    for (Stitch& s : m_current.stitches) { s.x += off.x(); s.y += off.y(); }

    setCurrentSequence(m_current, QStringLiteral("Platziert"));
    statusBar()->showMessage(
        QStringLiteral("Motiv im Rahmen %1 platziert (Versatz %2 / %3 mm).")
            .arg(QString::fromLatin1(hs.name)).arg(off.x(), 0, 'f', 1).arg(off.y(), 0, 'f', 1), 6000);
}

// ---------------------------------------------------------------------------
void MainWindow::exportSizeVariants()
{
    if (m_current.empty()) {
        QMessageBox::information(this, windowTitle(), QStringLiteral("Zuerst ein Motiv erzeugen."));
        return;
    }
    const QString base = QFileDialog::getSaveFileName(this,
        QStringLiteral("Größen-Varianten speichern (Basisname)"),
        QStringLiteral("logo.jef"), QStringLiteral("Janome JEF (*.jef)"));
    if (base.isEmpty()) return;
    const QFileInfo fi(base);
    const QString stem = fi.dir().filePath(fi.completeBaseName());

    double x0, y0, x1, y1; m_current.bounds(x0, y0, x1, y1);
    const double curW = std::max(1e-6, x1 - x0);
    const double cx = 0.5 * (x0 + x1), cy = 0.5 * (y0 + y1);
    const auto& mp = MachineProfile::current();

    int made = 0;
    for (double w : { 40.0, 60.0, 80.0, 100.0, 120.0 }) {
        StitchSequence v = m_current;
        const double f = w / curW;
        double h = (y1 - y0) * f;
        if (!mp.fits(w, h)) continue;
        for (Stitch& s : v.stitches) { s.x = cx + (s.x - cx) * f; s.y = cy + (s.y - cy) * f; }
        const QString path = QStringLiteral("%1_%2mm.jef").arg(stem).arg(int(w));
        if (JefCodec::exportToFile(path, v, mp.hoopFor(w, h)).ok) ++made;
    }
    statusBar()->showMessage(
        QStringLiteral("%1 Größen-Varianten gespeichert (40–120 mm, nur passende).").arg(made), 8000);
}

// ---------------------------------------------------------------------------
void MainWindow::digitizeImage()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Bild digitalisieren"),
        QString(), QStringLiteral("Bilder (*.png *.jpg *.jpeg *.bmp *.gif)"));
    if (path.isEmpty()) return;
    QImage img(path);
    if (img.isNull()) { QMessageBox::warning(this, windowTitle(), QStringLiteral("Bild konnte nicht geladen werden.")); return; }
    m_editor->setBackgroundImage(img);
    digitizeWithDialog(img, QStringLiteral("Bild digitalisiert"));
}

// ---------------------------------------------------------------------------
void MainWindow::digitizeWithDialog(const QImage& img, const QString& label)
{
    ImageDialog dlg(img, this);
    if (dlg.exec() != QDialog::Accepted) return;
    ImageDigitizer::Params ip = dlg.params();
    ip.densityMm   = m_density->value();
    ip.maxStitchMm = m_maxStitch->value();
    StitchSequence s = ImageDigitizer::generate(img, ip);
    if (s.empty()) {
        QMessageBox::information(this, windowTitle(),
            QStringLiteral("Keine Stiche erzeugt — probiere mehr Kontrast oder einen anderen Schwellwert."));
        return;
    }
    m_editor->setBackgroundImage(img);
    setCurrentSequence(s, label);
}

// ---------------------------------------------------------------------------
void MainWindow::digitizeFrom(const QImage& img, const QString& label)
{
    ImageDigitizer::Params ip;
    ip.colors    = 6;
    ip.widthMm   = 100.0;
    ip.densityMm = m_density ? m_density->value() : 0.5;
    ip.brand     = MachineProfile::current().defaultBrand;
    StitchSequence s = ImageDigitizer::generate(img, ip);
    if (s.empty()) {
        QMessageBox::information(this, windowTitle(), QStringLiteral("Keine Stiche erzeugt (zu wenig Kontrast?)."));
        return;
    }
    m_editor->setBackgroundImage(img);
    setCurrentSequence(s, label);
}

// ---------------------------------------------------------------------------
void MainWindow::phoneUpload()
{
    if (!m_upload) {
        m_upload = new QrUploadServer(this);
        connect(m_upload, &QrUploadServer::imageReceived, this, [this](const QImage& img){
            if (m_uploadStatus)
                m_uploadStatus->setText(QStringLiteral("✓ Bild empfangen – Einstellungen wählen…"));
            m_editor->setBackgroundImage(img);
            digitizeWithDialog(img, QStringLiteral("Handy-Bild digitalisiert"));
            if (m_uploadStatus)
                m_uploadStatus->setText(QStringLiteral("✓ %1 Stiche. Du kannst weitere Bilder senden.")
                    .arg(m_current.realStitchCount()));
        });
    }
    if (!m_upload->isListening() && !m_upload->start(8080)) {
        QMessageBox::warning(this, windowTitle(), QStringLiteral("Konnte den Upload-Server nicht starten (Port belegt?)."));
        return;
    }
    const QString url = m_upload->url();
    const auto matrix = QrCode::encode(url.toStdString());

    auto* d = new QDialog(this);
    d->setWindowTitle(QStringLiteral("Handy-Upload"));
    d->setAttribute(Qt::WA_DeleteOnClose);
    auto* lay = new QVBoxLayout(d);
    auto* qr = new QLabel; qr->setPixmap(qrPixmap(matrix, 8, 4)); qr->setAlignment(Qt::AlignCenter);
    lay->addWidget(qr);
    auto* info = new QLabel(QStringLiteral("Scanne den QR-Code mit dem Handy (gleiches WLAN).\nOder öffne im Browser:  %1").arg(url));
    info->setAlignment(Qt::AlignCenter); info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lay->addWidget(info);
    m_uploadStatus = new QLabel(QStringLiteral("Warte auf Upload…"));
    m_uploadStatus->setAlignment(Qt::AlignCenter); m_uploadStatus->setStyleSheet(QStringLiteral("color:#2dd4bf"));
    lay->addWidget(m_uploadStatus);
    connect(d, &QObject::destroyed, this, [this]{ m_uploadStatus = nullptr; });
    d->show();
}

// ---------------------------------------------------------------------------
void MainWindow::exportJef()
{
    if (m_current.empty()) {
        QMessageBox::information(this, windowTitle(), QStringLiteral("Nichts erzeugt – bitte zuerst „Erzeugen“."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Als JEF speichern"),
        QStringLiteral("design.jef"), QStringLiteral("Janome JEF (*.jef)"));
    if (path.isEmpty()) return;

    double x0, y0, x1, y1; m_current.bounds(x0, y0, x1, y1);
    const auto& mp = MachineProfile::current();
    HoopType hoop;
    if (m_hoopCombo->currentIndex() <= 0)
        hoop = mp.hoopFor(x1 - x0, y1 - y0);
    else
        hoop = static_cast<HoopType>(m_hoopCombo->currentData().toInt());

    const JefCodec::Result res = JefCodec::exportToFile(path, m_current, hoop);
    if (res.ok)
        statusBar()->showMessage(QStringLiteral("Export (%1): %2").arg(QString::fromLatin1(hoopSpec(hoop).name), res.message), 8000);
    else
        QMessageBox::warning(this, windowTitle(), QStringLiteral("Export fehlgeschlagen: %1").arg(res.message));
}

// ---------------------------------------------------------------------------
void MainWindow::setStatusForSequence(const QString& prefix)
{
    updateInfoPanel();
    double a, b, c, d;
    if (!m_current.bounds(a, b, c, d)) return;
    statusBar()->showMessage(QStringLiteral("%1 — %2 Stiche · %3 Farben · %4 × %5 mm")
        .arg(prefix.isEmpty() ? QStringLiteral("Design") : prefix)
        .arg(m_current.realStitchCount()).arg(m_current.palette.size())
        .arg(c - a, 0, 'f', 1).arg(d - b, 0, 'f', 1));
}

// ---------------------------------------------------------------------------
void MainWindow::captureManualScreenshots(const QString& outDir)
{
    QDir().mkpath(outDir);
    resize(1440, 920);

    // 1. MEF Original Badge in Main Window (Full Studio View)
    loadMefOriginal(0);
    QApplication::processEvents();
    this->grab().save(outDir + QStringLiteral("/01_studio_main_mef.png"));

    // 2. Close-up of 2D Stitch & Design Canvas
    if (m_editor) {
        m_editor->grab().save(outDir + QStringLiteral("/02_canvas_2d_closeup.png"));
    }

    // 3. Close-up of 3D OpenGL Thread Simulation
    if (m_view) {
        m_view->grab().save(outDir + QStringLiteral("/03_gl_simulation_closeup.png"));
    }

    // 4. Background Artwork Dimming Overlay
    if (m_editor) {
        QImage bgImg(640, 420, QImage::Format_ARGB32_Premultiplied);
        bgImg.fill(Qt::transparent);
        QPainter p(&bgImg);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(179, 139, 69, 160), 2.0, Qt::DashLine));
        p.drawRoundedRect(30, 30, 580, 360, 16, 16);
        QFont f(QStringLiteral("DejaVu Sans"), 16, QFont::Bold);
        p.setFont(f);
        p.setPen(QColor(218, 178, 85, 230));
        p.drawText(QRectF(0, 60, 640, 40), Qt::AlignCenter, QStringLiteral("Modewerkstatt Knüppel"));
        p.setFont(QFont(QStringLiteral("DejaVu Sans"), 11));
        p.setPen(QColor(160, 170, 190, 200));
        p.drawText(QRectF(0, 105, 640, 30), Qt::AlignCenter, QStringLiteral("Original Vektorvorlage · SVG Vektor-Digitalisierung"));
        p.end();

        m_editor->setBackgroundImage(bgImg, 0.45);
        QApplication::processEvents();
        m_editor->grab().save(outDir + QStringLiteral("/06_artwork_dimming_overlay.png"));
        m_editor->clearBackgroundImage();
    }

    // 5. Bézier Vector Path Editor Mode
    if (m_editor) {
        m_editor->setCanvasMode(PathEditorWidget::CanvasMode::BezierPaths);
        loadSatinDemo();
        QApplication::processEvents();
        m_editor->grab().save(outDir + QStringLiteral("/05_bezier_editor_closeup.png"));
        m_editor->setCanvasMode(PathEditorWidget::CanvasMode::Stitches2D);
    }

    // 6. Janome Digitizer Jr Lettering Dialog
    {
        QDialog d(this);
        d.setWindowTitle(QStringLiteral("Janome Digitizer Jr — Schriftzug & Lettering"));
        d.resize(520, 560);
        d.setStyleSheet(styleSheet());
        auto* mainLay = new QVBoxLayout(&d);
        auto* form = new QFormLayout;
        form->setLabelAlignment(Qt::AlignLeft);

        auto* textEdit = new QLineEdit(QStringLiteral("StickCore Studio"), &d);
        form->addRow(QStringLiteral("<b>Text:</b>"), textEdit);

        auto* baseCombo = new QComboBox(&d);
        baseCombo->addItem(QStringLiteral("⌒ Bogen Oben (Arc Up)"), int(TextDigitizer::Baseline::ArcUp));
        baseCombo->addItem(QStringLiteral("━ Gerade (Horizontal)"), int(TextDigitizer::Baseline::Straight));
        baseCombo->addItem(QStringLiteral("⌣ Bogen Unten (Arc Down)"), int(TextDigitizer::Baseline::ArcDown));
        baseCombo->addItem(QStringLiteral("⭕ Kreis (Circle)"), int(TextDigitizer::Baseline::Circle));
        baseCombo->setCurrentIndex(0);
        form->addRow(QStringLiteral("Bogenform:"), baseCombo);

        auto* radSpin = new QDoubleSpinBox(&d);
        radSpin->setRange(15.0, 300.0);
        radSpin->setValue(55.0);
        radSpin->setSuffix(QStringLiteral(" mm"));
        form->addRow(QStringLiteral("Bogenradius:"), radSpin);

        auto* fontCombo = new QComboBox(&d);
        fontCombo->addItem(QStringLiteral("DejaVu Sans (Klar & Modern)"));
        fontCombo->addItem(QStringLiteral("Serif (Klassisch Elegant)"));
        fontCombo->addItem(QStringLiteral("Brush Script / Schreibschrift"));
        form->addRow(QStringLiteral("Schriftart:"), fontCombo);

        auto* boldCheck = new QCheckBox(QStringLiteral("Fett (Bold)"), &d);
        boldCheck->setChecked(true);
        auto* italicCheck = new QCheckBox(QStringLiteral("Kursiv (Italic)"), &d);
        italicCheck->setChecked(true);
        auto* styleBox = new QHBoxLayout;
        styleBox->addWidget(boldCheck);
        styleBox->addWidget(italicCheck);
        form->addRow(QStringLiteral("Schriftschnitt:"), styleBox);

        auto* heightSpin = new QDoubleSpinBox(&d);
        heightSpin->setRange(6.0, 120.0);
        heightSpin->setValue(22.0);
        heightSpin->setSuffix(QStringLiteral(" mm"));
        form->addRow(QStringLiteral("Schrifthöhe:"), heightSpin);

        auto* spaceSpin = new QDoubleSpinBox(&d);
        spaceSpin->setRange(-2.0, 15.0);
        spaceSpin->setValue(1.5);
        spaceSpin->setSuffix(QStringLiteral(" mm"));
        form->addRow(QStringLiteral("Zeichenabstand (Kerning):"), spaceSpin);

        auto* slantSpin = new QDoubleSpinBox(&d);
        slantSpin->setRange(-30.0, 30.0);
        slantSpin->setValue(12.0);
        slantSpin->setSuffix(QStringLiteral("°"));
        form->addRow(QStringLiteral("Neigung (Slant):"), slantSpin);

        auto* stitchCombo = new QComboBox(&d);
        stitchCombo->addItem(QStringLiteral("✦ Erhabener Satin-Rand auf Füllung (Janome Jr)"));
        stitchCombo->addItem(QStringLiteral("🧵 Reine Satin-Kontur (Satin Outline)"));
        stitchCombo->addItem(QStringLiteral("▦ Dichte Tatami-Webung (Tatami Only)"));
        stitchCombo->addItem(QStringLiteral("✒ Feiner Steppstich (Run Stitch)"));
        stitchCombo->setCurrentIndex(0);
        form->addRow(QStringLiteral("Stick-Stil:"), stitchCombo);

        mainLay->addLayout(form);
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
        mainLay->addWidget(bb);

        d.show();
        QApplication::processEvents();
        d.grab().save(outDir + QStringLiteral("/04_janome_lettering_dialog.png"));
        d.close();
    }

    // 7. Render Curved Lettering Result in 2D & 3D
    {
        TextDigitizer::Params tp;
        tp.text            = QStringLiteral("StickCore Studio");
        tp.family          = QStringLiteral("DejaVu Sans");
        tp.bold            = true;
        tp.italic          = true;
        tp.heightMm        = 22.0;
        tp.baseline        = TextDigitizer::Baseline::ArcUp;
        tp.arcRadiusMm     = 55.0;
        tp.letterSpacingMm = 1.5;
        tp.slantDeg        = 12.0;
        tp.style           = TextDigitizer::StitchStyle::Raised;
        tp.raised          = true;
        tp.densityMm       = 0.40;
        tp.maxStitchMm     = 4.0;
        tp.underlay        = true;

        StitchSequence letterSeq = TextDigitizer::generate(tp);
        letterSeq.palette = { ThreadCatalog::snap(QColor(45, 212, 191)) };
        setCurrentSequence(letterSeq, QStringLiteral("Janome ArcUp Lettering"));
        QApplication::processEvents();
        if (m_editor) m_editor->grab().save(outDir + QStringLiteral("/04b_curved_lettering_canvas.png"));
        if (m_view)   m_view->grab().save(outDir + QStringLiteral("/04c_curved_lettering_3d.png"));
    }

    // Restore MEF Logo
    loadMefOriginal(0);
    QApplication::processEvents();
}

// ---------------------------------------------------------------------------
void MainWindow::loadHuntingMotif(int typeIdx, bool editable)
{
    const auto type = HuntingMotifs::MotifType(std::clamp(typeIdx, 0, 3));
    const QString name = HuntingMotifs::motifName(type);

    if (editable) {
        QVector<EditPath> paths = HuntingMotifs::generateEditablePaths(type, 90.0);
        m_editor->loadPaths(paths);
        m_editor->setMode(PathEditorWidget::Mode::TatamiPolygon);
        statusBar()->showMessage(QStringLiteral("'%1' als bearbeitbare Bézier-Pfade geladen (Punkte verschiebbar, F5 zum Berechnen).").arg(name), 6000);
    } else {
        HuntingMotifs::Params hp;
        hp.widthMm = 90.0;
        hp.fillAngleDeg = m_fillAngleSpin ? m_fillAngleSpin->value() : 45.0;
        hp.pattern = m_patternCombo ? TatamiFill::PatternType(m_patternCombo->currentData().toInt()) : TatamiFill::PatternType::StandardTatami;
        hp.densityMm = m_density ? m_density->value() : 0.40;
        hp.underlay = m_underlay ? m_underlay->isChecked() : true;
        m_current = HuntingMotifs::generateStitches(type, hp);
        setCurrentSequence(m_current, name);
        statusBar()->showMessage(QStringLiteral("'%1' als fertiges Stickmotiv erzeugt.").arg(name), 5000);
    }
}

// ---------------------------------------------------------------------------
void MainWindow::smartColorSort()
{
    if (m_current.empty()) {
        QMessageBox::information(this, windowTitle(), QStringLiteral("Kein Motiv geladen zum Sortieren."));
        return;
    }

    ColorSorter::Stats stats;
    StitchSequence sorted = ColorSorter::sort(m_current, &stats);

    if (stats.colorChangesAfter < stats.colorChangesBefore) {
        m_current = sorted;
        setCurrentSequence(m_current, QStringLiteral("Farbsortiert"));
        QMessageBox::information(this, QStringLiteral("Intelligente Farbsortierung (Smart Color Sort)"),
            QStringLiteral("Farbwechsel erfolgreich reduziert!\n\n"
                           "• Vorher: %1 Garnwechsel\n"
                           "• Nachher: %2 Garnwechsel\n"
                           "• Ersparnis: %3% weniger Umfädeln\n\n"
                           "Die 2D-Schichtenreihenfolge wurde durch topologische Kollisionsanalyse exakt gewahrt.")
                .arg(stats.colorChangesBefore)
                .arg(stats.colorChangesAfter)
                .arg(stats.savedPercent, 0, 'f', 1));
    } else {
        QMessageBox::information(this, QStringLiteral("Intelligente Farbsortierung"),
            QStringLiteral("Die Farbfolge ist bereits optimal sortiert. Weitere Zusammenfassungen würden die physikalischen Schichten verletzen."));
    }
}

// ---------------------------------------------------------------------------
void MainWindow::addBastingBox()
{
    if (m_current.empty()) {
        QMessageBox::information(this, windowTitle(), QStringLiteral("Kein Motiv geladen für Heftrahmen."));
        return;
    }

    BastingDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        const auto bp = dlg.params();
        const auto& hoop = m_view->hoop();
        m_current = BastingGenerator::prependTo(m_current, bp, hoop.widthMm, hoop.heightMm);
        setCurrentSequence(m_current, QStringLiteral("Mit Heftrahmen"));
        statusBar()->showMessage(QStringLiteral("Heftrahmen (Basting Box) erfolgreich vor das Muster gesetzt."), 6000);
    }
}

// ---------------------------------------------------------------------------
void MainWindow::digitizeSfumato()
{
    SfumatoDialog dlg(QImage(), this);
    if (dlg.exec() == QDialog::Accepted) {
        if (dlg.sourceImage().isNull()) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("Bitte zuerst ein Bild in der Vorschau laden."));
            return;
        }
        const auto sp = dlg.params();
        m_current = SfumatoGenerator::generate(dlg.sourceImage(), sp);
        setCurrentSequence(m_current, QStringLiteral("Sfumato Photo-Stitch"));
        statusBar()->showMessage(QStringLiteral("Sfumato Photo-Stitch mit %1 Stichen generiert.").arg(m_current.size()), 6000);
    }
}

// ---------------------------------------------------------------------------
void MainWindow::digitizeCrossStitch()
{
    CrossStitchDialog dlg(QImage(), this);
    if (dlg.exec() == QDialog::Accepted) {
        if (dlg.sourceImage().isNull()) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("Bitte zuerst ein Bild wählen."));
            return;
        }
        const auto cp = dlg.params();
        m_current = CrossStitchGenerator::generate(dlg.sourceImage(), cp);
        setCurrentSequence(m_current, QStringLiteral("Traditioneller Kreuzstich"));
        statusBar()->showMessage(QStringLiteral("Kreuzstichmuster mit %1 Stichen generiert.").arg(m_current.size()), 6000);
    }
}

// ---------------------------------------------------------------------------
void MainWindow::multiHoopSplit()
{
    if (m_current.empty()) {
        QMessageBox::information(this, windowTitle(), QStringLiteral("Kein Motiv geladen zum Teilen."));
        return;
    }

    MultiHoopDialog dlg(m_current.boundingRect(), this);
    if (dlg.exec() == QDialog::Accepted) {
        const auto mp = dlg.params();
        MultiHoopSplitter::Result res = MultiHoopSplitter::split(m_current, mp);
        if (!res.success) {
            QMessageBox::warning(this, QStringLiteral("Mehrfach-Rahmung fehlgeschlagen"), res.summary);
            return;
        }

        const QString baseFn = QFileDialog::getSaveFileName(
            this, QStringLiteral("Basis-Dateiname für Teil 1 & Teil 2 speichern"),
            QStringLiteral("Geteiltes_Motiv.jef"), QStringLiteral("Janome JEF (*.jef);;Tajima DST (*.dst)"));

        if (!baseFn.isEmpty()) {
            QFileInfo fi(baseFn);
            const QString ext = fi.suffix().toLower();
            const QString p1Fn = fi.path() + "/" + fi.completeBaseName() + "_Teil1." + ext;
            const QString p2Fn = fi.path() + "/" + fi.completeBaseName() + "_Teil2." + ext;

            if (ext == "dst") {
                DstCodec::exportToFile(p1Fn, res.hoop1);
                DstCodec::exportToFile(p2Fn, res.hoop2);
            } else {
                JefCodec::exportToFile(p1Fn, res.hoop1, HoopType::HoopB_140x200);
                JefCodec::exportToFile(p2Fn, res.hoop2, HoopType::HoopB_140x200);
            }

            m_current = res.hoop1;
            setCurrentSequence(m_current, QStringLiteral("Rahmen 1 (Teil 1)"));

            QMessageBox::information(
                this, QStringLiteral("Mehrfach-Rahmung erfolgreich"),
                QStringLiteral("%1\n\nDateien gespeichert:\n• %2\n• %3\n\nTeil 1 ist nun zur Ansicht geladen.")
                    .arg(res.summary).arg(p1Fn).arg(p2Fn));
        }
    }
}

// ---------------------------------------------------------------------------
void MainWindow::applyTatamiCarving()
{
    QStringList presets;
    presets << QStringLiteral("🌿 Traditionelles Eichenlaub (OakLeaf)")
            << QStringLiteral("⭐ Fünfzackiger Stern (Star)")
            << QStringLiteral("❤ Klassisches Zierherz (Heart)")
            << QStringLiteral("❖ Rauten-Prägung / Jacquard (DiamondGrid)")
            << QStringLiteral("〰 Fließende Wellenlinien (WaveLines)");

    bool ok = false;
    const QString item = QInputDialog::getItem(
        this, QStringLiteral("Tatami-Prägemuster (Carving Pattern) wählen"),
        QStringLiteral("Ornament wählen, das als Nadelstich-Relief eingeprägt werden soll:"),
        presets, 0, false, &ok);
    if (!ok) return;

    CarvingPattern::Preset chosen = CarvingPattern::Preset::OakLeaf;
    if (item.contains("Stern")) chosen = CarvingPattern::Preset::Star;
    else if (item.contains("Zierherz")) chosen = CarvingPattern::Preset::Heart;
    else if (item.contains("Rauten")) chosen = CarvingPattern::Preset::DiamondGrid;
    else if (item.contains("Wellen")) chosen = CarvingPattern::Preset::WaveLines;

    const double scale = QInputDialog::getDouble(
        this, QStringLiteral("Prägemuster-Größe"),
        QStringLiteral("Größe des Ornaments in mm:"), 30.0, 10.0, 100.0, 1, &ok);
    if (!ok) return;

    const auto paths = m_editor->paths();
    QPolygonF poly;
    for (const auto& p : paths) {
        if (p.count() >= 3) {
            poly = p.toPolygon();
            break;
        }
    }

    if (poly.isEmpty()) {
        const double s = 35.0;
        poly << QPointF(-s, -s + 5) << QPointF(-s + 5, -s)
             << QPointF(s - 5, -s) << QPointF(s, -s + 5)
             << QPointF(s, s - 5) << QPointF(s - 5, s)
             << QPointF(-s + 5, s) << QPointF(-s, s - 5);
    }

    TatamiFill::Params tp;
    tp.fillAngleDeg = m_fillAngleSpin ? m_fillAngleSpin->value() : 45.0;
    tp.rowSpacingMm = m_density ? m_density->value() : 0.40;
    tp.maxStitchMm = m_maxStitch ? m_maxStitch->value() : 4.0;
    tp.underlay = m_underlay ? m_underlay->isChecked() : true;
    tp.carving = chosen;
    tp.carvingScaleMm = scale;

    m_current = TatamiFill::generate(poly, tp);
    m_current.palette = { ThreadCatalog::snap(QColor(34, 139, 34)) };
    setCurrentSequence(m_current, QStringLiteral("Tatami mit %1").arg(CarvingPattern::presetName(chosen)));
    statusBar()->showMessage(
        QStringLiteral("Tatami-Prägemuster (%1) erfolgreich mit %2 Stichen generiert.")
            .arg(CarvingPattern::presetName(chosen)).arg(m_current.size()), 6000);
}

} // namespace stick

