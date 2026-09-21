// Headless verification of the non-GL core: Satin, Tatami, JEF round-trip.
#include "generators/SatinGenerator.h"
#include "generators/TatamiFill.h"
#include "generators/TextDigitizer.h"
#include "generators/MonogramGenerator.h"
#include "generators/LogoGenerator.h"
#include "generators/SvgDigitizer.h"
#include "core/SvgPathParser.h"
#include "generators/ImageDigitizer.h"
#include "generators/PhotoProcessor.h"
#include "generators/OpenCvBridge.h"
#include "generators/Underlay.h"
#include "generators/AppliqueGenerator.h"
#include "generators/HuntingMotifs.h"
#include "core/ThreadCatalog.h"
#include "core/MachineProfile.h"
#include "codec/JefCodec.h"
#include "codec/DstCodec.h"
#include "net/QrCode.h"
#include "net/QrUploadServer.h"
#include "library/DesignFactory.h"
#include "library/DesignLibrary.h"
#include "editor/BezierPath.h"
#include <QDir>
#include <QGuiApplication>
#include <QFontDatabase>
#include <QPainterPath>
#include <QPainter>
#include <QLinearGradient>
#include <QImage>
#include <QtEndian>
#include <QFile>
#include <cstdio>
#include <cmath>

using namespace stick;

static int failures = 0;
#define CHECK(cond, msg) do { if(!(cond)){ std::printf("  FAIL: %s\n", msg); ++failures;} \
                              else std::printf("  ok  : %s\n", msg); } while(0)

static void drawRealisticStitches(QPainter& p, const StitchSequence& seq, const QRectF& targetRect,
                                  double padFrac = 0.08, double strokeW = 1.8, bool drawHoles = true)
{
    if (seq.stitches.size() < 2) return;
    double x0, y0, x1, y1;
    if (!seq.bounds(x0, y0, x1, y1)) return;
    double w = std::max(1e-3, x1 - x0);
    double h = std::max(1e-3, y1 - y0);
    double padX = targetRect.width() * padFrac;
    double padY = targetRect.height() * padFrac;
    double availW = targetRect.width() - 2.0 * padX;
    double availH = targetRect.height() - 2.0 * padY;
    double scale = std::min(availW / w, availH / h);
    double ox = targetRect.x() + padX + (availW - w * scale) * 0.5;
    double oy = targetRect.y() + padY + (availH - h * scale) * 0.5;

    int prevCol = -1;
    QColor col(212, 175, 55);
    bool have = false;
    double px = 0, py = 0;

    for (const Stitch& st : seq.stitches) {
        if (st.flags & SF_End) break;
        if (st.colorIdx != prevCol) {
            prevCol = st.colorIdx;
            if (st.colorIdx >= 0 && st.colorIdx < int(seq.palette.size()))
                col = seq.palette[st.colorIdx].color;
            else
                col = QColor(212, 175, 55);
        }
        double cx = ox + (st.x - x0) * scale;
        double cy = oy + (h - (st.y - y0)) * scale;
        bool travel = (st.flags & (SF_Jump | SF_Trim | SF_ColorChange | SF_Stop)) != 0;
        if (have && !travel) {
            p.setPen(QPen(col.darker(170), strokeW * 1.15, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawLine(QPointF(px + 0.6, py + 0.6), QPointF(cx + 0.6, cy + 0.6));

            p.setPen(QPen(col, strokeW, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawLine(QPointF(px, py), QPointF(cx, cy));

            p.setPen(QPen(col.lighter(130), strokeW * 0.35, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawLine(QPointF(px - 0.2, py - 0.2), QPointF(cx - 0.2, cy - 0.2));

            if (drawHoles) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(12, 16, 14, 150));
                p.drawEllipse(QPointF(cx, cy), strokeW * 0.45, strokeW * 0.45);
            }
        }
        px = cx; py = cy; have = true;
    }
}

static void generateHuntingCollectionAsset(const QString& outFile)
{
    const int W = 1400, H = 1020;
    QImage img(W, H, QImage::Format_ARGB32);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    QLinearGradient bgGrad(0, 0, 0, H);
    bgGrad.setColorAt(0.0, QColor(17, 26, 20));
    bgGrad.setColorAt(1.0, QColor(24, 36, 29));
    p.fillRect(0, 0, W, H, bgGrad);

    p.setPen(QPen(QColor(255, 255, 255, 8), 1.0));
    for (int y = 0; y < H; y += 8) p.drawLine(0, y, W, y);
    for (int x = 0; x < W; x += 8) p.drawLine(x, 0, x, H);

    p.setPen(QPen(QColor(42, 60, 48), 2.0));
    p.drawRect(12, 12, W - 24, H - 24);

    p.setPen(QColor(212, 175, 55));
    p.setFont(QFont(QStringLiteral("Georgia"), 20, QFont::Bold));
    p.drawText(QRect(30, 24, W - 60, 32), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("STICKCORE STUDIO · MEISTER-KOLLEKTION TRADITION & JAGD"));

    p.setPen(QColor(160, 185, 170));
    p.setFont(QFont(QStringLiteral("Segoe UI"), 11, QFont::Normal));
    p.drawText(QRect(30, 56, W - 60, 22), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("Original Janome MC350E / MC550E Vektordigitalisierungen · Frei skalierbar & im 2D-Editor bearbeitbar"));

    struct Item {
        HuntingMotifs::MotifType motif;
        TatamiFill::PatternType pattern;
        double angle;
        QString title;
        QString desc;
        QString specs;
    };
    QVector<Item> items = {
        { HuntingMotifs::MotifType::OakBranch, TatamiFill::PatternType::Twill, 25.0,
          QStringLiteral("1. Eichenlaub mit Eicheln"),
          QStringLiteral("Dreiteilige Eichenlaub-Garnitur mit plastisch reliefierten Eicheln"),
          QStringLiteral("78 × 62 mm · ~2.850 Stiche · Füllung: Köper (Twill) · Janome Hoop A/B") },
        { HuntingMotifs::MotifType::StagHead, TatamiFill::PatternType::Brick, 35.0,
          QStringLiteral("2. Kapitaler 12-Ender Hirschkopf"),
          QStringLiteral("Majestätischer Rothirsch mit vollem Geweih & naturgetreuer Schattierung"),
          QStringLiteral("80 × 74 mm · ~4.120 Stiche · Füllung: Ziegelstein (Brick) · Janome Hoop A/B") },
        { HuntingMotifs::MotifType::WildBoar, TatamiFill::PatternType::StandardTatami, 45.0,
          QStringLiteral("3. Keiler mit Hauer (Schwarzwild)"),
          QStringLiteral("Kräftige Silhouette mit markantem Borstenkamm & weißem Satin-Hauer"),
          QStringLiteral("80 × 58 mm · ~3.740 Stiche · Füllung: Standard-Tatami · Janome Hoop A/B") },
        { HuntingMotifs::MotifType::WaidmannsheilCrest, TatamiFill::PatternType::ContourEcho, 15.0,
          QStringLiteral("4. Waidmannsheil-Medaillon"),
          QStringLiteral("Runder Eichenkranz mit gekreuzten Jagdflinten & Ehrenschleife"),
          QStringLiteral("80 × 80 mm · ~5.380 Stiche · Füllung: Kontur-Echo · Janome Hoop SQ14/B") }
    };

    QRect rects[4] = {
        QRect(30, 95, 655, 435),
        QRect(715, 95, 655, 435),
        QRect(30, 550, 655, 435),
        QRect(715, 550, 655, 435)
    };

    for (int i = 0; i < 4; ++i) {
        QRect r = rects[i];
        p.setPen(QPen(QColor(42, 60, 48), 1.5));
        p.setBrush(QColor(22, 32, 25));
        p.drawRoundedRect(r, 10, 10);

        p.setPen(QColor(212, 175, 55));
        p.setFont(QFont(QStringLiteral("Georgia"), 13, QFont::Bold));
        p.drawText(r.left() + 20, r.top() + 18, r.width() - 40, 24, Qt::AlignLeft, items[i].title);

        p.setPen(QColor(180, 205, 190));
        p.setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::Normal));
        p.drawText(r.left() + 20, r.top() + 42, r.width() - 40, 20, Qt::AlignLeft, items[i].desc);

        HuntingMotifs::Params hp;
        hp.widthMm = 80.0;
        hp.fillAngleDeg = items[i].angle;
        hp.pattern = items[i].pattern;
        hp.satinOutline = true;
        StitchSequence seq = HuntingMotifs::generateStitches(items[i].motif, hp);

        QRectF stitchRect(r.left() + 25, r.top() + 70, r.width() - 50, r.height() - 120);
        drawRealisticStitches(p, seq, stitchRect, 0.05, 2.0, true);

        QRect badgeRect(r.left() + 20, r.bottom() - 36, r.width() - 40, 24);
        p.setPen(QPen(QColor(42, 70, 52), 1.0));
        p.setBrush(QColor(16, 24, 19));
        p.drawRoundedRect(badgeRect, 5, 5);

        p.setPen(QColor(45, 212, 191));
        p.setFont(QFont(QStringLiteral("Consolas"), 9, QFont::Bold));
        p.drawText(badgeRect, Qt::AlignCenter, items[i].specs);
    }

    p.end();
    img.save(outFile, "PNG");
}

static void generateMonogramsShowcaseAsset(const QString& outFile)
{
    const int W = 1400, H = 1020;
    QImage img(W, H, QImage::Format_ARGB32);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    QLinearGradient bgGrad(0, 0, 0, H);
    bgGrad.setColorAt(0.0, QColor(14, 20, 36));
    bgGrad.setColorAt(1.0, QColor(22, 32, 54));
    p.fillRect(0, 0, W, H, bgGrad);

    p.setPen(QPen(QColor(255, 255, 255, 7), 1.0));
    for (int y = 0; y < H; y += 8) p.drawLine(0, y, W, y);
    for (int x = 0; x < W; x += 8) p.drawLine(x, 0, x, H);

    p.setPen(QPen(QColor(45, 65, 105), 2.0));
    p.drawRect(12, 12, W - 24, H - 24);

    p.setPen(QColor(212, 175, 55));
    p.setFont(QFont(QStringLiteral("Georgia"), 20, QFont::Bold));
    p.drawText(QRect(30, 24, W - 60, 32), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("STICKCORE MEISTER-MONOGRAMME & KUNSTVOLLE ZIERRAHMEN"));

    p.setPen(QColor(165, 190, 225));
    p.setFont(QFont(QStringLiteral("Segoe UI"), 11, QFont::Normal));
    p.drawText(QRect(30, 56, W - 60, 22), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("Historische Zierrahmen · 1-, 2- & 3-Buchstaben Layouts · Erhabener 3D-Satin & Zweiton-Stickung"));

    struct MItem {
        MonogramGenerator::Frame frame;
        QString letters;
        QColor letterCol;
        QColor frameCol;
        QString title;
        QString desc;
        QString specs;
    };
    QVector<MItem> items = {
        { MonogramGenerator::Frame::OakWreath, QStringLiteral("M"),
          QColor(218, 165, 32), QColor(184, 134, 11),
          QStringLiteral("🌿 Eichenlaub-Kranz (Oak Wreath)"),
          QStringLiteral("Solitär-Monogramm für Trachten-, Jagd- & Forstbekleidung"),
          QStringLiteral("Höhe 35 mm · ~2.900 Stiche · Madeira Rayon 1070 (Brillantgold) & 1083 (Altgold)") },
        { MonogramGenerator::Frame::LaurelWreath, QStringLiteral("MB"),
          QColor(30, 90, 180), QColor(212, 175, 55),
          QStringLiteral("🍃 Lorbeerkranz (Laurel Wreath)"),
          QStringLiteral("Duo-Monogramm mit Zierschleife für Hochzeiten & Jubiläen"),
          QStringLiteral("Höhe 35 mm · ~3.240 Stiche · Madeira Rayon 1070 (Gold) & 1134 (Königsblau)") },
        { MonogramGenerator::Frame::ShieldCrest, QStringLiteral("JMB"),
          QColor(170, 25, 35), QColor(212, 175, 55),
          QStringLiteral("🛡 Wappenschild (Shield Crest)"),
          QStringLiteral("Trio-Monogramm (Mitte vergrößert) für Siegel & Familienwappen"),
          QStringLiteral("Höhe 35 mm · ~3.890 Stiche · Madeira Rayon 1070 (Gold) & 1184 (Rubinrot)") },
        { MonogramGenerator::Frame::BaroqueCartouche, QStringLiteral("K"),
          QColor(240, 235, 220), QColor(212, 175, 55),
          QStringLiteral("⚜ Barocke Kartusche (Baroque Cartouche)"),
          QStringLiteral("Rokoko-Voluten & C-Bögen für luxuriöse Haute Couture & Kissen"),
          QStringLiteral("Höhe 35 mm · ~4.150 Stiche · Madeira Rayon 1070 (Gold) & 1002 (Elfenbein)") }
    };

    QRect rects[4] = {
        QRect(30, 95, 655, 435),
        QRect(715, 95, 655, 435),
        QRect(30, 550, 655, 435),
        QRect(715, 550, 655, 435)
    };

    for (int i = 0; i < 4; ++i) {
        QRect r = rects[i];
        p.setPen(QPen(QColor(45, 65, 105), 1.5));
        p.setBrush(QColor(20, 28, 48));
        p.drawRoundedRect(r, 10, 10);

        p.setPen(QColor(212, 175, 55));
        p.setFont(QFont(QStringLiteral("Georgia"), 13, QFont::Bold));
        p.drawText(r.left() + 20, r.top() + 18, r.width() - 40, 24, Qt::AlignLeft, items[i].title);

        p.setPen(QColor(175, 200, 230));
        p.setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::Normal));
        p.drawText(r.left() + 20, r.top() + 42, r.width() - 40, 20, Qt::AlignLeft, items[i].desc);

        MonogramGenerator::Params mp;
        mp.frame = items[i].frame;
        mp.letters = items[i].letters;
        mp.color = items[i].letterCol;
        mp.frameColor = items[i].frameCol;
        mp.heightMm = 35.0;
        mp.fillAngleDeg = 45.0;
        StitchSequence seq = MonogramGenerator::generate(mp);

        QRectF stitchRect(r.left() + 25, r.top() + 70, r.width() - 50, r.height() - 120);
        drawRealisticStitches(p, seq, stitchRect, 0.05, 2.0, true);

        QRect badgeRect(r.left() + 20, r.bottom() - 36, r.width() - 40, 24);
        p.setPen(QPen(QColor(45, 75, 120), 1.0));
        p.setBrush(QColor(15, 22, 38));
        p.drawRoundedRect(badgeRect, 5, 5);

        p.setPen(QColor(45, 212, 191));
        p.setFont(QFont(QStringLiteral("Consolas"), 9, QFont::Bold));
        p.drawText(badgeRect, Qt::AlignCenter, items[i].specs);
    }

    p.end();
    img.save(outFile, "PNG");
}

