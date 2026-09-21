// ---------------------------------------------------------------------------
//  StickCore  –  LogoGenerator.cpp
// ---------------------------------------------------------------------------
#include "generators/LogoGenerator.h"
#include "generators/TextDigitizer.h"
#include "generators/ContourFill.h"
#include "generators/TatamiFill.h"
#include "generators/SvgDigitizer.h"
#include "core/SvgPathParser.h"
#include <QFile>

#include <QPainterPath>
#include <QPolygonF>
#include <QVector>
#include <cmath>

namespace stick {
namespace {

constexpr double PI = 3.14159265358979323846;

// Warp horizontal text (centred at origin, +Y up) onto a circular arc.
// top=true arches over the top; top=false smiles along the bottom.
QPainterPath warpArc(const QPainterPath& in, double R, bool top)
{
    QPainterPath out;
    for (const QPolygonF& poly : in.toSubpathPolygons()) {
        QPolygonF w;
        for (const QPointF& p : poly) {
            const double th = p.x() / R;
            const double r  = R + (top ? p.y() : -p.y());
            double X, Y;
            if (top) { X = r*std::sin(th); Y = r*std::cos(th) - R; }
            else     { X = r*std::sin(th); Y = R - r*std::cos(th); }
            w << QPointF(X, Y);
        }
        if (!w.isEmpty()) {
            out.moveTo(w.first());
            for (int i = 1; i < w.size(); ++i) out.lineTo(w[i]);
            out.closeSubpath();
        }
    }
    out.setFillRule(in.fillRule());
    return out;
}

void translate(StitchSequence& s, double dx, double dy)
{ for (Stitch& st : s.stitches) { st.x += dx; st.y += dy; } }

void append(StitchSequence& d, const StitchSequence& s)
{ d.stitches.insert(d.stitches.end(), s.stitches.begin(), s.stitches.end()); }

// Solid tatami fill of a glyph path (clean look for small/fine text).
StitchSequence fillSolid(const QPainterPath& p, double density)
{
    QVector<QPolygonF> region;
    for (const QPolygonF& poly : p.toSubpathPolygons()) region << poly;
    TatamiFill::Params tp; tp.rowSpacingMm = density; tp.maxStitchMm = 2.6;
    tp.underlay = false; tp.fillAngleDeg = 0.0;
    return TatamiFill::generate(region, tp);
}

// Artistic contour (echo) fill — for the big word.
StitchSequence fillPath(const QPainterPath& p, double spacing)
{
    ContourFill::Params cp; cp.spacingMm = spacing; cp.maxStitchMm = 2.6; cp.colorIdx = 0;
    StitchSequence s = ContourFill::generate(p, cp);
    if (!s.empty()) return s;
    // fallback if OpenCV is missing: a solid fill
    return fillSolid(p, 0.4);
}

StitchSequence textLine(const QString& t, const QString& fam, double h,
                        double cx, double cy, double spacing, bool solid)
{
    TextDigitizer::Params tp; tp.text = t; tp.families = { fam }; tp.heightMm = h;
    const QPainterPath path = TextDigitizer::textPath(tp);
    StitchSequence s = solid ? fillSolid(path, 0.4) : fillPath(path, spacing);
    translate(s, cx, cy);
    return s;
}

StitchSequence textArc(const QString& t, const QString& fam, double h,
                       double R, bool top, double cy, double spacing, bool solid)
{
    TextDigitizer::Params tp; tp.text = t; tp.families = { fam }; tp.heightMm = h;
    const QPainterPath path = warpArc(TextDigitizer::textPath(tp), R, top);
    StitchSequence s = solid ? fillSolid(path, 0.4) : fillPath(path, spacing);
    translate(s, 0, cy);
    return s;
}

// Authentic dense radial satin oval border (Kettelrand / Satinkante) with stabilizing underlays
StitchSequence satinOvalBorder(double a_out, double b_out, double widthMm, double pitchMm, int colorIdx)
{
    StitchSequence s;
    const double a_in = a_out - widthMm;
    const double b_in = b_out - widthMm;
    const double a_mid = 0.5 * (a_out + a_in);
    const double b_mid = 0.5 * (b_out + b_in);

    // 1. Underlay Center-Walk Track
    const double circMid = PI * (3.0*(a_mid+b_mid) - std::sqrt((3.0*a_mid+b_mid)*(a_mid+3.0*b_mid)));
    const int nMid = std::max(60, int(circMid / 2.5));
    s.add(a_mid, 0.0, SF_Jump, colorIdx);
    for (int i = 1; i <= nMid; ++i) {
        double t = 2.0 * PI * i / nMid;
        s.add(a_mid * std::cos(t), b_mid * std::sin(t), SF_Normal, colorIdx);
    }
    // Inner & Outer Edge-Walk Underlay
    const double a_eIn = a_in + 0.4, b_eIn = b_in + 0.4;
    for (int i = 0; i <= nMid; ++i) {
        double t = 2.0 * PI * i / nMid;
        s.add(a_eIn * std::cos(t), b_eIn * std::sin(t), SF_Normal, colorIdx);
    }
    const double a_eOut = a_out - 0.4, b_eOut = b_out - 0.4;
    for (int i = 0; i <= nMid; ++i) {
        double t = 2.0 * PI * i / nMid;
        s.add(a_eOut * std::cos(t), b_eOut * std::sin(t), SF_Normal, colorIdx);
    }

    // 2. Cover Satin: Dense radial rungs with pull compensation
    const double pull = 0.15;
    const int nRungs = std::max(120, int(circMid / pitchMm));
    for (int i = 0; i <= nRungs; ++i) {
        const double t = 2.0 * PI * i / nRungs;
        const double cosT = std::cos(t);
        const double sinT = std::sin(t);
        const double xOut = (a_out + pull) * cosT;
        const double yOut = (b_out + pull) * sinT;
        const double xIn  = (a_in - pull)  * cosT;
        const double yIn  = (b_in - pull)  * sinT;

        s.add(xIn,  yIn,  SF_Normal, colorIdx);
        s.add(xOut, yOut, SF_Normal, colorIdx);
    }
    s.add(a_in - pull, 0.0, SF_Normal, colorIdx);
    return s;
}

StitchSequence innerAccentRing(double a, double b, double stepMm, int colorIdx)
{
    StitchSequence s;
    const double circ = PI * (3.0*(a+b) - std::sqrt((3.0*a+b)*(a+3.0*b)));
    const int N = std::max(80, int(circ / stepMm));
    s.add(a, 0.0, SF_Jump, colorIdx);
    for (int i = 1; i <= N; ++i) {
        double t = 2.0 * PI * i / N;
        s.add(a * std::cos(t), b * std::sin(t), SF_Normal, colorIdx);
    }
    for (int i = N - 1; i >= 0; --i) {
        double t = 2.0 * PI * i / N;
        s.add(a * std::cos(t), b * std::sin(t), SF_Normal, colorIdx);
    }
    return s;
}

} // namespace

StitchSequence LogoGenerator::generate(const Badge& b)
{
    StitchSequence L;

    // Oval sized to the requested width (aspect ~1.53, like the reference).
    const double a_out = b.widthMm * 0.5;
    const double b_out = a_out / 1.535;
    const double borderW = 3.8;
    const double a_in  = a_out - borderW;
    const double b_in  = b_out - borderW;

    // 1. Heavy Satin Oval Border (Kettelrand)
    append(L, satinOvalBorder(a_out, b_out, borderW, 0.36, 0));

    // 2. Inner Accent Ring (1.8 mm inset)
    append(L, innerAccentRing(a_in - 1.8, b_in - 1.8, 2.2, 0));

    const QString F = b.family;
    const double sc = b_out / 43.0;

    // 3. Letters with solid raised satin
    if (!b.topArc.isEmpty())
        append(L, textArc(b.topArc, F, 9.5*sc, 66*sc, true,  25*sc, 0.35, true));
    if (!b.centerBig.isEmpty())
        append(L, textLine(b.centerBig, F, 16.5*sc, 0, 1*sc, 0.35, true));
    if (!b.centerSmall.isEmpty())
        append(L, textLine(b.centerSmall, F, 9*sc, 0, -15*sc, 0.35, true));
    if (!b.bottomArc.isEmpty())
        append(L, textArc(b.bottomArc, F, 10*sc, 58*sc, false, -30*sc, 0.35, true));

    L.palette.clear();
    L.palette.emplace_back(b.gold, QStringLiteral("Madeira 1083 Gold"));
    return L;
}

StitchSequence LogoGenerator::knueppelBadge(const QColor& gold)
{
    Badge b;
    b.topArc      = QStringLiteral("Meisterbetrieb");
    b.centerBig   = QStringLiteral("Modewerkstatt");
    b.centerSmall = QString::fromUtf8("Knüppel");
    b.bottomArc   = QStringLiteral("Haute Couture");
    b.family      = QStringLiteral("Alex Brush");
    b.widthMm     = 132.0;
    b.gold        = gold;
    return generate(b);
}

StitchSequence LogoGenerator::mefOriginalBadge(int style, double widthMm, const QColor& gold)
{
    const QStringList paths = {
        QStringLiteral(":/svg/meflogo.svg"),
        QStringLiteral("media/meflogoplain.svg"),
        QStringLiteral("../media/meflogoplain.svg"),
        QStringLiteral("C:/Users/micbu/Desktop/meflogoplain.svg")
    };

    QPainterPath path;
    for (const QString& p : paths) {
        if (QFile::exists(p)) {
            path = SvgPathParser::parseSvgFile(p);
            if (!path.isEmpty()) break;
        }
    }

    if (path.isEmpty()) {
        return knueppelBadge(gold);
    }

    SvgDigitizer::Params params;
    params.targetWidthMm = widthMm;
    params.primaryColor = gold;
    params.secondaryColor = QColor(218, 178, 85); // Madeira 1070 Brilliant Gold

    switch (style) {
    case 0:
    default:
        params.style = SvgDigitizer::StitchStyle::AuthenticPatchSatin;
        params.spacingMm = 0.36;
        break;
    case 1:
        params.style = SvgDigitizer::StitchStyle::TatamiWeave;
        params.spacingMm = 0.38;
        break;
    case 2:
        params.style = SvgDigitizer::StitchStyle::RoyalDuotoneGold;
        params.spacingMm = 0.40;
        break;
    case 3:
        params.style = SvgDigitizer::StitchStyle::FineOutlineRun;
        params.maxStitchMm = 2.2;
        break;
    case 4:
        params.style = SvgDigitizer::StitchStyle::ContourEcho;
        params.spacingMm = 0.40;
        params.maxStitchMm = 2.4;
        break;
    }

    return SvgDigitizer::digitize(path, params);
}


} // namespace stick
