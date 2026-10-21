// ---------------------------------------------------------------------------
//  StickCore  –  LibraryDialog.cpp
// ---------------------------------------------------------------------------
#include "library/LibraryDialog.h"
#include "library/DesignLibrary.h"
#include "library/LibraryBuilder.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLabel>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QIcon>
#include <QPixmap>
#include <algorithm>

namespace stick {

namespace {

QString hoopFitText(double wMm, double hMm)
{
    if (wMm <= 0.0 || hMm <= 0.0) return QString();
    const double minD = std::min(wMm, hMm);
    const double maxD = std::max(wMm, hMm);

    if (maxD <= 50.0)
        return QStringLiteral("✅ Freiarm-Rahmen C (50×50 mm)");
    if (minD <= 110.0 && maxD <= 126.0)
        return QStringLiteral("✅ Janome Rahmen A (126×110 mm)");
    if (minD <= 140.0 && maxD <= 200.0)
        return QStringLiteral("✅ Janome Rahmen B (140×200 mm)");
    if (minD <= 200.0 && maxD <= 230.0)
        return QStringLiteral("ℹ️ Janome Giga-Rahmen (230×200 mm)");
    return QStringLiteral("⚠️ Größer als Janome MC350E Rahmen B");
}

} // namespace

LibraryDialog::LibraryDialog(DesignLibrary* lib,
                             const QString& initialCategory,
                             QWidget* parent)
    : QDialog(parent), m_lib(lib)
{
    setWindowTitle(QStringLiteral("StickCore Motiv-Bibliothek"));
    resize(1000, 680);
    setStyleSheet(QStringLiteral(
        "QDialog { background: #13151c; color: #e2e8f0; font-family: 'Segoe UI', sans-serif; }"
        "QLineEdit, QComboBox { background: #1b1e2a; color: #f8fafc; border: 1px solid #2d3345; border-radius: 6px; padding: 6px 10px; font-size: 13px; }"
        "QLineEdit:focus, QComboBox:focus { border: 1px solid #00c8b3; }"
        "QListWidget { background: #171922; border: 1px solid #232734; border-radius: 8px; padding: 8px; }"
        "QListWidget::item { background: #1d212d; border: 1px solid #292f40; border-radius: 6px; margin: 4px; padding: 6px; color: #cbd5e1; }"
        "QListWidget::item:hover { background: #242a3a; border-color: #3b445c; color: #ffffff; }"
        "QListWidget::item:selected { background: #16363a; border-color: #00c8b3; color: #00f2d8; font-weight: bold; }"
        "QPushButton { background: #232734; color: #f1f5f9; border: 1px solid #333a4c; border-radius: 6px; padding: 7px 16px; font-weight: 500; font-size: 13px; }"
        "QPushButton:hover { background: #2c3242; border-color: #454e66; }"
        "QPushButton#primaryBtn { background: #008f80; color: #ffffff; border: 1px solid #00c8b3; font-weight: 600; }"
        "QPushButton#primaryBtn:hover { background: #00b4a1; }"
    ));

    auto* root = new QVBoxLayout(this);
    root->setSpacing(10);
    root->setContentsMargins(14, 14, 14, 14);

    // --- filter bar ---
    auto* bar = new QHBoxLayout;
    bar->setSpacing(8);

    m_search = new QLineEdit;
    m_search->setPlaceholderText(QStringLiteral("🔍 Suchen … (z. B. „A\", „College\", „Feuerwehr\", „2in\", „Wappen\")"));
    m_search->setClearButtonEnabled(true);
    bar->addWidget(m_search, 4);

    m_cat = new QComboBox;
    m_cat->addItem(QStringLiteral("Alle Kategorien"));
    m_cat->addItems(m_lib->categoriesPresent());
    if (!initialCategory.isEmpty()) {
        int idx = m_cat->findText(initialCategory);
        if (idx >= 0) m_cat->setCurrentIndex(idx);
    }
    bar->addWidget(m_cat, 3);

    m_size = new QComboBox;
    m_size->addItem(QStringLiteral("Alle Größen / Auflösungen"), QStringLiteral(""));
    m_size->addItem(QStringLiteral("1\" (25 mm)"), QStringLiteral("1in"));
    m_size->addItem(QStringLiteral("1.25\" (32 mm)"), QStringLiteral("1.25in"));
    m_size->addItem(QStringLiteral("1.5\" (38 mm)"), QStringLiteral("1.5in"));
    m_size->addItem(QStringLiteral("1.75\" (44 mm)"), QStringLiteral("1.75in"));
    m_size->addItem(QStringLiteral("2\" (50 mm)"), QStringLiteral("2in"));
    m_size->addItem(QStringLiteral("2.5\" (64 mm)"), QStringLiteral("2.5in"));
    m_size->addItem(QStringLiteral("3\" (76 mm)"), QStringLiteral("3in"));
    m_size->addItem(QStringLiteral("3.5\" (89 mm)"), QStringLiteral("3.5in"));
    m_size->addItem(QStringLiteral("4\" (102 mm)"), QStringLiteral("4in"));
    m_size->addItem(QStringLiteral("4.5\" (114 mm)"), QStringLiteral("4.5in"));
    m_size->addItem(QStringLiteral("5\" (127 mm)"), QStringLiteral("5in"));
    m_size->addItem(QStringLiteral("5.5\" (140 mm)"), QStringLiteral("5.5in"));
    m_size->addItem(QStringLiteral("6\" (152 mm)"), QStringLiteral("6in"));
    m_size->addItem(QStringLiteral("6.5\" (165 mm)"), QStringLiteral("6.5in"));
    m_size->addItem(QStringLiteral("7\" (178 mm)"), QStringLiteral("7in"));
    m_size->addItem(QStringLiteral("8\" (203 mm)"), QStringLiteral("8in"));
    m_size->addItem(QStringLiteral("9\" (228 mm)"), QStringLiteral("9in"));
    m_size->addItem(QStringLiteral("10\" (254 mm)"), QStringLiteral("10in"));
    bar->addWidget(m_size, 3);

    root->addLayout(bar);

    // --- Quick-filter buttons for instant 1-click access on Laptop ---
    auto* quickBar = new QHBoxLayout;
    quickBar->setSpacing(6);
    auto addQuickBtn = [&](const QString& label, const QString& catText, const QString& searchTerm, bool highlight = false) {
        auto* btn = new QPushButton(label, this);
        if (highlight) {
            btn->setStyleSheet(QStringLiteral(
                "QPushButton { background:#1e3a3a; color:#2dd4bf; border:1px solid #00c8b3; border-radius:6px; padding:5px 10px; font-weight:700; font-size:12px; }"
                "QPushButton:hover { background:#254b4b; color:#ffffff; }"));
        } else {
            btn->setStyleSheet(QStringLiteral(
                "QPushButton { background:#1d212d; color:#cbd5e1; border:1px solid #2e3548; border-radius:6px; padding:5px 10px; font-weight:600; font-size:12px; }"
                "QPushButton:hover { background:#272d3d; border-color:#00c8b3; color:#ffffff; }"));
        }
        connect(btn, &QPushButton::clicked, this, [this, catText, searchTerm]{
            m_size->setCurrentIndex(0);
            if (catText.isEmpty()) {
                m_cat->setCurrentIndex(0);
            } else {
                const int idx = m_cat->findText(catText, Qt::MatchContains);
                m_cat->setCurrentIndex(idx >= 0 ? idx : 0);
            }
            m_search->setText(searchTerm);
            refresh();
        });
        quickBar->addWidget(btn);
    };

    addQuickBtn(QStringLiteral("🚒 Alle Feuerwehr & Kiel"), QStringLiteral("Feuerwehr"), QString(), true);
    addQuickBtn(QStringLiteral("🛡️ Wappen (WWWAP & Kiel)"), QStringLiteral("Feuerwehr"), QStringLiteral("wappen"), true);
    addQuickBtn(QStringLiteral("⬆️ Oberer Bogen (Berufsfeuerwehr)"), QStringLiteral("Feuerwehr"), QStringLiteral("oberbogen"), true);
    addQuickBtn(QStringLiteral("⬇️ Gegenbogen Unten (Nachtwache II / 2)"), QStringLiteral("Feuerwehr"), QStringLiteral("gegenbogen"), true);
    addQuickBtn(QStringLiteral("🔢 Ärmel-Zahl 30"), QStringLiteral("Feuerwehr"), QStringLiteral("30"), true);
    addQuickBtn(QStringLiteral("🏫 College A–Z"), QStringLiteral("College Buchstaben"), QString(), false);
    addQuickBtn(QStringLiteral("🪡 Applikation 0–9"), QStringLiteral("Applikation Ziffern"), QString(), false);
    addQuickBtn(QStringLiteral("📚 Alle (760+)"), QString(), QString(), false);
    quickBar->addStretch(1);
    root->addLayout(quickBar);

    // --- grid ---
    m_list = new QListWidget;
    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(QSize(136, 136));
    m_list->setGridSize(QSize(165, 195));
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setSpacing(6);
    m_list->setWordWrap(true);
    m_list->setUniformItemSizes(true);
    root->addWidget(m_list, 1);

    // --- detail banner ---
    m_detail = new QLabel;
    m_detail->setStyleSheet(QStringLiteral(
        "QLabel { background: #1a1e2a; border: 1px solid #272c3d; border-radius: 6px; padding: 8px 12px; font-size: 13px; color: #cbd5e1; }"
    ));
    m_detail->setText(QStringLiteral("Wähle ein Stickmuster aus, um Details anzuzeigen."));
    root->addWidget(m_detail);

    // --- footer ---
    auto* foot = new QHBoxLayout;
    foot->setSpacing(10);

    auto* importBtn = new QPushButton(QStringLiteral("📁 Eigene JEF-Muster importieren…"));
    foot->addWidget(importBtn);

    m_count = new QLabel;
    m_count->setStyleSheet(QStringLiteral("color: #94a3b8; font-weight: 500;"));
    foot->addWidget(m_count);

    foot->addStretch(1);

    auto* cancel = new QPushButton(QStringLiteral("Abbrechen"));
    m_openBtn = new QPushButton(QStringLiteral("In Editor laden"));
    m_openBtn->setObjectName(QStringLiteral("primaryBtn"));
    m_openBtn->setEnabled(false);

    foot->addWidget(cancel);
    foot->addWidget(m_openBtn);
    root->addLayout(foot);

    // Connections
    connect(m_search, &QLineEdit::textChanged, this, &LibraryDialog::refresh);
    connect(m_cat, qOverload<int>(&QComboBox::currentIndexChanged), this, &LibraryDialog::refresh);
    connect(m_size, qOverload<int>(&QComboBox::currentIndexChanged), this, &LibraryDialog::refresh);
    connect(m_list, &QListWidget::itemSelectionChanged, this, &LibraryDialog::onSelectionChanged);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &LibraryDialog::openSelected);
    connect(m_openBtn, &QPushButton::clicked, this, &LibraryDialog::openSelected);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(importBtn, &QPushButton::clicked, this, &LibraryDialog::importFiles);