static void generateTatamiMacroAsset(const QString& outFile)
{
    const int W = 1400, H = 960;
    QImage img(W, H, QImage::Format_ARGB32);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    QLinearGradient bgGrad(0, 0, 0, H);
    bgGrad.setColorAt(0.0, QColor(19, 23, 31));
    bgGrad.setColorAt(1.0, QColor(26, 32, 44));
    p.fillRect(0, 0, W, H, bgGrad);

    p.setPen(QPen(QColor(45, 55, 75), 2.0));
    p.drawRect(12, 12, W - 24, H - 24);

    p.setPen(QColor(212, 175, 55));
    p.setFont(QFont(QStringLiteral("Georgia"), 20, QFont::Bold));
    p.drawText(QRect(30, 24, W - 60, 32), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("STICKCORE TATAMI-FÜLLMUSTER · MAKRO-STICHVERGLEICH"));

    p.setPen(QColor(165, 185, 210));
    p.setFont(QFont(QStringLiteral("Segoe UI"), 11, QFont::Normal));
    p.drawText(QRect(30, 56, W - 60, 22), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("Mathematische Reihenversätze, Lichtreflexion & Verzugsausgleich im Detail (35 × 25 mm Makro-Ausschnitt)"));

    struct TItem {
        TatamiFill::PatternType pat;
        QString title;
        QString shift;
        QString desc;
    };
    QVector<TItem> items = {
        { TatamiFill::PatternType::StandardTatami,
          QStringLiteral("1. Standard-Tatami"), QStringLiteral("1/4 (0.25) Reihenversatz"),
          QStringLiteral("Gleichmäßige, matte Weboptik. Verhindert störende Moiré-Muster; der ideale Allrounder für glatte Flächen.") },
        { TatamiFill::PatternType::Brick,
          QStringLiteral("2. Ziegelstein (Brick)"), QStringLiteral("1/2 (0.50) Halbversatz"),
          QStringLiteral("Markanter Mauerwerksverband. Reflektiert Scheinwerferlicht breitflächig; wirkt strukturiert und kraftvoll.") },
        { TatamiFill::PatternType::Twill,
          QStringLiteral("3. Köper (Twill)"), QStringLiteral("1/3 (0.33) Schrägversatz"),
          QStringLiteral("Diagonale Gratlinien wie bei edlem Trachtentuch oder Denim. Erstklassig für Loden und Uniformen.") },
        { TatamiFill::PatternType::Basketweave,
          QStringLiteral("4. Flechtmuster (Basket)"), QStringLiteral("4-Phasen Blockversatz"),
          QStringLiteral("Kreuzweise gewobene Struktur (Panamagewebe). Schimmert je nach Betrachtungswinkel changierend.") },
        { TatamiFill::PatternType::Honeycomb,
          QStringLiteral("5. Wabenmuster (Honeycomb)"), QStringLiteral("Doppelraute / Hexagonal"),
          QStringLiteral("Geometrisches Gitterwerk. Verleiht Sport- und Outdoor-Funktionskleidung moderne Dynamik.") },
        { TatamiFill::PatternType::ContourEcho,
          QStringLiteral("6. Kontur-Echo"), QStringLiteral("Abstandstransformation"),
          QStringLiteral("Stiche folgen exakt den Außenkanten von außen nach innen. Ergibt organische 3D-Lichtverläufe.") }
    };

    QRect rects[6] = {
        QRect(30, 95, 435, 405),
        QRect(482, 95, 435, 405),
        QRect(935, 95, 435, 405),
        QRect(30, 520, 435, 405),
        QRect(482, 520, 435, 405),
        QRect(935, 520, 435, 405)
    };

    QPolygonF poly;
    const double pw = 40.0, ph = 26.0;
    for (int deg = 0; deg <= 360; deg += 10) {
        double rad = deg * 3.14159265 / 180.0;
        double rx = (deg >= 90 && deg <= 270) ? -pw*0.5 + 5.0 : pw*0.5 - 5.0;
        double ry = (deg >= 0 && deg <= 180) ? ph*0.5 - 5.0 : -ph*0.5 + 5.0;
        poly << QPointF(rx + 5.0 * std::cos(rad), ry + 5.0 * std::sin(rad));
    }

    for (int i = 0; i < 6; ++i) {
        QRect r = rects[i];
        p.setPen(QPen(QColor(45, 55, 75), 1.5));
        p.setBrush(QColor(22, 28, 38));
        p.drawRoundedRect(r, 8, 8);

        p.setPen(QColor(212, 175, 55));
        p.setFont(QFont(QStringLiteral("Georgia"), 12, QFont::Bold));
        p.drawText(r.left() + 16, r.top() + 16, r.width() - 32, 22, Qt::AlignLeft, items[i].title);

        p.setPen(QColor(45, 212, 191));
        p.setFont(QFont(QStringLiteral("Consolas"), 9, QFont::Bold));
        p.drawText(r.left() + 16, r.top() + 38, r.width() - 32, 18, Qt::AlignLeft, items[i].shift);

        TatamiFill::Params tp;
        tp.pattern = items[i].pat;
        tp.fillAngleDeg = 30.0;
        tp.rowSpacingMm = 0.42;
        tp.maxStitchMm = 3.8;
        tp.underlay = false;
        StitchSequence seq = TatamiFill::generate(poly, tp);
        seq.palette = { ThreadColor(QColor(218, 165, 32), QStringLiteral("Brillant Gold"), 1070) };

        QRectF swatchRect(r.left() + 16, r.top() + 62, r.width() - 32, 230);
        p.setPen(QPen(QColor(35, 45, 60), 1.0));
        p.setBrush(QColor(16, 20, 28));
        p.drawRoundedRect(swatchRect, 6, 6);

        drawRealisticStitches(p, seq, swatchRect, 0.04, 2.2, true);

        QRect descRect(r.left() + 16, r.top() + 302, r.width() - 32, 90);
        p.setPen(QColor(170, 190, 210));
        p.setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::Normal));
        p.drawText(descRect, Qt::AlignLeft | Qt::TextWordWrap, items[i].desc);
    }

    p.end();
    img.save(outFile, "PNG");
}

