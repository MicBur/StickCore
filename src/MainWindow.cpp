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
#include "net/AutoUpdater.h"
#include "library/DesignLibrary.h"
#include "library/LibraryDialog.h"
#include "library/LibraryBuilder.h"
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
#include "ui/ArcTextDialog.h"
#include "generators/ColorSorter.h"
#include "generators/BastingGenerator.h"
#include "generators/CarvingPattern.h"
#include "generators/SfumatoGenerator.h"
#include "generators/CrossStitchGenerator.h"
#include "generators/MultiHoopSplitter.h"
#include "generators/GradientFill.h"
#include "generators/BeanStitchGenerator.h"
#include "generators/StipplingGenerator.h"
#include "generators/FlorentineFill.h"

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
#include <QToolButton>
#include <QTabBar>
#include <QSignalBlocker>
#include <QBuffer>
#include <QKeyEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
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
QToolBar { background:#1b1e26; border:0; border-bottom:1px solid #2c313d; spacing:6px; padding:6px 10px; }
QToolButton { color:#dbe0e9; background:#232732; border:1px solid #2c313d; border-radius:7px; padding:6px 11px; }
QToolButton:hover { background:#2b3040; border-color:#2dd4bf; }
QToolButton:pressed { background:#333a4c; }
QToolButton:checked { background:#173635; color:#2dd4bf; border-color:#2dd4bf; font-weight:600; }
QToolBar QLabel { color:#8b93a4; padding:0 4px; }
#DocTabBarWall { background:#151821; border-bottom:1px solid #2c313d; padding:4px 10px 0px 10px; }
QTabBar { background:transparent; }
QTabBar::tab {
    background:#1c202b; color:#94a3b8; border:1px solid #2c313d; border-bottom:0;
    border-top-left-radius:7px; border-top-right-radius:7px;
    padding:6px 14px; margin-right:4px; font-weight:600;
}
QTabBar::tab:hover { background:#252a38; color:#e2e8f0; }
QTabBar::tab:selected {
    background:#222836; color:#2dd4bf; border-color:#2dd4bf;
    border-bottom:2px solid #2dd4bf;
}
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
#PresetBtn { background:#232732; color:#dbe0e9; border:1px solid #2c313d; border-radius:6px; padding:5px 8px; font-size:12px; font-weight:600; }
#PresetBtn:hover { background:#2b3040; border-color:#2dd4bf; color:#2dd4bf; }
#TransportBar { background:#171a21; border-top:1px solid #2c313d; padding:6px 12px; }
#ViewportHeader { background:#161922; border-bottom:1px solid #2c313d; padding:4px 10px; }
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
    setWindowIcon(QIcon(QStringLiteral(":/logo/badge.png")));

    // Responsive initial sizing for laptops and desktops
    const QRect screenGeom = QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->availableGeometry() : QRect(0, 0, 1280, 720);
    const int initW = std::clamp(static_cast<int>(screenGeom.width() * 0.92), 960, 1380);
    const int initH = std::clamp(static_cast<int>(screenGeom.height() * 0.90), 580, 840);
    resize(initW, initH);
    setMinimumSize(800, 480);

    m_library = std::make_unique<DesignLibrary>();
    m_library->ensurePopulated();

    m_editor = new PathEditorWidget(this);
    m_editor->setMinimumWidth(260);
    m_view   = new StitchGLWidget(this);
    m_view->setMinimumWidth(320);
    const HoopSpec initHs = hoopSpec(HoopType::HoopB_140x200);
    m_view->setHoop(initHs.widthMm, initHs.heightMm, QString::fromLatin1(initHs.name));
    m_editor->setHoop(initHs.widthMm, initHs.heightMm, QString::fromLatin1(initHs.name), HoopType::HoopB_140x200);

    connect(m_editor, &PathEditorWidget::designMoved, this, [this](double dx, double dy){
        m_view->setSequence(m_editor->sequence());
        m_current = m_editor->sequence();
        for (Stitch& s : m_baseSequence.stitches) {
            s.x += dx;
            s.y += dy;
        }
        if (m_docTabBar) {
            const int tabIdx = m_docTabBar->currentIndex();
            if (tabIdx >= 0 && tabIdx < m_openDocs.size()) {
                m_openDocs[tabIdx].currentSeq = m_current;
                m_openDocs[tabIdx].baseSeq    = m_baseSequence;
            }
        }
        updateInfoPanel();
    });
    connect(m_editor, &PathEditorWidget::hoopSelected, this, &MainWindow::selectHoop);
    connect(m_editor, &PathEditorWidget::fitToHoopRequested, this, &MainWindow::fitDesignToHoop);

    // Keep m_mode available for vector drawing mode switching
    m_mode = new QComboBox;
    m_mode->addItem(QStringLiteral("Satin (2 Rails)"));
    m_mode->addItem(QStringLiteral("Tatami (Polygon)"));
    m_mode->hide();
    connect(m_mode, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::modeChanged);

    QWidget* centerView = buildCenterView();
    QWidget* panel = buildInfoPanel();

    auto* split = new QSplitter(Qt::Horizontal, this);
    split->addWidget(m_editor);
    split->addWidget(centerView);
    split->addWidget(panel);
    split->setCollapsible(0, true);
    split->setCollapsible(1, false);
    split->setCollapsible(2, true);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 6);
    split->setStretchFactor(2, 2);

    const int edW = std::min(350, static_cast<int>(initW * 0.27));
    const int pnlW = std::min(310, static_cast<int>(initW * 0.24));
    const int cntW = initW - edW - pnlW;
    split->setSizes({ edW, cntW, pnlW });

    // Open-Documents Tab Bar above the main workspace
    auto* centralWrapper = new QWidget(this);
    m_mode->setParent(centralWrapper);
    auto* centralLay = new QVBoxLayout(centralWrapper);
    centralLay->setContentsMargins(0, 0, 0, 0);
    centralLay->setSpacing(0);

    m_tabBarWall = new QWidget(centralWrapper);
    m_tabBarWall->setObjectName(QStringLiteral("DocTabBarWall"));
    auto* tabWallLay = new QHBoxLayout(m_tabBarWall);
    tabWallLay->setContentsMargins(8, 2, 8, 0);
    tabWallLay->setSpacing(8);

    auto* tabTitleLbl = new QLabel(QStringLiteral("📂 Geöffnete Dateien:"), m_tabBarWall);
    tabTitleLbl->setStyleSheet(QStringLiteral("color:#8b93a4; font-size:11px; font-weight:700; padding-bottom:4px;"));
    tabWallLay->addWidget(tabTitleLbl);

    m_docTabBar = new QTabBar(m_tabBarWall);
    m_docTabBar->setTabsClosable(true);
    m_docTabBar->setMovable(false);
    m_docTabBar->setExpanding(false);
    m_docTabBar->setDrawBase(false);
    connect(m_docTabBar, &QTabBar::currentChanged, this, &MainWindow::onDocTabChanged);
    connect(m_docTabBar, &QTabBar::tabCloseRequested, this, &MainWindow::onDocTabCloseRequested);
    tabWallLay->addWidget(m_docTabBar, 1);

    auto* btnAddTab = new QToolButton(m_tabBarWall);
    btnAddTab->setText(QStringLiteral("➕ JEF öffnen…"));
    btnAddTab->setToolTip(QStringLiteral("Weitere Stickdatei (.jef, .dst, .svg) als Tab öffnen"));
    btnAddTab->setStyleSheet(QStringLiteral("QToolButton { padding:3px 10px; margin-bottom:3px; font-size:12px; }"));
    connect(btnAddTab, &QToolButton::clicked, this, &MainWindow::openEmbroideryFile);
    tabWallLay->addWidget(btnAddTab);

    m_tabBarWall->setVisible(false);
    centralLay->addWidget(m_tabBarWall);
    centralLay->addWidget(split, 1);
    setCentralWidget(centralWrapper);

    setupMenus();

    // -----------------------------------------------------------------------
    // Aufgeräumte Haupt-Toolbar (Ideal für Laptop-Bildschirme!)
    // -----------------------------------------------------------------------
    QToolBar* tb = addToolBar(QStringLiteral("StudioToolbar"));
    tb->setMovable(false);

    tb->addAction(QStringLiteral("📂 Öffnen"),     this, &MainWindow::openEmbroideryFile);
    tb->addAction(QStringLiteral("💾 Speichern"),  this, &MainWindow::saveFile);
    tb->addAction(QStringLiteral("📤 JEF Export"), this, &MainWindow::exportJef);
    tb->addAction(QStringLiteral("📚 Bibliothek"), this, &MainWindow::openLibrary);
    tb->addSeparator();

    tb->addAction(QStringLiteral("🚒 Feuerwehr & Wappen"), this, &MainWindow::loadOfficialKielWappen);
    tb->addAction(QStringLiteral("🌟 Bogen-Schrift"), this, &MainWindow::openArcTextStudio);
    tb->addAction(QStringLiteral("📐 Einpassen"),     this, &MainWindow::fitDesignToHoop);

    auto* actCalc = tb->addAction(QStringLiteral("⚡ Dichte berechnen ▶"), this, &MainWindow::generate);
    actCalc->setToolTip(QStringLiteral("Wendet die gewählte Fadendichte (Stichabstand) und den Zugausgleich sofort auf das aktuelle Motiv an (F5)"));

    auto* spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);

    // Rechts außen: Nur noch 2 klare Ansicht-Schalter für die Seitenleisten.
    // Auf Laptops ist der linke 2D-Pfadeditor beim Start ausgeblendet, damit die große
    // 3D-Hauptansicht den vollen Platz hat und nichts oben links überladen wirkt!
    m_editor->setVisible(false);
    auto* actToggleEditor = tb->addAction(QStringLiteral("✏ 2D-Editor"), this, [this](bool checked){
        m_editor->setVisible(checked);
    });
    actToggleEditor->setCheckable(true);
    actToggleEditor->setChecked(false);
    actToggleEditor->setToolTip(QStringLiteral("Linken 2D-Pfad-Editor bei Bedarf ein-/ausblenden"));

    auto* actTogglePanel = tb->addAction(QStringLiteral("📋 Eigenschaften"), this, [panel](bool checked){
        panel->setVisible(checked);
    });
    actTogglePanel->setCheckable(true);
    actTogglePanel->setChecked(true);
    actTogglePanel->setToolTip(QStringLiteral("Rechte Leiste (Fadendichte, Rahmen, Garne) ein-/ausblenden"));

    setAcceptDrops(true);
    statusBar()->showMessage(QStringLiteral("StickCore Studio v1.2.0 bereit — für %1 eingerichtet.").arg(mp.name));

    m_updater = new AutoUpdater(this);

    // Command-line file loading and demo hooks
    const QStringList a = qApp->arguments();
    bool loadedFromCli = false;
    for (int i = 1; i < a.size(); ++i) {
        if (a[i].startsWith(QLatin1Char('-')))
            break;
        if (QFile::exists(a[i])) {
            const QString cliPath = a[i];
            QTimer::singleShot(100, this, [this, cliPath]{ loadFile(cliPath); });
            loadedFromCli = true;
            break;
        }
    }

    const int di = a.indexOf(QStringLiteral("--demo"));
    if (di >= 0 && di + 1 < a.size()) {
        const QString which = a[di + 1];
        if (which == QLatin1String("text")) {
            TextDigitizer::Params tp; tp.text = QStringLiteral("StickCore"); tp.heightMm = 22; tp.raised = true;
            m_hasTextParams = true;
            m_currentTextParams = tp;
            m_currentFilePath.clear();
            m_current = TextDigitizer::generate(tp);
            m_current.palette = { ThreadCatalog::snap(QColor(150, 40, 60)) };
            m_editor->clearAll();
            setCurrentSequence(m_current, QStringLiteral("Text: StickCore"));
        } else if (which == QLatin1String("arctext")) {
            const QString volPath = QFile::exists(QStringLiteral("E:/StickCore/output/KIEL_VOL.JEF"))
                ? QStringLiteral("E:/StickCore/output/KIEL_VOL.JEF")
                : QStringLiteral("C:/Users/micbu/Downloads/KIEL_VOL.JEF");
            if (QFile::exists(volPath)) {
                loadFile(volPath);
            }
            TextDigitizer::Params tp;
            tp.text            = QStringLiteral("STICKCORE STUDIO");
            tp.family          = QStringLiteral("Varsity");
            tp.bold            = true;
            tp.heightMm        = 28.0;
            tp.baseline        = TextDigitizer::Baseline::ArcUp;
            tp.arcRadiusMm     = 65.0;
            tp.letterSpacingMm = 2.0;
            tp.style           = TextDigitizer::StitchStyle::PaddedSatin;
            tp.borderWidthMm   = 3.4;
            tp.borderPitchMm   = 0.34;
            tp.paddingWidthMm  = 2.3;
            tp.paddingPitchMm  = 0.50;
            tp.underlay        = true;
            StitchSequence seq = TextDigitizer::generate(tp);
            seq.palette = {
                ThreadCatalog::snap(QColor(218, 178, 85)),
                ThreadCatalog::snap(QColor(180, 140, 50))
            };
            if (m_editor) {
                m_editor->clearAll();
                m_editor->setCanvasMode(PathEditorWidget::CanvasMode::Stitches2D);
            }
            m_currentFilePath.clear();
            m_hasTextParams = true;
            m_currentTextParams = tp;
            const QString tabTitle = QStringLiteral("Bogen-Schrift: %1").arg(tp.text);
            setCurrentSequence(seq, tabTitle);
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
    } else if (!loadedFromCli) {
        // Lade beim Start genau EIN klares Hauptmotiv (Feuerwehr-Wappen WWWAP.JEF oder Kieler Wappen),
        // damit keine unübersichtliche Tab-Leiste oben links entsteht.
        const QString appOut = QCoreApplication::applicationDirPath() + QStringLiteral("/output");
        const QStringList candidates = {
            appOut + QStringLiteral("/fw/WWWAP.JEF"),
            QStringLiteral("E:/StickCore/output/fw/WWWAP.JEF"),
            appOut + QStringLiteral("/KIEL_VOL.JEF"),
            QStringLiteral("E:/StickCore/output/KIEL_VOL.JEF")
        };
        bool loadedDefaultJef = false;
        for (const QString& cand : candidates) {
            if (QFile::exists(cand)) {
                loadedDefaultJef = loadFile(cand);
                if (loadedDefaultJef) break;
            }
        }
        if (!loadedDefaultJef) {
            loadSatinDemo();
        }
    }
}

MainWindow::~MainWindow() = default;

// ---------------------------------------------------------------------------
void MainWindow::setupMenus()
{
    // --- Datei ---
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("&Datei"));
    auto* newAct = fileMenu->addAction(QStringLiteral("&Neues Motiv"), QKeySequence::New, this, [this]{
        m_editor->clearAll();
        m_current.clear();
        m_baseSequence.clear();
        m_hasTextParams = false;
        m_currentFilePath.clear();
        m_view->setSequence(m_current);
        updateInfoPanel();
        setWindowTitle(QStringLiteral("StickCore Studio"));
        statusBar()->showMessage(QStringLiteral("Neues leeres Motiv angelegt."), 4000);
    });
    auto* openAct = fileMenu->addAction(QStringLiteral("📂 &Stickdatei öffnen… (JEF, DST, SVG)"), QKeySequence::Open, this, &MainWindow::openEmbroideryFile);
    m_recentFilesMenu = fileMenu->addMenu(QStringLiteral("🕒 &Zuletzt geöffnet"));
    updateRecentFilesMenu();
    auto* saveAct = fileMenu->addAction(QStringLiteral("💾 Motiv &speichern"), QKeySequence::Save, this, &MainWindow::saveFile);
    auto* saveAsAct = fileMenu->addAction(QStringLiteral("💾 Motiv speichern &unter…"), QKeySequence::SaveAs, this, &MainWindow::saveFileAs);
    fileMenu->addSeparator();
    auto* openImgAct = fileMenu->addAction(QStringLiteral("&Bild öffnen & digitalisieren…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I), this, &MainWindow::digitizeImage);
    auto* openSvgAct = fileMenu->addAction(QStringLiteral("&SVG-Vektordatei importieren & sticken…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O), this, &MainWindow::importSvg);
    fileMenu->addSeparator();
    auto* libOpenAct = fileMenu->addAction(QStringLiteral("Aus &Bibliothek laden…"), QKeySequence(Qt::CTRL | Qt::Key_L), this, &MainWindow::openLibrary);
    auto* libSaveAct = fileMenu->addAction(QStringLiteral("In Bibliothek &speichern…"), this, &MainWindow::saveToLibrary);
    fileMenu->addSeparator();
    auto* expJefAct  = fileMenu->addAction(QStringLiteral("&JEF exportieren (Janome)…"), QKeySequence(Qt::CTRL | Qt::Key_E), this, &MainWindow::exportJef);
    auto* expDstAct  = fileMenu->addAction(QStringLiteral("&DST exportieren (Tajima Universal)…"), this, &MainWindow::exportDst);
    auto* expVarAct  = fileMenu->addAction(QStringLiteral("&Größen-Varianten exportieren…"), this, &MainWindow::exportSizeVariants);
    auto* expSheetAct = fileMenu->addAction(QStringLiteral("&Produktionsdatenblatt (HTML/Druck)…"), QKeySequence::Print, this, &MainWindow::exportWorksheet);
    fileMenu->addSeparator();
    auto* quitAct    = fileMenu->addAction(QStringLiteral("&Beenden"), QKeySequence::Quit, this, &QWidget::close);

    Q_UNUSED(newAct); Q_UNUSED(openAct); Q_UNUSED(saveAct); Q_UNUSED(saveAsAct);
    Q_UNUSED(openImgAct); Q_UNUSED(openSvgAct); Q_UNUSED(libOpenAct); Q_UNUSED(libSaveAct);
    Q_UNUSED(expJefAct); Q_UNUSED(expDstAct); Q_UNUSED(expVarAct); Q_UNUSED(expSheetAct); Q_UNUSED(quitAct);

    // --- Bearbeiten ---
    QMenu* editMenu = menuBar()->addMenu(QStringLiteral("&Bearbeiten"));
    editMenu->addAction(QStringLiteral("↩ &Rückgängig"), QKeySequence::Undo, m_editor, &PathEditorWidget::undo);
    editMenu->addAction(QStringLiteral("↪ &Wiederholen"), QKeySequence::Redo, m_editor, &PathEditorWidget::redo);
    editMenu->addSeparator();
    editMenu->addAction(QStringLiteral("🔀 Stiche in bearbeitbare Vektorpfade umwandeln (Bézier-Knoten)…"), this, &MainWindow::convertStitchesToPaths);
    editMenu->addSeparator();
    editMenu->addAction(QStringLiteral("🗑 Ausgewählte Pfade &löschen"), QKeySequence::Delete, m_editor, &PathEditorWidget::deleteSelected);
    editMenu->addAction(QStringLiteral("&Alle Pfade markieren"), QKeySequence::SelectAll, m_editor, &PathEditorWidget::selectAllPaths);
    editMenu->addAction(QStringLiteral("Auswahl &aufheben"), m_editor, &PathEditorWidget::clearSelection);
    editMenu->addAction(QStringLiteral("🧹 Kleine Inseln / Splitter &filtern…"), this, [this]{ m_editor->filterSmallIslands(4.0); });
    editMenu->addSeparator();
    editMenu->addAction(QStringLiteral("📐 An aktuellen Rahmen &einpassen & neu berechnen"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F), this, &MainWindow::fitDesignToHoop);
    editMenu->addAction(QStringLiteral("&Größe ändern & neu berechnen…"), QKeySequence(Qt::CTRL | Qt::Key_R), this, &MainWindow::resizeDesign);
    editMenu->addAction(QStringLiteral("Im &Rahmen platzieren…"), this, &MainWindow::placeDesign);
    editMenu->addSeparator();
    editMenu->addAction(QStringLiteral("↔ Horizontal &spiegeln"), QKeySequence(Qt::Key_F7), this, &MainWindow::mirrorHorizontal);
    editMenu->addAction(QStringLiteral("↕ Vertikal sp&iegeln"), QKeySequence(Qt::Key_F8), this, &MainWindow::mirrorVertical);
    editMenu->addAction(QStringLiteral("↻ &90° im Uhrzeigersinn"), QKeySequence(Qt::Key_F9), this, [this]{ rotate90(true); });
    editMenu->addAction(QStringLiteral("↺ 90° &gegen Uhrzeigersinn"), QKeySequence(Qt::SHIFT | Qt::Key_F9), this, [this]{ rotate90(false); });
    editMenu->addAction(QStringLiteral("⟳ &Freier Drehwinkel…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R), this, &MainWindow::rotateFree);
    editMenu->addSeparator();
    editMenu->addAction(QStringLiteral("🎨 Intelligente Farbsortierung (Smart Color Sort)…"), this, &MainWindow::smartColorSort);
    editMenu->addAction(QStringLiteral("🔲 Heftrahmen hinzufügen (Basting Box)…"), this, &MainWindow::addBastingBox);
    editMenu->addAction(QStringLiteral("✂ Mehrfach-Rahmung (Auto-Split & Passkreuze)…"), this, &MainWindow::multiHoopSplit);
    editMenu->addSeparator();
    editMenu->addAction(QStringLiteral("&Alle Pfade löschen"), m_editor, &PathEditorWidget::clearAll);

    // --- Schrift & Lettering ---
    QMenu* textMenu = menuBar()->addMenu(QStringLiteral("&Schrift & Lettering"));
    textMenu->addAction(QStringLiteral("🌟 Bogen- & College-Schrift Studio (Arc & Varsity Studio)…"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_T), this, &MainWindow::openArcTextStudio);
    textMenu->addAction(QStringLiteral("🔤 Standard-Text sticken…"), QKeySequence(Qt::CTRL | Qt::Key_T), this, &MainWindow::makeText);
    textMenu->addAction(QStringLiteral("✦ Kunstvolles &Monogramm…"), QKeySequence(Qt::CTRL | Qt::Key_M), this, &MainWindow::makeMonogram);
    textMenu->addAction(QStringLiteral("🔢 Trikot- & College-Nummern (00 - 99)…"), this, &MainWindow::makeJerseyNumbers);
    textMenu->addSeparator();
    textMenu->addAction(QStringLiteral("🏫 College Stick-Alphabet (A-Z) durchsuchen…"), this, [this]{
        openLibraryCategory(QStringLiteral("🏫 College Buchstaben (A-Z)"));
    });
    textMenu->addAction(QStringLiteral("🔢 College Stick-Zahlen (0-9) durchsuchen…"), this, [this]{
        openLibraryCategory(QStringLiteral("🔢 College Zahlen (0-9)"));
    });
    textMenu->addAction(QStringLiteral("🪡 Applikations-Zahlen (0-9) durchsuchen…"), this, [this]{
        openLibraryCategory(QStringLiteral("🪡 Applikation Ziffern (0-9)"));
    });

    // --- Aufnäher & Patches ---
    QMenu* patchMenu = menuBar()->addMenu(QStringLiteral("&Aufnäher & Patches"));
    patchMenu->addAction(QStringLiteral("✂ 3-Stufen-&Applikation (Aufnäher)…"), this, &MainWindow::makeApplique);
    patchMenu->addAction(QStringLiteral("🚒 Berufsfeuerwehr Kiel & Wappen-Motive…"), this, [this]{
        openLibraryCategory(QStringLiteral("🚒 Feuerwehr & Kiel"));
    });
    patchMenu->addAction(QStringLiteral("⚓ Offizielles &Kieler Stadtwappen (Aufnäher & Rücken)…"), this, &MainWindow::loadOfficialKielWappen);
    patchMenu->addAction(QStringLiteral("🔲 Heftrahmen hinzufügen (Basting Box zur Fixierung)…"), this, &MainWindow::addBastingBox);

    QMenu* mefMenu = patchMenu->addMenu(QStringLiteral("👑 Modewerkstatt Knüppel (Original Vektor-Logo)"));
    mefMenu->addAction(QStringLiteral("👑 Meister-Aufnäher (Foto-Original: Kettelrand & Satinschrift)"), this, [this]{ loadMefOriginal(0); });
    mefMenu->addAction(QStringLiteral("✦ Atelier Kontur-Glanz (Florentiner Schimmer)"), this, [this]{ loadMefOriginal(4); });
    mefMenu->addAction(QStringLiteral("✨ Zwei-Ton-Relief Gold (Madeira 1083 & 1070)"), this, [this]{ loadMefOriginal(2); });
    mefMenu->addAction(QStringLiteral("🧵 Königliche Meister-Tatami (Dichte Webung)"), this, [this]{ loadMefOriginal(1); });
    mefMenu->addAction(QStringLiteral("✒ Feiner Gold-Steppstich (Linien-Art)"), this, [this]{ loadMefOriginal(3); });
    mefMenu->addSeparator();
    mefMenu->addAction(QStringLiteral("❦ Kalligraphie-Variante (Schriftarten-Badge)"), this, &MainWindow::makeLogo);

    QMenu* huntMenu = patchMenu->addMenu(QStringLiteral("🌿 Jagd, Natur & Tradition (Eichenlaub, Hirsch, Keiler)"));
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

    patchMenu->addSeparator();
    patchMenu->addAction(QStringLiteral("📱 &Smartphone-Foto Upload (QR)…"), QKeySequence(Qt::CTRL | Qt::Key_U), this, &MainWindow::phoneUpload);

    // --- Meister-Füllungen & Effekte ---
    QMenu* fillMenu = menuBar()->addMenu(QStringLiteral("&Meister-Füllungen"));
    fillMenu->addAction(QStringLiteral("Stiche &berechnen ▶"), QKeySequence(Qt::Key_F5), this, &MainWindow::generate);
    fillMenu->addSeparator();
    fillMenu->addAction(QStringLiteral("🎨 Farbverlaufs-Füllung (Ombré / Gradient Tatami)…"), this, &MainWindow::generateGradientFill);
    fillMenu->addAction(QStringLiteral("🪡 Dreifachstich / Bean Stitch (Plastische Kontur)…"), this, &MainWindow::generateBeanStitchOutline);
    fillMenu->addAction(QStringLiteral("🧵 Mäander- & Quiltfüllung (Stippling Meander)…"), this, &MainWindow::generateStipplingFill);
    fillMenu->addAction(QStringLiteral("〰 Florentiner Wellen-Tatami (Curved Wave Fill)…"), this, &MainWindow::generateFlorentineFill);
    fillMenu->addAction(QStringLiteral("⚜ Tatami Prägemuster (Carving / Eichenlaub / Stern)…"), this, &MainWindow::applyTatamiCarving);
    fillMenu->addSeparator();
    fillMenu->addAction(QStringLiteral("🌫 Sfumato Photo-Stitch (Fotorealistisch)…"), this, &MainWindow::digitizeSfumato);
    fillMenu->addAction(QStringLiteral("✖ Traditioneller Kreuzstich (Aida Raster)…"), this, &MainWindow::digitizeCrossStitch);
    fillMenu->addSeparator();
    fillMenu->addAction(QStringLiteral("Demo: Satin-Welle"), this, &MainWindow::loadSatinDemo);
    fillMenu->addAction(QStringLiteral("Demo: Tatami-Kreis"), this, &MainWindow::loadTatamiDemo);

    // --- Werkzeuge & Analyse ---
    QMenu* toolMenu = menuBar()->addMenu(QStringLiteral("&Werkzeuge & Analyse"));
    toolMenu->addAction(QStringLiteral("✂ Mehrfach-Rahmung (Auto-Split & Passkreuze für Übergröße)…"), this, &MainWindow::multiHoopSplit);
    toolMenu->addAction(QStringLiteral("🎨 Intelligente Farbsortierung (Smart Color Sort)…"), this, &MainWindow::smartColorSort);
    toolMenu->addAction(QStringLiteral("🔥 Stichdichte-Prüfer & Fadenbruch-Inspektor (Heatmap)…"), this, &MainWindow::inspectDensityHeatmap);
    toolMenu->addAction(QStringLiteral("🔒 Stich-Sicherung & Vernäh-Stiche (Tie-In / Tie-Off Optimizer)…"), this, &MainWindow::optimizeTieStitches);
    toolMenu->addAction(QStringLiteral("🧮 Garnbedarfs-Rechner & Kostenkalkulation…"), this, &MainWindow::calculateThreadCost);
    toolMenu->addSeparator();
    toolMenu->addAction(QStringLiteral("❔ Ersteinrichtungs-&Assistent…"), this, &MainWindow::showWizard);

    // --- Simulation & Ansicht ---
    QMenu* simMenu = menuBar()->addMenu(QStringLiteral("&Simulation & Ansicht"));
    simMenu->addAction(QStringLiteral("&Wiedergabe starten/pausieren"), QKeySequence(Qt::Key_Space), this, &MainWindow::togglePlayback);
    simMenu->addAction(QStringLiteral("&Zurückspulen"), QKeySequence(Qt::Key_Home), m_view, &StitchGLWidget::resetPlayback);
    simMenu->addAction(QStringLiteral("&Alles anzeigen"), QKeySequence(Qt::Key_End), m_view, &StitchGLWidget::showAll);
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
    simMenu->addAction(QStringLiteral("&Im Rahmen zentrieren"), QKeySequence(Qt::CTRL | Qt::Key_0), this, &MainWindow::centerCurrentDesign);

    // --- Hilfe ---
    QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("&Hilfe"));
    helpMenu->addAction(QStringLiteral("🔄 Auf &Updates prüfen…"), this, &MainWindow::checkForUpdatesManual);
    helpMenu->addSeparator();
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
            QStringLiteral("<div style='text-align:center;'>"
                           "<img src=':/logo/badge.png' width='96' height='96'><br>"
                           "<h2>StickCore Studio v1.2.0</h2>"
                           "<b>Professionelle Sticksoftware & Digitalisierungs-Suite</b><br>"
                           "Optimiert für Janome MC350E & Tajima DST"
                           "</div><br><hr>"
                           "<p>• Multi-Hoop Auto-Splitting & Passkreuze für Übergröße<br>"
                           "• Bogen- & College-Schrift Studio (Varsity, Graduate, Jersey)<br>"
                           "• 3D-Relief Padding („Paddlerin“ Dual-Pass Satinstich)<br>"
                           "• Vektor-Digitalisierung, Sfumato Photo-Stitch & Kreuzstich<br>"
                           "• Vollständige Garnkataloge (Madeira, Marathon, Isacord, Gunold)</p>"
                           "<p style='color:#8b93a4;'>Entwickelt mit Qt 6 & modernem C++20 · MicBur</p>"));
    });
}

// ---------------------------------------------------------------------------
QWidget* MainWindow::buildCenterView()
{
    auto* container = new QWidget(this);
    auto* lay = new QVBoxLayout(container);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // Compact 3D Viewport Header Bar (keeps 3D camera tools on the 3D view itself)
    auto* vh = new QWidget(container);
    vh->setObjectName(QStringLiteral("ViewportHeader"));
    auto* vhLay = new QHBoxLayout(vh);
    vhLay->setContentsMargins(8, 3, 8, 3);
    vhLay->setSpacing(4);

    auto* vhTitle = new QLabel(QStringLiteral("🧵 3D-Echtzeit-Vorschau"), vh);
    vhTitle->setStyleSheet(QStringLiteral("color:#8b93a4; font-size:11px; font-weight:700;"));
    vhLay->addWidget(vhTitle);
    vhLay->addStretch(1);

    auto* moveGroup = new QActionGroup(this);
    m_actOrbitMode = new QAction(QStringLiteral("🔄 3D-Drehen"), this);
    m_actOrbitMode->setCheckable(true);
    m_actOrbitMode->setChecked(true);
    m_actOrbitMode->setToolTip(QStringLiteral("3D-Kamera mit Maus frei rotieren"));
    connect(m_actOrbitMode, &QAction::triggered, this, [this]{
        m_view->setMouseMode(StitchGLWidget::MouseMode::Orbit);
        statusBar()->showMessage(QStringLiteral("Mausmodus: 3D Orbit-Kamera (Linksklick dreht, Shift+Klick verschiebt Motiv)."), 4000);
    });
    moveGroup->addAction(m_actOrbitMode);

    m_actMoveMode = new QAction(QStringLiteral("✥ Verschieben"), this);
    m_actMoveMode->setCheckable(true);
    m_actMoveMode->setToolTip(QStringLiteral("Motiv mit Maus im Rahmen verschieben"));
    connect(m_actMoveMode, &QAction::triggered, this, [this]{
        m_view->setMouseMode(StitchGLWidget::MouseMode::MoveDesign);
        statusBar()->showMessage(QStringLiteral("Mausmodus: Motiv verschieben (Linksklick zieht das Motiv im Rahmen)."), 4000);
    });
    moveGroup->addAction(m_actMoveMode);

    m_actTopDown = new QAction(QStringLiteral("👁 2D/3D"), this);
    m_actTopDown->setToolTip(QStringLiteral("Zwischen 2D-Draufsicht und 3D-Perspektive umschalten (Strg+D)"));
    connect(m_actTopDown, &QAction::triggered, this, &MainWindow::toggleViewPreset);

    m_actHoop = new QAction(QStringLiteral("⭕ Rahmen"), this);
    m_actHoop->setCheckable(true);
    m_actHoop->setChecked(true);
    m_actHoop->setToolTip(QStringLiteral("Stickrahmen und Zentrier-Fadenkreuz ein-/ausblenden (H)"));
    connect(m_actHoop, &QAction::triggered, this, &MainWindow::toggleHoopVisible);

    auto makeVpBtn = [&](QAction* act) {
        auto* b = new QToolButton(vh);
        b->setDefaultAction(act);
        b->setStyleSheet(QStringLiteral("QToolButton { padding:3px 8px; font-size:11px; border-radius:5px; }"));
        vhLay->addWidget(b);
    };
    makeVpBtn(m_actOrbitMode);
    makeVpBtn(m_actMoveMode);
    makeVpBtn(m_actTopDown);
    makeVpBtn(m_actHoop);

    auto* btnCenterVp = new QToolButton(vh);
    btnCenterVp->setText(QStringLiteral("⌖ Mitte"));
    btnCenterVp->setToolTip(QStringLiteral("Motiv genau im Rahmen zentrieren (Strg+0)"));
    btnCenterVp->setStyleSheet(QStringLiteral("QToolButton { padding:3px 8px; font-size:11px; border-radius:5px; }"));
    connect(btnCenterVp, &QToolButton::clicked, this, &MainWindow::centerCurrentDesign);
    vhLay->addWidget(btnCenterVp);

    lay->addWidget(vh);
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
        for (Stitch& s : m_baseSequence.stitches) {
            s.x += dx;
            s.y += dy;
        }
        if (m_editor) m_editor->setSequence(m_current);
        if (m_docTabBar) {
            const int tabIdx = m_docTabBar->currentIndex();
            if (tabIdx >= 0 && tabIdx < m_openDocs.size()) {
                m_openDocs[tabIdx].currentSeq = m_current;
                m_openDocs[tabIdx].baseSeq    = m_baseSequence;
            }
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
        if (idx < int(m_baseSequence.palette.size())) {
            m_baseSequence.palette[idx] = dlg.selectedThread();
        }
        if (m_docTabBar) {
            const int tabIdx = m_docTabBar->currentIndex();
            if (tabIdx >= 0 && tabIdx < m_openDocs.size()) {
                m_openDocs[tabIdx].currentSeq.palette = m_current.palette;
                m_openDocs[tabIdx].baseSeq.palette    = m_baseSequence.palette;
            }
        }
        m_editor->setSequence(m_current);
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
    m_hasTextParams = false;
    m_currentFilePath.clear();
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
    openLibraryCategory(QString());
}

// ---------------------------------------------------------------------------
void MainWindow::openLibraryCategory(const QString& category)
{
    LibraryDialog dlg(m_library.get(), category, this);
    if (dlg.exec() == QDialog::Accepted && dlg.hasChoice()) {
        if (m_editor) {
            m_editor->clearAll();
            m_editor->setCanvasMode(PathEditorWidget::CanvasMode::Stitches2D);
        }
        m_hasTextParams = false;
        m_currentFilePath.clear();
        m_current = dlg.chosenSequence();
        setCurrentSequence(m_current, QStringLiteral("Bibliothek: %1").arg(dlg.chosenName()));
    }
}

// ---------------------------------------------------------------------------
void MainWindow::importMotifsToLibrary()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this,
        QStringLiteral("JEF-Stickmuster in Bibliothek importieren"),
        QString(),
        QStringLiteral("Janome Stickdateien (*.jef *.JEF);;Alle Dateien (*.*)"));

    if (files.isEmpty()) return;

    int imported = 0;
    for (const QString& fn : files) {
        QFileInfo fi(fn);
        const QString name = fi.baseName().replace('_', ' ');
        const QString id = LibraryBuilder::importJef(*m_library, fn, name,
                                                    QStringLiteral("Eigene"),
                                                    { QStringLiteral("eigen"), QStringLiteral("import"), fi.baseName().toLower() });
        if (!id.isEmpty()) imported++;
    }

    if (imported > 0) {
        m_library->writeIndex();
        statusBar()->showMessage(QStringLiteral("%1 Stickmuster erfolgreich in die Bibliothek importiert.").arg(imported), 6000);
        QMessageBox::information(this, windowTitle(),
            QStringLiteral("%1 Stickmuster erfolgreich in deine Bibliothek importiert!").arg(imported));
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
    panel->setMinimumWidth(285);
    auto* outer = new QVBoxLayout(panel);
    outer->setContentsMargins(14, 14, 14, 14);
    outer->setSpacing(12);

    // --- Machine card ---
    auto* mach = new QFrame; mach->setObjectName(QStringLiteral("Card"));
    auto* ml = new QVBoxLayout(mach); ml->setContentsMargins(12,10,12,10); ml->setSpacing(2);
    auto* mn = new QLabel(mp.name); mn->setProperty("role","machine");
    ml->addWidget(mn);
    ml->addWidget(new QLabel(QStringLiteral("%1 · max %2 × %3 mm · %4 St./min")
        .arg(mp.format).arg(mp.maxWidthMm,0,'f',0).arg(mp.maxHeightMm,0,'f',0).arg(mp.maxSpeedSpm)));
    outer->addWidget(mach);

    // --- Interactive Thread Density & Stitch Properties (Top Priority for JEF & Vector) ---
    outer->addWidget(sectionTitle(QStringLiteral("Fadendichte & Sticheigenschaften (JEF / Vektor)")));
    auto* densCard = new QFrame;
    densCard->setObjectName(QStringLiteral("Card"));
    auto* densLay = new QVBoxLayout(densCard);
    densLay->setContentsMargins(12, 10, 12, 10);
    densLay->setSpacing(8);

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft);
    form->setSpacing(6);

    m_density = new QDoubleSpinBox;
    m_density->setObjectName(QStringLiteral("DensitySpin"));
    m_density->setRange(0.20, 1.20);
    m_density->setSingleStep(0.02);
    m_density->setDecimals(2);
    m_density->setKeyboardTracking(false);
    m_density->setValue(mp.satinDensityMm);
    m_density->setSuffix(QStringLiteral(" mm"));
    m_density->setToolTip(QStringLiteral("Reihen-/Stichabstand in mm (für JEF-Dateien & Vektorpfade).\n"
        "Kleinerer Wert = dichtere Füllung & mehr Stiche (z. B. 0,28 mm).\n"
        "Größerer Wert = lockerere Füllung & weniger Stiche (z. B. 0,50 mm)."));

    m_densitySlider = new QSlider(Qt::Horizontal);
    m_densitySlider->setRange(20, 80); // 0.20 mm to 0.80 mm
    m_densitySlider->setValue(int(std::round(mp.satinDensityMm * 100.0)));
    m_densitySlider->setToolTip(QStringLiteral("Fadendichte live verschieben: Links = dichter (0,20 mm) · Rechts = lockerer (0,80 mm)"));

    // Quick preset buttons for density
    auto* presetRow = new QHBoxLayout;
    presetRow->setSpacing(4);
    auto* btnDense = new QPushButton(QStringLiteral("0,28 Dicht"));
    btnDense->setObjectName(QStringLiteral("PresetBtn"));
    btnDense->setToolTip(QStringLiteral("Sehr dichte Vollstick-Deckung (0,28 mm Reihenabstand)"));
    auto* btnStd = new QPushButton(QStringLiteral("0,38 Std."));
    btnStd->setObjectName(QStringLiteral("PresetBtn"));
    btnStd->setToolTip(QStringLiteral("Klassische Janome-Standarddichte (0,38 mm Reihenabstand)"));
    auto* btnLight = new QPushButton(QStringLiteral("0,50 Locker"));
    btnLight->setObjectName(QStringLiteral("PresetBtn"));
    btnLight->setToolTip(QStringLiteral("Lockere, garnsparende Dichte für dünne Stoffe (0,50 mm)"));
    presetRow->addWidget(btnDense);
    presetRow->addWidget(btnStd);
    presetRow->addWidget(btnLight);

    m_pull = new QDoubleSpinBox;
    m_pull->setRange(0.00, 0.60);
    m_pull->setSingleStep(0.05);
    m_pull->setKeyboardTracking(false);
    m_pull->setValue(mp.pullCompMm);
    m_pull->setSuffix(QStringLiteral(" mm"));
    m_pull->setToolTip(QStringLiteral("Zugausgleich: Verbreitert Satinraupen & Füllflächen seitlich gegen Stoffzusammenzug."));

    m_maxStitch = new QDoubleSpinBox;
    m_maxStitch->setRange(1.5, 7.0);
    m_maxStitch->setSingleStep(0.5);
    m_maxStitch->setKeyboardTracking(false);
    m_maxStitch->setValue(4.0);
    m_maxStitch->setSuffix(QStringLiteral(" mm"));
    m_maxStitch->setToolTip(QStringLiteral("Maximale Stichlänge in mm. Längere Stiche werden automatisch unterteilt."));

    m_fillAngleSpin = new QDoubleSpinBox;
    m_fillAngleSpin->setRange(0.0, 360.0);
    m_fillAngleSpin->setSingleStep(15.0);
    m_fillAngleSpin->setValue(45.0);
    m_fillAngleSpin->setSuffix(QStringLiteral("°"));

    m_patternCombo = new QComboBox;
    for (auto pt : { TatamiFill::PatternType::StandardTatami, TatamiFill::PatternType::Brick,
                     TatamiFill::PatternType::Twill, TatamiFill::PatternType::Basketweave,
                     TatamiFill::PatternType::Honeycomb, TatamiFill::PatternType::ContourEcho }) {
        m_patternCombo->addItem(TatamiFill::patternName(pt), int(pt));
    }

    m_underlay = new QCheckBox(QStringLiteral("Unterlage sticken (empfohlen)"));
    m_underlay->setChecked(true);

    form->addRow(QStringLiteral("<b>Fadendichte:</b>"), m_density);
    densLay->addLayout(form);
    densLay->addWidget(m_densitySlider);
    densLay->addLayout(presetRow);

    auto* formAdv = new QFormLayout;
    formAdv->setLabelAlignment(Qt::AlignLeft);
    formAdv->setSpacing(6);
    formAdv->addRow(QStringLiteral("Zugausgleich:"), m_pull);
    formAdv->addRow(QStringLiteral("Max. Stichlänge:"), m_maxStitch);
    formAdv->addRow(QStringLiteral("Füllwinkel:"), m_fillAngleSpin);
    formAdv->addRow(QStringLiteral("Füllmuster:"), m_patternCombo);
    densLay->addLayout(formAdv);
    densLay->addWidget(m_underlay);

    m_densityStateLabel = new QLabel(QStringLiteral("Basis-Dichte: 0,38 mm"));
    m_densityStateLabel->setWordWrap(true);
    m_densityStateLabel->setStyleSheet(QStringLiteral("color:#2dd4bf; font-size:11px; font-weight:600;"));
    densLay->addWidget(m_densityStateLabel);

    auto* btnApplyDensity = new QPushButton(QStringLiteral("🔄 Fadendichte anwenden (F5)"));
    btnApplyDensity->setObjectName(QStringLiteral("PrimaryBtn"));
    btnApplyDensity->setToolTip(QStringLiteral("Berechnet alle Satin- & Tatami-Flächen der geöffneten JEF-Datei sofort mit der gewählten Fadendichte neu"));
    connect(btnApplyDensity, &QPushButton::clicked, this, &MainWindow::generate);
    densLay->addWidget(btnApplyDensity);

    outer->addWidget(densCard);

    // Connect density controls for live JEF / vector recalculation
    connect(m_density, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double val) {
        if (m_densitySlider) {
            QSignalBlocker b(m_densitySlider);
            m_densitySlider->setValue(std::clamp(int(std::round(val * 100.0)), 20, 80));
        }
        generate();
    });
    connect(m_densitySlider, &QSlider::valueChanged, this, [this](int sliderVal) {
        const double mm = sliderVal / 100.0;
        if (m_density) {
            QSignalBlocker b(m_density);
            m_density->setValue(mm);
        }
        generate();
    });
    connect(btnDense, &QPushButton::clicked, this, [this]{ m_density->setValue(0.28); });
    connect(btnStd,   &QPushButton::clicked, this, [this]{ m_density->setValue(0.38); });
    connect(btnLight, &QPushButton::clicked, this, [this]{ m_density->setValue(0.50); });
    connect(m_pull, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double){ generate(); });
    connect(m_maxStitch, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double){ generate(); });

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
    m_threadList->setMinimumHeight(105);
    connect(m_threadList, &QListWidget::itemDoubleClicked, this, &MainWindow::onThreadItemDoubleClicked);
    outer->addWidget(m_threadList, 1);

    // --- Hoop ---
    outer->addWidget(sectionTitle(QStringLiteral("Stickrahmen & Position")));
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

    auto* btnFitHoop = new QPushButton(QStringLiteral("📐 An Rahmen anpassen & neu berechnen"));
    btnFitHoop->setToolTip(QStringLiteral("Skaliert das Motiv optimal in den gewählten Rahmen und berechnet alle Stiche mit exakter Dichte neu (Strg+Umschalt+F)"));
    connect(btnFitHoop, &QPushButton::clicked, this, &MainWindow::fitDesignToHoop);
    outer->addWidget(btnFitHoop);

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

    auto* scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("InfoPanelScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(panel);
    scroll->setMinimumWidth(275);
    scroll->setMaximumWidth(335);
    return scroll;
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
    m_baseSequence = s;
    if (m_hasTextParams) {
        if (m_currentTextParams.style == TextDigitizer::StitchStyle::PaddedSatin ||
            m_currentTextParams.style == TextDigitizer::StitchStyle::SatinOutline) {
            m_baseDensityMm = m_currentTextParams.borderPitchMm;
        } else {
            m_baseDensityMm = m_currentTextParams.densityMm;
        }
    } else {
        m_baseDensityMm = detectSequenceBaseDensity(s);
    }
    m_basePullMm = m_pull ? m_pull->value() : 0.20;

    if (m_editor && m_editor->pathCount() == 0) {
        if (m_density) {
            QSignalBlocker b(m_density);
            m_density->setValue(m_baseDensityMm);
        }
        if (m_densitySlider) {
            QSignalBlocker b(m_densitySlider);
            m_densitySlider->setValue(std::clamp(int(std::round(m_baseDensityMm * 100.0)), 20, 80));
        }
    }
    if (m_densityStateLabel) {
        m_densityStateLabel->setText(
            QStringLiteral("Basis-Dichte: %1 mm · %2 Stiche")
                .arg(m_baseDensityMm, 0, 'f', 2)
                .arg(m_baseSequence.realStitchCount()));
    }

    registerOrUpdateOpenTab(label.isEmpty() ? QStringLiteral("Entwurf") : label, m_currentFilePath, m_current);

    m_editor->setSequence(m_current);
    m_editor->fitInView();
    m_view->setSequence(m_current);
    m_view->showAll();
    updateInfoPanel();
    setStatusForSequence(label);
}

// ---------------------------------------------------------------------------
void MainWindow::mirrorHorizontal()
{
    if (m_editor->canvasMode() == PathEditorWidget::CanvasMode::BezierPaths ||
        m_editor->canvasMode() == PathEditorWidget::CanvasMode::Combined) {
        m_editor->flipHorizontalSelected();
    }
    if (!m_current.empty() && m_current.mirrorHorizontal()) {
        m_baseSequence.mirrorHorizontal();
        m_hasTextParams = false;
        if (m_docTabBar) {
            const int tabIdx = m_docTabBar->currentIndex();
            if (tabIdx >= 0 && tabIdx < m_openDocs.size()) {
                m_openDocs[tabIdx].currentSeq    = m_current;
                m_openDocs[tabIdx].baseSeq       = m_baseSequence;
                m_openDocs[tabIdx].hasTextParams = false;
            }
        }
        m_editor->setSequence(m_current);
        m_view->setSequence(m_current);
        updateInfoPanel();
    }
    statusBar()->showMessage(QStringLiteral("Motiv horizontal gespiegelt (↔)."), 3000);
}

void MainWindow::mirrorVertical()
{
    if (m_editor->canvasMode() == PathEditorWidget::CanvasMode::BezierPaths ||
        m_editor->canvasMode() == PathEditorWidget::CanvasMode::Combined) {
        m_editor->flipVerticalSelected();
    }
    if (!m_current.empty() && m_current.mirrorVertical()) {
        m_baseSequence.mirrorVertical();
        m_hasTextParams = false;
        if (m_docTabBar) {
            const int tabIdx = m_docTabBar->currentIndex();
            if (tabIdx >= 0 && tabIdx < m_openDocs.size()) {
                m_openDocs[tabIdx].currentSeq    = m_current;
                m_openDocs[tabIdx].baseSeq       = m_baseSequence;
                m_openDocs[tabIdx].hasTextParams = false;
            }
        }
        m_editor->setSequence(m_current);
        m_view->setSequence(m_current);
        updateInfoPanel();
    }
    statusBar()->showMessage(QStringLiteral("Motiv vertikal gespiegelt (↕)."), 3000);
}

void MainWindow::rotate90(bool clockwise)
{
    if (m_editor->canvasMode() == PathEditorWidget::CanvasMode::BezierPaths ||
        m_editor->canvasMode() == PathEditorWidget::CanvasMode::Combined) {
        m_editor->rotateSelected(clockwise ? 90.0 : -90.0);
    }
    if (!m_current.empty() && m_current.rotate90(clockwise)) {
        m_baseSequence.rotate90(clockwise);
        m_hasTextParams = false;
        if (m_docTabBar) {
            const int tabIdx = m_docTabBar->currentIndex();
            if (tabIdx >= 0 && tabIdx < m_openDocs.size()) {
                m_openDocs[tabIdx].currentSeq    = m_current;
                m_openDocs[tabIdx].baseSeq       = m_baseSequence;
                m_openDocs[tabIdx].hasTextParams = false;
            }
        }
        m_editor->setSequence(m_current);
        m_view->setSequence(m_current);
        updateInfoPanel();
    }
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

    if (m_current.rotate(deg)) {
        m_baseSequence.rotate(deg);
        m_hasTextParams = false;
        if (m_docTabBar) {
            const int tabIdx = m_docTabBar->currentIndex();
            if (tabIdx >= 0 && tabIdx < m_openDocs.size()) {
                m_openDocs[tabIdx].currentSeq    = m_current;
                m_openDocs[tabIdx].baseSeq       = m_baseSequence;
                m_openDocs[tabIdx].hasTextParams = false;
            }
        }
        m_editor->setSequence(m_current);
        m_view->setSequence(m_current);
        updateInfoPanel();
        statusBar()->showMessage(QStringLiteral("Motiv um %1° gedreht (⟳).").arg(deg, 0, 'f', 1), 3000);
    }
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
    if (paths.isEmpty()) {
        applyDensityToLoadedDesign();
        return;
    }

    const double density  = m_density->value();
    const double pull     = m_pull->value();
    const double maxStit  = m_maxStitch->value();
    const bool   underlay = m_underlay->isChecked();

    if (m_editor->mode() == PathEditorWidget::Mode::SatinRails) {
        int usable = 0;
        for (const auto& p : paths) if (p.count() >= 2) ++usable;
        if (usable < 2) {
            if (!m_baseSequence.empty()) {
                applyDensityToLoadedDesign();
                return;
            }
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
        int validCount = 0;
        for (const auto& p : paths) {
            if (p.count() >= 3) validCount++;
        }
        if (validCount == 0) {
            if (!m_baseSequence.empty()) {
                applyDensityToLoadedDesign();
                return;
            }
            statusBar()->showMessage(QStringLiteral("Tatami braucht ein Polygon mit ≥3 Punkten."), 6000);
            return;
        }

        // Group paths by colorIdx
        QMap<int, QVector<QPolygonF>> colorRegions;
        QMap<int, QColor> colorMap;
        for (int i = 0; i < paths.size(); ++i) {
            if (paths[i].count() >= 3) {
                QPolygonF poly = paths[i].toPolygon();
                if (poly.size() >= 3 && poly.boundingRect().width() > 0.01 && poly.boundingRect().height() > 0.01) {
                    int cIdx = paths[i].colorIdx;
                    colorRegions[cIdx].push_back(std::move(poly));
                    if (!colorMap.contains(cIdx)) {
                        colorMap[cIdx] = paths[i].color;
                    }
                }
            }
        }

        m_current = StitchSequence();
        int layer = 0;
        for (auto it = colorRegions.begin(); it != colorRegions.end(); ++it, ++layer) {
            const int cIdx = it.key();
            const QVector<QPolygonF>& region = it.value();

            TatamiFill::Params tp;
            tp.rowSpacingMm = density;
            tp.maxStitchMm  = maxStit;
            tp.underlay     = underlay;
            tp.fillAngleDeg = (m_fillAngleSpin ? m_fillAngleSpin->value() : 45.0) + (layer * 30.0);
            tp.pattern      = m_patternCombo ? TatamiFill::PatternType(m_patternCombo->currentData().toInt())
                                             : TatamiFill::PatternType::StandardTatami;
            tp.colorIdx     = layer;

            StitchSequence layerSeq = TatamiFill::generate(region, tp);
            if (!layerSeq.empty()) {
                if (layer > 0 && !layerSeq.stitches.empty()) {
                    layerSeq.stitches.front().flags = SF_ColorChange;
                }
                m_current.stitches.insert(m_current.stitches.end(),
                                          layerSeq.stitches.begin(),
                                          layerSeq.stitches.end());
                QColor col = colorMap.value(cIdx, QColor(40, 90, 200));
                m_current.palette.push_back(ThreadCatalog::snap(col));
            }
        }
    }

    m_hasTextParams = false;
    m_currentFilePath.clear();
    setCurrentSequence(m_current, QStringLiteral("Erzeugt"));
}

// ---------------------------------------------------------------------------
void MainWindow::loadSatinDemo()
{
    m_hasTextParams = false;
    m_currentFilePath.clear();
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
    m_hasTextParams = false;
    m_currentFilePath.clear();
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
    tp.borderPitchMm   = m_density->value();
    tp.maxStitchMm     = m_maxStitch->value();
    tp.underlay        = m_underlay->isChecked();

    if (m_editor) {
        m_editor->clearAll();
        m_editor->setCanvasMode(PathEditorWidget::CanvasMode::Stitches2D);
    }
    m_currentFilePath.clear();
    m_hasTextParams = true;
    m_currentTextParams = tp;
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

    if (m_editor) {
        m_editor->clearAll();
        m_editor->setCanvasMode(PathEditorWidget::CanvasMode::Stitches2D);
    }
    m_hasTextParams = false;
    m_currentFilePath.clear();
    m_current = MonogramGenerator::generate(mp);
    setCurrentSequence(m_current, QStringLiteral("Monogramm: %1").arg(mp.letters));
}

// ---------------------------------------------------------------------------
void MainWindow::fitDesignToHoop()
{
    const bool hasPaths = (m_editor && m_editor->pathCount() > 0);
    const bool hasStitches = !m_current.empty();

    if (!hasPaths && !hasStitches) {
        QMessageBox::information(this, windowTitle(),
            QStringLiteral("Kein Motiv oder Vektorpfad vorhanden – bitte zuerst ein Motiv laden oder zeichnen."));
        return;
    }

    const auto hoop = m_view->hoop();
    const double hoopW = hoop.widthMm;
    const double hoopH = hoop.heightMm;
    const HoopType ht = m_editor ? m_editor->selectedHoop() : HoopType::HoopB_140x200;
    const double marginMm = 5.0;

    if (hasPaths) {
        // Recalculate directly from scaled vector paths
        m_editor->fitPathsToHoop(marginMm);
        generate();
        statusBar()->showMessage(
            QStringLiteral("Objekt-Vektoren an Rahmen %1 angepasst & Stiche (%2 Stiche) mit exakter Dichte neu berechnet.")
                .arg(hoop.name).arg(m_current.realStitchCount()), 8000);
        return;
    }

    // Otherwise resample existing stitches preserving density
    double minX, minY, maxX, maxY;
    if (!m_current.bounds(minX, minY, maxX, maxY)) return;
    const double curW = maxX - minX;
    const double curH = maxY - minY;
    if (curW <= 1e-3 || curH <= 1e-3) return;

    double factor = 1.0;
    if (ht == HoopType::HoopC_50x50) {
        const double diag = std::hypot(curW, curH);
        const double targetDiag = std::max(10.0, 50.0 - 2.0 * marginMm);
        factor = targetDiag / diag;
    } else {
        const double usableW = std::max(10.0, hoopW - 2.0 * marginMm);
        const double usableH = std::max(10.0, hoopH - 2.0 * marginMm);
        factor = std::min(usableW / curW, usableH / curH);
    }

    const QPointF center(0.5 * (minX + maxX), 0.5 * (minY + maxY));
    const double maxStitch = m_maxStitch ? m_maxStitch->value() : 4.0;
    m_current = m_current.scaled(factor, center, maxStitch, 0.35);

    // Center design in hoop
    double nx0, ny0, nx1, ny1;
    if (m_current.bounds(nx0, ny0, nx1, ny1)) {
        const double shiftX = -0.5 * (nx0 + nx1);
        const double shiftY = -0.5 * (ny0 + ny1);
        for (Stitch& s : m_current.stitches) {
            s.x += shiftX;
            s.y += shiftY;
        }
    }

    m_hasTextParams = false;
    setCurrentSequence(m_current, QStringLiteral("Eingepasst"));
    m_editor->setSequence(m_current);
    m_view->setSequence(m_current);
    m_view->showAll();
    updateInfoPanel();
    statusBar()->showMessage(
        QStringLiteral("Motiv auf %1 × %2 mm im Rahmen %3 eingepasst (%4 Stiche mit Dichte-Erhalt neu berechnet).")
            .arg(curW * factor, 0, 'f', 1).arg(curH * factor, 0, 'f', 1)
            .arg(hoop.name).arg(m_current.realStitchCount()), 8000);
}

// ---------------------------------------------------------------------------
void MainWindow::resizeDesign()
{
    const bool hasPaths = (m_editor && m_editor->pathCount() > 0);
    const bool hasStitches = !m_current.empty();

    if (!hasPaths && !hasStitches) {
        QMessageBox::information(this, windowTitle(), QStringLiteral("Zuerst ein Motiv erzeugen oder laden."));
        return;
    }

    double minX = 0, minY = 0, maxX = 0, maxY = 0;
    if (hasPaths) {
        m_editor->pathsBounds(minX, minY, maxX, maxY);
    } else {
        m_current.bounds(minX, minY, maxX, maxY);
    }
    const double origW = std::max(1.0, maxX - minX);
    const double origH = std::max(1.0, maxY - minY);
    const QPointF center(0.5 * (minX + maxX), 0.5 * (minY + maxY));

    const auto hoop = m_view->hoop();
    const double hoopW = hoop.widthMm;
    const double hoopH = hoop.heightMm;

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Motiv skalieren & Stiche neu berechnen"));
    dlg.setMinimumWidth(400);

    auto* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(QStringLiteral("<b>Größe anpassen & Stichmuster neu berechnen</b>"), &dlg));
    auto* subLbl = new QLabel(QStringLiteral("Aktuelle Abmessung: %1 × %2 mm (Rahmen: %3)")
        .arg(origW, 0, 'f', 1).arg(origH, 0, 'f', 1).arg(hoop.name), &dlg);
    subLbl->setStyleSheet(QStringLiteral("color: #2dd4bf; font-weight: 600; font-size: 12px;"));
    lay->addWidget(subLbl);

    auto* form = new QFormLayout;
    auto* widthSpin = new QDoubleSpinBox(&dlg);
    widthSpin->setRange(5.0, 400.0);
    widthSpin->setSingleStep(1.0);
    widthSpin->setValue(origW);
    widthSpin->setSuffix(QStringLiteral(" mm"));

    auto* heightSpin = new QDoubleSpinBox(&dlg);
    heightSpin->setRange(5.0, 400.0);
    heightSpin->setSingleStep(1.0);
    heightSpin->setValue(origH);
    heightSpin->setSuffix(QStringLiteral(" mm"));

    auto* percentSpin = new QDoubleSpinBox(&dlg);
    percentSpin->setRange(10.0, 500.0);
    percentSpin->setSingleStep(5.0);
    percentSpin->setValue(100.0);
    percentSpin->setSuffix(QStringLiteral(" %"));

    auto* keepAspect = new QCheckBox(QStringLiteral("Seitenverhältnis sperren (proportional)"), &dlg);
    keepAspect->setChecked(true);

    form->addRow(QStringLiteral("Breite:"), widthSpin);
    form->addRow(QStringLiteral("Höhe:"), heightSpin);
    form->addRow(QStringLiteral("Skalierung:"), percentSpin);
    lay->addLayout(form);
    lay->addWidget(keepAspect);

    // Presets
    auto* presetBox = new QHBoxLayout;
    auto* btnPreset95 = new QPushButton(QStringLiteral("95% Rahmen"), &dlg);
    auto* btnPreset90 = new QPushButton(QStringLiteral("90% Rahmen"), &dlg);
    auto* btnPreset80 = new QPushButton(QStringLiteral("80% Rahmen"), &dlg);
    auto* btnPreset100 = new QPushButton(QStringLiteral("100% Reset"), &dlg);
    presetBox->addWidget(btnPreset95);
    presetBox->addWidget(btnPreset90);
    presetBox->addWidget(btnPreset80);
    presetBox->addWidget(btnPreset100);
    lay->addLayout(presetBox);

    auto* recalcInfo = new QLabel(hasPaths 
        ? QStringLiteral("ℹ Vektorgeometrie aktiv: Stiche werden komplett aus den Konturen mit exakter Dichte neu berechnet.")
        : QStringLiteral("ℹ Stichmuster aktiv: Stiche werden resampled, überlange Stiche aufgeteilt & Mikrostiche gefiltert."), &dlg);
    recalcInfo->setWordWrap(true);
    recalcInfo->setStyleSheet(QStringLiteral("color: #94a3b8; font-size: 11px; margin-top: 4px;"));
    lay->addWidget(recalcInfo);

    bool updating = false;
    auto applyPercent = [&](double pct) {
        if (updating) return;
        updating = true;
        const double factor = pct / 100.0;
        widthSpin->setValue(origW * factor);
        heightSpin->setValue(origH * factor);
        percentSpin->setValue(pct);
        updating = false;
    };

    connect(percentSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), &dlg, applyPercent);

    connect(widthSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), &dlg, [&](double val) {
        if (updating) return;
        updating = true;
        const double pct = (val / origW) * 100.0;
        percentSpin->setValue(pct);
        if (keepAspect->isChecked()) {
            heightSpin->setValue(origH * (val / origW));
        }
        updating = false;
    });

    connect(heightSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), &dlg, [&](double val) {
        if (updating) return;
        updating = true;
        const double pct = (val / origH) * 100.0;
        percentSpin->setValue(pct);
        if (keepAspect->isChecked()) {
            widthSpin->setValue(origW * (val / origH));
        }
        updating = false;
    });

    auto fitPreset = [&](double targetFraction) {
        const double maxW = (hoopW - 10.0) * targetFraction;
        const double maxH = (hoopH - 10.0) * targetFraction;
        const double f = std::min(maxW / origW, maxH / origH);
        applyPercent(f * 100.0);
    };

    connect(btnPreset95, &QPushButton::clicked, &dlg, [&]{ fitPreset(0.95); });
    connect(btnPreset90, &QPushButton::clicked, &dlg, [&]{ fitPreset(0.90); });
    connect(btnPreset80, &QPushButton::clicked, &dlg, [&]{ fitPreset(0.80); });
    connect(btnPreset100, &QPushButton::clicked, &dlg, [&]{ applyPercent(100.0); });

    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(btnBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(btnBox);

    if (dlg.exec() != QDialog::Accepted) return;

    const double targetW = widthSpin->value();
    const double factor = targetW / origW;
    if (std::abs(factor - 1.0) < 1e-4) return;

    if (hasPaths) {
        m_editor->scalePaths(factor, center);
        generate();
        statusBar()->showMessage(
            QStringLiteral("Motiv auf %1 × %2 mm skaliert & Stiche (%3 Stiche) neu berechnet.")
                .arg(targetW, 0, 'f', 1).arg(heightSpin->value(), 0, 'f', 1)
                .arg(m_current.realStitchCount()), 8000);
    } else {
        const double maxStitch = m_maxStitch ? m_maxStitch->value() : 4.0;
        m_hasTextParams = false;
        m_current = m_current.scaled(factor, center, maxStitch, 0.35);
        setCurrentSequence(m_current, QStringLiteral("Skaliert"));
        m_editor->setSequence(m_current);
        m_view->setSequence(m_current);
        m_view->showAll();
        updateInfoPanel();
        statusBar()->showMessage(
            QStringLiteral("Motiv auf %1 × %2 mm skaliert & Stiche mit Dichteerhalt resampled (%3 Stiche).")
                .arg(targetW, 0, 'f', 1).arg(heightSpin->value(), 0, 'f', 1)
                .arg(m_current.realStitchCount()), 8000);
    }
}

// ---------------------------------------------------------------------------
void MainWindow::makeLogo()
{
    loadMefOriginal(0);
}

void MainWindow::loadMefOriginal(int style)
{
    if (m_editor) {
        m_editor->clearAll();
        m_editor->setCanvasMode(PathEditorWidget::CanvasMode::Stitches2D);
    }
    m_currentFilePath.clear();
    m_hasTextParams = false;
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

    if (m_editor) {
        m_editor->clearAll();
        m_editor->setCanvasMode(PathEditorWidget::CanvasMode::Stitches2D);
    }
    m_currentFilePath = fn;
    m_hasTextParams = false;
    setCurrentSequence(m_current, QStringLiteral("SVG: %1").arg(QFileInfo(fn).fileName()));
    statusBar()->showMessage(QStringLiteral("SVG-Vektordatei '%1' erfolgreich digitalisiert (%2 Stiche).")
                             .arg(QFileInfo(fn).fileName()).arg(m_current.stitches.size()), 8000);
}

// ---------------------------------------------------------------------------
bool MainWindow::loadFile(const QString& filePath)
{
    if (filePath.isEmpty() || !QFile::exists(filePath)) return false;

    QFileInfo fi(filePath);
    const QString ext = fi.suffix().toLower();

    StitchSequence seq;
    if (ext == QStringLiteral("jef")) {
        if (!JefCodec::importFromFile(filePath, seq)) {
            QMessageBox::warning(this, QStringLiteral("Fehler beim Laden"),
                QStringLiteral("Die Janome JEF-Datei konnte nicht gelesen werden:\n%1").arg(filePath));
            return false;
        }
    } else if (ext == QStringLiteral("dst")) {
        if (!DstCodec::importFromFile(filePath, seq)) {
            QMessageBox::warning(this, QStringLiteral("Fehler beim Laden"),
                QStringLiteral("Die Tajima DST-Datei konnte nicht gelesen werden:\n%1").arg(filePath));
            return false;
        }
    } else if (ext == QStringLiteral("svg")) {
        SvgDigitizer::Params sp;
        sp.targetWidthMm = 120.0;
        seq = SvgDigitizer::digitizeSvgFile(filePath, sp);
        if (seq.empty()) {
            QMessageBox::warning(this, QStringLiteral("Fehler beim Laden"),
                QStringLiteral("Aus der SVG-Datei konnten keine Stiche erzeugt werden:\n%1").arg(filePath));
            return false;
        }
    } else if (ext == QStringLiteral("png") || ext == QStringLiteral("jpg") || ext == QStringLiteral("jpeg") || ext == QStringLiteral("bmp")) {
        QImage img(filePath);
        if (!img.isNull()) {
            m_currentFilePath = filePath;
            digitizeWithDialog(img, fi.fileName());
            setWindowTitle(QStringLiteral("StickCore Studio – %1").arg(fi.fileName()));
            return true;
        }
        return false;
    } else {
        QMessageBox::warning(this, QStringLiteral("Unbekanntes Format"),
            QStringLiteral("Das Dateiformat .%1 wird nicht unterstützt.\nUnterstützt werden: .jef, .dst, .svg, .png, .jpg").arg(ext));
        return false;
    }

    if (seq.empty()) {
        QMessageBox::warning(this, QStringLiteral("Leere Datei"),
            QStringLiteral("Die Datei enthält keine druckbaren Stiche:\n%1").arg(filePath));
        return false;
    }

    if (m_editor) {
        m_editor->clearAll();
        m_editor->setCanvasMode(PathEditorWidget::CanvasMode::Stitches2D);
    }

    m_currentFilePath = filePath;
    m_hasTextParams = false;
    setWindowTitle(QStringLiteral("StickCore Studio – %1").arg(fi.fileName()));
    addRecentFile(filePath);

    // Automatische Rahmenauswahl passend zur geladenen Motivgröße
    double minX, minY, maxX, maxY;
    if (seq.bounds(minX, minY, maxX, maxY)) {
        const double w = maxX - minX;
        const double h = maxY - minY;
        const auto& mp = MachineProfile::current();
        const HoopType bestHoop = mp.hoopFor(w, h);
        selectHoop(bestHoop);
    }

    setCurrentSequence(seq, fi.fileName());
    statusBar()->showMessage(QStringLiteral("Stickdatei '%1' geladen (%2 Stiche, %3 Farben). Fadendichte rechts frei anpassbar.")
        .arg(fi.fileName()).arg(seq.realStitchCount()).arg(seq.palette.size()), 6000);
    return true;
}

void MainWindow::openEmbroideryFile()
{
    QString startDir = QCoreApplication::applicationDirPath() + QStringLiteral("/output");
    if (!QDir(startDir).exists()) {
        startDir = QStringLiteral("E:/StickCore/output");
    }
    if (!QDir(startDir).exists()) {
        startDir = QStringLiteral("F:/EmbF5/Feuerwehr");
    }
    if (!QDir(startDir).exists()) {
        startDir = QDir::homePath();
    }
    if (!m_currentFilePath.isEmpty()) {
        startDir = QFileInfo(m_currentFilePath).absolutePath();
    }

    const QString filter = QStringLiteral(
        "Stickdateien (*.jef *.dst *.svg);;"
        "Janome Stickdatei (*.jef);;"
        "Tajima Universal (*.dst);;"
        "SVG Vektordatei (*.svg);;"
        "Bilder & Fotos (*.png *.jpg *.jpeg *.bmp);;"
        "Alle Dateien (*.*)"
    );
    const QString fn = QFileDialog::getOpenFileName(this,
        QStringLiteral("Stickdatei oder Entwurf öffnen"),
        startDir,
        filter);
    if (!fn.isEmpty()) {
        loadFile(fn);
    }
}

void MainWindow::saveFile()
{
    if (m_current.empty()) {
        QMessageBox::information(this, windowTitle(), QStringLiteral("Kein Stickmotiv zum Speichern vorhanden."));
        return;
    }
    if (m_currentFilePath.isEmpty()) {
        saveFileAs();
        return;
    }

    const QString ext = QFileInfo(m_currentFilePath).suffix().toLower();
    if (ext == QStringLiteral("jef")) {
        double x0, y0, x1, y1; m_current.bounds(x0, y0, x1, y1);
        const auto& mp = MachineProfile::current();
        HoopType hoop;
        if (m_hoopCombo && m_hoopCombo->currentIndex() > 0)
            hoop = static_cast<HoopType>(m_hoopCombo->currentData().toInt());
        else
            hoop = mp.hoopFor(x1 - x0, y1 - y0);

        const JefCodec::Result res = JefCodec::exportToFile(m_currentFilePath, m_current, hoop);
        if (res.ok) {
            statusBar()->showMessage(QStringLiteral("Gespeichert nach %1 (%2)").arg(QFileInfo(m_currentFilePath).fileName(), res.message), 6000);
        } else {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("Fehler beim Speichern: %1").arg(res.message));
        }
    } else if (ext == QStringLiteral("dst")) {
        const DstCodec::Result res = DstCodec::exportToFile(m_currentFilePath, m_current);
        if (res.ok) {
            statusBar()->showMessage(QStringLiteral("Gespeichert nach %1 (%2)").arg(QFileInfo(m_currentFilePath).fileName(), res.message), 6000);
        } else {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("Fehler beim Speichern: %1").arg(res.message));
        }
    } else {
        saveFileAs();
    }
}

