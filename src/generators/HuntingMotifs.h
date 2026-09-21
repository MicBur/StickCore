// ---------------------------------------------------------------------------
//  StickCore  –  HuntingMotifs.h
//
//  Traditional Hunting & Nature Motifs (Jagd, Forst & Schützentradition):
//    • Eichenlaub mit Eicheln (Oak Branch & Acorns)
//    • Kapitaler 12-Ender Hirschkopf (Royal Red Deer Stag)
//    • Keiler / Schwarzwild (Wild Boar Silhouette & Tusks)
//    • Waidmannsheil-Medaillon (Crossed Rifles & Oak Wreath)
//
//  Every motif can be produced either as finished embroidery stitches
//  or as fully editable Bézier paths for node-level manipulation in the 2D editor.
// ---------------------------------------------------------------------------
#pragma once

#include "core/StitchTypes.h"
#include "editor/BezierPath.h"
#include "generators/TatamiFill.h"
#include <QString>
#include <QVector>

namespace stick {

class HuntingMotifs {
public:
    enum class MotifType {
        OakBranch,          ///< Eichenlaub mit Stiel & Eicheln (Trachten & Schützen)
        StagHead,           ///< Kapitaler 12-Ender Rothirsch (Silhouette & Geweih)
        WildBoar,           ///< Keiler / Schwarzwild mit Hauer
        WaidmannsheilCrest  ///< Waidmannsheil-Medaillon mit Eichenkranz & Jagdflinten
    };

    struct Params {
        double widthMm      = 80.0;    ///< Zielbreite in mm
        double fillAngleDeg = 45.0;    ///< Füllwinkel (0° - 360°)
        TatamiFill::PatternType pattern = TatamiFill::PatternType::StandardTatami;
        double densityMm    = 0.40;
        bool   underlay     = true;
        bool   satinOutline = true;    ///< Erhabener Kettel-/Satinrand
    };

    static QString motifName(MotifType type);
    static QString motifDescription(MotifType type);

    /// Generates the complete, multi-color embroidery sequence for the machine
    static StitchSequence generateStitches(MotifType type, const Params& p);
    static StitchSequence generateStitches(MotifType type);

    /// Generates fully editable Bézier paths ready for 2D node-by-node editing in PathEditorWidget
    static QVector<EditPath> generateEditablePaths(MotifType type, double targetWidthMm = 80.0);

    /// Helper to convert any QPainterPath into a list of editable Bézier paths
    static QVector<EditPath> pathToEditPaths(const QPainterPath& path);
};

} // namespace stick