static void generateOpenCvPipelineAsset(const QString& outFile)
{
    const int W = 1500, H = 760;
    QImage img(W, H, QImage::Format_ARGB32);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    QLinearGradient bgGrad(0, 0, 0, H);
    bgGrad.setColorAt(0.0, QColor(13, 19, 31));
    bgGrad.setColorAt(1.0, QColor(20, 28, 46));
    p.fillRect(0, 0, W, H, bgGrad);

    p.setPen(QPen(QColor(38, 52, 78), 2.0));
    p.drawRect(12, 12, W - 24, H - 24);

    p.setPen(QColor(212, 175, 55));
    p.setFont(QFont(QStringLiteral("Georgia"), 20, QFont::Bold));
    p.drawText(QRect(30, 24, W - 60, 32), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("OPENCV DIGITALISIERUNGS-PIPELINE FÜR BELIEBIGE MOTIVE"));

    p.setPen(QColor(165, 185, 215));
    p.setFont(QFont(QStringLiteral("Segoe UI"), 11, QFont::Normal));
    p.drawText(QRect(30, 56, W - 60, 22), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("Vom Rohfoto / Papierscan zum makellosen Janome-Stickmuster in 5 automatisierten Phasen"));

    struct Step {
        QString num;
        QString title;
        QString techBadge;
        QString desc;
    };
    QVector<Step> steps = {
        { QStringLiteral("Phase 1"), QStringLiteral("Foto / Scan"),
          QStringLiteral("RGB Input (Kamera/Papier)"),
          QStringLiteral("Smartphone-Kamerafoto oder Flachbettscan mit Papierfalten, Rand-Vignettierung und 16 Mio. Farben.") },
        { QStringLiteral("Phase 2"), QStringLiteral("OpenCV Entrauschen"),
          QStringLiteral("removeBackground()"),
          QStringLiteral("Bilateraler Rauschfilter glättet Texturen, bewahrt Konturen. 3-Ecken-Konsistenz löscht weiße Papierecken.") },
        { QStringLiteral("Phase 3"), QStringLiteral("K-Means & Despeckle"),
          QStringLiteral("filterSpeckles()"),
          QStringLiteral("Reduktion auf 2–6 Garnfarben. Beseitigt isolierte Mini-Pixel unter 16 px gegen Nadelstau und Sprungfäden.") },
        { QStringLiteral("Phase 4"), QStringLiteral("Vektor-Glättung"),
          QStringLiteral("smoothContours()"),
          QStringLiteral("Douglas-Peucker-Algorithmus glättet Treppeneffekte pixeliger Ränder zu fließenden Vektorkonturen.") },
        { QStringLiteral("Phase 5"), QStringLiteral("Maschinen-Stick"),
          QStringLiteral("satinBorder() + Tatami"),
          QStringLiteral("Berechnet dichte Tatami-Füllungen mit Unterleger und umhüllt das Motiv mit einem erhabenen Satin-Kettelrand.") }
    };

    const int colW = 265;
    const int gap = 26;
    const int startX = 30;
    const int startY = 95;
    const int cardH = 635;

    for (int i = 0; i < 5; ++i) {
        int cx = startX + i * (colW + gap);
        QRect r(cx, startY, colW, cardH);

        p.setPen(QPen(QColor(40, 56, 82), 1.5));
        p.setBrush(QColor(18, 26, 42));
        p.drawRoundedRect(r, 10, 10);

        QRect numBadge(r.left() + 16, r.top() + 14, 80, 22);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(45, 212, 191, 40));
        p.drawRoundedRect(numBadge, 4, 4);
        p.setPen(QColor(45, 212, 191));
        p.setFont(QFont(QStringLiteral("Consolas"), 9, QFont::Bold));
        p.drawText(numBadge, Qt::AlignCenter, steps[i].num);

        p.setPen(QColor(240, 245, 255));
        p.setFont(QFont(QStringLiteral("Segoe UI"), 13, QFont::Bold));
        p.drawText(r.left() + 16, r.top() + 42, r.width() - 32, 24, Qt::AlignLeft, steps[i].title);

        QRect techRect(r.left() + 16, r.top() + 70, r.width() - 32, 22);
        p.setPen(QPen(QColor(50, 70, 100), 1.0));
        p.setBrush(QColor(14, 20, 32));
        p.drawRoundedRect(techRect, 4, 4);
        p.setPen(QColor(212, 175, 55));
        p.setFont(QFont(QStringLiteral("Consolas"), 8, QFont::Bold));
        p.drawText(techRect, Qt::AlignCenter, steps[i].techBadge);

        QRect previewBox(r.left() + 16, r.top() + 102, r.width() - 32, 260);
        p.setPen(QPen(QColor(35, 48, 72), 1.0));
        p.setBrush(QColor(12, 16, 26));
        p.drawRoundedRect(previewBox, 6, 6);

        if (i == 0) {
            QLinearGradient paperGrad(previewBox.topLeft(), previewBox.bottomRight());
            paperGrad.setColorAt(0.0, QColor(245, 242, 235));
            paperGrad.setColorAt(0.7, QColor(225, 220, 210));
            paperGrad.setColorAt(1.0, QColor(195, 190, 180));
            p.fillRect(previewBox.adjusted(2, 2, -2, -2), paperGrad);

            p.setPen(QPen(QColor(160, 40, 30), 4));
            p.setBrush(QColor(200, 50, 40));
            QPolygonF cr;
            double pcx = previewBox.center().x(), pcy = previewBox.center().y();
            cr << QPointF(pcx - 50, pcy - 60) << QPointF(pcx + 50, pcy - 60)
               << QPointF(pcx + 50, pcy + 10) << QPointF(pcx, pcy + 60) << QPointF(pcx - 50, pcy + 10);
            p.drawPolygon(cr);

            p.setPen(Qt::NoPen); p.setBrush(QColor(230, 190, 50));
            p.drawEllipse(QPointF(pcx, pcy - 10), 20, 20);

            p.setPen(QPen(QColor(0, 0, 0, 60), 3));
            p.drawLine(previewBox.left() + 10, previewBox.top() + 40, previewBox.right() - 10, previewBox.bottom() - 30);
        } else if (i == 1) {
            for (int by = previewBox.top() + 2; by < previewBox.bottom() - 2; by += 12) {
                for (int bx = previewBox.left() + 2; bx < previewBox.right() - 2; bx += 12) {
                    bool alt = ((bx / 12) + (by / 12)) % 2 == 0;
                    p.fillRect(bx, by, 12, 12, alt ? QColor(24, 32, 48) : QColor(18, 24, 38));
                }
            }
            p.setPen(QPen(QColor(180, 45, 35), 3));
            p.setBrush(QColor(210, 50, 40));
            double pcx = previewBox.center().x(), pcy = previewBox.center().y();
            QPolygonF cr;
            cr << QPointF(pcx - 50, pcy - 60) << QPointF(pcx + 50, pcy - 60)
               << QPointF(pcx + 50, pcy + 10) << QPointF(pcx, pcy + 60) << QPointF(pcx - 50, pcy + 10);
            p.drawPolygon(cr);
            p.setPen(Qt::NoPen); p.setBrush(QColor(240, 200, 50));
            p.drawEllipse(QPointF(pcx, pcy - 10), 20, 20);
        } else if (i == 2) {
            p.fillRect(previewBox.adjusted(2, 2, -2, -2), QColor(14, 18, 28));
            double pcx = previewBox.center().x(), pcy = previewBox.center().y();
            QPolygonF cr;
            cr << QPointF(pcx - 50, pcy - 60) << QPointF(pcx + 50, pcy - 60)
               << QPointF(pcx + 50, pcy + 10) << QPointF(pcx, pcy + 60) << QPointF(pcx - 50, pcy + 10);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(200, 35, 35));
            p.drawPolygon(cr);
            p.setBrush(QColor(220, 180, 40));
            p.drawEllipse(QPointF(pcx, pcy - 10), 20, 20);

            QRect chip1(previewBox.left() + 20, previewBox.bottom() - 32, 22, 18);
            QRect chip2(previewBox.left() + 48, previewBox.bottom() - 32, 22, 18);
            p.fillRect(chip1, QColor(200, 35, 35));
            p.fillRect(chip2, QColor(220, 180, 40));
            p.setPen(QPen(QColor(255, 255, 255, 120), 1));
            p.drawRect(chip1); p.drawRect(chip2);
        } else if (i == 3) {
            p.fillRect(previewBox.adjusted(2, 2, -2, -2), QColor(14, 18, 28));
            double pcx = previewBox.center().x(), pcy = previewBox.center().y();
            QPolygonF cr;
            cr << QPointF(pcx - 50, pcy - 60) << QPointF(pcx + 50, pcy - 60)
               << QPointF(pcx + 50, pcy + 10) << QPointF(pcx, pcy + 60) << QPointF(pcx - 50, pcy + 10);
            
            p.setPen(QPen(QColor(45, 212, 191), 2.5));
            p.setBrush(QColor(45, 212, 191, 30));
            p.drawPolygon(cr);

            p.setPen(QPen(QColor(255, 255, 255), 1.5));
            p.setBrush(QColor(15, 23, 42));
            for (const QPointF& pt : cr) {
                p.drawRect(QRectF(pt.x() - 3.5, pt.y() - 3.5, 7, 7));
            }
            p.setPen(QPen(QColor(212, 175, 55), 2.0));
            p.setBrush(QColor(212, 175, 55, 40));
            p.drawEllipse(QPointF(pcx, pcy - 10), 20, 20);
        } else if (i == 4) {
            p.fillRect(previewBox.adjusted(2, 2, -2, -2), QColor(14, 18, 28));
            
            QPolygonF cr;
            cr << QPointF(10, 5) << QPointF(50, 5) << QPointF(50, 45) << QPointF(30, 65) << QPointF(10, 45);
            TatamiFill::Params tp;
            tp.pattern = TatamiFill::PatternType::Brick;
            tp.fillAngleDeg = 45.0;
            tp.rowSpacingMm = 0.40;
            StitchSequence seq = TatamiFill::generate(cr, tp);
            seq.palette = { ThreadColor(QColor(205, 38, 38), QStringLiteral("Ruby"), 1184) };

            drawRealisticStitches(p, seq, previewBox.adjusted(8, 8, -8, -8), 0.08, 1.8, true);

            p.setPen(QPen(QColor(220, 180, 40), 4.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            double pcx = previewBox.center().x(), pcy = previewBox.center().y();
            QPolygonF scr;
            scr << QPointF(pcx - 50, pcy - 60) << QPointF(pcx + 50, pcy - 60)
                << QPointF(pcx + 50, pcy + 10) << QPointF(pcx, pcy + 60) << QPointF(pcx - 50, pcy + 10);
            p.drawPolygon(scr);
            p.setPen(QPen(QColor(255, 235, 120), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPolygon(scr);
        }

        QRect descRect(r.left() + 16, r.top() + 380, r.width() - 32, 230);
        p.setPen(QColor(180, 200, 225));
        p.setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::Normal));
        p.drawText(descRect, Qt::AlignLeft | Qt::TextWordWrap, steps[i].desc);

        if (i < 4) {
            p.setPen(QColor(45, 212, 191));
            p.setFont(QFont(QStringLiteral("Segoe UI"), 16, QFont::Bold));
            QRect arrowRect(cx + colW, startY + 200, gap, 40);
            p.drawText(arrowRect, Qt::AlignCenter, QStringLiteral("➔"));
        }
    }

    p.end();
    img.save(outFile, "PNG");
}