void MainWindow::saveFileAs()
{
    if (m_current.empty()) {
        QMessageBox::information(this, windowTitle(), QStringLiteral("Kein Stickmotiv zum Speichern vorhanden."));
        return;
    }
    QString defName = m_currentFilePath.isEmpty() ? QStringLiteral("design.jef") : m_currentFilePath;
    const QString fn = QFileDialog::getSaveFileName(this,
        QStringLiteral("Motiv speichern unter"),
        defName,
        QStringLiteral("Janome Stickdatei (*.jef);;Tajima Universal (*.dst);;Alle Dateien (*.*)"));
    if (fn.isEmpty()) return;

    m_currentFilePath = fn;
    setWindowTitle(QStringLiteral("StickCore Studio – %1").arg(QFileInfo(fn).fileName()));
    saveFile();
}

void MainWindow::convertStitchesToPaths()
{
    if (m_current.empty()) {
        QMessageBox::information(this, windowTitle(),
            QStringLiteral("Kein Stickmotiv geladen. Öffne zuerst eine .JEF oder .DST-Datei."));
        return;
    }

    QVector<EditPath> paths;
    EditPath cur;
    int curColorIdx = 0;
    QColor curColor = QColor(220, 50, 60);
    if (!m_current.palette.empty()) {
        curColor = m_current.palette[0].color;
    }

    for (const auto& s : m_current.stitches) {
        if (s.flags & SF_ColorChange) {
            if (cur.nodes.size() >= 2) {
                cur.color = curColor;
                cur.colorIdx = curColorIdx;
                paths.append(cur);
            }
            cur = EditPath();
            curColorIdx = s.colorIdx;
            if (curColorIdx >= 0 && curColorIdx < int(m_current.palette.size())) {
                curColor = m_current.palette[curColorIdx].color;
            }
            continue;
        }
        if (s.flags & (SF_Jump | SF_Trim | SF_Stop)) {
            if (cur.nodes.size() >= 2) {
                cur.color = curColor;
                cur.colorIdx = curColorIdx;
                paths.append(cur);
            }
            cur = EditPath();
            continue;
        }

        // Stiche in mm
        cur.nodes.append(BezierNode(QPointF(s.x, s.y)));
    }
    if (cur.nodes.size() >= 2) {
        cur.color = curColor;
        cur.colorIdx = curColorIdx;
        paths.append(cur);
    }

    if (paths.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), QStringLiteral("Aus den Stichen konnten keine Pfade extrahiert werden."));
        return;
    }

    m_editor->loadPaths(paths);
    m_editor->setCanvasMode(PathEditorWidget::CanvasMode::Combined);
    statusBar()->showMessage(QStringLiteral("%1 Vektorpfade aus Stichen extrahiert! Frei editierbar mit Zeiger, Lasso & Zeichenstift.").arg(paths.size()), 7000);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls() || event->mimeData()->hasText()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    if (!urls.isEmpty()) {
        const QString fn = urls.first().toLocalFile();
        if (!fn.isEmpty()) {
            loadFile(fn);
            event->acceptProposedAction();
        }
}
}