    refresh();
}

void LibraryDialog::refresh()
{
    if (!m_list) return;
    m_list->clear();

    const QString cat = (m_cat->currentIndex() <= 0) ? QString() : m_cat->currentText();
    const QString sizeTag = m_size->currentData().toString();
    const auto entries = m_lib->search(m_search->text(), cat, sizeTag);

    for (const LibraryEntry& e : entries) {
        auto* it = new QListWidgetItem(QIcon(QPixmap(m_lib->absPath(e.thumb))), e.name);
        it->setData(Qt::UserRole, e.id);
        it->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);

        const QString hoop = hoopFitText(e.wMm, e.hMm);
        it->setToolTip(QStringLiteral("%1\n%2 · %3 Stiche · %4 Farben\nGröße: %5 × %6 mm\n%7\nTags: %8")
            .arg(e.name, e.category)
            .arg(e.stitches).arg(e.colors)
            .arg(e.wMm, 0, 'f', 1).arg(e.hMm, 0, 'f', 1)
            .arg(hoop, e.tags.join(QStringLiteral(", "))));

        m_list->addItem(it);
    }

    m_count->setText(QStringLiteral("%1 Motive gefunden").arg(entries.size()));
    if (m_list->count() > 0) {
        m_list->setCurrentRow(0);
    } else {
        onSelectionChanged();
    }
}

