// ---------------------------------------------------------------------------
//  StickCore  –  DesignFactory.cpp
// ---------------------------------------------------------------------------
#include "library/DesignFactory.h"
#include "generators/TatamiFill.h"
#include "generators/TextDigitizer.h"
#include "generators/LogoGenerator.h"
#include "core/ThreadCatalog.h"
#include "core/Geometry.h"

#include <QPainterPath>
#include <QPolygonF>
#include <cmath>

namespace stick {
namespace {

constexpr double PI = 3.14159265358979323846;

// ---- shape outlines (mm, centred near origin) -----------------------------
QPolygonF circlePoly(double r, int n = 56) {
    QPolygonF p; for (int i = 0; i < n; ++i) { double a = 2*PI*i/n; p << QPointF(r*std::cos(a), r*std::sin(a)); } return p;
}
QPolygonF regPoly(int sides, double r, double rotDeg = 0) {
    QPolygonF p; const double rot = rotDeg*PI/180.0;
    for (int i = 0; i < sides; ++i) { double a = rot + 2*PI*i/sides; p << QPointF(r*std::cos(a), r*std::sin(a)); } return p;
}
QPolygonF starPoly(double rO, double rI, int pts) {
    QPolygonF p; for (int i = 0; i < pts*2; ++i) { double a = PI/2 + PI*i/pts; double r = (i%2)?rI:rO; p << QPointF(r*std::cos(a), r*std::sin(a)); } return p;
}
QPolygonF heartPoly(double sc = 1.3) {
    QPolygonF p; const int N = 90;
    for (int i = 0; i <= N; ++i) { double t = 2*PI*i/N;
        double x = 16*std::pow(std::sin(t),3);
        double y = 13*std::cos(t) - 5*std::cos(2*t) - 2*std::cos(3*t) - std::cos(4*t);
        p << QPointF(x*sc, y*sc); } return p;
}
QPolygonF flowerPoly(int petals, double rIn, double rOut) {
    QPolygonF p; const int N = 200;
    for (int i = 0; i < N; ++i) { double t = 2*PI*i/N;
        double bump = 0.5*(1+std::cos(petals*t)); double r = rIn + (rOut-rIn)*std::pow(bump,0.6);
        p << QPointF(r*std::cos(t), r*std::sin(t)); } return p;
}
QPolygonF ellipsePoly(double a, double b, double rotDeg, int n = 60) {
    QPolygonF p; const double c = std::cos(rotDeg*PI/180.0), s = std::sin(rotDeg*PI/180.0);
    for (int i = 0; i < n; ++i) { double t = 2*PI*i/n; double x = a*std::cos(t), y = b*std::sin(t);
        p << QPointF(x*c - y*s, x*s + y*c); } return p;
}
QPolygonF eggPoly(double a, double b, int n = 60) {
    QPolygonF p; for (int i = 0; i < n; ++i) { double t = 2*PI*i/n;
        double x = a*std::cos(t)*(1 - 0.16*std::sin(t)); double y = b*std::sin(t); p << QPointF(x, y); } return p;
}
QPolygonF cloverPoly() {
    QPolygonF p; auto lobe=[&](double cx,double cy,double r){ for(int i=0;i<=40;i++){double a=2*PI*i/40; p<<QPointF(cx+r*std::cos(a),cy+r*std::sin(a));}};
    lobe(0,10,10); lobe(-9,-4,10); lobe(9,-4,10); return p;   // rough trefoil
}
QPolygonF crescentPoly(double R, double dx) {
    QPolygonF p; const int N = 50;
    for (int i = 0; i <= N; ++i) { double a = PI*0.5 + PI*i/N; p << QPointF(R*std::cos(a), R*std::sin(a)); }
    for (int i = 0; i <= N; ++i) { double a = PI*1.5 - PI*i/N; p << QPointF(dx + R*std::cos(a), R*std::sin(a)); }
    return p;
}
QPolygonF treePoly() {
    QPolygonF p;
    p << QPointF(0,32) << QPointF(-9,18) << QPointF(-4,18) << QPointF(-14,6) << QPointF(-6,6)
      << QPointF(-18,-8) << QPointF(-4,-8) << QPointF(-4,-18) << QPointF(4,-18) << QPointF(4,-8)
      << QPointF(18,-8) << QPointF(6,6) << QPointF(14,6) << QPointF(4,18) << QPointF(9,18);
    return p;
}
QPolygonF housePoly() {
    QPolygonF p;
    p << QPointF(-15,-16) << QPointF(15,-16) << QPointF(15,8) << QPointF(19,8)
      << QPointF(0,24) << QPointF(-19,8) << QPointF(-15,8);
    return p;
}
QPolygonF arrowPoly() {
    QPolygonF p;
    p << QPointF(-22,-5) << QPointF(6,-5) << QPointF(6,-13) << QPointF(22,0)
      << QPointF(6,13) << QPointF(6,5) << QPointF(-22,5);
    return p;
}
QPolygonF boltPoly() {
    QPolygonF p;
    p << QPointF(3,22) << QPointF(-9,3) << QPointF(-1,3) << QPointF(-6,-22)
      << QPointF(11,1) << QPointF(2,1);
    return p;
}

using Seg = std::vector<QPointF>;
using Segs = std::vector<Seg>;

// ---- builders -------------------------------------------------------------
void satinBorder(const QPolygonF& poly, double width, double pitch, StitchSequence& seq, int c) {
    if (poly.size() < 2) return;
    QPainterPath path; path.moveTo(poly.first());
    for (int i = 1; i < poly.size(); ++i) path.lineTo(poly[i]);
    path.closeSubpath();
    ArcLengthCurve cv(path); if (!cv.isValid()) return;
    const int steps = std::max(2, int(std::llround(cv.length()/std::max(pitch,1e-3))));
    bool outer = true, first = true;
    for (int i = 0; i <= steps; ++i) { double s = double(i)/steps;
        QPointF p = cv.pointAt(s), n = cv.normalAt(s);
        QPointF t = p + n*(outer ? width*0.5 : -width*0.5); outer = !outer;
        seq.add(t.x(), t.y(), first ? SF_Jump : SF_Normal, c); first = false; }
}

StitchSequence fillShape(const QPolygonF& poly, QColor color, double angle, bool border = true) {
    TatamiFill::Params tp; tp.fillAngleDeg = angle; tp.rowSpacingMm = 0.5; tp.maxStitchMm = 4.0;
    QVector<QPolygonF> region; region << poly;
    StitchSequence seq = TatamiFill::generate(region, tp);
    if (border) satinBorder(poly, 1.5, 0.4, seq, 0);
    seq.palette.clear(); seq.palette.push_back(ThreadCatalog::snap(color));
    return seq;
}
StitchSequence lineShape(const Segs& segs, QColor color, double maxStitch = 3.0) {
    StitchSequence seq;
    for (const Seg& poly : segs) {
        if (poly.size() < 2) continue;
        seq.add(poly[0].x(), poly[0].y(), SF_Jump, 0);
        for (std::size_t i = 1; i < poly.size(); ++i) {
            const QPointF a = poly[i-1], b = poly[i];
            const double L = std::hypot(b.x()-a.x(), b.y()-a.y());
            const int n = std::max(1, int(std::ceil(L/maxStitch)));
            for (int k = 1; k <= n; ++k) { double f = double(k)/n;
                seq.add(a.x()+(b.x()-a.x())*f, a.y()+(b.y()-a.y())*f, SF_Normal, 0); }
        }
    }
    seq.palette.clear(); seq.palette.push_back(ThreadCatalog::snap(color));
    return seq;
}
StitchSequence textShape(const QString& t, QColor color, double h = 26) {
    TextDigitizer::Params p; p.text = t; p.heightMm = h; p.raised = true;
    StitchSequence seq = TextDigitizer::generate(p);
    seq.palette.clear(); seq.palette.push_back(ThreadCatalog::snap(color));
    return seq;
}

Design D(const QString& n, const QString& cat, QStringList tags, StitchSequence s) {
    return Design{ n, cat, std::move(tags), std::move(s) };
}

} // namespace

QStringList DesignFactory::categories()
{
    return { QStringLiteral("Logos"), QStringLiteral("Formen"),
             QStringLiteral("Monogramme"), QStringLiteral("Ränder"),
             QStringLiteral("Feiertage"), QStringLiteral("Symbole") };
}

std::vector<Design> DesignFactory::starterSet()
{
    std::vector<Design> v;
    const QString F=QStringLiteral("Formen"), M=QStringLiteral("Monogramme"),
                  R=QStringLiteral("Ränder"), H=QStringLiteral("Feiertage"), S=QStringLiteral("Symbole"),
                  L=QStringLiteral("Logos");

    // --- Logos ---
    v.push_back(D(QString::fromUtf8("Modewerkstatt Knüppel (Foto-Original Meister-Aufnäher)"), L,
                  {"logo","mef","daniela","atelier","modewerkstatt","knüppel","aufnäher","satin","kettelrand","original"},
                  LogoGenerator::mefOriginalBadge(0, 150.0)));
    v.push_back(D(QString::fromUtf8("Modewerkstatt Knüppel (Florentiner Kontur-Glanz)"), L,
                  {"logo","mef","daniela","atelier","modewerkstatt","knüppel","vektor","gold","kontur"},
                  LogoGenerator::mefOriginalBadge(4, 150.0)));
    v.push_back(D(QString::fromUtf8("Modewerkstatt Knüppel (Zwei-Ton-Relief)"), L,
                  {"logo","mef","daniela","relief","duotone","gold"},
                  LogoGenerator::mefOriginalBadge(2, 150.0)));
    v.push_back(D(QString::fromUtf8("Modewerkstatt Knüppel (Meister-Tatami)"), L,
                  {"logo","mef","daniela","tatami","webung"},
                  LogoGenerator::mefOriginalBadge(1, 150.0)));
    v.push_back(D(QString::fromUtf8("Modewerkstatt Knüppel (Kalligraphie-Badge)"), L,
                  {"logo","daniela","atelier","modewerkstatt","knüppel","haute couture"},
                  LogoGenerator::knueppelBadge()));

    // --- Formen ---
    v.push_back(D("Herz",     F, {"herz","liebe","form"},      fillShape(heartPoly(1.3),           QColor(200,45,60), 40)));
    v.push_back(D("Stern",    F, {"stern","form"},             fillShape(starPoly(24,10,5),        QColor(240,190,50), 0)));
    v.push_back(D("Kreis",    F, {"kreis","punkt","form"},     fillShape(circlePoly(22),           QColor(60,120,200), 30)));
    v.push_back(D("Quadrat",  F, {"quadrat","form"},           fillShape(regPoly(4,24,45),         QColor(90,170,110), 0)));
    v.push_back(D("Dreieck",  F, {"dreieck","form"},           fillShape(regPoly(3,26,90),         QColor(180,120,60), 20)));
    v.push_back(D("Sechseck", F, {"sechseck","form"},          fillShape(regPoly(6,24,0),          QColor(150,90,180), 15)));
    v.push_back(D("Blume",    F, {"blume","natur","form"},     fillShape(flowerPoly(6,10,26),      QColor(220,90,140), 0)));
    v.push_back(D("Blatt",    F, {"blatt","natur","form"},     fillShape(ellipsePoly(10,26,35),    QColor(70,150,80), 55)));
    v.push_back(D("Kleeblatt",F, {"klee","glück","natur"},     fillShape(cloverPoly(),             QColor(60,150,80), 25)));

    // --- Monogramme ---
    v.push_back(D("Buchstabe A", M, {"a","monogramm","text"},  textShape("A", QColor(150,40,60))));
    v.push_back(D("Buchstabe M", M, {"m","monogramm","text"},  textShape("M", QColor(40,90,160))));
    v.push_back(D("Buchstabe S", M, {"s","monogramm","text"},  textShape("S", QColor(60,140,90))));
    v.push_back(D("„LOVE\"",     M, {"love","liebe","wort"},   textShape("LOVE", QColor(200,45,90), 20)));

    // --- Ränder ---
    { Segs s; Seg w; for (int i=0;i<=80;i++){ double x=-40+i; w.push_back(QPointF(x, 6*std::sin(x*0.4))); } s.push_back(w);
      v.push_back(D("Wellenband", R, {"welle","rand","bordüre"}, lineShape(s, QColor(60,120,200)))); }
    { Segs s; Seg z; for (int i=0;i<=20;i++){ double x=-40+i*4; z.push_back(QPointF(x, (i%2)?7:-7)); } s.push_back(z);
      v.push_back(D("Zickzack", R, {"zickzack","rand"}, lineShape(s, QColor(220,120,40)))); }
    { Segs s; for (int i=0;i<16;i++){ double x=-38+i*5; s.push_back({QPointF(x,0),QPointF(x+2,0)}); }
      v.push_back(D("Punktlinie", R, {"punkte","rand","gestrichelt"}, lineShape(s, QColor(120,80,160)))); }

    // --- Feiertage ---
    { Segs s; for (int a=0;a<6;a++){ double ang=PI*a/3; double dx=std::cos(ang),dy=std::sin(ang);
        s.push_back({QPointF(0,0),QPointF(24*dx,24*dy)});
        s.push_back({QPointF(14*dx,14*dy),QPointF(14*dx+6*std::cos(ang+0.6),14*dy+6*std::sin(ang+0.6))});
        s.push_back({QPointF(14*dx,14*dy),QPointF(14*dx+6*std::cos(ang-0.6),14*dy+6*std::sin(ang-0.6))}); }
      v.push_back(D("Schneeflocke", H, {"schnee","winter","weihnachten"}, lineShape(s, QColor(120,180,230)))); }
    v.push_back(D("Tannenbaum", H, {"baum","weihnachten","winter"}, fillShape(treePoly(), QColor(40,130,70), 0)));
    v.push_back(D("Osterei",    H, {"ei","ostern","frühling"},   fillShape(eggPoly(16,22),       QColor(230,150,70), 25)));
    v.push_back(D("Herz rot",   H, {"herz","valentin","liebe"},  fillShape(heartPoly(1.3),        QColor(210,40,50), 40)));

    // --- Symbole ---
    { StitchSequence sun = fillShape(circlePoly(14), QColor(245,190,40), 0, false);
      for (int a=0;a<12;a++){ double ang=2*PI*a/12; double dx=std::cos(ang),dy=std::sin(ang);
        sun.add(17*dx,17*dy,SF_Jump,0); sun.add(25*dx,25*dy,SF_Normal,0); }
      sun.palette.clear(); sun.palette.push_back(ThreadCatalog::snap(QColor(245,190,40)));
      v.push_back(D("Sonne", S, {"sonne","sommer","symbol"}, sun)); }
    v.push_back(D("Mond",  S, {"mond","nacht","symbol"},  fillShape(crescentPoly(20,16), QColor(230,205,90), 20)));
    v.push_back(D("Blitz", S, {"blitz","symbol"},         fillShape(boltPoly(),          QColor(240,200,50), 30)));
    v.push_back(D("Pfeil", S, {"pfeil","symbol"},         fillShape(arrowPoly(),         QColor(70,130,200), 0)));
    v.push_back(D("Haus",  S, {"haus","symbol"},          fillShape(housePoly(),         QColor(180,90,70), 15)));

    return v;
}

} // namespace stick
