// ---------------------------------------------------------------------------
//  StickCore  –  DesignLibrary.h
//
//  A categorised, searchable library of embroidery designs with preview
//  thumbnails and a JSON index. Designs are stored losslessly (stitches +
//  palette) so they reload with full colour; a thumbnail PNG is kept per
//  design for the grid. Ships with a built-in starter set (DesignFactory).
// ---------------------------------------------------------------------------
#pragma once

#include "core/StitchTypes.h"
#include <QString>
#include <QStringList>
#include <QVector>

namespace stick {

struct LibraryEntry {
    QString     id;
    QString     name;
    QString     category;
    QStringList tags;
    QString     file;       // relative path to the design data (.scd)
    QString     thumb;      // relative path to the preview png
    int         stitches = 0;
    int         colors   = 0;
    double      wMm = 0, hMm = 0;
};

class DesignLibrary {
public:
    /// Uses the app data location by default; pass a dir for tests.
    explicit DesignLibrary(const QString& baseDir = QString());

    QString baseDir() const { return m_base; }

    /// Create the folder structure and, if the library is new, fill it with
    /// the built-in starter designs. Returns the number of designs present.
    int ensurePopulated();

    QVector<LibraryEntry> all() const { return m_entries; }
    QStringList categoriesPresent() const;

    /// Filter by free text (name + tags + category) and/or a category.
    QVector<LibraryEntry> search(const QString& query, const QString& category = QString()) const;

    /// Add a design (writes data + thumbnail + index). Returns its id, or "".
    QString add(const QString& name, const QString& category,
                const QStringList& tags, const StitchSequence& seq,
                bool updateIndex = true);

    /// Load a design's full stitch data (with colours).
    bool load(const LibraryEntry& e, StitchSequence& out) const;

    QString absPath(const QString& rel) const;

private:
    bool  readIndex();
    bool  writeIndex() const;
    static QString slug(const QString& s);

    QString m_base;
    QVector<LibraryEntry> m_entries;
};

} // namespace stick