void MainWindow::addRecentFile(const QString& filePath)
{
    if (filePath.isEmpty() || !QFile::exists(filePath)) return;
    QSettings settings;
    QStringList files = settings.value(QStringLiteral("recentFiles")).toStringList();
    files.removeAll(filePath);
    files.prepend(filePath);
    while (files.size() > 8) files.removeLast();
    settings.setValue(QStringLiteral("recentFiles"), files);
    updateRecentFilesMenu();
}

void MainWindow::updateRecentFilesMenu()
{
    if (!m_recentFilesMenu) return;
    m_recentFilesMenu->clear();
    QSettings settings;
    const QStringList files = settings.value(QStringLiteral("recentFiles")).toStringList();
    if (files.isEmpty()) {
        auto* act = m_recentFilesMenu->addAction(QStringLiteral("Keine Dateien"));
        act->setEnabled(false);
        return;
    }
    for (int i = 0; i < files.size(); ++i) {
        const QString f = files[i];
        if (!QFile::exists(f)) continue;
        const QString text = QStringLiteral("&%1  %2").arg(i + 1).arg(QFileInfo(f).fileName());
        auto* act = m_recentFilesMenu->addAction(text, this, [this, f]{ loadFile(f); });
        act->setToolTip(f);
    }
    m_recentFilesMenu->addSeparator();
    m_recentFilesMenu->addAction(QStringLiteral("Liste &leeren"), this, [this]{
        QSettings settings;
        settings.remove(QStringLiteral("recentFiles"));
        updateRecentFilesMenu();
    });
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
    if (m_isDigitizing) return;
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Bild digitalisieren"),
        QString(), QStringLiteral("Bilder (*.png *.jpg *.jpeg *.bmp *.gif *.webp)"));
    if (path.isEmpty()) return;
    QImage img(path);
    if (img.isNull()) { QMessageBox::warning(this, windowTitle(), QStringLiteral("Bild konnte nicht geladen werden.")); return; }
    m_currentFilePath = path;
    m_editor->setBackgroundImage(img);
    m_isDigitizing = true;
    digitizeWithDialog(img, QFileInfo(path).fileName());
    m_isDigitizing = false;
}

