// ---------------------------------------------------------------------------
//  StickCore  –  Underlay.h
//
//  Underlay ("Unterlage") is stitched BEFORE the visible top stitches. It
//  tacks the fabric to the backing and gives the cover stitches a firm base,
//  so the result looks crisp and does not sink into the fabric. This mirrors
//  how professional digitizing software works: contours/underlay first, then
//  the intelligent fill on top.
//
//  Techniques implemented (per common digitizing practice):
//   • Edge run     – a running stitch just inside the outline (a "break wall").
//   • Cross layer  – a sparse fill perpendicular to the top fill direction.
//   • Satin center / zigzag underlay – see SatinGenerator.
// ---------------------------------------------------------------------------
#pragma once

#include "core/StitchTypes.h"
#include <QPolygonF>
#include <QVector>

namespace stick {

class Underlay {
public:
    /// Underlay for a filled region: edge run around every ring plus a sparse
    /// cross layer (~90° to the cover fill). Returns the underlay stitches.
    static StitchSequence forFill(const QVector<QPolygonF>& region,
                                  double fillAngleDeg, int colorIdx,
                                  double stepMm = 2.5, double insetMm = 0.6);

    /// Append an edge-run (running stitch) around one closed polygon with an inward inset.
    static void edgeRun(const QPolygonF& poly, double stepMm, int colorIdx,
                        StitchSequence& out, double insetMm = 0.6);
};

} // namespace stick
