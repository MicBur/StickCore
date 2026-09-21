// ---------------------------------------------------------------------------
//  StickCore  –  WelcomeWizard.h
//
//  A friendly first-run assistant that welcomes the user, explains the flow
//  in a few steps, and lets them jump straight into a task.
// ---------------------------------------------------------------------------
#pragma once

#include <QWizard>

class QButtonGroup;
class QCheckBox;
class QLabel;
class QMovie;

namespace stick {

class WelcomeWizard : public QWizard {
    Q_OBJECT
public:
    enum class Choice { None, Library, Text, Image, Draw, Phone };

    explicit WelcomeWizard(QWidget* parent = nullptr,
                           const QString& userName = QString());

    Choice choice() const;
    bool   dontShowAgain() const;

private:
    void showPreview(Choice c);

    QButtonGroup* m_group = nullptr;
    QCheckBox*    m_dont  = nullptr;
    QLabel*       m_preview = nullptr;
    QLabel*       m_prevCap = nullptr;
    QMovie*       m_movie = nullptr;
};

} // namespace stick
