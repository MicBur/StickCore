// ---------------------------------------------------------------------------
//  StickCore  –  LibraryDialog.h
//
//  The design library browser: category filter, live search and a grid of
//  preview thumbnails. Picking a design returns its full stitch sequence.
// ---------------------------------------------------------------------------
#pragma once

#include "core/StitchTypes.h"
#include <QDialog>

class QLineEdit;
class QComboBox;
class QListWidget;
class QLabel;

namespace stick {

class DesignLibrary;

class LibraryDialog : public QDialog {
    Q_OBJECT
public:
    LibraryDialog(DesignLibrary* lib, QWidget* parent = nullptr);

    bool                  hasChoice() const { return m_hasChoice; }
    const StitchSequence& chosenSequence() const { return m_chosen; }
    QString               chosenName() const { return m_chosenName; }

private slots:
    void refresh();
    void openSelected();

private:
    DesignLibrary* m_lib = nullptr;
    QLineEdit*     m_search = nullptr;
    QComboBox*     m_cat = nullptr;
    QListWidget*   m_list = nullptr;
    QLabel*        m_count = nullptr;

    StitchSequence m_chosen;
    QString        m_chosenName;
    bool           m_hasChoice = false;
};

} // namespace stick
