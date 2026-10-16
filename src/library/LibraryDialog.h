// ---------------------------------------------------------------------------
//  StickCore  –  LibraryDialog.h
//
//  The design library browser: category filter, size/resolution filter, live search
//  and a grid of preview thumbnails with Janome hoop fit indicator.
// ---------------------------------------------------------------------------
#pragma once

#include "core/StitchTypes.h"
#include <QDialog>

class QLineEdit;
class QComboBox;
class QListWidget;
class QLabel;
class QPushButton;

namespace stick {

class DesignLibrary;

class LibraryDialog : public QDialog {
    Q_OBJECT
public:
    explicit LibraryDialog(DesignLibrary* lib,
                           const QString& initialCategory = QString(),
                           QWidget* parent = nullptr);

    bool                  hasChoice() const { return m_hasChoice; }
    const StitchSequence& chosenSequence() const { return m_chosen; }
    QString               chosenName() const { return m_chosenName; }

private slots:
    void refresh();
    void onSelectionChanged();
    void openSelected();
    void importFiles();

private:
    DesignLibrary* m_lib = nullptr;
    QLineEdit*     m_search = nullptr;
    QComboBox*     m_cat = nullptr;
    QComboBox*     m_size = nullptr;
    QListWidget*   m_list = nullptr;
    QLabel*        m_count = nullptr;
    QLabel*        m_detail = nullptr;
    QPushButton*   m_openBtn = nullptr;

    StitchSequence m_chosen;
    QString        m_chosenName;
    bool           m_hasChoice = false;
};

} // namespace stick