void LibraryDialog::onSelectionChanged()
{
    auto* it = m_list->currentItem();
    if (!it) {
        m_detail->setText(QStringLiteral("Kein Motiv ausgewählt."));
        m_openBtn->setEnabled(false);
        return;
    }

    const QString id = it->data(Qt::UserRole).toString();
    for (const LibraryEntry& e : m_lib->all()) {
        if (e.id == id) {
            const QString hoop = hoopFitText(e.wMm, e.hMm);
            m_detail->setText(QStringLiteral("<b>%1</b> &nbsp;·&nbsp; Kategorie: <span style='color:#00c8b3;'>%2</span> &nbsp;·&nbsp; "
                                             "Maße: <b>%3 × %4 mm</b> &nbsp;·&nbsp; <b>%5</b> Stiche &nbsp;·&nbsp; <b>%6</b> Farbe(n) &nbsp;·&nbsp; %7")
                .arg(e.name, e.category)
                .arg(e.wMm, 0, 'f', 1).arg(e.hMm, 0, 'f', 1)
                .arg(e.stitches).arg(e.colors)
                .arg(hoop));
            m_openBtn->setEnabled(true);
            return;
        }
    }
    m_openBtn->setEnabled(false);
}

void LibraryDialog::openSelected()
{
    auto* it = m_list->currentItem();
    if (!it) return;
    const QString id = it->data(Qt::UserRole).toString();
    for (const LibraryEntry& e : m_lib->all()) {
        if (e.id == id) {
            if (m_lib->load(e, m_chosen)) {
                m_chosenName = e.name;
                m_hasChoice = true;
                accept();
            } else {
                QMessageBox::warning(this, windowTitle(),
                    QStringLiteral("Das Motiv „%1\" konnte nicht geladen werden.").arg(e.name));
            }
            return;
        }
    }
}

void LibraryDialog::importFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this,
        QStringLiteral("JEF-Stickmuster zum Importieren auswählen"),
        QString(),
        QStringLiteral("Janome Stickdateien (*.jef *.JEF);;Alle Dateien (*.*)"));

    if (files.isEmpty()) return;

    int imported = 0;
    for (const QString& fn : files) {
        QFileInfo fi(fn);
        const QString name = fi.baseName().replace('_', ' ');
        const QString id = LibraryBuilder::importJef(*m_lib, fn, name,
                                                    QStringLiteral("Eigene"),
                                                    { QStringLiteral("eigen"), QStringLiteral("import"), fi.baseName().toLower() });
        if (!id.isEmpty()) imported++;
    }

    if (imported > 0) {
        m_lib->writeIndex();
        refresh();
        QMessageBox::information(this, windowTitle(),
            QStringLiteral("%1 Stickmuster erfolgreich in deine Bibliothek importiert!").arg(imported));
    }
}

} // namespace stick