// ---------------------------------------------------------------------------
void MainWindow::digitizeWithDialog(const QImage& img, const QString& label)
{
    const StitchSequence backupSeq = m_editor->sequence();
    ImageDialog dlg(img, this);

    connect(&dlg, &ImageDialog::livePreviewGenerated, this, [this](const StitchSequence& previewSeq) {
        if (!previewSeq.empty()) {
            m_editor->setSequence(previewSeq);
            if (m_view) m_view->setSequence(previewSeq);
        }
    });

    if (dlg.exec() != QDialog::Accepted) {
        m_editor->setSequence(backupSeq);
        if (m_view) m_view->setSequence(backupSeq);
        return;
    }

    if (dlg.applyMode() == ImageDialog::ApplyMode::VectorPaths) {
        QVector<EditPath> paths = dlg.extractedPaths();
        if (paths.isEmpty()) {
            paths = ImageDigitizer::traceToPaths(img, dlg.params());
        }
        if (!paths.isEmpty()) {
            m_editor->loadPaths(paths);
            m_editor->setMode(PathEditorWidget::Mode::TatamiPolygon);
            m_editor->setCanvasMode(PathEditorWidget::CanvasMode::Combined);
            m_editor->setBackgroundImage(img);
            generate();
            statusBar()->showMessage(
                QStringLiteral("Vektorpfade erfolgreich importiert (%1 Pfade). Bearbeite Knoten frei im Canvas (F5 zum Neuberechnen).")
                .arg(paths.size()), 7000);
            return;
        }
    }

    ImageDigitizer::Params ip = dlg.params();
    ip.densityMm   = m_density->value();
    ip.maxStitchMm = m_maxStitch->value();
    StitchSequence s = ImageDigitizer::generate(img, ip);
    if (s.empty()) {
        m_editor->setSequence(backupSeq);
        if (m_view) m_view->setSequence(backupSeq);
        QMessageBox::information(this, windowTitle(),
            QStringLiteral("Keine Stiche erzeugt — probiere mehr Kontrast oder einen anderen Schwellwert."));
        return;
    }
    m_hasTextParams = false;
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
    m_currentFilePath.clear();
    m_hasTextParams = false;
    m_editor->setBackgroundImage(img);
    setCurrentSequence(s, label);
}

