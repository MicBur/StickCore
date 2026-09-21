// ---------------------------------------------------------------------------
//  StickCore  –  BastingDialog.cpp
//
//  Heftrahmen / Basting Box Assistant Dialog Implementation.
// ---------------------------------------------------------------------------
#include "ui/BastingDialog.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace stick {

BastingDialog::BastingDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Heftrahmen (Basting Box) erstellen"));
    setMinimumWidth(380);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(12);

    auto* header = new QLabel(
        QStringLiteral("<b>Heftrahmen zur Stoffstabilisierung</b><br>"
                       "Stickt lockere Heftstiche vor dem eigentlichen Muster, "
                       "um empfindliche Stoffe (Samt, Loden, Leder) ohne Einspannabdrücke "
                       "auf dem Stickvlies zu fixieren."), this);
    header->setWordWrap(true);
    root->addWidget(header);

    auto* form = new QFormLayout();
    form->setSpacing(8);

    m_targetCombo = new QComboBox(this);
    m_targetCombo->addItem(QStringLiteral("Um das Motiv (Motif Bounds)"), static_cast<int>(BastingGenerator::Target::MotifBounds));
    m_targetCombo->addItem(QStringLiteral("Entlang des Stickrahmens (Hoop Bounds)"), static_cast<int>(BastingGenerator::Target::HoopBounds));
    form->addRow(QStringLiteral("Platzierung:"), m_targetCombo);

    m_marginSpin = new QDoubleSpinBox(this);
    m_marginSpin->setRange(1.0, 30.0);
    m_marginSpin->setValue(4.0);
    m_marginSpin->setSuffix(QStringLiteral(" mm"));
    form->addRow(QStringLiteral("Sicherheitsabstand:"), m_marginSpin);

    m_stitchLenSpin = new QDoubleSpinBox(this);
    m_stitchLenSpin->setRange(2.0, 10.0);
    m_stitchLenSpin->setValue(5.0);
    m_stitchLenSpin->setSingleStep(0.5);
    m_stitchLenSpin->setSuffix(QStringLiteral(" mm"));
    form->addRow(QStringLiteral("Heftstichlänge:"), m_stitchLenSpin);

    m_doublePassChk = new QCheckBox(QStringLiteral("Doppelter Durchgang (extra Halt bei Hochflor)"), this);
    m_doublePassChk->setChecked(false);
    form->addRow(QStringLiteral(""), m_doublePassChk);

    root->addLayout(form);

    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    btnBox->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Heftrahmen anfügen"));
    connect(btnBox, &QDialogButtonBox::accepted, this, &BastingDialog::onAccept);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(btnBox);
}

void BastingDialog::onAccept()
{
    m_params.mode = static_cast<BastingGenerator::Mode>(m_targetCombo->currentData().toInt());
    m_params.marginMm = m_marginSpin->value();
    m_params.stitchLengthMm = m_stitchLenSpin->value();
    m_params.doublePass = m_doublePassChk->isChecked();
    accept();
}

} // namespace stick
