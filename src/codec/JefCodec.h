// ---------------------------------------------------------------------------
//  StickCore  –  JefCodec.h
//
//  Reader / writer for the Janome JEF embroidery format.
//
//  NOTE ON ACCURACY
//  ----------------
//  JEF is a reverse-engineered, undocumented format. The header layout used
//  here follows the well-known open-source consensus (libembroidery /
//  Embroidermodder) and is arithmetically self-consistent (116-byte fixed
//  header + 4 bytes per colour). Every field is named and commented so it can
//  be trimmed to a real reference file. If you drop a genuine .jef sample in,
//  the byte layout can be locked exactly against it.
// ---------------------------------------------------------------------------
#pragma once

#include "core/StitchTypes.h"
#include <QString>

namespace stick {

class JefCodec {
public:
    struct Result {
        bool    ok = false;
        QString message;
        int     stitchesWritten = 0;
        bool    withinHoop = true;
    };

    /// Write 'stitches' to 'path' for the given hoop.
    /// Coordinates are millimetres; JEF stores 0.1 mm with a flipped Y axis
    /// and the design centred on the hoop origin.
    static Result exportToFile(const QString& path,
                               const StitchSequence& stitches,
                               HoopType hoop);

    /// Same, but with an explicit raw hoop code and hoop dimensions (mm).
    /// Use this for modern JEF+ hoops whose code is outside the classic 0-4
    /// range (the reference machine used code 28 for a large hoop).
    static Result exportToFile(const QString& path,
                               const StitchSequence& stitches,
                               int hoopCode, double hoopWidthMm, double hoopHeightMm);

    /// Minimal reader – restores absolute stitches (mm) and colour count.
    /// Useful for round-trip verification against a reference file.
    static bool importFromFile(const QString& path, StitchSequence& out);
};

} // namespace stick