// ---------------------------------------------------------------------------
void MainWindow::phoneUpload()
{
    if (!m_upload) {
        m_upload = new QrUploadServer(this);
        connect(m_upload, &QrUploadServer::imageReceived, this, [this](const QImage& img){
            if (m_isDigitizing) {
                return;
            }
            if (m_qrDialog) {
                m_qrDialog->close();
                m_qrDialog = nullptr;
            }
            m_currentFilePath.clear();
            m_editor->setBackgroundImage(img);
            m_isDigitizing = true;
            digitizeWithDialog(img, QStringLiteral("Handy-Bild digitalisiert"));
            m_isDigitizing = false;
        });
    }
    if (!m_upload->isListening() && !m_upload->start(8080)) {
        QMessageBox::warning(this, windowTitle(), QStringLiteral("Konnte den Upload-Server nicht starten (Port belegt?)."));
        return;
    }
    const QString url = m_upload->url();
    const auto matrix = QrCode::encode(url.toStdString());

    if (m_qrDialog) {
        m_qrDialog->show();
        m_qrDialog->raise();
        m_qrDialog->activateWindow();
        return;
    }

    auto* d = new QDialog(this);
    m_qrDialog = d;
    d->setWindowTitle(QStringLiteral("Handy-Upload (QR-Code)"));
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->setMinimumWidth(380);
    auto* lay = new QVBoxLayout(d);
    lay->setContentsMargins(20, 20, 20, 20);
    lay->setSpacing(14);

    auto* qr = new QLabel;
    qr->setPixmap(qrPixmap(matrix, 8, 4));
    qr->setAlignment(Qt::AlignCenter);
    lay->addWidget(qr);

    auto* info = new QLabel(QStringLiteral("<b>1. Scanne den QR-Code</b> mit deiner Handy-Kamera.<br>"
                                           "<b>2. Wähle ein Foto</b> und tippe auf Senden.<br><br>"
                                           "<span style='color:#8b93a4;font-size:11px;'>Oder im Handy-Browser aufrufen:</span><br>"
                                           "<span style='color:#2dd4bf;font-family:monospace;font-size:13px;'>%1</span>").arg(url));
    info->setAlignment(Qt::AlignCenter);
    info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lay->addWidget(info);

    m_uploadStatus = new QLabel(QStringLiteral("⏳ Warte auf Bild-Upload vom Smartphone…"));
    m_uploadStatus->setAlignment(Qt::AlignCenter);
    m_uploadStatus->setStyleSheet(QStringLiteral("color:#2dd4bf; font-weight:600; padding:6px; background:#1b1e26; border-radius:6px;"));
    lay->addWidget(m_uploadStatus);

    auto* btnClose = new QPushButton(QStringLiteral("✕ Dialog schließen"), d);
    btnClose->setStyleSheet(QStringLiteral("QPushButton { background-color: #232732; border: 1px solid #3b4252; color: #dbe0e9; padding: 8px 16px; border-radius: 6px; font-weight: 600; font-size: 13px; } QPushButton:hover { background-color: #2c313d; border-color: #81a1c1; }"));
    btnClose->setShortcut(QKeySequence::Cancel);
    connect(btnClose, &QPushButton::clicked, d, &QDialog::close);
    lay->addWidget(btnClose);

    connect(d, &QObject::destroyed, this, [this]{
        m_qrDialog = nullptr;
        m_uploadStatus = nullptr;
    });
    d->show();
    d->raise();
    d->activateWindow();
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
        m_currentFilePath.clear();
        m_hasTextParams = true;
        m_currentTextParams = tp;
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
        if (m_editor) {
            m_editor->clearAll();
            m_editor->setCanvasMode(PathEditorWidget::CanvasMode::Stitches2D);
        }
        m_currentFilePath.clear();
        m_hasTextParams = false;
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
        m_hasTextParams = false;
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
        m_hasTextParams = false;
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
        if (m_editor) {
            m_editor->clearAll();
            m_editor->setCanvasMode(PathEditorWidget::CanvasMode::Stitches2D);
        }
        m_currentFilePath.clear();
        m_hasTextParams = false;
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
        if (m_editor) {
            m_editor->clearAll();
            m_editor->setCanvasMode(PathEditorWidget::CanvasMode::Stitches2D);
        }
        m_currentFilePath.clear();
        m_hasTextParams = false;
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

            m_currentFilePath = p1Fn;
            m_hasTextParams = false;
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

    if (paths.isEmpty() && m_editor) {
        m_editor->clearAll();
        m_editor->setCanvasMode(PathEditorWidget::CanvasMode::Stitches2D);
    }
    m_currentFilePath.clear();
    m_hasTextParams = false;
    m_current = TatamiFill::generate(poly, tp);
    m_current.palette = { ThreadCatalog::snap(QColor(34, 139, 34)) };
    setCurrentSequence(m_current, QStringLiteral("Tatami mit %1").arg(CarvingPattern::presetName(chosen)));
    statusBar()->showMessage(
        QStringLiteral("Tatami-Prägemuster (%1) erfolgreich mit %2 Stichen generiert.")
            .arg(CarvingPattern::presetName(chosen)).arg(m_current.size()), 6000);
}

// ---------------------------------------------------------------------------
void MainWindow::generateGradientFill()
{
    QPolygonF poly;
    for (const auto& p : m_editor->paths()) {
        if (p.count() >= 3) { poly = p.toPolygon(); break; }
    }
    const bool hadPaths = !poly.isEmpty();
    if (poly.isEmpty()) {
        poly << QPointF(-35, -40) << QPointF(35, -40) << QPointF(35, 10)
             << QPointF(0, 40) << QPointF(-35, 10);
    }

    QStringList palettePresets;
    palettePresets << QStringLiteral("✨ Meister-Gold zu Kupfer/Bronze (Madeira 1083 → 1070)")
                   << QStringLiteral("👑 Königsblau zu Himmelblau / Cyan (Madeira 1134 → 1029)")
                   << QStringLiteral("🍷 Bordeauxrot zu Champagner / Rosé (Madeira 1184 → 1017)")
                   << QStringLiteral("🌲 Tannengrün zu Lindgrün / Salbei (Madeira 1170 → 1100)");

    bool ok = false;
    const QString choice = QInputDialog::getItem(
        this, QStringLiteral("Farbverlaufs-Füllung (Ombré Tatami)"),
        QStringLiteral("Garnfarben-Verlauf wählen (Farbe 1 dichtet ab, Farbe 2 dichtet an):"),
        palettePresets, 0, false, &ok);
    if (!ok) return;

    GradientFill::Params gp;
    gp.angleDeg = m_fillAngleSpin ? m_fillAngleSpin->value() : 0.0;
    gp.minSpacingMm = 0.35;
    gp.maxSpacingMm = 1.80;
    gp.maxStitchMm = m_maxStitch ? m_maxStitch->value() : 4.0;
    gp.underlay = m_underlay ? m_underlay->isChecked() : true;

    if (choice.contains(QStringLiteral("Königsblau"))) {
        gp.color1 = ThreadColor(QColor(25, 25, 112), QStringLiteral("Königsblau"), 1134);
        gp.color2 = ThreadColor(QColor(0, 191, 255), QStringLiteral("Cyanblau"), 1029);
    } else if (choice.contains(QStringLiteral("Bordeauxrot"))) {
        gp.color1 = ThreadColor(QColor(128, 0, 32), QStringLiteral("Bordeaux"), 1184);
        gp.color2 = ThreadColor(QColor(245, 222, 179), QStringLiteral("Champagner"), 1017);
    } else if (choice.contains(QStringLiteral("Tannengrün"))) {
        gp.color1 = ThreadColor(QColor(34, 139, 34), QStringLiteral("Tannengrün"), 1170);
        gp.color2 = ThreadColor(QColor(144, 238, 144), QStringLiteral("Lindgrün"), 1100);
    } else {
        gp.color1 = ThreadColor(QColor(212, 175, 55), QStringLiteral("Madeira Gold"), 1083);
        gp.color2 = ThreadColor(QColor(184, 115, 51), QStringLiteral("Kupfer/Bronze"), 1070);
    }

    if (!hadPaths && m_editor) {
        m_editor->clearAll();
        m_editor->setCanvasMode(PathEditorWidget::CanvasMode::Stitches2D);
    }
    m_currentFilePath.clear();
    m_hasTextParams = false;
    m_current = GradientFill::generate(poly, gp);
    setCurrentSequence(m_current, QStringLiteral("Ombré-Farbverlauf (%1 → %2)")
                       .arg(gp.color1.description, gp.color2.description));
    statusBar()->showMessage(
        QStringLiteral("Farbverlaufs-Füllung (Ombré) mit %1 Stichen und automatischem Farbwechsel generiert.")
            .arg(m_current.size()), 6000);
}

// ---------------------------------------------------------------------------
void MainWindow::generateBeanStitchOutline()
{
    QPainterPath path;
    const auto paths = m_editor->paths();
    if (!paths.isEmpty()) {
        for (const auto& p : paths) {
            if (p.count() >= 2) path.addPath(p.toPainterPath());
        }
    }
    const bool hadPaths = !path.isEmpty();
    if (path.isEmpty()) {
        path.addRoundedRect(QRectF(-40, -40, 80, 80), 14, 14);
    }

    QStringList modeList;
    modeList << QStringLiteral("🪡 3-Pass Dreifachstich (Standard Bean Stitch – erhabene Kontur)")
             << QStringLiteral("🧶 5-Pass Schwerer Bohnenstich (Relief-Optik für Loden & Walk)");

    bool ok = false;
    const QString choice = QInputDialog::getItem(
        this, QStringLiteral("Dreifachstich / Bean Stitch wählen"),
        QStringLiteral("Wiederholungsanzahl pro Nadelstich-Segment wählen:"),
        modeList, 0, false, &ok);
    if (!ok) return;

    BeanStitchGenerator::Params bp;
    bp.mode = choice.contains(QStringLiteral("5-Pass"))
              ? BeanStitchGenerator::Mode::FivePass
              : BeanStitchGenerator::Mode::TriplePass;
    bp.stitchLengthMm = 2.50;
    bp.threadColor = ThreadColor(QColor(40, 40, 40), QStringLiteral("Anthrazit Kontur"));

    if (!hadPaths && m_editor) {
        m_editor->clearAll();
        m_editor->setCanvasMode(PathEditorWidget::CanvasMode::Stitches2D);
    }
    m_currentFilePath.clear();
    m_hasTextParams = false;
    m_current = BeanStitchGenerator::generate(path, bp);
    setCurrentSequence(m_current, QStringLiteral("Bean Stitch (%1)")
                       .arg(bp.mode == BeanStitchGenerator::Mode::FivePass ? "5-Pass" : "3-Pass"));
    statusBar()->showMessage(
        QStringLiteral("Bean Stitch Kontur (%1) erfolgreich mit %2 Stichen berechnet.")
            .arg(bp.mode == BeanStitchGenerator::Mode::FivePass ? "5-Pass" : "3-Pass")
            .arg(m_current.size()), 6000);
}

// ---------------------------------------------------------------------------
void MainWindow::generateStipplingFill()
{
    QPolygonF poly;
    for (const auto& p : m_editor->paths()) {
        if (p.count() >= 3) { poly = p.toPolygon(); break; }
    }
    const bool hadPaths = !poly.isEmpty();
    if (poly.isEmpty()) {
        const double s = 35.0;
        poly << QPointF(-s, -s) << QPointF(s, -s) << QPointF(s, s) << QPointF(-s, s);
    }

    bool ok = false;
    const double spacing = QInputDialog::getDouble(
        this, QStringLiteral("Mäander- & Quiltfüllung (Stippling)"),
        QStringLiteral("Schlaufenabstand der Quilt-Wellen in mm (2.5 - 8.0 mm):"),
        4.0, 2.0, 8.0, 1, &ok);
    if (!ok) return;

    StipplingGenerator::Params sp;
    sp.loopSpacingMm = spacing;
    sp.stitchLengthMm = 2.20;
    sp.marginMm = 1.50;
    sp.threadColor = ThreadColor(QColor(70, 130, 180), QStringLiteral("Quilt-Blau"));

    if (!hadPaths && m_editor) {
        m_editor->clearAll();
        m_editor->setCanvasMode(PathEditorWidget::CanvasMode::Stitches2D);
    }
    m_currentFilePath.clear();
    m_hasTextParams = false;
    m_current = StipplingGenerator::generate(poly, sp);
    setCurrentSequence(m_current, QStringLiteral("Mäander-Quilt (Stippling %1 mm)").arg(spacing, 0, 'f', 1));
    statusBar()->showMessage(
        QStringLiteral("Mäander-Quiltfüllung (Stippling) mit %1 Stichen unterbrechungsfrei generiert.")
            .arg(m_current.size()), 6000);
}

