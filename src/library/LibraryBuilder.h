// ---------------------------------------------------------------------------
//  StickCore  –  LibraryBuilder.h
//
//  Ingestion and batch-builder for the StickCore design library:
//  Loads bought College outline letters & numbers, applique digits,
//  and official Feuerwehr & Kieler Wappen designs, and registers them.
// ---------------------------------------------------------------------------
#pragma once

#include <QString>
#include <QStringList>

namespace stick {

class DesignLibrary;

class LibraryBuilder {
public:
    struct Stats {
        int collegeLetters  = 0;
        int collegeNumbers  = 0;
        int appliqueNumbers = 0;
        int feuerwehr       = 0;
        int starter         = 0;
        int total           = 0;
    };

    /// Build the full unified library into targetDir from bought assets & starter designs.
    static Stats buildAll(const QString& targetDir,
                          const QString& mefBaseDir = QString(),
                          const QString& fwDir = QString());

    /// Import a single JEF file into an existing DesignLibrary
    static QString importJef(DesignLibrary& lib,
                             const QString& filePath,
                             const QString& name,
                             const QString& category,
                             const QStringList& tags,
                             const QString& sizeTag = QString(),
                             const QString& explicitId = QString());

    /// Import an entire folder of JEF files into DesignLibrary
    static int importFolder(DesignLibrary& lib,
                            const QString& folderPath,
                            const QString& category);
};

} // namespace stick
