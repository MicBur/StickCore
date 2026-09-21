// ---------------------------------------------------------------------------
//  StickCore  –  ThreadPickDialog.cpp
// ---------------------------------------------------------------------------
#include "ui/ThreadPickDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QColorDialog>
#include <QPainter>
#include <QIcon>
#include <QDialogButtonBox>

namespace stick {

namespace {

QPixmap makeSwatch(const QColor& c, int size = 20)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(0, 0, 0, 80), 1));
    p.setBrush(c);
    p.drawRoundedRect(0, 0, size - 1, size - 1, 4, 4);
    p.end();
    return pm;
}

} // namespace

ThreadPickDialog::ThreadPickDialog(const ThreadColor& current, QWidget* parent)
    : QDialog(parent), m_selected(current)
{
    setWindowTitle(QStringLiteral("Garnfarbe auswählen"));
    resize(420, 520);

    auto* lay = new QVBoxLayout(this);
    lay->setSpacing(8);

    auto* topLay = new QHBoxLayout;
    m_brandFilter = new QComboBox(this);
    m_brandFilter->addItem(QStringLiteral("Alle Marken"), QString());
    m_brandFilter->addItem(QStringLiteral("Janome"), QStringLiteral("Janome"));
    m_brandFilter->addItem(QStringLiteral("Madeira"), QStringLiteral("Madeira"));
    m_brandFilter->addItem(QStringLiteral("Robison-Anton"), QStringLiteral("Robison-Anton"));
    m_brandFilter->addItem(QStringLiteral("Mettler"), QStringLiteral("Mettler"));

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(QStringLiteral("Farbe oder Code suchen…"));

    topLay->addWidget(m_brandFilter);
    topLay->addWidget(m_searchEdit);
    lay->addLayout(topLay);

    m_list = new QListWidget(this);
    m_list->setIconSize(QSize(20, 20));
    lay->addWidget(m_list);

    auto* btnLay = new QHBoxLayout;
    auto* customBtn = new QPushButton(QStringLiteral("Eigene RGB-Farbe…"), this);
    connect(customBtn, &QPushButton::clicked, this, &ThreadPickDialog::pickCustomColor);
    btnLay->addWidget(customBtn);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    btnLay->addWidget(bb);
    lay->addLayout(btnLay);

    connect(m_brandFilter, qOverload<int>(&QComboBox::currentIndexChanged), this, &ThreadPickDialog::filterChanged);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &ThreadPickDialog::filterChanged);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
        itemSelected();
        accept();
    });
    connect(m_list, &QListWidget::itemSelectionChanged, this, &ThreadPickDialog::itemSelected);

    populateList();
}

void ThreadPickDialog::populateList()
{
    m_list->clear();
    const QString brand = m_brandFilter->currentData().toString();
    const QString filter = m_searchEdit->text().trimmed().toLower();

    const auto& catalog = ThreadCatalog::all();
    for (const auto& t : catalog) {
        if (!brand.isEmpty() && t.brand != brand) continue;
        const QString codeStr = QString::number(t.code);
        if (!filter.isEmpty()) {
            if (!t.name.toLower().contains(filter) &&
                !t.brand.toLower().contains(filter) &&
                !codeStr.contains(filter)) {
                continue;
            }
        }

        QString label = QStringLiteral("%1 · %2").arg(t.brand, t.name);
        if (t.code >= 0) label += QStringLiteral(" (#%1)").arg(t.code);

        auto* item = new QListWidgetItem(QIcon(makeSwatch(t.color)), label, m_list);
        item->setData(Qt::UserRole, QVariant::fromValue(t.color));
        item->setData(Qt::UserRole + 1, t.name);
        item->setData(Qt::UserRole + 2, t.code);
        item->setData(Qt::UserRole + 3, t.brand);

        if (t.color == m_selected.color || (t.code >= 0 && t.code == m_selected.janomeCode))
            m_list->setCurrentItem(item);
    }
}

void ThreadPickDialog::filterChanged()
{
    populateList();
}

void ThreadPickDialog::itemSelected()
{
    auto* cur = m_list->currentItem();
    if (!cur) return;
    const QColor col = cur->data(Qt::UserRole).value<QColor>();
    const QString name = cur->data(Qt::UserRole + 1).toString();
    const int code = cur->data(Qt::UserRole + 2).toInt();
    m_selected = ThreadColor(col, name, code);
}

void ThreadPickDialog::pickCustomColor()
{
    QColor c = QColorDialog::getColor(m_selected.color, this, QStringLiteral("Farbe wählen"));
    if (!c.isValid()) return;
    m_selected = ThreadCatalog::snap(c);
    accept();
}

} // namespace stick
