// ---------------------------------------------------------------------------
//  StickCore  –  HuntingMotifs.cpp
// ---------------------------------------------------------------------------
#include "generators/HuntingMotifs.h"
#include "generators/SatinGenerator.h"
#include "core/ThreadCatalog.h"

#include <QPainterPath>
#include <QTransform>
#include <cmath>

namespace stick {

QString HuntingMotifs::motifName(MotifType type)
{
    switch (type) {
        case MotifType::OakBranch:          return QStringLiteral("Eichenlaub mit Eicheln (Trachten & Schützen)");
        case MotifType::StagHead:           return QStringLiteral("Kapitaler 12-Ender Hirschkopf");
        case MotifType::WildBoar:           return QStringLiteral("Keiler / Schwarzwild mit Hauer");
        case MotifType::WaidmannsheilCrest: return QStringLiteral("Waidmannsheil-Medaillon (Eichenkranz & Flinten)");
    }
    return QStringLiteral("Jagdmotiv");
}

QString HuntingMotifs::motifDescription(MotifType type)
{
    switch (type) {
        case MotifType::OakBranch:
            return QStringLiteral("Klassisches Doppel-Eichenblatt mit Stiel und zwei reliefierten Eicheln. "
                                  "Ideal für Schützenhüte, Trachtenjanker und forstliche Abzeichen.");
        case MotifType::StagHead:
            return QStringLiteral("Majestätischer Rothirschkopf mit vollem 12-Ender-Geweih (Augsprosse, Eissprosse, "
                                  "Mittelsprosse und Krone). Perfekt in Gold- oder Bronzegarn.");
        case MotifType::WildBoar:
            return QStringLiteral("Kraftvolle Silhouette eines reifen Keilers mit markantem Kamm, Hauer und Borsten. "
                                  "Traditionelles Emblem der Schwarzwildjagd.");
        case MotifType::WaidmannsheilCrest:
            return QStringLiteral("Kreisrundes Ehrenabzeichen mit dichtem Eichenlaubkranz, Schleife und gekreuzten "
                                  "Jagdgewehren. Meisterhaftes Wappen für Waidmänner.");
    }
    return QString();
}

QVector<EditPath> HuntingMotifs::pathToEditPaths(const QPainterPath& path)
{
    QVector<EditPath> result;
    EditPath current;

    for (int i = 0; i < path.elementCount(); ++i) {
        const auto el = path.elementAt(i);
        if (el.isMoveTo()) {
            if (!current.empty()) {
                result.push_back(current);
                current = EditPath();
            }
            BezierNode n(QPointF(el.x, el.y));
            current.nodes.push_back(n);
        } else if (el.isLineTo()) {
            if (current.empty()) current.nodes.push_back(BezierNode(QPointF(el.x, el.y)));
            else {
                current.nodes.back().ctrlOut = current.nodes.back().pos;
                BezierNode n(QPointF(el.x, el.y));
                n.ctrlIn = n.pos;
                n.ctrlOut = n.pos;
                current.nodes.push_back(n);
            }
        } else if (el.isCurveTo() && i + 2 < path.elementCount()) {
            const auto c1 = el;
            const auto c2 = path.elementAt(i + 1);
            const auto p2 = path.elementAt(i + 2);
            i += 2; // advance past the two curve data elements

            if (!current.empty()) {
                current.nodes.back().ctrlOut = QPointF(c1.x, c1.y);
            }
            BezierNode n(QPointF(p2.x, p2.y));
            n.ctrlIn = QPointF(c2.x, c2.y);
            n.ctrlOut = n.pos;
            current.nodes.push_back(n);
        }
    }
    if (!current.empty()) {
        current.closed = true;
        result.push_back(current);
    }
    return result;
}

// ---------------------------------------------------------------------------
//  Raw vector geometries (normalized ~100mm space, +Y up)
// ---------------------------------------------------------------------------
static QPainterPath buildOakLeaf(const QPointF& origin, double scale, double angleDeg)
{
    QPainterPath p;
    p.moveTo(0, 0);
    // Sinuous oak leaf lobes
    p.cubicTo(-6, 8, -16, 10, -12, 18);
    p.cubicTo(-22, 22, -26, 32, -18, 38);
    p.cubicTo(-26, 44, -24, 56, -14, 58);
    p.cubicTo(-16, 68, -8, 76, 0, 82); // Tip
    p.cubicTo(8, 76, 16, 68, 14, 58);
    p.cubicTo(24, 56, 26, 44, 18, 38);
    p.cubicTo(26, 32, 22, 22, 12, 18);
    p.cubicTo(16, 10, 6, 8, 0, 0);
    p.closeSubpath();

    QTransform t;
    t.translate(origin.x(), origin.y());
    t.rotate(angleDeg);
    t.scale(scale, scale);
    return t.map(p);
}

static QPainterPath buildAcornNut(const QPointF& center, double radius)
{
    QPainterPath p;
    // Oval nut with tapered tip at bottom
    p.moveTo(-radius * 0.75, 0);
    p.cubicTo(-radius * 0.75, -radius * 0.8, -radius * 0.3, -radius * 1.5, 0, -radius * 1.7);
    p.cubicTo(radius * 0.3, -radius * 1.5, radius * 0.75, -radius * 0.8, radius * 0.75, 0);
    p.closeSubpath();

    QTransform t;
    t.translate(center.x(), center.y());
    return t.map(p);
}

static QPainterPath buildAcornCap(const QPointF& center, double radius)
{
    QPainterPath p;
    // Cupule cap sitting on top of the nut
    p.moveTo(-radius * 0.9, 0);
    p.cubicTo(-radius * 0.9, radius * 0.8, radius * 0.9, radius * 0.8, radius * 0.9, 0);
    p.cubicTo(radius * 0.7, -radius * 0.2, -radius * 0.7, -radius * 0.2, -radius * 0.9, 0);
    p.closeSubpath();

    QTransform t;
    t.translate(center.x(), center.y());
    return t.map(p);
}

// ---------------------------------------------------------------------------
static QPainterPath createOakBranchPath()
{
    QPainterPath path;
    // Two large majestic oak leaves
    path.addPath(buildOakLeaf(QPointF(-10, -15), 0.75, -28));
    path.addPath(buildOakLeaf(QPointF(8, -12), 0.68, 32));

    // Curved stem branch
    QPainterPath stem;
    stem.moveTo(-20, -32);
    stem.cubicTo(-10, -22, 0, -10, 5, 10);
    stem.cubicTo(10, 25, 12, 35, 10, 42);
    stem.cubicTo(8, 35, 6, 25, 1, 10);
    stem.cubicTo(-4, -10, -14, -22, -24, -32);
    stem.closeSubpath();
    path.addPath(stem);

    // Two acorns
    path.addPath(buildAcornNut(QPointF(-14, -8), 9.0));
    path.addPath(buildAcornCap(QPointF(-14, -8), 9.0));

    path.addPath(buildAcornNut(QPointF(16, -5), 8.0));
    path.addPath(buildAcornCap(QPointF(16, -5), 8.0));
    return path;
}

// ---------------------------------------------------------------------------
static QPainterPath createStagHeadPath()
{
    QPainterPath p;
    // Head and neck silhouette (noble stag)
    p.moveTo(0, -38); // Chest base
    p.cubicTo(-12, -35, -18, -25, -16, -12); // Neck left
    p.cubicTo(-18, -5, -20, 5, -12, 10);     // Throat & jaw left
    p.cubicTo(-18, 14, -25, 20, -22, 28);    // Left ear tip
    p.cubicTo(-16, 26, -10, 20, -7, 16);     // Left ear base
    p.cubicTo(-4, 18, 0, 18, 0, 18);         // Crown of head
    p.cubicTo(0, 18, 4, 18, 7, 16);          // Right ear base
    p.cubicTo(10, 20, 16, 26, 22, 28);       // Right ear tip
    p.cubicTo(25, 20, 18, 14, 12, 10);       // Throat & jaw right
    p.cubicTo(20, 5, 18, -5, 16, -12);       // Neck right
    p.cubicTo(18, -25, 12, -35, 0, -38);     // Chest base
    p.closeSubpath();

    // Muzzle & Snout
    QPainterPath muzzle;
    muzzle.moveTo(-7, 8);
    muzzle.cubicTo(-6, 0, -5, -10, 0, -14); // Nose tip
    muzzle.cubicTo(5, -10, 6, 0, 7, 8);
    muzzle.closeSubpath();
    p.addPath(muzzle);

    // Left Antler (12-Ender)
    QPainterPath leftAntler;
    leftAntler.moveTo(-5, 18); // Burdock / base
    leftAntler.cubicTo(-10, 22, -18, 25, -22, 20); // Brow tine tip (Augsprosse)
    leftAntler.cubicTo(-18, 26, -14, 28, -12, 32);
    leftAntler.cubicTo(-22, 36, -28, 38, -30, 32); // Bez tine tip (Eissprosse)
    leftAntler.cubicTo(-24, 38, -18, 42, -16, 46);
    leftAntler.cubicTo(-28, 52, -36, 56, -38, 48); // Trez tine tip (Mittelsprosse)
    leftAntler.cubicTo(-30, 56, -24, 62, -22, 68); // Crown tine 1
    leftAntler.cubicTo(-20, 66, -16, 72, -14, 76); // Crown tine 2 (top)
    leftAntler.cubicTo(-14, 70, -10, 66, -8, 62);  // Crown tine 3
    leftAntler.cubicTo(-12, 52, -10, 38, -4, 20);  // Inner beam back to base
    leftAntler.closeSubpath();
    p.addPath(leftAntler);

    // Right Antler (Mirrored)
    QTransform mirror;
    mirror.scale(-1.0, 1.0);
    p.addPath(mirror.map(leftAntler));

    return p;
}

// ---------------------------------------------------------------------------
static QPainterPath createWildBoarPath()
{
    QPainterPath p;
    // Wild boar full profile silhouette
    p.moveTo(-42, -4);  // Snout tip
    p.cubicTo(-40, 2, -36, 8, -28, 14);     // Sloping forehead
    p.cubicTo(-22, 20, -12, 24, 0, 24);     // Bristled crest / back ridge (Kamm)
    p.cubicTo(15, 23, 30, 18, 38, 8);       // Rump / hindquarters
    p.cubicTo(44, 10, 46, 5, 42, 0);        // Tail (Pürzel)
    p.cubicTo(36, 2, 32, -4, 34, -14);      // Back leg
    p.cubicTo(30, -15, 24, -12, 18, -10);   // Belly
    p.cubicTo(10, -10, 0, -12, -8, -16);    // Chest
    p.cubicTo(-14, -18, -18, -22, -24, -18);// Front leg
    p.cubicTo(-26, -12, -32, -8, -42, -4);  // Lower jaw to snout
    p.closeSubpath();

    // Prominent upward curved tusk (Hauer / Gewölle)
    QPainterPath tusk;
    tusk.moveTo(-36, -6);
    tusk.cubicTo(-34, -2, -32, 4, -30, 6);
    tusk.cubicTo(-32, 3, -35, -2, -37, -5);
    tusk.closeSubpath();
    p.addPath(tusk);

    return p;
}

// ---------------------------------------------------------------------------
static QPainterPath createWaidmannsheilCrestPath()
{
    QPainterPath p;
    // Oak wreath circle
    const int leafCount = 14;
    const double r = 36.0;
    for (int i = 0; i < leafCount; ++i) {
        const double aDeg = (double(i) / leafCount) * 360.0;
        const double rad = aDeg * M_PI / 180.0;
        const QPointF pt(std::cos(rad) * r, std::sin(rad) * r);
        p.addPath(buildOakLeaf(pt, 0.28, aDeg + 90.0));
    }

    // Crossed hunting shotguns / rifles in center
    QPainterPath gun1;
    gun1.moveTo(-24, -24);
    gun1.lineTo(24, 24);
    gun1.lineTo(22, 26);
    gun1.lineTo(-26, -22);
    gun1.closeSubpath();
    p.addPath(gun1);

    QPainterPath gun2;
    gun2.moveTo(24, -24);
    gun2.lineTo(-24, 24);
    gun2.lineTo(-22, 26);
    gun2.lineTo(26, -22);
    gun2.closeSubpath();
    p.addPath(gun2);

    // Elegant ribbon bow at bottom
    QPainterPath bow;
    bow.moveTo(0, -36);
    bow.cubicTo(-8, -42, -18, -40, -14, -32);
    bow.cubicTo(-10, -34, -4, -34, 0, -36);
    bow.cubicTo(4, -34, 10, -34, 14, -32);
    bow.cubicTo(18, -40, 8, -42, 0, -36);
    bow.closeSubpath();
    p.addPath(bow);

    return p;
}

// ---------------------------------------------------------------------------
static QPainterPath getRawPathForMotif(HuntingMotifs::MotifType type, double targetWidthMm)
{
    QPainterPath raw;
    switch (type) {
        case HuntingMotifs::MotifType::OakBranch:          raw = createOakBranchPath(); break;
        case HuntingMotifs::MotifType::StagHead:           raw = createStagHeadPath(); break;
        case HuntingMotifs::MotifType::WildBoar:           raw = createWildBoarPath(); break;
        case HuntingMotifs::MotifType::WaidmannsheilCrest: raw = createWaidmannsheilCrestPath(); break;
    }

    const QRectF b = raw.boundingRect();
    if (b.width() < 1e-4) return raw;

    const double scale = targetWidthMm / b.width();
    QTransform t;
    t.scale(scale, scale);
    t.translate(-b.center().x(), -b.center().y());
    return t.map(raw);
}

// ---------------------------------------------------------------------------
QVector<EditPath> HuntingMotifs::generateEditablePaths(MotifType type, double targetWidthMm)
{
    const QPainterPath path = getRawPathForMotif(type, targetWidthMm);
    return pathToEditPaths(path);
}

// ---------------------------------------------------------------------------
StitchSequence HuntingMotifs::generateStitches(MotifType type)
{
    return generateStitches(type, Params{});
}

// ---------------------------------------------------------------------------
StitchSequence HuntingMotifs::generateStitches(MotifType type, const Params& p)
{
    StitchSequence seq;
    const QPainterPath path = getRawPathForMotif(type, p.widthMm);
    const QPolygonF outerPoly = path.toFillPolygon();
    if (outerPoly.size() < 3) return seq;

    // Palette & Thread Assignment
    QColor mainColor(34, 85, 34);   // Forest Green
    QColor accentColor(160, 95, 35); // Gold Bronze

    if (type == MotifType::StagHead) {
        mainColor = QColor(140, 75, 25);    // Chestnut Bronze
        accentColor = QColor(210, 170, 70); // Golden Horn
    } else if (type == MotifType::WildBoar) {
        mainColor = QColor(35, 35, 40);     // Charcoal Bristle
        accentColor = QColor(240, 240, 245);// White Tusk
    } else if (type == MotifType::WaidmannsheilCrest) {
        mainColor = QColor(28, 75, 35);     // Imperial Hunter Green
        accentColor = QColor(195, 155, 45); // Antique Brass / Gold
    }

    // 1. Underlay & Tatami Fill with requested angle and pattern
    TatamiFill::Params tp;
    tp.fillAngleDeg = p.fillAngleDeg;
    tp.pattern      = p.pattern;
    tp.rowSpacingMm = p.densityMm;
    tp.underlay     = p.underlay;
    tp.colorIdx     = 0;

    QVector<QPolygonF> region;
    // Subdivide painter path into polygons
    const QList<QPolygonF> polys = path.toSubpathPolygons();
    for (const QPolygonF& poly : polys) {
        if (poly.size() >= 3) region.push_back(poly);
    }
    if (region.isEmpty()) region.push_back(outerPoly);

    StitchSequence fillSeq = TatamiFill::generate(region, tp);
    seq.stitches.insert(seq.stitches.end(), fillSeq.stitches.begin(), fillSeq.stitches.end());

    // 2. Optional Raised Satin Outline Border along exterior
    if (p.satinOutline) {
        StitchSequence border = SatinGenerator::fromCenterline(path, 1.4, 0.40, 1);
        seq.stitches.insert(seq.stitches.end(), border.stitches.begin(), border.stitches.end());
    }

    seq.palette.push_back(ThreadCatalog::snap(mainColor));
    if (p.satinOutline) {
        seq.palette.push_back(ThreadCatalog::snap(accentColor));
    }

    return seq;
}

} // namespace stick