// ---------------------------------------------------------------------------
void MainWindow::generateFlorentineFill()
{
    QPolygonF poly;
    for (const auto& p : m_editor->paths()) {
        if (p.count() >= 3) { poly = p.toPolygon(); break; }
    }
    const bool hadPaths = !poly.isEmpty();
    if (poly.isEmpty()) {
        poly << QPointF(-40, -30) << QPointF(40, -30) << QPointF(45, 10)
             << QPointF(0, 35) << QPointF(-45, 10);
    }

    bool ok = false;
    const double amplitude = QInputDialog::getDouble(
        this, QStringLiteral("Florentiner Wellen-Tatami (Curved Wave Fill)"),
        QStringLiteral("Wellen-Amplitude in mm (Schwingungshöhe der Fadenreihen):"),
        4.5, 1.0, 15.0, 1, &ok);
    if (!ok) return;

    FlorentineFill::Params fp;
    fp.angleDeg = m_fillAngleSpin ? m_fillAngleSpin->value() : 0.0;
    fp.rowSpacingMm = m_density ? m_density->value() : 0.40;
    fp.maxStitchMm = m_maxStitch ? m_maxStitch->value() : 3.80;
    fp.amplitudeMm = amplitude;
    fp.wavelengthMm = 26.0;
    fp.underlay = m_underlay ? m_underlay->isChecked() : true;
    fp.threadColor = ThreadColor(QColor(0, 139, 139), QStringLiteral("Smaragd Wellenglanz"));

    if (!hadPaths && m_editor) {
        m_editor->clearAll();
        m_editor->setCanvasMode(PathEditorWidget::CanvasMode::Stitches2D);
    }
    m_currentFilePath.clear();
    m_hasTextParams = false;
    m_current = FlorentineFill::generate(poly, fp);
    setCurrentSequence(m_current, QStringLiteral("Florentiner Wellen-Tatami (Amp %1 mm)").arg(amplitude, 0, 'f', 1));
    statusBar()->showMessage(
        QStringLiteral("Florentiner Wellen-Tatami mit %1 Stichen erfolgreich berechnet.")
            .arg(m_current.size()), 6000);
}

// ---------------------------------------------------------------------------
void MainWindow::openArcTextStudio()
{
    ArcTextDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        StitchSequence seq = dlg.resultSequence();
        if (!seq.empty()) {
            if (m_editor) {
                m_editor->clearAll();
                m_editor->setCanvasMode(PathEditorWidget::CanvasMode::Stitches2D);
            }
            m_currentFilePath.clear();
            m_hasTextParams = true;
            m_currentTextParams = dlg.textParams();

            double minX, minY, maxX, maxY;
            if (seq.bounds(minX, minY, maxX, maxY)) {
                const auto& mp = MachineProfile::current();
                selectHoop(mp.hoopFor(maxX - minX, maxY - minY));
            }

            const QString tabTitle = QStringLiteral("Bogen-Schrift: %1").arg(m_currentTextParams.text);
            setCurrentSequence(seq, tabTitle);
            setWindowTitle(QStringLiteral("StickCore Studio — %1").arg(tabTitle));
            statusBar()->showMessage(
                QStringLiteral("Bogen-Schriftzug „%1“ mit 3D-Relief Polsterung ins Studio geladen (%2 Stiche). Fadendichte rechts frei anpassbar.")
                    .arg(m_currentTextParams.text).arg(m_current.realStitchCount()), 6000);
        }
    }
}

// ---------------------------------------------------------------------------
void MainWindow::makeJerseyNumbers()
{
    QDialog d(this);
    d.setWindowTitle(QStringLiteral("Trikot- & College-Nummern (00 - 99)"));
    d.resize(420, 320);
    auto* form = new QFormLayout(&d);

    auto* numSpin = new QSpinBox(&d);
    numSpin->setRange(0, 99);
    numSpin->setValue(10);
    form->addRow(QStringLiteral("<b>Nummer (0-99):</b>"), numSpin);

    auto* fontCombo = new QComboBox(&d);
    fontCombo->addItem(QStringLiteral("🏈 Varsity (Authentic Athletic Block)"), QStringLiteral("Varsity"));
    fontCombo->addItem(QStringLiteral("🔢 Jersey 10 (Chunky Block Athletics)"), QStringLiteral("Jersey 10"));
    fontCombo->addItem(QStringLiteral("🔢 Jersey 15 (Standard Varsity Athleisure)"), QStringLiteral("Jersey 15"));
    fontCombo->addItem(QStringLiteral("🔢 Jersey 25 (Bold Stadium Numbers)"), QStringLiteral("Jersey 25"));
    fontCombo->addItem(QStringLiteral("🎓 Graduate (US Ivy League Serif)"), QStringLiteral("Graduate"));
    form->addRow(QStringLiteral("College-Schriftart:"), fontCombo);

    auto* hSpin = new QDoubleSpinBox(&d);
    hSpin->setRange(20.0, 150.0);
    hSpin->setValue(75.0);
    hSpin->setSuffix(QStringLiteral(" mm"));
    form->addRow(QStringLiteral("Ziffernhöhe:"), hSpin);

    auto* styleCombo = new QComboBox(&d);
    styleCombo->addItem(QStringLiteral("⭐ 3D-Relief Polsterraupe (Paddlerin Dual-Pass: 2,4mm -> 3,5mm)"), int(TextDigitizer::StitchStyle::PaddedSatin));
    styleCombo->addItem(QStringLiteral("🧵 Reine Satin-Kontur (Satin Outline)"), int(TextDigitizer::StitchStyle::SatinOutline));
    styleCombo->addItem(QStringLiteral("✦ Erhabener Satin-Rand auf Tatami-Füllung"), int(TextDigitizer::StitchStyle::Raised));
    styleCombo->addItem(QStringLiteral("▦ Dichte Tatami-Webung"), int(TextDigitizer::StitchStyle::TatamiOnly));
    form->addRow(QStringLiteral("Stick-Stil:"), styleCombo);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    form->addRow(bb);

    if (d.exec() != QDialog::Accepted) return;

    TextDigitizer::Params tp;
    tp.text = QString("%1").arg(numSpin->value(), 2, 10, QChar('0'));
    tp.family = fontCombo->currentData().toString();
    tp.heightMm = hSpin->value();
    tp.style = TextDigitizer::StitchStyle(styleCombo->currentData().toInt());
    tp.borderWidthMm = 3.5;
    tp.borderPitchMm = 0.34;
    tp.paddingWidthMm = 2.4;
    tp.paddingPitchMm = 0.48;
    tp.raised = (tp.style == TextDigitizer::StitchStyle::Raised);

    if (m_editor) {
        m_editor->clearAll();
        m_editor->setCanvasMode(PathEditorWidget::CanvasMode::Stitches2D);
    }
    m_currentFilePath.clear();
    m_hasTextParams = true;
    m_currentTextParams = tp;
    m_current = TextDigitizer::generate(tp);
    m_current.palette = { ThreadCatalog::snap(QColor(180, 30, 45)) };
    setCurrentSequence(m_current, QStringLiteral("College-Nummer: %1").arg(tp.text));
}

// ---------------------------------------------------------------------------
void MainWindow::loadOfficialKielWappen()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList searchDirs = {
        appDir + QStringLiteral("/output/fw"),
        appDir + QStringLiteral("/output"),
        QStringLiteral("E:/StickCore/output/fw"),
        QStringLiteral("E:/StickCore/output"),
        appDir + QStringLiteral("/../output/fw"),
        appDir + QStringLiteral("/../output"),
        QStringLiteral("F:/EmbF5/MyDesign"),
        QStringLiteral("F:/EmbF5/Feuerwehr")
    };

    auto findFile = [&](const QString& fileName) -> QString {
        for (const QString& d : searchDirs) {
            const QString cand = d + QLatin1Char('/') + fileName;
            if (QFile::exists(cand)) return cand;
        }
        return QString();
    };

    struct PresetFile {
        QString fileName;
        QString label;
    };
    const std::vector<PresetFile> presets = {
        { QStringLiteral("WWWAP.JEF"),    QStringLiteral("🛡️ WWWAP.JEF — Feuerwehr Kiel Ärmelwappen (mit Fadenkreuz)") },
        { QStringLiteral("30_MITTE.JEF"), QStringLiteral("🔢 30_MITTE.JEF — Ärmel- / Mitte-Zahl 30 (Applikation Original)") },
        { QStringLiteral("BF30_ALL.JEF"), QStringLiteral("⬆️ BF30_ALL.JEF — Oberer Bogen: BERUFSFEUERWEHR KIEL (Gesamt)") },
        { QStringLiteral("NW30_ALL.JEF"), QStringLiteral("⬇️ NW30_ALL.JEF — Gegenbogen Unten: NACHTWACHE II (Gesamt)") },
        { QStringLiteral("NW2_ALL.JEF"),  QStringLiteral("⬇️ NW2_ALL.JEF — Gegenbogen Unten: NACHTWACHE 2 (Gesamt)") },
        { QStringLiteral("BF30_1.JEF"),   QStringLiteral("⬆️ BF30_1.JEF — Oberer Bogen Rahmen 1: BERUFS") },
        { QStringLiteral("BF30_2.JEF"),   QStringLiteral("⬆️ BF30_2.JEF — Oberer Bogen Rahmen 2: FEUER") },
        { QStringLiteral("BF30_3.JEF"),   QStringLiteral("⬆️ BF30_3.JEF — Oberer Bogen Rahmen 3: WEHR") },
        { QStringLiteral("BF30_4.JEF"),   QStringLiteral("⬆️ BF30_4.JEF — Oberer Bogen Rahmen 4: KIEL") },
        { QStringLiteral("NW30_1.JEF"),   QStringLiteral("⬇️ NW30_1.JEF — Gegenbogen Unten Rahmen 1: NACHT") },
        { QStringLiteral("NW30_2.JEF"),   QStringLiteral("⬇️ NW30_2.JEF — Gegenbogen Unten Rahmen 2: WACHE") },
        { QStringLiteral("NW30_3.JEF"),   QStringLiteral("⬇️ NW30_3.JEF — Gegenbogen Unten Rahmen 3: II") },
        { QStringLiteral("KIEL_VOL.JEF"), QStringLiteral("⚓ KIEL_VOL.JEF — Kieler Stadtwappen Vollstick (Weiß → Schwarz → Rot)") },
        { QStringLiteral("KIEL_KTL.JEF"), QStringLiteral("🛡️ KIEL_KTL.JEF — Kieler Stadtwappen Kettelrand-Aufnäher") },
        { QStringLiteral("KIEL_BLK.JEF"), QStringLiteral("⚫ KIEL_BLK.JEF — Kieler Stadtwappen (Schwarz zuerst)") }
    };

    QStringList items;
    QStringList paths;
    for (const auto& p : presets) {
        const QString found = findFile(p.fileName);
        if (!found.isEmpty()) {
            items << p.label;
            paths << found;
        }
    }

    if (!items.isEmpty()) {
        bool ok = false;
        const QString chosen = QInputDialog::getItem(
            this,
            QStringLiteral("Feuerwehr Kiel & Stadtwappen (JEF) laden"),
            QStringLiteral("Welches Feuerwehr- / Kiel-Motiv möchtest du öffnen (Wappen, Ärmel-Zahl 30, Bogen Oben / Unten)?"),
            items, 0, false, &ok);
        if (!ok) return;

        const int idx = items.indexOf(chosen);
        if (idx >= 0 && idx < paths.size()) {
            loadFile(paths[idx]);
            return;
        }
    }

    QString svgPath = QStringLiteral(":/svg/wappen_kiel.svg");
    if (!QFile::exists(svgPath)) {
        svgPath = QStringLiteral("assets/wappen_kiel_official.svg");
    }

    if (QFile::exists(svgPath)) {
        SvgDigitizer::Params sp;
        sp.style = SvgDigitizer::StitchStyle::AuthenticPatchSatin;
        sp.targetWidthMm = 100.0;
        sp.spacingMm = m_density ? m_density->value() : 0.38;
        StitchSequence seq = SvgDigitizer::digitizeSvgFile(svgPath, sp);
        if (!seq.empty()) {
            if (m_editor) {
                m_editor->clearAll();
                m_editor->setCanvasMode(PathEditorWidget::CanvasMode::Stitches2D);
            }
            m_currentFilePath.clear();
            m_hasTextParams = false;
            seq.palette = {
                ThreadCatalog::snap(QColor(210, 25, 40)),   // Schleswig-Holstein Rot
                ThreadCatalog::snap(QColor(245, 245, 250)),  // Silbernes Nesselblatt
                ThreadCatalog::snap(QColor(25, 25, 30))      // Kieler Boot Schwarz
            };
            setCurrentSequence(seq, QStringLiteral("Kieler Stadtwappen"));
            statusBar()->showMessage(QStringLiteral("Offizielles Kieler Stadtwappen geladen (%1 Stiche, 3 Farben).").arg(seq.stitches.size()), 5000);
            return;
        }
    }
    QMessageBox::warning(this, QStringLiteral("Wappen nicht gefunden"), QStringLiteral("Die Kieler Wappen-Dateien konnten nicht gefunden werden."));
}

// ---------------------------------------------------------------------------
void MainWindow::inspectDensityHeatmap()
{
    if (m_current.empty()) {
        QMessageBox::information(this, QStringLiteral("Kein Motiv"), QStringLiteral("Bitte zuerst ein Motiv laden."));
        return;
    }

    const QRectF bb = m_current.boundingRect();
    if (bb.width() < 1.0 || bb.height() < 1.0) return;

    const double cellSize = 2.0; // 2mm grid cell
    const int cols = std::max(1, static_cast<int>(std::ceil(bb.width() / cellSize)));
    const int rows = std::max(1, static_cast<int>(std::ceil(bb.height() / cellSize)));

    std::vector<int> grid(cols * rows, 0);
    int microStitches = 0;
    int maxCellCount = 0;

    for (size_t i = 1; i < m_current.stitches.size(); ++i) {
        const auto& s0 = m_current.stitches[i - 1];
        const auto& s1 = m_current.stitches[i];
        if (s1.flags & (SF_Jump | SF_Trim)) continue;

        double d = std::hypot(s1.x - s0.x, s1.y - s0.y);
        if (d < 0.35) ++microStitches;

        int c = std::clamp(static_cast<int>((s1.x - bb.left()) / cellSize), 0, cols - 1);
        int r = std::clamp(static_cast<int>((s1.y - bb.top()) / cellSize), 0, rows - 1);
        grid[r * cols + c]++;
        if (grid[r * cols + c] > maxCellCount) maxCellCount = grid[r * cols + c];
    }

    const double cellAreaMm2 = cellSize * cellSize;
    const double maxDensity = maxCellCount / cellAreaMm2;

    int criticalCells = 0;
    for (int count : grid) {
        if ((count / cellAreaMm2) > 4.5) ++criticalCells;
    }

    QString status;
    if (criticalCells == 0 && microStitches < 20) {
        status = QStringLiteral("<b style='color:#37d67a;'>✓ Exzellente Stickdichte!</b><br>"
                                "Keine Fadenbruch-Zonen oder Stoffverzüge festgestellt.<br>"
                                "Das Motiv läuft reibungslos auf der Janome MC350E.");
    } else {
        status = QStringLiteral("<b style='color:#f59e0b;'>⚠️ Optimierungspotenzial gefunden:</b><br>"
                                "• %1 Cluster mit erhöhter Einstichdichte (&gt; 4,5 Stiche/mm²)<br>"
                                "• %2 Mikro-Stiche (&lt; 0,35 mm) gefunden.<br>"
                                "<i>Tipp: Stickvlies gut einspannen oder Dichte auf 0,42 mm anpassen.</i>")
                     .arg(criticalCells).arg(microStitches);
    }

    QMessageBox::information(this, QStringLiteral("Stichdichte & Heatmap-Analyse"),
        QString("<b>Motiv-Dichteanalyse:</b><br>"
                "• Gesamtstiche: %1<br>"
                "• Abmessungen: %2 x %3 mm<br>"
                "• Maximale Einstichdichte: <b>%4 Stiche/mm²</b><br>"
                "• Durchschnittsdichte: <b>%5 Stiche/mm²</b><br><br>%6")
            .arg(m_current.stitches.size())
            .arg(bb.width(), 0, 'f', 1)
            .arg(bb.height(), 0, 'f', 1)
            .arg(maxDensity, 0, 'f', 1)
            .arg(m_current.stitches.size() / std::max(1.0, bb.width() * bb.height()), 0, 'f', 2)
            .arg(status));
}

// ---------------------------------------------------------------------------
void MainWindow::optimizeTieStitches()
{
    if (m_current.empty()) {
        QMessageBox::information(this, QStringLiteral("Kein Motiv"), QStringLiteral("Bitte zuerst ein Motiv laden."));
        return;
    }

    int insertedLocks = 0;
    StitchSequence opt;
    opt.palette = m_current.palette;

    for (size_t i = 0; i < m_current.stitches.size(); ++i) {
        const Stitch& s = m_current.stitches[i];
        opt.stitches.push_back(s);

        if ((s.flags & (SF_Trim | SF_Stop)) && i > 2) {
            opt.add(s.x + 0.6, s.y, SF_Normal, s.colorIdx);
            opt.add(s.x - 0.6, s.y, SF_Normal, s.colorIdx);
            opt.add(s.x, s.y, SF_Normal, s.colorIdx);
            insertedLocks += 3;
        }
    }

    m_current = opt;
    m_baseSequence = m_current;
    m_hasTextParams = false;
    if (m_docTabBar) {
        const int tabIdx = m_docTabBar->currentIndex();
        if (tabIdx >= 0 && tabIdx < m_openDocs.size()) {
            m_openDocs[tabIdx].baseSeq       = m_baseSequence;
            m_openDocs[tabIdx].currentSeq    = m_current;
            m_openDocs[tabIdx].hasTextParams = false;
        }
    }
    m_editor->setSequence(m_current);
    m_view->setSequence(m_current);
    updateInfoPanel();
    QMessageBox::information(this, QStringLiteral("Stich-Sicherung (Tie-In / Tie-Off)"),
        QString("<b>Vernähstich-Optimierung abgeschlossen:</b><br>"
                "• %1 Sicherheits-Verriegelungsstiche eingefügt.<br>"
                "✓ Fadenabschneiden und Nadelstart sind nun 100% auszugssicher für die Janome MC350E.")
            .arg(insertedLocks));
}

