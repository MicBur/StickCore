// ---------------------------------------------------------------------------
//  StickCore  –  MonogramGenerator.cpp
// ---------------------------------------------------------------------------
#include "generators/MonogramGenerator.h"
#include "generators/TextDigitizer.h"
#include "generators/SatinGenerator.h"
#include "generators/ContourFill.h"
#include "core/Geometry.h"

#include <QPainterPath>
#include <QPolygonF>
#include <cmath>

namespace stick {
namespace {

constexpr double PI = 3.14159265358979323846;

void translate(StitchSequence& s, double dx, double dy) {
    for (Stitch& st : s.stitches) { st.x += dx; st.y += dy; }
}
void append(StitchSequence& dst, const StitchSequence& src) {
    dst.stitches.insert(dst.stitches.end(), src.stitches.begin(), src.stitches.end());
}

// Map a monogram style to concrete font families (with Windows + Linux
// fallbacks) plus weight/slant. First installed family wins.
void applyStyle(TextDigitizer::Params& tp, MonogramGenerator::Style s) {
    using S = MonogramGenerator::Style;
    switch (s) {
    case S::Serif:
        tp.families = { "Georgia","Times New Roman","DejaVu Serif","Serif" };
        tp.serif = true;  tp.bold = true;  tp.italic = false; break;
    case S::Sans:
        tp.families = { "Segoe UI","Arial","Helvetica","DejaVu Sans","Sans Serif" };
        tp.serif = false; tp.bold = true;  tp.italic = false; break;
    case S::Script:
        tp.families = { "Segoe Script","Brush Script MT","Lucida Handwriting",
                        "URW Chancery L","DejaVu Serif","Serif" };
        tp.serif = false; tp.bold = false; tp.italic = true;  break;
    case S::SlabBold:
        tp.families = { "Rockwell","Arial Black","Franklin Gothic Heavy",
                        "DejaVu Serif","Serif" };
        tp.serif = true;  tp.bold = true;  tp.italic = false; break;
    case S::Elegant:
        tp.families = { "Georgia","Times New Roman","DejaVu Serif","Serif" };
        tp.serif = true;  tp.bold = false; tp.italic = true;  break;
    // Bundled calligraphy faces (exact family names; shipped with the app).
    case S::GreatVibes:
        tp.families = { "Great Vibes" };            tp.bold = false; tp.italic = false; break;
    case S::Tangerine:
        tp.families = { "Tangerine" };              tp.bold = true;  tp.italic = false; break;
    case S::Pinyon:
        tp.families = { "Pinyon Script" };          tp.bold = false; tp.italic = false; break;
    case S::Parisienne:
        tp.families = { "Parisienne" };             tp.bold = false; tp.italic = false; break;
    case S::Allura:
        tp.families = { "Allura" };                 tp.bold = false; tp.italic = false; break;
    case S::PetitFormal:
        tp.families = { "Petit Formal Script" };    tp.bold = false; tp.italic = false; break;
    case S::Muellerhoff:
        tp.families = { "Herr Von Muellerhoff" };   tp.bold = false; tp.italic = false; break;
    case S::AlexBrush:
        tp.families = { "Alex Brush" };             tp.bold = false; tp.italic = false; break;
    }
}

StitchSequence letter(QChar c, double h, const MonogramGenerator::Params& mp) {
    TextDigitizer::Params tp;
    tp.text = QString(c);
    applyStyle(tp, mp.style);
    tp.heightMm = h;
    tp.densityMm = mp.densityMm;
    tp.maxStitchMm = mp.maxStitchMm;
    tp.underlay = mp.underlay;

    if (mp.fill == MonogramGenerator::Fill::Contour && ContourFill::available()) {
        const QPainterPath path = TextDigitizer::textPath(tp);
        ContourFill::Params cp;
        cp.spacingMm   = std::max(mp.densityMm * 1.6, 0.7);  // rings from the density
        cp.maxStitchMm = std::min(mp.maxStitchMm, 3.0);
        StitchSequence s = ContourFill::generate(path, cp);
        if (!s.empty()) return s;
        // else fall through to the raised fill
    }
    tp.raised = true;
    return TextDigitizer::generate(tp);
}

} // namespace

QString MonogramGenerator::styleName(Style s)
{
    switch (s) {
    case Style::Serif:    return QStringLiteral("Klassisch (Serife)");
    case Style::Sans:     return QStringLiteral("Modern (serifenlos)");
    case Style::Script:   return QStringLiteral("Schreibschrift");
    case Style::SlabBold: return QStringLiteral("Kräftig (Block)");
    case Style::Elegant:  return QStringLiteral("Elegant (kursiv)");
    case Style::GreatVibes: return QStringLiteral("Kalligrafie · Great Vibes");
    case Style::Tangerine:  return QStringLiteral("Kalligrafie · Tangerine");
    case Style::Pinyon:     return QStringLiteral("Kalligrafie · Pinyon");
    case Style::Parisienne: return QStringLiteral("Kalligrafie · Parisienne");
    case Style::Allura:     return QStringLiteral("Kalligrafie · Allura (Commercial Script)");
    case Style::PetitFormal:return QStringLiteral("Kalligrafie · Petit Formal");
    case Style::Muellerhoff:return QStringLiteral("Kalligrafie · Muellerhoff");
    case Style::AlexBrush:  return QStringLiteral("Kalligrafie · Alex Brush");
    }
    return QStringLiteral("Klassisch");
}

QString MonogramGenerator::fillName(Fill f)
{
    switch (f) {
    case Fill::Raised:  return QStringLiteral("Erhaben (gefüllt + Rand)");
    case Fill::Contour: return QStringLiteral("Künstlerisch (Kontur-Füllung)");
    }
    return QStringLiteral("Erhaben");
}

StitchSequence MonogramGenerator::generate(const Params& p)
{
    QString L = p.letters.trimmed();
    if (L.size() > 3) L = L.left(3);
    StitchSequence seq;
    if (L.isEmpty()) return seq;

    const int n = L.size();
    // per-letter heights
    std::vector<double> h(n, p.heightMm);
    if (p.centerLarge && n == 3) { h[0] = h[2] = p.heightMm * 0.62; h[1] = p.heightMm; }

    // build each letter, measure width
    std::vector<StitchSequence> parts;
    std::vector<double> w(n, 0);
    for (int i = 0; i < n; ++i) {
        StitchSequence s = letter(L[i], h[i], p);
        double x0, y0, x1, y1;
        if (s.bounds(x0, y0, x1, y1)) w[i] = x1 - x0;
        parts.push_back(std::move(s));
    }

    const double gap = 0.16 * p.heightMm;
    double totalW = 0; for (int i = 0; i < n; ++i) totalW += w[i]; totalW += gap * (n - 1);

    double cursor = -totalW * 0.5;
    for (int i = 0; i < n; ++i) {
        // place letter centre at (cursor + w/2, 0)  (letters are origin-centred)
        translate(parts[i], cursor + w[i] * 0.5, 0.0);
        append(seq, parts[i]);
        cursor += w[i] + gap;
    }

    // optional frame
    if (p.frame != Frame::None && !seq.empty()) {
        double x0, y0, x1, y1; seq.bounds(x0, y0, x1, y1);
        const double cx = 0.5*(x0+x1), cy = 0.5*(y0+y1);
        const double margin = 0.34 * p.heightMm;
        double a = 0.5*(x1-x0) + margin, b = 0.5*(y1-y0) + margin*1.15;
        if (p.frame == Frame::Circle) { a = b = std::max(a, b); }
        QPolygonF ell; const int N = 72;
        QPainterPath ellPath;
        ellPath.addPolygon(ell);
        ellPath.closeSubpath();
        StitchSequence fBand = SatinGenerator::fromCenterline(ellPath, 2.0, 0.45, 0);
        append(seq, fBand);
    }

    seq.palette.clear();
    seq.palette.emplace_back(p.color, QStringLiteral("Monogramm"));
    return seq;
}

} // namespace stick