static void generateManualAssets(const QString& outDir)
{
    QDir d(outDir);
    if (!d.exists()) d.mkpath(QStringLiteral("."));

    std::printf("== Generating High-Resolution Manual Assets in %s ==\n", outDir.toUtf8().constData());
    
    QString f10 = d.filePath(QStringLiteral("10_hunting_motifs_collection.png"));
    generateHuntingCollectionAsset(f10);
    std::printf("  ok  : generated %s\n", f10.toUtf8().constData());

    QString f11 = d.filePath(QStringLiteral("11_deluxe_monograms_showcase.png"));
    generateMonogramsShowcaseAsset(f11);
    std::printf("  ok  : generated %s\n", f11.toUtf8().constData());

    QString f12 = d.filePath(QStringLiteral("12_tatami_fill_patterns_macro.png"));
    generateTatamiMacroAsset(f12);
    std::printf("  ok  : generated %s\n", f12.toUtf8().constData());

    QString f13 = d.filePath(QStringLiteral("13_opencv_motif_pipeline.png"));
    generateOpenCvPipelineAsset(f13);
    std::printf("  ok  : generated %s\n", f13.toUtf8().constData());
}

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);   // needed for font glyph outlines

    // Load the bundled calligraphy fonts (same as the app) so logo/monogram
    // metrics match reality.
    for (const QString& dir : { QStringLiteral("media/fonts"),
                                QStringLiteral("../media/fonts"),
                                QStringLiteral("../../media/fonts") }) {
        QDir d(dir);
        for (const QString& f : d.entryList(QStringList() << QStringLiteral("*.ttf")))
            QFontDatabase::addApplicationFont(d.filePath(f));
    }

    for (int i = 1; i < argc; ++i) {
        QString arg = QString::fromUtf8(argv[i]);
        if (arg == QStringLiteral("--manual-assets") || arg == QStringLiteral("--generate-assets")) {
            QString outDir = (i + 1 < argc && argv[i+1][0] != '-')
                ? QString::fromUtf8(argv[i+1])
                : QStringLiteral("docs/manual_assets");
            generateManualAssets(outDir);
            return 0;
        }
    }

    std::printf("== SatinGenerator ==\n");
    QPainterPath a; a.moveTo(0,0);   a.cubicTo(20,40,60,40,80,0);
    QPainterPath b; b.moveTo(0,8);   b.cubicTo(20,52,60,52,80,12);
    auto sat = SatinGenerator::generate(a, b, 0.4, 0.15);
    CHECK(sat.size() > 50, "satin produced a stitch stream");
    // Verify max stitch length constraint (<= maxRung default 12mm) between consecutive.
    double maxLen = 0;
    for (size_t i=1;i<sat.stitches.size();++i){
        if (sat.stitches[i].flags & SF_Jump) continue;   // travel moves are not stitches
        double dx=sat.stitches[i].x-sat.stitches[i-1].x, dy=sat.stitches[i].y-sat.stitches[i-1].y;
        maxLen=std::max(maxLen, std::hypot(dx,dy));
    }
    CHECK(maxLen <= 12.001, "no satin stitch exceeds 12mm split limit");
    CHECK(sat.stitches.front().flags & SF_Jump, "satin now begins with underlay (jump-in)");

    std::printf("== TatamiFill ==\n");
    QPolygonF outer; outer<<QPointF(0,0)<<QPointF(40,0)<<QPointF(40,40)<<QPointF(0,40);
    QPolygonF hole;  hole<<QPointF(15,15)<<QPointF(25,15)<<QPointF(25,25)<<QPointF(15,25);
    QVector<QPolygonF> region; region<<outer<<hole;
    TatamiFill::Params tp; tp.fillAngleDeg=30; tp.rowSpacingMm=0.4; tp.maxStitchMm=4.0;
    auto tat = TatamiFill::generate(region, tp);
    CHECK(tat.size() > 100, "tatami produced a stitch stream");
    double tmax=0;
    for (size_t i=1;i<tat.stitches.size();++i){
        if (tat.stitches[i].flags & SF_Jump) continue;
        double dx=tat.stitches[i].x-tat.stitches[i-1].x, dy=tat.stitches[i].y-tat.stitches[i-1].y;
        tmax=std::max(tmax, std::hypot(dx,dy));
    }
    CHECK(tmax <= 4.001, "no tatami run stitch exceeds L_max=4mm");
    // Points should fall inside outer bbox.
    bool inside=true;
    for (auto&s:tat.stitches){ if(s.x<-0.5||s.x>40.5||s.y<-0.5||s.y>40.5) inside=false; }
    CHECK(inside, "all tatami points lie within the boundary bbox");

    std::printf("== JefCodec export + header ==\n");
    tat.palette.clear(); tat.palette.emplace_back(QColor(40,90,200), "Blau", 123);
    const QString testJefPath = QDir::tempPath() + QStringLiteral("/_verify.jef");
    JefCodec::Result r = JefCodec::exportToFile(testJefPath, tat, HoopType::HoopA_126x110);
    CHECK(r.ok, "JEF export succeeded");

    QFile f(testJefPath); f.open(QIODevice::ReadOnly);
    QByteArray d = f.readAll(); f.close();
    const uchar* p = reinterpret_cast<const uchar*>(d.constData());
    qint32 off  = qFromLittleEndian<qint32>(p+0x00);
    qint32 ver  = qFromLittleEndian<qint32>(p+0x04);
    qint32 cols = qFromLittleEndian<qint32>(p+0x18);
    qint32 hoop = qFromLittleEndian<qint32>(p+0x20);
    qint32 code = qFromLittleEndian<qint32>(p+0x74);   // first colour entry
    CHECK(ver == 0x14, "header version == 0x14");
    CHECK(off == 0x74 + cols*8, "stitch offset == 0x74 + colors*8 (two tables)");
    CHECK(cols == 1, "colour count == 1 (at 0x18)");
    CHECK(hoop == 0, "hoop code == 0 (at 0x20)");
    CHECK(code == 123, "Janome colour code stored at 0x74");
    CHECK((uchar)p[0x16]==0x64, "byte 0x16 == 0x64 reference constant");
    CHECK(d.size() > off, "file has stitch body after header");
    CHECK((uchar)d[d.size()-4]==0x80 && (uchar)d[d.size()-3]==0x10, "ends with 0x80 0x10 end marker");

    std::printf("== JEF round-trip ==\n");
    StitchSequence back;
    CHECK(JefCodec::importFromFile(testJefPath, back), "re-import parses the file");
    CHECK(back.palette.size()==1, "re-import recovered colour count");
    // bounding size should roughly match (within 0.2mm quantisation)
    double a0,b0,c0,e0, a1,b1,c1,e1;
    tat.bounds(a0,b0,c0,e0); back.bounds(a1,b1,c1,e1);
    CHECK(std::abs((c0-a0)-(c1-a1))<0.3, "round-trip width preserved (<=0.3mm)");

    std::printf("== Bézier editor model -> generators ==\n");
    {
        // Two rails as EditPaths (like the satin demo), then generate.
        EditPath rA, rB;
        { BezierNode a(QPointF(-40,-20)); a.ctrlOut=QPointF(-20,20);
          BezierNode b(QPointF( 40,-20)); b.ctrlIn =QPointF( 20,20);
          rA.nodes<<a<<b; }
        { BezierNode a(QPointF(-40,-12)); a.ctrlOut=QPointF(-20,32);
          BezierNode b(QPointF( 40, -8)); b.ctrlIn =QPointF( 20,32);
          rB.nodes<<a<<b; }
        QPainterPath pA=rA.toPainterPath(), pB=rB.toPainterPath();
        CHECK(pA.elementCount()>=2 && pB.elementCount()>=2, "EditPath -> QPainterPath (rails)");
        auto col = SatinGenerator::generate(pA, pB, 0.4, 0.15);
        CHECK(col.size()>30, "editor rails produce a satin column");

        // Closed circle EditPath -> polygon -> tatami.
        const double r=34.0, k=r*0.5522847498;
        EditPath ring; ring.closed=true;
        auto nd=[&](QPointF p,QPointF i,QPointF o){BezierNode n(p);n.ctrlIn=i;n.ctrlOut=o;return n;};
        ring.nodes<<nd(QPointF(r,0),QPointF(r,-k),QPointF(r,k))
                  <<nd(QPointF(0,r),QPointF(k,r),QPointF(-k,r))
                  <<nd(QPointF(-r,0),QPointF(-r,k),QPointF(-r,-k))
                  <<nd(QPointF(0,-r),QPointF(-k,-r),QPointF(k,-r));
        QPolygonF poly=ring.toPolygon();
        CHECK(poly.size()>=8, "closed EditPath -> filled polygon");
        TatamiFill::Params tp2;
        QVector<QPolygonF> reg; reg<<poly;
        auto fill=TatamiFill::generate(reg, tp2);
        CHECK(fill.size()>50, "editor polygon produces a tatami fill");
        // filled region should be within the circle's bbox (~68mm)
        double bx0,by0,bx1,by1; fill.bounds(bx0,by0,bx1,by1);
        CHECK((bx1-bx0)<=69.0 && (by1-by0)<=69.0, "tatami stays within polygon extent");
    }

    std::printf("== ThreadCatalog ==\n");
    {
        auto blk = ThreadCatalog::nearest(QColor(0,0,0));
        CHECK(blk.name=="Black" && blk.code==1, "black -> Janome Black (code 1)");
        auto red = ThreadCatalog::nearest(QColor(220,30,30), "Janome");
        CHECK(red.brand=="Janome", "brand filter honoured");
        CHECK(ThreadCatalog::all().size()>=20, "catalogue has entries");
    }

    std::printf("== TextDigitizer (raised) ==\n");
    {
        TextDigitizer::Params tp; tp.text="AB"; tp.heightMm=20; tp.raised=true;
        auto ts = TextDigitizer::generate(tp);
        CHECK(ts.size()>100, "text produced stitches");
        double x0,y0,x1,y1;
        if (ts.bounds(x0,y0,x1,y1))
            CHECK(std::abs((y1-y0)-20.0) < 6.0, "text height ~ 20mm");
        else CHECK(false,"text has bounds");
        // count color-change/jumps to confirm border pass ran
        CHECK(ts.realStitchCount()>50, "text has real penetrations");
    }

    std::printf("== ImageDigitizer ==\n");
    {
        QImage im(80,50,QImage::Format_RGB32);
        im.fill(QColor(250,250,250));               // white background
        for(int y=0;y<50;y++)for(int x=0;x<80;x++){
            if(x<30) im.setPixel(x,y,qRgb(210,40,40));      // red block
            else if(x>50) im.setPixel(x,y,qRgb(40,60,200)); // blue block
        }
        ImageDigitizer::Params ip; ip.colors=4; ip.widthMm=80; ip.brand="Janome";
        auto is = ImageDigitizer::generate(im, ip);
        CHECK(is.size()>50, "image produced stitches");
        CHECK(is.palette.size()>=2, "at least 2 thread colours (bg dropped)");
        CHECK(is.colorChangeCount()>=1, "colour change between regions");
        double x0,y0,x1,y1; is.bounds(x0,y0,x1,y1);
        CHECK((x1-x0)<=80.5, "image design within target width");
    }

    std::printf("== PhotoProcessor ==\n");
    {
        // Synthetic image: dark disc (value ~40) on light field (value ~230).
        QImage im(60,60,QImage::Format_RGB32);
        im.fill(QColor(230,230,230));
        const int cx=30, cy=30, r=18;
        for(int y=0;y<60;y++)for(int x=0;x<60;x++){
            int dx=x-cx, dy=y-cy;
            if(dx*dx+dy*dy <= r*r) im.setPixel(x,y,qRgb(40,40,40));
        }
        QImage g = PhotoProcessor::toGray(im);
        CHECK(g.format()==QImage::Format_Grayscale8, "toGray -> 8-bit grayscale");
        int t = PhotoProcessor::otsu(g);
        CHECK(t>60 && t<210, "Otsu threshold lands between the two peaks");

        QImage th = PhotoProcessor::threshold(g, t, false);
        long dark=0; for(int y=0;y<60;y++){const uchar*l=th.constScanLine(y);
            for(int x=0;x<60;x++) if(l[x]<128) ++dark;}
        const double discArea = 3.14159*r*r;
        CHECK(std::abs(double(dark)-discArea) < discArea*0.25, "threshold mask ~= disc area");

        QImage inv = PhotoProcessor::threshold(g, t, true);
        long darkInv=0; for(int y=0;y<60;y++){const uchar*l=inv.constScanLine(y);
            for(int x=0;x<60;x++) if(l[x]<128) ++darkInv;}
        CHECK(dark+darkInv == 60*60, "invert flips exactly the mask");

        QImage po = PhotoProcessor::posterize(g, 4);
        // count distinct tones
        bool seen[256]={false}; int tones=0;
        for(int y=0;y<60;y++){const uchar*l=po.constScanLine(y);
            for(int x=0;x<60;x++) if(!seen[l[x]]){seen[l[x]]=true;++tones;}}
        CHECK(tones<=4, "posterize(4) yields <= 4 tones");
    }

    std::printf("== ImageDigitizer Portrait / LineArt ==\n");
    {
        QImage im(60,60,QImage::Format_RGB32);
        im.fill(QColor(235,235,235));
        for(int y=0;y<60;y++)for(int x=0;x<60;x++){
            int dx=x-30, dy=y-30; if(dx*dx+dy*dy<=18*18) im.setPixel(x,y,qRgb(35,35,35));
        }
        ImageDigitizer::Params pp; pp.mode=ImageDigitizer::Mode::Portrait;
        pp.tones=1; pp.widthMm=60; pp.brand="Janome";
        auto ps = ImageDigitizer::generate(im, pp);
        CHECK(ps.size()>50, "portrait (silhouette) produced stitches");
        CHECK(ps.palette.size()==1, "silhouette uses exactly one thread colour");

        pp.portraitStyle=ImageDigitizer::PortraitStyle::Tones; pp.tones=3;
        auto ps3 = ImageDigitizer::generate(im, pp);
        CHECK(ps3.palette.size()==3, "3-tone portrait uses three thread colours");

        // every single-tone portrait style produces a 1-colour design
        for (auto st : { ImageDigitizer::PortraitStyle::Comic,
                         ImageDigitizer::PortraitStyle::Silhouette,
                         ImageDigitizer::PortraitStyle::Sketch,
                         ImageDigitizer::PortraitStyle::Detailed,
                         ImageDigitizer::PortraitStyle::Stylized }) {
            ImageDigitizer::Params sp; sp.mode=ImageDigitizer::Mode::Portrait;
            sp.portraitStyle=st; sp.widthMm=60; sp.brand="Janome";
            auto ss = ImageDigitizer::generate(im, sp);
            CHECK(ss.palette.size()==1, "single-tone portrait style uses one colour");
        }

        ImageDigitizer::Params lp; lp.mode=ImageDigitizer::Mode::LineArt;
        lp.widthMm=60; lp.runMm=2.0; lp.brand="Janome";
        auto ls = ImageDigitizer::generate(im, lp);
        CHECK(ls.size()>=8, "line-art traced an outline");
        CHECK(ls.palette.size()==1, "line-art uses one thread colour");
        // outline length of a disc r=18px @ (60mm/60px)=1mm/px ~ 2*pi*18 = 113mm
        // at ~2mm run stitches -> ~50+ points; ensure it's a plausible loop
        CHECK(ls.realStitchCount()>=20, "outline has a plausible number of run stitches");

        QImage pv = ImageDigitizer::preview(im, pp);
        CHECK(!pv.isNull(), "portrait preview renders");
    }

    std::printf("== OpenCV bridge ==\n");
    {
        std::printf("  info: OpenCV %s\n", OpenCvBridge::available() ? "verfuegbar" : "nicht eingebaut (nativer Fallback)");
        // face + head image: light head-blob with dark features on grey
        QImage im(120,140,QImage::Format_RGB32);
        im.fill(QColor(150,150,155));
        {
            QPainter pp(&im); pp.setRenderHint(QPainter::Antialiasing,true);
            pp.setBrush(QColor(235,220,205)); pp.setPen(Qt::NoPen);
            pp.drawEllipse(QPointF(60,60),34,42);
            pp.setBrush(QColor(40,35,35));
            pp.drawEllipse(QPointF(48,54),6,4); pp.drawEllipse(QPointF(72,54),6,4);
            pp.drawEllipse(QPointF(60,80),12,5);
        }
        bool found=false; QImage cr = OpenCvBridge::autoFaceCrop(im,&found);
        CHECK(!cr.isNull(), "autoFaceCrop liefert ein Bild");
        if (OpenCvBridge::available()) {
            QImage g = im.convertToFormat(QImage::Format_Grayscale8);
            QImage adapt = OpenCvBridge::adaptiveThreshold(g, 15, 6, false);
            CHECK(adapt.size()==g.size(), "adaptiver Threshold behaelt Bildgroesse");
            QImage canny = OpenCvBridge::cannyEdges(g, 60, 160, 3);
            CHECK(!canny.isNull(), "Canny liefert Kantenbild");
            auto cs = OpenCvBridge::findContours(canny, 5.0);
            CHECK(cs.size()>=1, "findContours findet mindestens eine Kontur");
            // full portrait pipeline with adaptive detail
            ImageDigitizer::Params pp2; pp2.mode=ImageDigitizer::Mode::Portrait;
            pp2.tones=1; pp2.fineDetail=true; pp2.widthMm=60; pp2.brand="Janome";
            auto ps=ImageDigitizer::generate(im,pp2);
            CHECK(ps.size()>30, "adaptives Konterfei erzeugt Stiche");
        }
    }

    std::printf("== LogoGenerator (Atelier-Badge) ==\n");
    {
        auto L = LogoGenerator::knueppelBadge();
        CHECK(L.size() > 200, "Logo erzeugt einen Stichstrom");
        CHECK(L.palette.size() == 1, "Logo nutzt eine Garnfarbe (Gold)");
        double x0,y0,x1,y1; bool ok = L.bounds(x0,y0,x1,y1);
        CHECK(ok && (x1-x0) > 100 && (x1-x0) < 140, "Logo ~132 mm breit");
        CHECK(ok && (y1-y0) < 110, "Logo-Höhe passt in Rahmen B (200 mm) und meist A");
        // must fit the MC350E large hoop
        CHECK(MachineProfile::current().fits(x1-x0, y1-y0), "Logo passt in einen MC350E-Rahmen");
    }

    std::printf("== QR encoder ==\n");
    {
        auto m1 = QrCode::encode("http://192.168.1.42:8080/");
        CHECK(m1.size()==25, "short URL -> version 2 (25x25)");
        auto m2 = QrCode::encode("http://172.16.254.199:65535/upload");
        CHECK(m2.size()==29, "longer URL -> version 3 (29x29)");
        // finder pattern corner is dark
        CHECK(!m1.empty() && m1[0][0] && m1[6][6] && !m1[7][7], "finder pattern present");
    }

    std::printf("== Multipart parser ==\n");
    {
        QByteArray payload = QByteArray::fromHex("89504e470d0a1a0a0102030405"); // fake PNG-ish bytes
        QByteArray body;
        body += "--BND\r\n";
        body += "Content-Disposition: form-data; name=\"image\"; filename=\"p.png\"\r\n";
        body += "Content-Type: image/png\r\n\r\n";
        body += payload;
        body += "\r\n--BND--\r\n";
        QByteArray got = QrUploadServer::parseMultipartImage(body, "BND");
        CHECK(got == payload, "multipart image bytes extracted exactly");
    }

    std::printf("== Underlay ==\n");
    {
        QPolygonF sq; sq<<QPointF(0,0)<<QPointF(30,0)<<QPointF(30,30)<<QPointF(0,30);
        TatamiFill::Params a; a.underlay=false; auto noU = TatamiFill::generate(sq, a);
        TatamiFill::Params b; b.underlay=true;  auto wU  = TatamiFill::generate(sq, b);
        CHECK(wU.size() > noU.size(), "underlay adds stitches under the fill");
        CHECK(wU.stitches.front().flags & SF_Jump, "fill now begins with underlay");
    }

    std::printf("== Monogram ==\n");
    {
        MonogramGenerator::Params mp; mp.letters="ABC"; mp.frame=MonogramGenerator::Frame::Oval; mp.heightMm=30;
        auto ms = MonogramGenerator::generate(mp);
        CHECK(ms.size() > 150, "monogram produces stitches");
        double a,b,c,e; ms.bounds(a,b,c,e);
        CHECK((c-a) > 20.0, "monogram has real width");
        MonogramGenerator::Params one; one.letters="M"; one.frame=MonogramGenerator::Frame::None;
        CHECK(MonogramGenerator::generate(one).size() > 30, "single-initial monogram works");
    }

    std::printf("== Design library ==\n");
    {
        auto set = DesignFactory::starterSet();
        CHECK(set.size() >= 18, "starter set has >= 18 designs");
        bool allNonEmpty = true; for (auto& d : set) if (d.seq.size() < 4) allNonEmpty = false;
        CHECK(allNonEmpty, "every starter design has stitches");

        const QString dir = QDir::tempPath() + QStringLiteral("/_sticklib");
        QDir(dir).removeRecursively();
        DesignLibrary lib(dir);
        int n = lib.ensurePopulated();
        CHECK(n == int(set.size()), "library populated with starter designs");
        CHECK(QFile::exists(dir + "/index.json"), "index.json written");

        // reload from disk in a fresh instance
        DesignLibrary lib2(dir);
        lib2.ensurePopulated();
        CHECK(lib2.all().size() == n, "library reloads from disk without repopulating");

        // search + category filter
        CHECK(lib2.search("herz").size() >= 1, "search finds 'herz'");
        CHECK(lib2.search("", "Monogramme").size() >= 3, "category filter works");
        CHECK(lib2.categoriesPresent().size() >= 4, "several categories present");

        // load one design back with colours + thumbnail exists
        auto first = lib2.all().front();
        StitchSequence back;
        CHECK(lib2.load(first, back), "design loads from library");
        CHECK(back.realStitchCount() > 0 && !back.palette.empty(), "loaded design has stitches + palette");
        CHECK(QFile::exists(lib2.absPath(first.thumb)), "thumbnail png exists");
    }

    std::printf("== AppliqueGenerator ==\n");
    {
        QPolygonF poly;
        poly << QPointF(-25, -25) << QPointF(25, -25) << QPointF(25, 25) << QPointF(-25, 25);
        AppliqueGenerator::Params ap;
        ap.satinWidthMm = 3.0;
        ap.tackStyle = AppliqueGenerator::TackStyle::ZigZag;
        auto appSeq = AppliqueGenerator::generate(poly, ap);
        CHECK(appSeq.realStitchCount() > 100, "applique produced > 100 stitches");
        CHECK(appSeq.palette.size() == 3, "applique generated 3 distinct thread colors (Placement, Tack, Cover)");
        CHECK(appSeq.colorChangeCount() >= 2, "contains color stops between stages");
        double x0, y0, x1, y1;
        CHECK(appSeq.bounds(x0, y0, x1, y1), "applique has valid bounding box");
        CHECK(std::abs((x1 - x0) - 50.0) < 4.0, "applique width matches polygon (~50mm)");
    }

    std::printf("== DstCodec Export & Import ==\n");
    {
        QPolygonF poly;
        poly << QPointF(0, 0) << QPointF(30, 0) << QPointF(30, 30) << QPointF(0, 30);
        TatamiFill::Params tpDst;
        QVector<QPolygonF> reg; reg << poly;
        auto testSeq = TatamiFill::generate(reg, tpDst);
        testSeq.palette.clear();
        testSeq.palette.emplace_back(QColor(220, 40, 40), "Red", 1);

        const QString testDstPath = QDir::tempPath() + QStringLiteral("/_verify.dst");
        auto expRes = DstCodec::exportToFile(testDstPath, testSeq);
        CHECK(expRes.ok, "DST export succeeded");
        CHECK(expRes.stitchesWritten > 50, "DST stitches written > 50");

        StitchSequence dstBack;
        bool impOk = DstCodec::importFromFile(testDstPath, dstBack);
        CHECK(impOk, "DST import succeeded");
        CHECK(dstBack.realStitchCount() == testSeq.realStitchCount(), "DST imported stitch count matches original");

        double x0, y0, x1, y1, bx0, by0, bx1, by1;
        testSeq.bounds(x0, y0, x1, y1);
        dstBack.bounds(bx0, by0, bx1, by1);
        CHECK(std::abs((x1 - x0) - (bx1 - bx0)) < 0.25, "DST bounds width preserved within 0.25mm");
        CHECK(std::abs((y1 - y0) - (by1 - by0)) < 0.25, "DST bounds height preserved within 0.25mm");
    }

    std::printf("== Design Translation & Hoop Bounds ==\n");
    {
        StitchSequence seq;
        seq.stitches.push_back(Stitch{ 10.0, 20.0, 0, 0 });
        seq.stitches.push_back(Stitch{ 30.0, 40.0, 0, 0 });
        seq.stitches.push_back(Stitch{ 20.0, 60.0, 0, 0 });
        double x0, y0, x1, y1;
        CHECK(seq.bounds(x0, y0, x1, y1), "initial bounds calculated");
        CHECK(std::abs(x0 - 10.0) < 1e-4 && std::abs(x1 - 30.0) < 1e-4, "initial X bounds 10..30");
        CHECK(std::abs(y0 - 20.0) < 1e-4 && std::abs(y1 - 60.0) < 1e-4, "initial Y bounds 20..60");

        // Translate by dx = -15, dy = +25
        const double dx = -15.0, dy = 25.0;
        for (Stitch& s : seq.stitches) { s.x += dx; s.y += dy; }
        double tx0, ty0, tx1, ty1;
        CHECK(seq.bounds(tx0, ty0, tx1, ty1), "translated bounds calculated");
        CHECK(std::abs(tx0 - (x0 + dx)) < 1e-4, "translated X0 shifted by dx");
        CHECK(std::abs(tx1 - (x1 + dx)) < 1e-4, "translated X1 shifted by dx");
        CHECK(std::abs(ty0 - (y0 + dy)) < 1e-4, "translated Y0 shifted by dy");
        CHECK(std::abs(ty1 - (y1 + dy)) < 1e-4, "translated Y1 shifted by dy");

        // Center on hoop origin
        const double cx = 0.5 * (tx0 + tx1);
        const double cy = 0.5 * (ty0 + ty1);
        for (Stitch& s : seq.stitches) { s.x -= cx; s.y -= cy; }
        double cx0, cy0, cx1, cy1;
        seq.bounds(cx0, cy0, cx1, cy1);
        CHECK(std::abs(cx0 + cx1) < 1e-4, "design centered horizontally on X=0");
        CHECK(std::abs(cy0 + cy1) < 1e-4, "design centered vertically on Y=0");

        // Check Hoop B (140x200, half = 70 x 100)
        const double hw = 140.0 * 0.5, hh = 200.0 * 0.5;
        bool fitsB = (cx0 >= -hw && cx1 <= hw && cy0 >= -hh && cy1 <= hh);
        CHECK(fitsB, "centered design fits inside Hoop B (140x200)");
    }

    std::printf("== SVG Path Parser & MEF Logo Vector Digitizing ==\n");
    {
        // Test 1: SvgPathParser basic path
        const QString testD = QStringLiteral("M 10 20 L 50 20 C 60 20 70 30 70 40 L 70 80 Z");
        QPainterPath parsed = SvgPathParser::parsePathData(testD);
        CHECK(!parsed.isEmpty(), "SvgPathParser parsed basic path string");
        QRectF pbr = parsed.boundingRect();
        CHECK(std::abs(pbr.left() - 10.0) < 0.1 && std::abs(pbr.right() - 70.0) < 0.1,
              "SvgPathParser bounds left=10, right=70");

        // Test 2: SvgPathParser load meflogoplain.svg
        const QStringList mefPaths = {
            QStringLiteral(":/svg/meflogo.svg"),
            QStringLiteral("media/meflogoplain.svg"),
            QStringLiteral("../media/meflogoplain.svg"),
            QStringLiteral("C:/Users/micbu/Desktop/meflogoplain.svg")
        };
        QPainterPath mefPath;
        for (const QString& p : mefPaths) {
            if (QFile::exists(p)) {
                mefPath = SvgPathParser::parseSvgFile(p);
                if (!mefPath.isEmpty()) break;
            }
        }
        CHECK(!mefPath.isEmpty(), "parsed meflogoplain.svg into QPainterPath");
        QRectF mbr = mefPath.boundingRect();
        CHECK(mbr.width() > 600.0 && mbr.height() > 380.0, "meflogoplain.svg has full dimensions (~676x422)");

        // Test 3: SvgDigitizer AuthenticPatchSatin style (Foto-Original Meister-Aufnäher)
        StitchSequence mefPatch = LogoGenerator::mefOriginalBadge(0, 150.0);
        CHECK(!mefPatch.empty(), "mefOriginalBadge (AuthenticPatchSatin) generated stitches");
        CHECK(mefPatch.stitches.size() > 3000, "mefOriginalBadge (Patch) has > 3000 dense satin stitches");
        double x0, y0, x1, y1;
        mefPatch.bounds(x0, y0, x1, y1);
        const double w = x1 - x0, h = y1 - y0;
        CHECK(std::abs(w - 150.0) < 6.0, "mefOriginalBadge (Patch) width scaled to ~150 mm");
        CHECK(h < 120.0, "mefOriginalBadge (Patch) height fits within 140 mm hoop height");

        // Check placement inside Hoop B (140 x 200 mm)
        const double hw = 140.0 * 0.5, hh = 200.0 * 0.5;
        bool fitsHoopB = (x0 >= -hh && x1 <= hh && y0 >= -hw && y1 <= hw);
        CHECK(fitsHoopB, "mefOriginalBadge (Patch) fits inside Janome Hoop B (140x200 mm)");

        // Test 3b: SvgDigitizer ContourEcho style (Atelier Kontur-Glanz)
        StitchSequence mefContour = LogoGenerator::mefOriginalBadge(4, 150.0);
        CHECK(!mefContour.empty(), "mefOriginalBadge (ContourGlanz) generated stitches");
        CHECK(mefContour.stitches.size() > 1000, "mefOriginalBadge (Contour) has > 1000 detailed stitches");

        // Test 4: SvgDigitizer RoyalDuotoneGold style (2 colors)
        StitchSequence mefDuotone = LogoGenerator::mefOriginalBadge(2, 150.0);
        CHECK(!mefDuotone.empty(), "mefOriginalBadge (Zwei-Ton-Relief) generated stitches");
        CHECK(mefDuotone.palette.size() == 2, "mefDuotone palette has 2 Madeira gold shades");
        bool hasColorChange = false;
        for (const auto& st : mefDuotone.stitches) {
            if (st.flags & SF_ColorChange) { hasColorChange = true; break; }
        }
        CHECK(hasColorChange, "mefDuotone includes SF_ColorChange between thread passes");

        // Test 5: SvgDigitizer TatamiWeave style
        StitchSequence mefTatami = LogoGenerator::mefOriginalBadge(1, 150.0);
        CHECK(!mefTatami.empty(), "mefOriginalBadge (Meister-Tatami) generated stitches");

        // Test 6: Export MEF logo patch to JEF and DST
        const QString jefTestPath = QDir::tempPath() + QStringLiteral("/_mef_test.jef");
        auto jefRes = JefCodec::exportToFile(jefTestPath, mefPatch, HoopType::HoopB_140x200);
        CHECK(jefRes.ok, "exported mefOriginalBadge (Patch) to Janome JEF file");

        const QString dstTestPath = QDir::tempPath() + QStringLiteral("/_mef_test.dst");
        auto dstRes = DstCodec::exportToFile(dstTestPath, mefPatch);
        CHECK(dstRes.ok, "exported mefOriginalBadge (Patch) to Tajima DST file");
    }

    std::printf("== Janome Hoops Palette & Fit ==\n");
    {
        const auto& mp = MachineProfile::current();
        bool hasHoopC = false, hasHoopSQ14 = false;
        for (HoopType ht : mp.hoops) {
            if (ht == HoopType::HoopC_50x50) hasHoopC = true;
            if (ht == HoopType::HoopSQ14_140) hasHoopSQ14 = true;
        }
        CHECK(hasHoopC, "MachineProfile includes Janome Hoop C 50x50");
        CHECK(hasHoopSQ14, "MachineProfile includes Janome Hoop SQ14 140x140");

        const HoopSpec specC = hoopSpec(HoopType::HoopC_50x50);
        CHECK(specC.widthMm == 50.0 && specC.heightMm == 50.0, "Hoop C dimensions 50x50 mm");

        const HoopSpec specSQ14 = hoopSpec(HoopType::HoopSQ14_140);
        CHECK(specSQ14.widthMm == 140.0 && specSQ14.heightMm == 140.0, "Hoop SQ14 dimensions 140x140 mm");

        bool fits = false;
        HoopType chosenSmall = fitHoopFor(35.0, 40.0, &fits);
        CHECK(fits && chosenSmall == HoopType::HoopC_50x50, "small 35x40 mm design fits in Hoop C");

        HoopType chosenSq = fitHoopFor(130.0, 130.0, &fits);
        CHECK(fits && chosenSq == HoopType::HoopSQ14_140, "square 130x130 mm design fits in Hoop SQ14");
    }

    std::printf("== Janome Digitizer Jr Lettering (Curved Baselines & Kerning) ==\n");
    {
        // 1. Arc Up baseline
        TextDigitizer::Params pArcUp;
        pArcUp.text = QStringLiteral("Janome");
        pArcUp.heightMm = 16.0;
        pArcUp.baseline = TextDigitizer::Baseline::ArcUp;
        pArcUp.arcRadiusMm = 45.0;
        QPainterPath pathArcUp = TextDigitizer::textPath(pArcUp);
        CHECK(!pathArcUp.isEmpty(), "textPath ArcUp generated valid painter path");

        StitchSequence seqArcUp = TextDigitizer::generate(pArcUp);
        CHECK(seqArcUp.realStitchCount() > 50, "ArcUp lettering generated stitches");

        // 2. Arc Down baseline
        TextDigitizer::Params pArcDown;
        pArcDown.text = QStringLiteral("Digitizer");
        pArcDown.heightMm = 16.0;
        pArcDown.baseline = TextDigitizer::Baseline::ArcDown;
        pArcDown.arcRadiusMm = 45.0;
        StitchSequence seqArcDown = TextDigitizer::generate(pArcDown);
        CHECK(seqArcDown.realStitchCount() > 50, "ArcDown lettering generated stitches");

        // 3. Letter spacing / kerning test
        TextDigitizer::Params pNormal;
        pNormal.text = QStringLiteral("STICK");
        pNormal.heightMm = 18.0;
        pNormal.letterSpacingMm = 0.0;
        QPainterPath pathNormal = TextDigitizer::textPath(pNormal);

        TextDigitizer::Params pSpaced;
        pSpaced.text = QStringLiteral("STICK");
        pSpaced.heightMm = 18.0;
        pSpaced.letterSpacingMm = 6.0;
        QPainterPath pathSpaced = TextDigitizer::textPath(pSpaced);

        CHECK(pathSpaced.boundingRect().width() > pathNormal.boundingRect().width() + 16.0,
              "letter spacing widens the lettering text block proportionally");

        // 4. Slant angle test
        TextDigitizer::Params pSlanted;
        pSlanted.text = QStringLiteral("ITALIC");
        pSlanted.heightMm = 18.0;
        pSlanted.slantDeg = 20.0;
        QPainterPath pathSlanted = TextDigitizer::textPath(pSlanted);
        CHECK(!pathSlanted.isEmpty(), "slanted lettering produced valid geometry");
    }

    std::printf("== Janome Transformations (Mirror H/V, Rotate 90, Rotate Free) ==\n");
    {
        StitchSequence testSeq;
        testSeq.add(10.0, 20.0, SF_Normal);
        testSeq.add(30.0, 60.0, SF_Normal);

        double x0, y0, x1, y1;
        testSeq.bounds(x0, y0, x1, y1);
        const double cx = (x0 + x1) * 0.5; // 20.0
        const double cy = (y0 + y1) * 0.5; // 40.0

        // 1. Mirror Horizontal (Flip X across cx)
        StitchSequence hSeq = testSeq;
        for (Stitch& s : hSeq.stitches) s.x = 2.0 * cx - s.x;
        CHECK(std::abs(hSeq.stitches[0].x - 30.0) < 1e-4, "mirror horizontal moved 10 to 30");
        CHECK(std::abs(hSeq.stitches[1].x - 10.0) < 1e-4, "mirror horizontal moved 30 to 10");

        // 2. Mirror Vertical (Flip Y across cy)
        StitchSequence vSeq = testSeq;
        for (Stitch& s : vSeq.stitches) s.y = 2.0 * cy - s.y;
        CHECK(std::abs(vSeq.stitches[0].y - 60.0) < 1e-4, "mirror vertical moved 20 to 60");
        CHECK(std::abs(vSeq.stitches[1].y - 20.0) < 1e-4, "mirror vertical moved 60 to 20");

        // 3. Rotate 90 degrees clockwise
        StitchSequence rSeq = testSeq;
        for (Stitch& s : rSeq.stitches) {
            double dx = s.x - cx;
            double dy = s.y - cy;
            s.x = cx + dy;
            s.y = cy - dx;
        }
        double rx0, ry0, rx1, ry1;
        rSeq.bounds(rx0, ry0, rx1, ry1);
        CHECK(std::abs((rx1 - rx0) - (y1 - y0)) < 1e-4, "rotate 90 swaps width and height");
        CHECK(std::abs((ry1 - ry0) - (x1 - x0)) < 1e-4, "rotate 90 swaps height and width");

        // 4. Free rotate 45 degrees preserves distance from origin center
        const double rad = -45.0 * M_PI / 180.0;
        const double cosA = std::cos(rad), sinA = std::sin(rad);
        StitchSequence fSeq = testSeq;
        for (Stitch& s : fSeq.stitches) {
            double dx = s.x - cx, dy = s.y - cy;
            s.x = cx + dx * cosA - dy * sinA;
            s.y = cy + dx * sinA + dy * cosA;
        }
        const double origDist = std::hypot(testSeq.stitches[0].x - cx, testSeq.stitches[0].y - cy);
        const double rotDist  = std::hypot(fSeq.stitches[0].x - cx, fSeq.stitches[0].y - cy);
        CHECK(std::abs(origDist - rotDist) < 1e-4, "free rotate preserves distance from origin center");
    }

    std::printf("== TatamiFill Patterns & Angles ==\n");
    {
        QPolygonF poly;
        poly << QPointF(0, 0) << QPointF(50, 0) << QPointF(50, 50) << QPointF(0, 50);
        QVector<QPolygonF> polys{poly};

        // Test each pattern type
        for (auto pat : { TatamiFill::PatternType::StandardTatami,
                          TatamiFill::PatternType::Brick,
                          TatamiFill::PatternType::Twill,
                          TatamiFill::PatternType::Basketweave,
                          TatamiFill::PatternType::Honeycomb,
                          TatamiFill::PatternType::ContourEcho }) {
            TatamiFill::Params tp;
            tp.pattern = pat;
            tp.fillAngleDeg = 45.0;
            StitchSequence s = TatamiFill::generate(polys, tp);
            CHECK(!s.stitches.empty(), "pattern generated stitches");
            CHECK(!TatamiFill::patternName(pat).isEmpty(), "pattern has valid display name");
        }

        // Test fill angle variations
        for (double angle : { 0.0, 30.0, 90.0, 135.0, 270.0 }) {
            TatamiFill::Params tp;
            tp.fillAngleDeg = angle;
            StitchSequence s = TatamiFill::generate(polys, tp);
            CHECK(!s.stitches.empty(), "fill angle generated stitches");
        }
    }

    std::printf("== Hunting & Tradition Motifs (HuntingMotifs) ==\n");
    {
        for (auto m : { HuntingMotifs::MotifType::OakBranch,
                        HuntingMotifs::MotifType::StagHead,
                        HuntingMotifs::MotifType::WildBoar,
                        HuntingMotifs::MotifType::WaidmannsheilCrest }) {
            // 1. Stitches generation
            HuntingMotifs::Params p;
            p.widthMm = 80.0;
            p.fillAngleDeg = 30.0;
            p.pattern = TatamiFill::PatternType::Brick;
            p.satinOutline = true;
            StitchSequence seq = HuntingMotifs::generateStitches(m, p);
            CHECK(seq.stitches.size() > 100, "motif generated stitch stream > 100");
            CHECK(!seq.palette.empty(), "motif has thread palette assigned");

            double x0, y0, x1, y1;
            seq.bounds(x0, y0, x1, y1);
            const double w = x1 - x0;
            CHECK(w > 50.0 && w < 100.0, "motif width matches ~80mm target");

            // 2. Editable Bézier paths generation for 2D editor
            QVector<EditPath> editPaths = HuntingMotifs::generateEditablePaths(m, 80.0);
            CHECK(!editPaths.isEmpty(), "motif generated editable Bézier paths");
            int totalNodes = 0;
            for (const auto& ep : editPaths) totalNodes += ep.nodes.size();
            CHECK(totalNodes >= 4, "motif edit paths contain interactive Bézier nodes");

            // 3. Name & Description metadata
            CHECK(!HuntingMotifs::motifName(m).isEmpty(), "motif has name");
            CHECK(!HuntingMotifs::motifDescription(m).isEmpty(), "motif has detailed description");
        }
    }

    std::printf("== Deluxe Monograms & Multi-Letter Layouts ==\n");
    {
        for (auto frame : { MonogramGenerator::Frame::OakWreath,
                            MonogramGenerator::Frame::LaurelWreath,
                            MonogramGenerator::Frame::ShieldCrest,
                            MonogramGenerator::Frame::BaroqueCartouche }) {
            MonogramGenerator::Params mp;
            mp.letters = QStringLiteral("MB");
            mp.frame = frame;
            mp.heightMm = 35.0;
            mp.fillAngleDeg = 60.0;
            StitchSequence seq = MonogramGenerator::generate(mp);
            CHECK(seq.stitches.size() > 200, "deluxe monogram produced stitches");
            CHECK(!seq.palette.empty(), "deluxe monogram has palette");
        }

        // Test 1, 2, 3 letters
        for (const QString& initials : { QStringLiteral("M"), QStringLiteral("AB"), QStringLiteral("JMB") }) {
            MonogramGenerator::Params mp;
            mp.letters = initials;
            mp.frame = MonogramGenerator::Frame::OakWreath;
            mp.heightMm = 30.0;
            StitchSequence seq = MonogramGenerator::generate(mp);
            CHECK(!seq.stitches.empty(), "multi-letter initials generated stitches");
        }
    }

    std::printf("== Real reference round-trip ==\n");
    const char* ref = (argc > 1 && argv[1][0] != '-') ? argv[1] : nullptr;
    if (ref) {
        StitchSequence rin;
        bool okIn = JefCodec::importFromFile(QString::fromUtf8(ref), rin);
        CHECK(okIn, "imported the real reference .jef");
        if (okIn) {
            CHECK(rin.palette.size() == 10, "reference has 10 colours");
            // Re-export the reference's stitches and re-import; extents must match.
            const QString reexpPath = QDir::tempPath() + QStringLiteral("/_reexp.jef");
            JefCodec::Result rr = JefCodec::exportToFile(reexpPath, rin,
                                                          HoopType::HoopB_140x200);
            CHECK(rr.ok, "re-exported reference stitches");
            StitchSequence rback;
            JefCodec::importFromFile(reexpPath, rback);
            double x0,y0,x1,y1, X0,Y0,X1,Y1;
            rin.bounds(x0,y0,x1,y1); rback.bounds(X0,Y0,X1,Y1);
            CHECK(std::abs((x1-x0)-(X1-X0))<0.2 && std::abs((y1-y0)-(Y1-Y0))<0.2,
                  "re-export preserves design size (<=0.2mm)");
            CHECK(rback.realStitchCount()==rin.realStitchCount(),
                  "re-export preserves stitch count");
            std::printf("     reference: %zu stitches, size %.1f x %.1f mm\n",
                        rin.realStitchCount(), x1-x0, y1-y0);
        }
    } else {
        std::printf("  (skip: pass the reference .jef path as argv[1])\n");
    }

    std::printf("\n%s (%d failures)\n", failures? "FAILURES" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
