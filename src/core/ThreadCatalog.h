// ---------------------------------------------------------------------------
//  StickCore  –  ThreadCatalog.h
//
//  Thread colour catalogues for the brands the MC350E supports (Janome,
//  Robison-Anton, Madeira, Mettler) plus nearest-colour matching so any RGB
//  can be snapped to a real thread with a name and (for Janome) a JEF code.
//
//  NOTE: the RGB values here are a curated starter set. The Janome codes that
//  were read from real .jef files are exact; the RGBs are close approximations
//  and easy to refine against an official thread chart.
// ---------------------------------------------------------------------------
#pragma once

#include "core/StitchTypes.h"
#include <QColor>
#include <QString>
#include <vector>

namespace stick {

struct CatalogThread {
    QString brand;      ///< "Janome", "Madeira", ...
    QString name;       ///< human colour name
    int     code;       ///< Janome catalogue code (JEF); -1 for other brands
    QColor  color;

    ThreadColor toThread() const { return ThreadColor(color, name, code); }
};

class ThreadCatalog {
public:
    /// All catalogue entries (all brands).
    static const std::vector<CatalogThread>& all();

    /// Nearest catalogue thread to 'c'. If 'brand' is non-empty, restrict to it.
    static CatalogThread nearest(const QColor& c, const QString& brand = QString());

    /// Convenience: snap to a ThreadColor (with name + code) for a sequence.
    static ThreadColor snap(const QColor& c, const QString& brand = QString())
    {
        return nearest(c, brand).toThread();
    }
};

} // namespace stick