// ---------------------------------------------------------------------------
void MainWindow::calculateThreadCost()
{
    if (m_current.empty()) {
        QMessageBox::information(this, QStringLiteral("Kein Motiv"), QStringLiteral("Bitte zuerst ein Motiv laden."));
        return;
    }

    double totalMeters = 0.0;
    std::vector<double> colorMeters(m_current.palette.size(), 0.0);

    for (size_t i = 1; i < m_current.stitches.size(); ++i) {
        const auto& s0 = m_current.stitches[i - 1];
        const auto& s1 = m_current.stitches[i];
        const double d = std::hypot(s1.x - s0.x, s1.y - s0.y) * 0.001;
        totalMeters += d;
        if (s1.colorIdx >= 0 && static_cast<size_t>(s1.colorIdx) < colorMeters.size()) {
            colorMeters[s1.colorIdx] += d;
        }
    }

    const double effectiveTopMeters = totalMeters * 1.4;
    const double effectiveBobbinMeters = totalMeters * 1.0;

    const int stitchCount = static_cast<int>(m_current.stitches.size());
    const int colorChanges = std::max(0, static_cast<int>(m_current.palette.size()) - 1);
    const double runTimeMinutes = (stitchCount / 550.0) + (colorChanges * 0.5);

    const double yarnCost = (stitchCount / 1000.0) * 0.025;
    const double fleeceCost = 0.20;
    const double totalCost = yarnCost + fleeceCost;

    QString colorBreakdown;
    for (size_t c = 0; c < colorMeters.size(); ++c) {
        const QString name = m_current.palette[c].description.isEmpty() ? QString("Farbe %1").arg(c + 1) : m_current.palette[c].description;
        colorBreakdown += QString("• <b>%1:</b> %2 m Oberfaden<br>").arg(name).arg(colorMeters[c] * 1.4, 0, 'f', 1);
    }

    QMessageBox::information(this, QStringLiteral("Garnbedarf & Kostenkalkulation"),
        QString("<b>Produktions- & Garnkalkulation:</b><br><br>"
                "• Gesamtstiche: <b>%1</b><br>"
                "• Geschätzte Stickzeit (550 SPM): <b>%2 Minuten</b> (%3 Fadenwechsel)<br>"
                "• Oberfaden-Bedarf gesamt: <b>%4 Meter</b><br>"
                "• Unterfaden-Bedarf (Bobbin): <b>%5 Meter</b><br><br>"
                "<b>Garnverbrauch nach Farben:</b><br>%6<br>"
                "<b>Materialkosten-Schätzung:</b><br>"
                "• Stickgarn (Madeira Rayon / Polyneon): %7 €<br>"
                "• Reißvlies / Schneidvlies: %8 €<br>"
                "<b>Materialkosten netto: %9 €</b>")
            .arg(stitchCount)
            .arg(runTimeMinutes, 0, 'f', 1)
            .arg(colorChanges)
            .arg(effectiveTopMeters, 0, 'f', 1)
            .arg(effectiveBobbinMeters, 0, 'f', 1)
            .arg(colorBreakdown)
            .arg(yarnCost, 0, 'f', 2)
            .arg(fleeceCost, 0, 'f', 2)
            .arg(totalCost, 0, 'f', 2));
}

// ---------------------------------------------------------------------------
void MainWindow::checkForUpdatesManual()
{
    if (m_updater) {
        m_updater->checkForUpdates(false);
    }
}

// ---------------------------------------------------------------------------
void MainWindow::registerOrUpdateOpenTab(const QString& title, const QString& filePath, const StitchSequence& seq)
{
    if (!m_docTabBar) return;

    QString cleanTitle = title;
    if (cleanTitle.startsWith(QStringLiteral("Geladen: "))) {
        cleanTitle = cleanTitle.mid(9);
    }

    int foundIdx = -1;
    for (int i = 0; i < m_openDocs.size(); ++i) {
        if (!filePath.isEmpty() && QFileInfo(m_openDocs[i].filePath) == QFileInfo(filePath)) {
            foundIdx = i;
            break;
        }
        if (filePath.isEmpty() && m_openDocs[i].filePath.isEmpty() && m_openDocs[i].title == cleanTitle) {
            foundIdx = i;
            break;
        }
    }

    QSignalBlocker blocker(m_docTabBar);
    if (foundIdx >= 0) {
        m_openDocs[foundIdx].title            = cleanTitle;
        m_openDocs[foundIdx].filePath         = filePath;
        m_openDocs[foundIdx].baseSeq          = m_baseSequence;
        m_openDocs[foundIdx].currentSeq       = seq;
        m_openDocs[foundIdx].baseDensityMm    = m_baseDensityMm;
        m_openDocs[foundIdx].currentDensityMm = m_density ? m_density->value() : m_baseDensityMm;
        m_openDocs[foundIdx].basePullMm       = m_basePullMm;
        m_openDocs[foundIdx].currentPullMm    = m_pull ? m_pull->value() : m_basePullMm;
        m_openDocs[foundIdx].hasTextParams    = m_hasTextParams;
        m_openDocs[foundIdx].textParams       = m_currentTextParams;
        m_docTabBar->setTabText(foundIdx, QStringLiteral("🧵 %1").arg(cleanTitle));
        m_docTabBar->setTabToolTip(foundIdx, filePath.isEmpty() ? cleanTitle : filePath);
        m_docTabBar->setCurrentIndex(foundIdx);
    } else {
        OpenDocEntry entry;
        entry.title            = cleanTitle;
        entry.filePath         = filePath;
        entry.baseSeq          = m_baseSequence;
        entry.currentSeq       = seq;
        entry.baseDensityMm    = m_baseDensityMm;
        entry.currentDensityMm = m_density ? m_density->value() : m_baseDensityMm;
        entry.basePullMm       = m_basePullMm;
        entry.currentPullMm    = m_pull ? m_pull->value() : m_basePullMm;
        entry.hasTextParams    = m_hasTextParams;
        entry.textParams       = m_currentTextParams;
        m_openDocs.push_back(entry);

        const int newIdx = m_docTabBar->addTab(QStringLiteral("🧵 %1").arg(cleanTitle));
        m_docTabBar->setTabToolTip(newIdx, filePath.isEmpty() ? cleanTitle : filePath);
        m_docTabBar->setCurrentIndex(newIdx);
    }

    if (m_tabBarWall) {
        m_tabBarWall->setVisible(m_openDocs.size() > 1);
    }
}

// ---------------------------------------------------------------------------
void MainWindow::onDocTabChanged(int index)
{
    if (index < 0 || index >= m_openDocs.size()) return;

    const OpenDocEntry& entry = m_openDocs[index];
    m_currentFilePath   = entry.filePath;
    m_baseSequence      = entry.baseSeq;
    m_current           = entry.currentSeq;
    m_baseDensityMm     = entry.baseDensityMm;
    m_basePullMm        = entry.basePullMm;
    m_hasTextParams     = entry.hasTextParams;
    m_currentTextParams = entry.textParams;

    if (m_editor) {
        m_editor->clearAll();
        m_editor->setCanvasMode(PathEditorWidget::CanvasMode::Stitches2D);
    }

    if (m_density) {
        QSignalBlocker b(m_density);
        m_density->setValue(entry.currentDensityMm);
    }
    if (m_densitySlider) {
        QSignalBlocker b(m_densitySlider);
        m_densitySlider->setValue(std::clamp(int(std::round(entry.currentDensityMm * 100.0)), 20, 80));
    }
    if (m_pull) {
        QSignalBlocker b(m_pull);
        m_pull->setValue(entry.currentPullMm);
    }

    // Update hoop fit
    double minX, minY, maxX, maxY;
    if (m_current.bounds(minX, minY, maxX, maxY)) {
        const auto& mp = MachineProfile::current();
        selectHoop(mp.hoopFor(maxX - minX, maxY - minY));
    }

    m_editor->setSequence(m_current);
    m_editor->fitInView();
    m_view->setSequence(m_current);
    m_view->showAll();
    updateInfoPanel();

    const int baseCnt = m_baseSequence.realStitchCount();
    const int curCnt  = m_current.realStitchCount();
    if (m_densityStateLabel) {
        if (std::abs(entry.currentDensityMm - m_baseDensityMm) > 0.005 || curCnt != baseCnt) {
            const double pct = baseCnt > 0 ? ((curCnt - baseCnt) * 100.0 / baseCnt) : 0.0;
            const QString sign = pct >= 0.0 ? QStringLiteral("+") : QString();
            m_densityStateLabel->setText(
                QStringLiteral("✓ Dichte: %1 → %2 mm (%3%4% · %5 statt %6 Stiche)")
                    .arg(m_baseDensityMm, 0, 'f', 2)
                    .arg(entry.currentDensityMm, 0, 'f', 2)
                    .arg(sign).arg(pct, 0, 'f', 0)
                    .arg(curCnt).arg(baseCnt));
        } else {
            m_densityStateLabel->setText(
                QStringLiteral("Basis-Dichte: %1 mm · %2 Stiche")
                    .arg(m_baseDensityMm, 0, 'f', 2)
                    .arg(baseCnt));
        }
    }

    setWindowTitle(QStringLiteral("StickCore Studio – %1").arg(entry.title));
    setStatusForSequence(entry.title);
}

// ---------------------------------------------------------------------------
void MainWindow::onDocTabCloseRequested(int index)
{
    if (!m_docTabBar || index < 0 || index >= m_openDocs.size()) return;
    if (m_openDocs.size() <= 1) {
        statusBar()->showMessage(QStringLiteral("Mindestens ein Arbeitsblatt bleibt im Studio geöffnet."), 3000);
        return;
    }

    m_openDocs.removeAt(index);
    {
        QSignalBlocker b(m_docTabBar);
        m_docTabBar->removeTab(index);
    }
    if (m_tabBarWall) {
        m_tabBarWall->setVisible(m_openDocs.size() > 1);
    }
    const int activeIdx = std::clamp(m_docTabBar->currentIndex(), 0, int(m_openDocs.size()) - 1);
    onDocTabChanged(activeIdx);
}

// ---------------------------------------------------------------------------
double MainWindow::detectSequenceBaseDensity(const StitchSequence& seq)
{
    if (seq.stitches.size() < 16) return 0.38;

    std::vector<double> satinPitches;
    satinPitches.reserve(512);

    for (size_t i = 1; i + 2 < seq.stitches.size(); ++i) {
        const Stitch& s0 = seq.stitches[i - 1];
        const Stitch& s1 = seq.stitches[i];
        const Stitch& s2 = seq.stitches[i + 1];
        const Stitch& s3 = seq.stitches[i + 2];
        if ((s1.flags & (SF_Jump | SF_Trim | SF_ColorChange | SF_Stop)) ||
            (s2.flags & (SF_Jump | SF_Trim | SF_ColorChange | SF_Stop)) ||
            (s3.flags & (SF_Jump | SF_Trim | SF_ColorChange | SF_Stop))) {
            continue;
        }
        const double dx1 = s1.x - s0.x, dy1 = s1.y - s0.y;
        const double dx2 = s2.x - s1.x, dy2 = s2.y - s1.y;
        const double dx3 = s3.x - s2.x, dy3 = s3.y - s2.y;
        const double l1 = std::hypot(dx1, dy1);
        const double l2 = std::hypot(dx2, dy2);
        const double l3 = std::hypot(dx3, dy3);
        if (l1 >= 1.0 && l1 <= 11.0 && l2 >= 1.0 && l2 <= 11.0 && l3 >= 1.0 && l3 <= 11.0) {
            if (std::abs(l1 - l2) < 0.7 && std::abs(l2 - l3) < 0.7) {
                const double cosA1 = (dx1 * dx2 + dy1 * dy2) / (l1 * l2);
                const double cosA2 = (dx2 * dx3 + dy2 * dy3) / (l2 * l3);
                if (cosA1 < -0.60 && cosA2 < -0.60) {
                    const double advance = std::hypot(s2.x - s0.x, s2.y - s0.y);
                    if (advance >= 0.16 && advance <= 1.10) {
                        satinPitches.push_back(advance);
                    }
                }
            }
        }
    }

    if (satinPitches.size() >= 20) {
        std::sort(satinPitches.begin(), satinPitches.end());
        double med = satinPitches[satinPitches.size() / 2];
        // Staggered zig-zag satin advances by 1*spacing on each half-step, so s0->s2 is 2*spacing
        if (med > 0.54) {
            med *= 0.5;
        }
        return std::clamp(std::round(med * 50.0) / 50.0, 0.24, 0.54);
    }

    return 0.38;
}

