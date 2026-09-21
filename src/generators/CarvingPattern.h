// ---------------------------------------------------------------------------
//  StickCore  –  CarvingPattern.h
//
//  Tatami Carving & Texture Embossing (Prägemuster im Füllstich):
//  Embosses ornamental vector motifs (Oak leaf, Star, Heart, Diamond, Waves)
//  directly into smooth Tatami fills by aligning needle penetrations to the
//  embossing lines.
// ---------------------------------------------------------------------------
#pragma once

#include <QPainterPath>
#include <QString>
#include <QVector>

namespace stick {

class CarvingPattern {
public:
    enum class Preset {
        None,
        Star,         ///< Fünfzackiger Stern
        Heart,        ///< Klassisches Zierherz
        OakLeaf,      ///< Traditionelles Eichenblatt
        DiamondGrid,  ///< Rauten-Prägung / Jacquard-Gitter
        WaveLines,    ///< Fließende Wellenlinien
        Custom        ///< Benutzerdefinierter Vektorpfad
    };

    static QString presetName(Preset p);

    /// Generates a centered, scaled QPainterPath for the given preset
    static QPainterPath createPath(Preset preset, double widthMm = 25.0, double heightMm = 25.0,
                                   const QPointF& center = QPointF(0, 0));

    /// Calculates X intersection coordinates for a horizontal scanline Y against the carving path
    static QVector<double> findIntersections(const QPainterPath& path, double scanY);
};

} // namespace stick
