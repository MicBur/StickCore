// ---------------------------------------------------------------------------
//  StickCore  –  WelcomeWizard.cpp
// ---------------------------------------------------------------------------
#include "ui/WelcomeWizard.h"
#include "core/MachineProfile.h"

#include <QWizardPage>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QFrame>
#include <QMovie>
#include <QGridLayout>

namespace stick {

namespace {

QLabel* title(const QString& t) {
    auto* l = new QLabel(t);
    l->setStyleSheet(QStringLiteral("font-size:22px;font-weight:800;color:#dbe0e9;"));
    return l;
}
QLabel* body(const QString& t) {
    auto* l = new QLabel(t);
    l->setWordWrap(true);
    l->setTextFormat(Qt::RichText);
    l->setStyleSheet(QStringLiteral("color:#c2c8d2;font-size:13.5px;"));
    return l;
}

// One selectable start option: a radio button with a title and a description.
QWidget* option(QButtonGroup* g, int id, const QString& t, const QString& desc, bool checked = false)
{
    auto* w = new QFrame;
    w->setObjectName(QStringLiteral("Card"));
    auto* lay = new QHBoxLayout(w);
    lay->setContentsMargins(12, 10, 12, 10);
    auto* rb = new QRadioButton;
    rb->setChecked(checked);
    g->addButton(rb, id);
    lay->addWidget(rb, 0, Qt::AlignTop);
    auto* col = new QVBoxLayout; col->setSpacing(1);
    auto* tl = new QLabel(t); tl->setStyleSheet(QStringLiteral("font-weight:700;color:#dbe0e9;"));
    auto* dl = new QLabel(desc); dl->setStyleSheet(QStringLiteral("color:#8b93a4;font-size:12px;")); dl->setWordWrap(true);
    col->addWidget(tl); col->addWidget(dl);
    lay->addLayout(col, 1);
    // clicking anywhere on the card selects it
    QObject::connect(rb, &QRadioButton::toggled, w, [w](bool on){
        w->setStyleSheet(on ? QStringLiteral("#Card{background:#232a33;border:1px solid #2dd4bf;border-radius:10px;}")
                            : QString());
    });
    return w;
}

} // namespace

WelcomeWizard::WelcomeWizard(QWidget* parent, const QString& userName)
    : QWizard(parent)
{
    const QString who = userName.isEmpty() ? QStringLiteral("Daniela") : userName;
    const auto& mp = MachineProfile::current();

    setWindowTitle(QStringLiteral("Willkommen bei StickCore"));
    setWizardStyle(QWizard::ModernStyle);
    setOption(QWizard::NoDefaultButton, false);
    setMinimumSize(620, 480);
    setButtonText(QWizard::NextButton,   QStringLiteral("Weiter →"));
    setButtonText(QWizard::BackButton,   QStringLiteral("← Zurück"));
    setButtonText(QWizard::FinishButton, QStringLiteral("Los geht's ▶"));
    setButtonText(QWizard::CancelButton, QStringLiteral("Überspringen"));

    // --- Page 1: welcome ---------------------------------------------------
    {
        auto* p = new QWizardPage;
        auto* l = new QVBoxLayout(p);
        l->setSpacing(12); l->setContentsMargins(24, 24, 24, 24);
        l->addWidget(title(QStringLiteral("Hallo %1 — schön, dass du da bist! 🎉").arg(who)));
        l->addWidget(body(QStringLiteral(
            "Mit <b>StickCore</b> erstellst du eigene Stickmotive: aus der Bibliothek wählen, "
            "Text und Fotos verwandeln oder selbst zeichnen. Du siehst alles vorab in 3D und "
            "speicherst es als Datei für deine Stickmaschine.")));
        l->addSpacing(6);
        l->addWidget(body(QStringLiteral(
            "Dieses Programm ist fest auf deine <b>%1</b> eingestellt — Rahmen, Format (JEF) und "
            "Garnfarben passen automatisch. Du musst dich um nichts Technisches kümmern.")
            .arg(mp.name)));
        l->addStretch(1);
        l->addWidget(body(QStringLiteral("Dieser kurze Assistent zeigt dir in zwei Schritten, wie es geht.")));
        p->setLayout(l);
        addPage(p);
    }

    // --- Page 2: the flow --------------------------------------------------
    {
        auto* p = new QWizardPage;
        auto* l = new QVBoxLayout(p);
        l->setSpacing(12); l->setContentsMargins(24, 24, 24, 24);
        l->addWidget(title(QStringLiteral("In 5 Schritten zur Stickdatei")));
        l->addWidget(body(QStringLiteral(
            "<table cellspacing='8'>"
            "<tr><td valign='top'><b style='color:#2dd4bf'>1.</b></td><td><b>Motiv wählen oder erstellen</b><br>"
            "<span style='color:#8b93a4'>aus der 📚 Bibliothek, als Text, aus einem Foto, oder selbst gezeichnet</span></td></tr>"
            "<tr><td valign='top'><b style='color:#2dd4bf'>2.</b></td><td><b>In 3D ansehen</b><br>"
            "<span style='color:#8b93a4'>rechts siehst du, wie die Stiche auf dem Stoff wirken</span></td></tr>"
            "<tr><td valign='top'><b style='color:#2dd4bf'>3.</b></td><td><b>Anpassen</b><br>"
            "<span style='color:#8b93a4'>Größe/Dichte prüfen — „passt in Rahmen …“ sagt dir, ob es in deine Maschine passt</span></td></tr>"
            "<tr><td valign='top'><b style='color:#2dd4bf'>4.</b></td><td><b>Als JEF exportieren</b><br>"
            "<span style='color:#8b93a4'>ein Klick auf „JEF exportieren…“</span></td></tr>"
            "<tr><td valign='top'><b style='color:#2dd4bf'>5.</b></td><td><b>An die Maschine</b><br>"
            "<span style='color:#8b93a4'>Datei per USB-Stick auf die MC350E — fertig zum Sticken</span></td></tr>"
            "</table>")));
        l->addSpacing(4);
        // little "stroke video" of the intelligent fill (contour → underlay → cover)
        auto* anim = new QLabel; anim->setAlignment(Qt::AlignCenter);
        auto* mv = new QMovie(QStringLiteral(":/anim/underlay.gif"));
        anim->setMovie(mv); mv->start();
        l->addWidget(anim);
        l->addWidget(body(QStringLiteral(
            "<i>Intelligente Füllung: erst Kontur &amp; Unterlage, dann die Deckstiche — "
            "so wird die Stickerei fest und sauber.</i>")));
        l->addStretch(1);
        p->setLayout(l);
        addPage(p);
    }

    // --- Page 3: pick a start (with a live "stroke video" preview) --------
    {
        auto* p = new QWizardPage;
        auto* l = new QVBoxLayout(p);
        l->setSpacing(8); l->setContentsMargins(24, 24, 24, 24);
        l->addWidget(title(QStringLiteral("Womit möchtest du starten?")));

        auto* cols = new QHBoxLayout;
        auto* left = new QVBoxLayout; left->setSpacing(6);
        m_group = new QButtonGroup(this);
        left->addWidget(option(m_group, int(Choice::Library), QStringLiteral("📚 Aus der Bibliothek wählen"),
            QStringLiteral("Fertige Motive nach Kategorie durchstöbern."), true));
        left->addWidget(option(m_group, int(Choice::Text), QStringLiteral("✎ Text / Namen · Monogramm"),
            QStringLiteral("Ein Wort, Namen oder edle Initialen — erhaben.")));
        left->addWidget(option(m_group, int(Choice::Image), QStringLiteral("🖼 Ein Foto digitalisieren"),
            QStringLiteral("Ein Bild automatisch in Stiche umwandeln.")));
        left->addWidget(option(m_group, int(Choice::Draw), QStringLiteral("✐ Selbst zeichnen"),
            QStringLiteral("Eigene Formen aus Kurven.")));
        left->addStretch(1);
        cols->addLayout(left, 1);

        auto* right = new QVBoxLayout;
        m_preview = new QLabel; m_preview->setAlignment(Qt::AlignCenter);
        m_preview->setMinimumSize(300, 190);
        m_preview->setStyleSheet(QStringLiteral("background:#0f1116;border:1px solid #2c313d;border-radius:8px;"));
        m_prevCap = new QLabel; m_prevCap->setAlignment(Qt::AlignCenter);
        m_prevCap->setWordWrap(true); m_prevCap->setStyleSheet(QStringLiteral("color:#8b93a4;font-size:12px;"));
        right->addWidget(new QLabel(QStringLiteral("<span style='color:#8b93a4'>So funktioniert's:</span>")));
        right->addWidget(m_preview);
        right->addWidget(m_prevCap);
        right->addStretch(1);
        cols->addLayout(right, 1);
        l->addLayout(cols, 1);

        m_dont = new QCheckBox(QStringLiteral("Diesen Assistenten beim Start nicht mehr zeigen"));
        m_dont->setStyleSheet(QStringLiteral("color:#8b93a4;"));
        l->addWidget(m_dont);
        p->setLayout(l);
        addPage(p);
    }

    m_movie = new QMovie(this);
    connect(m_group, &QButtonGroup::idToggled, this, [this](int id, bool on){
        if (on) showPreview(static_cast<Choice>(id));
    });
    showPreview(Choice::Library);
}

void WelcomeWizard::showPreview(Choice c)
{
    if (!m_preview) return;
    QString gif, cap;
    switch (c) {
    case Choice::Library: gif = QStringLiteral(":/anim/fill.gif");
        cap = QStringLiteral("Fertige Motive werden gefüllt gestickt — auswählen und loslegen."); break;
    case Choice::Text:    gif = QStringLiteral(":/anim/monogram.gif");
        cap = QStringLiteral("Buchstaben & Monogramme: erhabene Satin-Ränder auf gefüllten Formen."); break;
    case Choice::Image:   gif = QStringLiteral(":/anim/image.gif");
        cap = QStringLiteral("Ein Foto wird in Farbflächen zerlegt und in Stiche umgewandelt."); break;
    case Choice::Draw:    gif = QStringLiteral(":/anim/satin.gif");
        cap = QStringLiteral("Zwei Kurven ergeben eine glänzende Satin-Säule (Zickzack)."); break;
    default: return;
    }
    m_movie->stop();
    m_movie->setFileName(gif);
    m_preview->setMovie(m_movie);
    m_movie->start();
    m_prevCap->setText(cap);
}

WelcomeWizard::Choice WelcomeWizard::choice() const
{
    if (!m_group || m_group->checkedId() < 0) return Choice::None;
    return static_cast<Choice>(m_group->checkedId());
}

bool WelcomeWizard::dontShowAgain() const { return m_dont && m_dont->isChecked(); }

} // namespace stick
