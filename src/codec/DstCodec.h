// ---------------------------------------------------------------------------
//  StickCore  –  DstCodec.h
//
//  Reader and writer for the industry-standard Tajima DST embroidery format.
//  DST is supported by virtually every embroidery machine on the market.
// ---------------------------------------------------------------------------
#pragma once

#include "core/StitchTypes.h"
#include <QString>

namespace stick {

class DstCodec {
public:
    struct Result {
        bool    ok = false;
        QString message;
        int     stitchesWritten = 0;
    };

    /// Write 'stitches' to 'path' in Tajima DST format.
    /// Internal coordinates are mm; DST stores 0.1 mm (+Y up).
    static Result exportToFile(const QString& path, const StitchSequence& stitches);

    /// Read stitches from a .dst file.
    static bool importFromFile(const QString& path, StitchSequence& out);
};

} // namespace stick
