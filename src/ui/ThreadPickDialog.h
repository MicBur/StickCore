// ---------------------------------------------------------------------------
//  StickCore  –  ThreadPickDialog.h
//
//  Dialog allowing the user to pick a thread color from supported catalogs
//  (Janome, Madeira, Robison-Anton, Mettler) or pick a custom RGB color.
// ---------------------------------------------------------------------------
#pragma once

#include "core/ThreadCatalog.h"
#include <QDialog>
#include <QColor>

class QListWidget;
class QComboBox;
class QLineEdit;

namespace stick {

class ThreadPickDialog : public QDialog {
    Q_OBJECT
public:
    explicit ThreadPickDialog(const ThreadColor& current, QWidget* parent = nullptr);

    ThreadColor selectedThread() const { return m_selected; }

private slots:
    void filterChanged();
    void itemSelected();
    void pickCustomColor();

private:
    void populateList();

    ThreadColor m_selected;
    QComboBox*  m_brandFilter = nullptr;
    QLineEdit*  m_searchEdit  = nullptr;
    QListWidget* m_list       = nullptr;
};

} // namespace stick
