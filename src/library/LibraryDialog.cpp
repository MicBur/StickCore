// ---------------------------------------------------------------------------
//  StickCore  –  LibraryDialog.cpp
// ---------------------------------------------------------------------------
#include "library/LibraryDialog.h"
#include "library/DesignLibrary.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLabel>
#include <QPushButton>
#include <QIcon>
#include <QPixmap>

namespace stick {

LibraryDialog::LibraryDialog(DesignLibrary* lib, QWidget* parent)
    : QDialog(parent), m_lib(lib)
{
    setWindowTitle(QStringLiteral("Motiv-Bibliothek"));
    resize(900, 620);

    auto* root = new QVBoxLayout(this);

    // --- filter bar ---
    auto* bar = new QHBoxLayout;
    m_search = new QLineEdit;
    m_search->setPlaceholderText(QStringLiteral("Suchen … (Name oder Stichwort, z. B. „herz\", „weihnachten\")"));
    m_search->setClearButtonEnabled(true);
    bar->addWidget(m_search, 1);

    m_cat = new QComboBox;
    m_cat->addItem(QStringLiteral("Alle Kategorien"));
    m_cat->addItems(m_lib->categoriesPresent());
    bar->addWidget(m_cat);
    root->addLayout(bar);

    // --- grid ---
    m_list = new QListWidget;
    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(QSize(150, 150));
    m_list->setGridSize(QSize(178, 200));
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setSpacing(8);
    m_list->setWordWrap(true);
    m_list->setUniformItemSizes(true);
    root->addWidget(m_list, 1);

    // --- footer ---
    auto* foot = new QHBoxLayout;
    m_count = new QLabel;
    m_count->setStyleSheet(QStringLiteral("color:#8b93a4"));
    foot->addWidget(m_count);
    foot->addStretch(1);
    auto* cancel = new QPushButton(QStringLiteral("Abbrechen"));
    cancel->setStyleSheet(QStringLiteral("background:#232732;color:#dbe0e9"));
    auto* open = new QPushButton(QStringLiteral("Öffnen"));
    foot->addWidget(cancel);
    foot->addWidget(open);
    root->addLayout(foot);

    connect(m_search, &QLineEdit::textChanged, this, &LibraryDialog::refresh);
    connect(m_cat, qOverload<int>(&QComboBox::currentIndexChanged), this, &LibraryDialog::refresh);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &LibraryDialog::openSelected);
    connect(open, &QPushButton::clicked, this, &LibraryDialog::openSelected);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    refresh();
}

void LibraryDialog::refresh()
{
    if (!m_list) return;
    m_list->clear();
    const QString cat = (m_cat->currentIndex() <= 0) ? QString() : m_cat->currentText();
    const auto entries = m_lib->search(m_search->text(), cat);
    for (const LibraryEntry& e : entries) {
        auto* it = new QListWidgetItem(QIcon(QPixmap(m_lib->absPath(e.thumb))), e.name);
        it->setData(Qt::UserRole, e.id);
        it->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
        it->setToolTip(QStringLiteral("%1\n%2 · %3 Stiche\n%4")
            .arg(e.name, e.category).arg(e.stitches).arg(e.tags.join(", ")));
        m_list->addItem(it);
    }
    m_count->setText(QStringLiteral("%1 Motive").arg(entries.size()));
    if (m_list->count() > 0) m_list->setCurrentRow(0);
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
            }
            return;
        }
    }
}

} // namespace stick
