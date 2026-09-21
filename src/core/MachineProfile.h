// ---------------------------------------------------------------------------
//  StickCore  –  MachineProfile.h
//
//  Everything StickCore needs to be "set up for" a specific embroidery machine.
//  The default profile is the Janome Memory Craft 350E (verified against real
//  .jef files and the machine's published specs).
// ---------------------------------------------------------------------------
#pragma once

#include "core/StitchTypes.h"
#include <QString>
#include <QStringList>
#include <vector>

namespace stick {

struct MachineProfile {
    QString name          = QStringLiteral("Janome MC350E");
    QString format        = QStringLiteral("JEF (.jef)");
    double  maxWidthMm    = 140.0;   // 5.5"
    double  maxHeightMm   = 200.0;   // 7.9"
    int     maxSpeedSpm   = 650;     // stitches per minute

    // Hoops physically available on Janome Memory Craft (in preferred size order).
    std::vector<HoopType> hoops = {
        HoopType::HoopA_126x110,  // Standard (126 x 110 mm)
        HoopType::HoopB_140x200,  // Large (140 x 200 mm)
        HoopType::HoopC_50x50,    // Free Arm / Pocket (50 x 50 mm)
        HoopType::HoopSQ14_140,   // Square (140 x 140 mm)
        HoopType::HoopF_126x110,  // Spring Loaded (126 x 110 mm)
        HoopType::HoopD_230x200,  // Giga (230 x 200 mm)
        HoopType::HoopSQ23_230    // Macro Square (230 x 230 mm)
    };

    // Thread brands the machine's colour matching supports.
    QStringList threadBrands = { QStringLiteral("Janome"),
                                 QStringLiteral("Robison-Anton"),
                                 QStringLiteral("Madeira"),
                                 QStringLiteral("Mettler") };
    QString defaultBrand  = QStringLiteral("Janome");

    // Sensible stitch defaults tuned for this machine / 40wt thread.
    double  satinDensityMm = 0.40;
    double  pullCompMm      = 0.15;
    double  fillRowMm       = 0.40;
    double  maxStitchMm     = 7.0;   // MC350E accepts up to ~12.7mm; 7mm is safe

    /// The single machine profile in use (MC350E by default).
    static const MachineProfile& current()
    {
        static const MachineProfile p;
        return p;
    }

    /// Does a design of w×h mm fit any hoop on this machine (rotation allowed)?
    bool fits(double w, double h) const
    {
        for (HoopType t : hoops) {
            const HoopSpec s = hoopSpec(t);
            if ((w <= s.widthMm + 1e-6 && h <= s.heightMm + 1e-6) ||
                (h <= s.widthMm + 1e-6 && w <= s.heightMm + 1e-6))
                return true;
        }
        return false;
    }

    /// Smallest hoop on this machine that holds w×h mm (falls back to largest).
    HoopType hoopFor(double w, double h) const
    {
        HoopType largest = hoops.empty() ? HoopType::HoopB_140x200 : hoops.front();
        double largestArea = -1;
        for (HoopType t : hoops) {
            const HoopSpec s = hoopSpec(t);
            const double area = s.widthMm * s.heightMm;
            if (area > largestArea) { largestArea = area; largest = t; }
            if ((w <= s.widthMm + 1e-6 && h <= s.heightMm + 1e-6) ||
                (h <= s.widthMm + 1e-6 && w <= s.heightMm + 1e-6))
                return t;
        }
        return largest;
    }
};

} // namespace stick