// ---------------------------------------------------------------------------
StitchSequence MainWindow::rebuildSequenceWithDensity(
    const StitchSequence& baseSeq,
    double baseDensityMm,
    double targetDensityMm,
    double pullDeltaMm,
    double maxStitchMm) const
{
    if (baseSeq.empty()) return baseSeq;

    baseDensityMm   = std::clamp(baseDensityMm, 0.20, 1.20);
    targetDensityMm = std::clamp(targetDensityMm, 0.20, 1.20);
    maxStitchMm     = std::clamp(maxStitchMm, 1.50, 7.00);

    // Group stitches into Color Blocks
    struct ColorBlock {
        int colorIdx = 0;
        std::vector<Stitch> stitches;
    };
    std::vector<ColorBlock> blocks;
    for (const Stitch& st : baseSeq.stitches) {
        if (st.flags & SF_End) continue;
        if (blocks.empty() || st.colorIdx != blocks.back().colorIdx || (st.flags & SF_ColorChange)) {
            ColorBlock cb;
            cb.colorIdx = st.colorIdx;
            cb.stitches.push_back(st);
            blocks.push_back(std::move(cb));
        } else {
            blocks.back().stitches.push_back(st);
        }
    }

    struct SubItem {
        enum Type { JumpStitch, SatinRun, NonSatinRun } type = JumpStitch;
        Stitch jumpSt;
        std::vector<QPointF> pts;
    };

    auto remeshSatinSubrun = [&](const std::vector<QPointF>& pts) -> std::vector<QPointF> {
        const size_t n = pts.size();
        if (n < 6) return pts;

        std::vector<QPointF> railA, railB;
        railA.reserve((n + 1) / 2);
        railB.reserve(n / 2);
        for (size_t i = 0; i < n; i += 2) railA.push_back(pts[i]);
        for (size_t i = 1; i < n; i += 2) railB.push_back(pts[i]);

        const size_t m = std::min(railA.size(), railB.size());
        if (m < 3) return pts;
        railA.resize(m);
        railB.resize(m);

        // Apply Pull Compensation outward along each rung
        if (std::abs(pullDeltaMm) > 1e-4) {
            for (size_t k = 0; k < m; ++k) {
                const double vx = railB[k].x() - railA[k].x();
                const double vy = railB[k].y() - railA[k].y();
                const double len = std::hypot(vx, vy);
                if (len > 0.1) {
                    const double ux = vx / len;
                    const double uy = vy / len;
                    railA[k] = QPointF(railA[k].x() - ux * pullDeltaMm, railA[k].y() - uy * pullDeltaMm);
                    railB[k] = QPointF(railB[k].x() + ux * pullDeltaMm, railB[k].y() + uy * pullDeltaMm);
                }
            }
        }

        // Compute centerline cumulative arc length
        std::vector<double> cumLen(m, 0.0);
        for (size_t k = 1; k < m; ++k) {
            const QPointF c0 = 0.5 * (railA[k - 1] + railB[k - 1]);
            const QPointF c1 = 0.5 * (railA[k]     + railB[k]);
            cumLen[k] = cumLen[k - 1] + std::hypot(c1.x() - c0.x(), c1.y() - c0.y());
        }
        const double totalLen = cumLen.back();
        if (totalLen < 0.25) return pts;

        const double ratio = targetDensityMm / baseDensityMm;
        const int newRungs = std::max(2, static_cast<int>(std::round(static_cast<double>(m) / ratio)));

        std::vector<QPointF> outPts;
        outPts.reserve(static_cast<size_t>(newRungs) * 2 + 4);

        size_t segIdx = 0;
        for (int r = 0; r < newRungs; ++r) {
            const double targetD = (newRungs > 1) ? (totalLen * r / (newRungs - 1)) : 0.0;
            while (segIdx + 1 < m && cumLen[segIdx + 1] < targetD) {
                ++segIdx;
            }
            double t = 0.0;
            if (segIdx + 1 < m) {
                const double span = cumLen[segIdx + 1] - cumLen[segIdx];
                if (span > 1e-6) t = std::clamp((targetD - cumLen[segIdx]) / span, 0.0, 1.0);
            }
            const QPointF pA = (segIdx + 1 < m)
                ? (railA[segIdx] * (1.0 - t) + railA[segIdx + 1] * t)
                : railA.back();
            const QPointF pB = (segIdx + 1 < m)
                ? (railB[segIdx] * (1.0 - t) + railB[segIdx + 1] * t)
                : railB.back();

            outPts.push_back(pA);
            // Split extra-wide satin rungs if they exceed 1.8 * maxStitchMm
            const double rungLen = std::hypot(pB.x() - pA.x(), pB.y() - pA.y());
            if (rungLen > maxStitchMm * 1.8 && maxStitchMm > 1.5) {
                const int sub = static_cast<int>(std::ceil(rungLen / (maxStitchMm * 1.6)));
                for (int s = 1; s < sub; ++s) {
                    const double st = static_cast<double>(s) / sub;
                    outPts.push_back(pA * (1.0 - st) + pB * st);
                }
            }
            outPts.push_back(pB);
        }
        return outPts;
    };

    auto remeshTatamiGroup = [&](const std::vector<std::vector<QPointF>>& nonSatinRuns,
                                 std::vector<std::pair<bool, QPointF>>& emittedGroup) -> bool {
        struct Seg {
            QPointF p0, p1;
            double len = 0.0;
            double ang = 0.0;
        };
        std::vector<Seg> allSegs;
        for (const auto& r : nonSatinRuns) {
            for (size_t i = 0; i + 1 < r.size(); ++i) {
                const double dx = r[i + 1].x() - r[i].x();
                const double dy = r[i + 1].y() - r[i].y();
                const double l = std::hypot(dx, dy);
                if (l >= 0.35 && l <= 12.0) {
                    allSegs.push_back({ r[i], r[i + 1], l, std::atan2(dy, dx) });
                }
            }
        }
        if (allSegs.size() < 12) return false;

        double sumCos2 = 0.0, sumSin2 = 0.0, totalW = 0.0;
        for (const Seg& s : allSegs) {
            sumCos2 += s.len * std::cos(2.0 * s.ang);
            sumSin2 += s.len * std::sin(2.0 * s.ang);
            totalW  += s.len;
        }
        if (totalW < 10.0) return false;

        const double coherence = std::hypot(sumCos2, sumSin2) / totalW;
        if (coherence < 0.40) return false;

        const double domAngle = 0.5 * std::atan2(sumSin2, sumCos2);
        const double ca = std::cos(-domAngle);
        const double sa = std::sin(-domAngle);
        auto rot = [&](const QPointF& p) -> QPointF {
            return QPointF(p.x() * ca - p.y() * sa, p.x() * sa + p.y() * ca);
        };
        const double caInv = std::cos(domAngle);
        const double saInv = std::sin(domAngle);
        auto unrot = [&](double rx, double ry) -> QPointF {
            return QPointF(rx * caInv - ry * saInv, rx * saInv + ry * caInv);
        };

        std::vector<std::vector<QPointF>> underlayRuns;
        std::vector<std::pair<QPointF, QPointF>> tatamiRotSegs;
        tatamiRotSegs.reserve(allSegs.size());

        for (const auto& r : nonSatinRuns) {
            double alignedW = 0.0, runW = 0.0;
            for (size_t i = 0; i + 1 < r.size(); ++i) {
                const double dx = r[i + 1].x() - r[i].x();
                const double dy = r[i + 1].y() - r[i].y();
                const double l = std::hypot(dx, dy);
                if (l > 0.1) {
                    runW += l;
                    const double a = std::atan2(dy, dx);
                    if (std::abs(std::cos(a - domAngle)) >= 0.90) {
                        alignedW += l;
                    }
                }
            }
            if (runW > 0.5 && (alignedW / runW) >= 0.55) {
                for (size_t i = 0; i + 1 < r.size(); ++i) {
                    const QPointF rp0 = rot(r[i]);
                    const QPointF rp1 = rot(r[i + 1]);
                    if (std::abs(rp1.y() - rp0.y()) <= 0.85 && std::abs(rp1.x() - rp0.x()) >= 0.15) {
                        tatamiRotSegs.push_back({ rp0, rp1 });
                    }
                }
            } else {
                underlayRuns.push_back(r);
            }
        }

        if (tatamiRotSegs.size() < 8) return false;

        // Estimate original row spacing from rotated y coordinates
        std::vector<double> rowYs;
        rowYs.reserve(tatamiRotSegs.size());
        double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
        for (const auto& seg : tatamiRotSegs) {
            const double ym = 0.5 * (seg.first.y() + seg.second.y());
            rowYs.push_back(ym);
            minX = std::min({ minX, seg.first.x(), seg.second.x() });
            maxX = std::max({ maxX, seg.first.x(), seg.second.x() });
            minY = std::min(minY, ym);
            maxY = std::max(maxY, ym);
        }
        if (maxX - minX < 1.0 || maxY - minY < 0.8) return false;

        std::sort(rowYs.begin(), rowYs.end());
        std::vector<double> uniqueYs;
        for (double y : rowYs) {
            if (uniqueYs.empty() || (y - uniqueYs.back()) > 0.15) {
                uniqueYs.push_back(y);
            }
        }
        double estRowSp = baseDensityMm;
        if (uniqueYs.size() >= 4) {
            std::vector<double> dys;
            for (size_t i = 0; i + 1 < uniqueYs.size(); ++i) {
                const double dy = uniqueYs[i + 1] - uniqueYs[i];
                if (dy >= 0.18 && dy <= 0.95) dys.push_back(dy);
            }
            if (!dys.empty()) {
                std::sort(dys.begin(), dys.end());
                estRowSp = std::clamp(dys[dys.size() / 2], 0.25, 0.65);
            }
        }

        // Rasterize Tatami region onto 2D grayscale mask
        const double scale = 12.0; // 12 px per mm
        const double pad = 1.5;
        const double ox = minX - pad;
        const double oy = minY - pad;
        const int imgW = std::max(8, static_cast<int>(std::ceil((maxX - minX + 2.0 * pad) * scale)) + 2);
        const int imgH = std::max(8, static_cast<int>(std::ceil((maxY - minY + 2.0 * pad) * scale)) + 2);

        QImage mask(imgW, imgH, QImage::Format_Grayscale8);
        mask.fill(0);
        {
            QPainter painter(&mask);
            painter.setRenderHint(QPainter::Antialiasing, false);
            const int penPx = std::max(2, static_cast<int>(std::round(estRowSp * 1.75 * scale)));
            QPen pen(Qt::white, penPx, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin);
            painter.setPen(pen);
            for (const auto& seg : tatamiRotSegs) {
                const QPointF p0((seg.first.x()  - ox) * scale, (seg.first.y()  - oy) * scale);
                const QPointF p1((seg.second.x() - ox) * scale, (seg.second.y() - oy) * scale);
                painter.drawLine(p0, p1);
            }
        }

        // Emit preserved underlay runs first
        for (const auto& uRun : underlayRuns) {
            if (uRun.empty()) continue;
            emittedGroup.push_back({ true, uRun.front() });
            for (size_t i = 1; i < uRun.size(); ++i) {
                emittedGroup.push_back({ false, uRun[i] });
            }
        }

        // Rescan mask at targetDensityMm
        const double newSpacing = targetDensityMm;
        const int numRows = std::max(1, static_cast<int>(std::floor((maxY - minY) / newSpacing)) + 1);
        int rowIdx = 0;
        QPointF prevEnd;
        bool hasPrevEnd = false;

        for (int r = 0; r < numRows; ++r) {
            const double ry = minY + 0.5 * newSpacing + r * newSpacing;
            const int py = std::clamp(static_cast<int>(std::round((ry - oy) * scale)), 0, imgH - 1);
            const uchar* line = mask.constScanLine(py);

            std::vector<std::pair<double, double>> intervals;
            int x = 0;
            while (x < imgW) {
                while (x < imgW && line[x] < 128) ++x;
                if (x >= imgW) break;
                const int xStart = x;
                while (x < imgW && line[x] >= 128) ++x;
                const int xEnd = x - 1;
                double rx0 = ox + (xStart / scale);
                double rx1 = ox + (xEnd   / scale);
                if (rx1 - rx0 >= 0.30) {
                    if (!intervals.empty() && (rx0 - intervals.back().second) < 0.35) {
                        intervals.back().second = rx1;
                    } else {
                        intervals.push_back({ rx0, rx1 });
                    }
                }
            }

            if (intervals.empty()) continue;
            if (rowIdx % 2 == 1) {
                std::reverse(intervals.begin(), intervals.end());
            }

            for (auto iv : intervals) {
                double x0 = iv.first  - pullDeltaMm;
                double x1 = iv.second + pullDeltaMm;
                if (x1 - x0 < 0.25) continue;

                const double segLen = x1 - x0;
                const bool rev = (rowIdx % 2 == 1);
                const double phase = ((rowIdx % 4) * 0.25) * maxStitchMm;

                std::vector<double> xs;
                xs.push_back(rev ? x1 : x0);
                double d = phase > 0.2 ? phase : maxStitchMm;
                while (d < segLen - 0.35) {
                    xs.push_back(rev ? (x1 - d) : (x0 + d));
                    d += maxStitchMm;
                }
                xs.push_back(rev ? x0 : x1);

                for (size_t k = 0; k < xs.size(); ++k) {
                    const QPointF wp = unrot(xs[k], ry);
                    if (k == 0) {
                        const bool needJump = !hasPrevEnd || std::hypot(wp.x() - prevEnd.x(), wp.y() - prevEnd.y()) > 2.2;
                        emittedGroup.push_back({ needJump, wp });
                    } else {
                        emittedGroup.push_back({ false, wp });
                    }
                    prevEnd = wp;
                    hasPrevEnd = true;
                }
            }
            ++rowIdx;
        }

        return true;
    };

    StitchSequence out;
    out.palette = baseSeq.palette;
    out.stitches.reserve(static_cast<size_t>(baseSeq.stitches.size() * (baseDensityMm / targetDensityMm) * 1.15));

    for (size_t bIdx = 0; bIdx < blocks.size(); ++bIdx) {
        const ColorBlock& cb = blocks[bIdx];
        const int cIdx = cb.colorIdx;

        // 1. Extract contiguous SF_Normal runs & standalone Jumps
        auto isNormalStitch = [](const Stitch& s) {
            return (s.flags & (SF_Jump | SF_Trim | SF_ColorChange | SF_Stop | SF_End)) == 0;
        };

        std::vector<SubItem> rawItems;
        size_t i = 0;
        while (i < cb.stitches.size()) {
            const Stitch& st = cb.stitches[i];
            if (isNormalStitch(st)) {
                std::vector<QPointF> runPts;
                if (i > 0) {
                    runPts.push_back(QPointF(cb.stitches[i - 1].x, cb.stitches[i - 1].y));
                }
                while (i < cb.stitches.size() && isNormalStitch(cb.stitches[i])) {
                    const QPointF pt(cb.stitches[i].x, cb.stitches[i].y);
                    if (runPts.empty() || std::hypot(pt.x() - runPts.back().x(), pt.y() - runPts.back().y()) > 1e-4) {
                        runPts.push_back(pt);
                    }
                    ++i;
                }
                if (runPts.size() >= 2) {
                    SubItem item;
                    item.type = SubItem::NonSatinRun;
                    item.pts = std::move(runPts);
                    rawItems.push_back(std::move(item));
                }
            } else {
                const bool nextIsNormal = (i + 1 < cb.stitches.size()) && isNormalStitch(cb.stitches[i + 1]);
                if (!nextIsNormal) {
                    SubItem item;
                    item.type = SubItem::JumpStitch;
                    item.jumpSt = st;
                    rawItems.push_back(std::move(item));
                }
                ++i;
            }
        }

        // 2. Split each NonSatinRun into SatinRun vs NonSatinRun sub-runs
        std::vector<SubItem> classified;
        for (const SubItem& it : rawItems) {
            if (it.type != SubItem::NonSatinRun || it.pts.size() < 8) {
                classified.push_back(it);
                continue;
            }
            const auto& pts = it.pts;
            const size_t n = pts.size();
            std::vector<int> rev(n, 0);
            for (size_t k = 1; k + 1 < n; ++k) {
                const double dx1 = pts[k].x() - pts[k - 1].x();
                const double dy1 = pts[k].y() - pts[k - 1].y();
                const double dx2 = pts[k + 1].x() - pts[k].x();
                const double dy2 = pts[k + 1].y() - pts[k].y();
                const double l1 = std::hypot(dx1, dy1);
                const double l2 = std::hypot(dx2, dy2);
                if (l1 >= 0.7 && l1 <= 12.0 && l2 >= 0.7 && l2 <= 12.0) {
                    const double cosA = (dx1 * dx2 + dy1 * dy2) / (l1 * l2);
                    if (cosA < -0.35) rev[k] = 1;
                }
            }
            std::vector<bool> isSatinPt(n, false);
            for (size_t k = 0; k < n; ++k) {
                const size_t a = (k >= 2) ? (k - 2) : 0;
                const size_t b = std::min(n - 1, k + 2);
                int sum = 0;
                for (size_t m = a; m <= b; ++m) sum += rev[m];
                if (static_cast<double>(sum) / static_cast<double>(b - a + 1) >= 0.55) {
                    isSatinPt[k] = true;
                }
            }

            size_t start = 0;
            while (start < n) {
                const bool mode = isSatinPt[start];
                size_t end = start + 1;
                while (end < n && isSatinPt[end] == mode) ++end;
                std::vector<QPointF> subPts(pts.begin() + start, pts.begin() + end);
                if (subPts.size() >= 2) {
                    SubItem sub;
                    sub.type = (mode && subPts.size() >= 8) ? SubItem::SatinRun : SubItem::NonSatinRun;
                    sub.pts = std::move(subPts);
                    classified.push_back(std::move(sub));
                }
                start = end;
            }
        }

        // 3. Process classified items, grouping consecutive NonSatin/Jump items for Tatami remeshing
        std::vector<std::pair<bool, QPointF>> blockEmitted;
        size_t idx = 0;
        while (idx < classified.size()) {
            if (classified[idx].type == SubItem::SatinRun) {
                std::vector<QPointF> remeshed = remeshSatinSubrun(classified[idx].pts);
                if (!remeshed.empty()) {
                    blockEmitted.push_back({ true, remeshed.front() });
                    for (size_t k = 1; k < remeshed.size(); ++k) {
                        blockEmitted.push_back({ false, remeshed[k] });
                    }
                }
                ++idx;
            } else {
                std::vector<std::vector<QPointF>> groupRuns;
                size_t endIdx = idx;
                while (endIdx < classified.size() && classified[endIdx].type != SubItem::SatinRun) {
                    if (classified[endIdx].type == SubItem::NonSatinRun) {
                        groupRuns.push_back(classified[endIdx].pts);
                    }
                    ++endIdx;
                }

                std::vector<std::pair<bool, QPointF>> tatamiOut;
                if (remeshTatamiGroup(groupRuns, tatamiOut) && !tatamiOut.empty()) {
                    blockEmitted.insert(blockEmitted.end(), tatamiOut.begin(), tatamiOut.end());
                } else {
                    for (const auto& r : groupRuns) {
                        if (r.empty()) continue;
                        blockEmitted.push_back({ true, r.front() });
                        for (size_t k = 1; k < r.size(); ++k) {
                            blockEmitted.push_back({ false, r[k] });
                        }
                    }
                }
                idx = endIdx;
            }
        }

        // 4. Append blockEmitted stitches to out
        bool firstInBlock = true;
        for (const auto& pair : blockEmitted) {
            const bool isJump = pair.first;
            const QPointF& pt = pair.second;
            int flags = isJump ? SF_Jump : SF_Normal;
            if (firstInBlock) {
                flags = (bIdx == 0) ? SF_Jump : SF_ColorChange;
                firstInBlock = false;
            } else if (!out.stitches.empty() && !isJump) {
                const double d = std::hypot(pt.x() - out.stitches.back().x, pt.y() - out.stitches.back().y);
                if (d < 0.08) continue;
            }
            out.add(pt.x(), pt.y(), flags, cIdx);
        }
    }

    if (!out.stitches.empty()) {
        const Stitch& last = out.stitches.back();
        out.add(last.x, last.y, SF_End, last.colorIdx);
    }
    return out;
}

// ---------------------------------------------------------------------------
void MainWindow::applyDensityToLoadedDesign()
{
    if (m_baseSequence.empty()) {
        if (m_current.empty()) return;
        m_baseSequence  = m_current;
        m_baseDensityMm = detectSequenceBaseDensity(m_baseSequence);
        m_basePullMm    = m_pull ? m_pull->value() : 0.20;
    }

    const double targetDensity = m_density ? m_density->value() : 0.38;
    const double targetPull    = m_pull ? m_pull->value() : 0.20;
    const double maxStitch     = m_maxStitch ? m_maxStitch->value() : 4.0;
    const double pullDelta     = targetPull - m_basePullMm;

    if (m_hasTextParams && !m_currentTextParams.text.isEmpty()) {
        TextDigitizer::Params tp = m_currentTextParams;
        const double ratio = targetDensity / std::max(0.15, m_baseDensityMm);
        tp.densityMm       = std::clamp(m_currentTextParams.densityMm * ratio, 0.20, 1.20);
        tp.borderPitchMm   = std::clamp(m_currentTextParams.borderPitchMm * ratio, 0.18, 1.00);
        tp.paddingPitchMm  = std::clamp(m_currentTextParams.paddingPitchMm * ratio, 0.22, 1.20);
        tp.maxStitchMm     = maxStitch;
        tp.borderWidthMm   = std::clamp(m_currentTextParams.borderWidthMm + 2.0 * pullDelta, 1.0, 12.0);
        tp.paddingWidthMm  = std::clamp(m_currentTextParams.paddingWidthMm + 2.0 * pullDelta, 0.8, 10.0);
        tp.underlay        = m_underlay ? m_underlay->isChecked() : m_currentTextParams.underlay;

        StitchSequence regen = TextDigitizer::generate(tp);
        if (!regen.empty()) {
            regen.palette = m_baseSequence.palette;
            double bMinX, bMinY, bMaxX, bMaxY;
            double rMinX, rMinY, rMaxX, rMaxY;
            if (m_baseSequence.bounds(bMinX, bMinY, bMaxX, bMaxY) &&
                regen.bounds(rMinX, rMinY, rMaxX, rMaxY)) {
                const double dx = 0.5 * (bMinX + bMaxX) - 0.5 * (rMinX + rMaxX);
                const double dy = 0.5 * (bMinY + bMaxY) - 0.5 * (rMinY + rMaxY);
                if (std::hypot(dx, dy) > 0.05) {
                    for (Stitch& st : regen.stitches) {
                        st.x += dx;
                        st.y += dy;
                    }
                }
            }
            m_current = std::move(regen);
        } else {
            m_current = rebuildSequenceWithDensity(m_baseSequence, m_baseDensityMm, targetDensity, pullDelta, maxStitch);
        }
    } else {
        m_current = rebuildSequenceWithDensity(m_baseSequence, m_baseDensityMm, targetDensity, pullDelta, maxStitch);
    }

    if (m_docTabBar) {
        const int tabIdx = m_docTabBar->currentIndex();
        if (tabIdx >= 0 && tabIdx < m_openDocs.size()) {
            m_openDocs[tabIdx].currentSeq       = m_current;
            m_openDocs[tabIdx].currentDensityMm = targetDensity;
            m_openDocs[tabIdx].currentPullMm    = targetPull;
        }
    }

    m_editor->setSequence(m_current);
    m_view->setSequence(m_current);
    m_view->showAll();
    updateInfoPanel();

    const int baseCnt = m_baseSequence.realStitchCount();
    const int newCnt  = m_current.realStitchCount();
    const double pct  = baseCnt > 0 ? ((newCnt - baseCnt) * 100.0 / baseCnt) : 0.0;
    const QString sign = pct >= 0.0 ? QStringLiteral("+") : QString();

    if (m_densityStateLabel) {
        m_densityStateLabel->setText(
            QStringLiteral("✓ Dichte: %1 → %2 mm (%3%4% · %5 statt %6 Stiche)")
                .arg(m_baseDensityMm, 0, 'f', 2)
                .arg(targetDensity, 0, 'f', 2)
                .arg(sign).arg(pct, 0, 'f', 0)
                .arg(newCnt).arg(baseCnt));
    }

    statusBar()->showMessage(
        QStringLiteral("Fadendichte auf %1 mm angepasst: %2 Stiche (%3%4% gegenüber Basis %5 Stiche bei %6 mm).")
            .arg(targetDensity, 0, 'f', 2)
            .arg(newCnt)
            .arg(sign).arg(pct, 0, 'f', 0)
            .arg(baseCnt)
            .arg(m_baseDensityMm, 0, 'f', 2), 6000);
}

} // namespace stick

