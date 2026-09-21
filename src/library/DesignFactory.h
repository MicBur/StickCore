// ---------------------------------------------------------------------------
//  StickCore  –  DesignFactory.h
//
//  Generates the built-in starter designs for the library, organised into
//  categories. These populate a fresh library so it is useful out of the box.
// ---------------------------------------------------------------------------
#pragma once

#include "core/StitchTypes.h"
#include <QString>
#include <QStringList>
#include <vector>

namespace stick {

struct Design {
    QString        name;
    QString        category;
    QStringList    tags;
    StitchSequence seq;
};

class DesignFactory {
public:
    /// The full built-in catalogue (~20 designs across several categories).
    static std::vector<Design> starterSet();

    /// The category names, in display order.
    static QStringList categories();
};

} // namespace stick
