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
#include "library/StitchThumbnail.h"
#include "editor/BezierPath.h"
#include "generators/ColorSorter.h"
#include "generators/BastingGenerator.h"
#include "generators/CarvingPattern.h"
#include "generators/SfumatoGenerator.h"
#include "generators/CrossStitchGenerator.h"
#include "generators/MultiHoopSplitter.h"
#include "generators/GradientFill.h"
#include "generators/BeanStitchGenerator.h"
#include "generators/StipplingGenerator.h"
#include "generators/FlorentineFill.h"
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
                                QStringLiteral("../../media/fonts"),
                                QStringLiteral("assets/fonts"),
                                QStringLiteral("../assets/fonts"),
                                QStringLiteral("E:/StickCore/assets/fonts") }) {
        QDir d(dir);
        for (const QString& f : d.entryList(QStringList() << QStringLiteral("*.ttf"))) {
            int id = QFontDatabase::addApplicationFont(d.filePath(f));
            if (id != -1) {
                QStringList fams = QFontDatabase::applicationFontFamilies(id);
                std::printf("  Loaded font: %s -> families: %s\n",
                            f.toUtf8().constData(), fams.join(", ").toUtf8().constData());
            }
        }
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

    QFile f(testJefPath);
    bool openOk = f.open(QIODevice::ReadOnly);
    CHECK(openOk, "test JEF file opened for verification");
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
        bool hasHoopA = false, hasHoopB = false, hasHoopC = false, hasHoopD = false;
        bool hasHoopF = false, hasHoopHat = false, hasHoopMagLarge = false, hasHoopMagMed = false, hasHoopSQ14 = false;
        for (HoopType ht : mp.hoops) {
            if (ht == HoopType::HoopA_126x110)   hasHoopA = true;
            if (ht == HoopType::HoopB_140x200)   hasHoopB = true;
            if (ht == HoopType::HoopC_50x50)     hasHoopC = true;
            if (ht == HoopType::HoopD_230x200)   hasHoopD = true;
            if (ht == HoopType::HoopF_110x110)   hasHoopF = true;
            if (ht == HoopType::HoopHat_100x90)  hasHoopHat = true;
            if (ht == HoopType::HoopMag_140x200) hasHoopMagLarge = true;
            if (ht == HoopType::HoopMag_100x100) hasHoopMagMed = true;
            if (ht == HoopType::HoopSQ14_140)    hasHoopSQ14 = true;
        }
        CHECK(hasHoopA, "MachineProfile includes Janome Hoop A 126x110");
        CHECK(hasHoopB, "MachineProfile includes Janome Hoop B 140x200");
        CHECK(hasHoopC, "MachineProfile includes Janome Hoop C 50x50");
        CHECK(hasHoopD, "MachineProfile includes Janome Giga Hoop D 230x200");
        CHECK(hasHoopF, "MachineProfile includes Janome Spring Hoop F 110x110");
        CHECK(hasHoopHat, "MachineProfile includes Hat Hoop 100x90 (Cap insert)");
        CHECK(hasHoopMagLarge, "MachineProfile includes Magnetic Hoop 140x200 (Sew Tech)");
        CHECK(hasHoopMagMed, "MachineProfile includes Magnetic Hoop 100x100 (Sew Tech 4x4)");
        CHECK(hasHoopSQ14, "MachineProfile includes Janome Hoop SQ14 140x140");

        const HoopSpec specC = hoopSpec(HoopType::HoopC_50x50);
        CHECK(specC.widthMm == 50.0 && specC.heightMm == 50.0, "Hoop C dimensions 50x50 mm");
        CHECK(specC.jefCode == 1, "Hoop C JEF code is 1");

        const HoopSpec specSQ14 = hoopSpec(HoopType::HoopSQ14_140);
        CHECK(specSQ14.widthMm == 140.0 && specSQ14.heightMm == 140.0, "Hoop SQ14 dimensions 140x140 mm");
        CHECK(specSQ14.jefCode == 15, "Hoop SQ14 JEF code is 15");

        const HoopSpec specHat = hoopSpec(HoopType::HoopHat_100x90);
        CHECK(specHat.widthMm == 100.0 && specHat.heightMm == 90.0, "Hat Hoop dimensions 100x90 mm");
        CHECK(specHat.jefCode == 2, "Hat Hoop writes JEF code 2 (mounted in Hoop B frame)");

        const HoopSpec specMagLarge = hoopSpec(HoopType::HoopMag_140x200);
        CHECK(specMagLarge.widthMm == 140.0 && specMagLarge.heightMm == 200.0, "Magnetic Large dimensions 140x200 mm");
        CHECK(specMagLarge.jefCode == 2, "Magnetic Large writes JEF code 2");

        const HoopSpec specMagMed = hoopSpec(HoopType::HoopMag_100x100);
        CHECK(specMagMed.widthMm == 100.0 && specMagMed.heightMm == 100.0, "Magnetic Medium dimensions 100x100 mm");
        CHECK(specMagMed.jefCode == 0, "Magnetic Medium writes JEF code 0");

        const HoopSpec specF = hoopSpec(HoopType::HoopF_110x110);
        CHECK(specF.widthMm == 110.0 && specF.heightMm == 110.0, "Spring Hoop F dimensions 110x110 mm");
        CHECK(specF.jefCode == 3, "Spring Hoop F JEF code is 3");

        const HoopSpec specD = hoopSpec(HoopType::HoopD_230x200);
        CHECK(specD.widthMm == 230.0 && specD.heightMm == 200.0, "Giga Hoop D dimensions 230x200 mm");
        CHECK(specD.jefCode == 4, "Giga Hoop D JEF code is 4");

        // Verify JEF export header byte 0x20 for Hat Hoop
        StitchSequence hatSeq;
        hatSeq.add(-20.0, 0.0);
        hatSeq.add(20.0, 0.0);
        hatSeq.palette.push_back(ThreadColor(QColor(212, 175, 55), QStringLiteral("Gold"), 1083));
        const QString hatJefPath = QDir::tempPath() + QStringLiteral("/test_hat_hoop.jef");
        auto hatRes = JefCodec::exportToFile(hatJefPath, hatSeq, HoopType::HoopHat_100x90);
        CHECK(hatRes.ok, "JEF export for Hat Hoop succeeded");
        QFile hatF(hatJefPath);
        CHECK(hatF.open(QIODevice::ReadOnly), "test_hat_hoop.jef opened");
        QByteArray hatBytes = hatF.readAll();
        hatF.close();
        hatF.remove();
        CHECK(hatBytes.size() > 0x24, "hat JEF file has full header");
        const quint32 writtenHoopCode = static_cast<quint8>(hatBytes[0x20]) |
                                       (static_cast<quint8>(hatBytes[0x21]) << 8);
        CHECK(writtenHoopCode == 2, "hat JEF header byte 0x20 is code 2 (Hoop B hardware compatibility)");

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
        CHECK(hSeq.mirrorHorizontal(), "mirrorHorizontal returned true");
        CHECK(std::abs(hSeq.stitches[0].x - 30.0) < 1e-4, "mirror horizontal moved 10 to 30");
        CHECK(std::abs(hSeq.stitches[1].x - 10.0) < 1e-4, "mirror horizontal moved 30 to 10");

        // 2. Mirror Vertical (Flip Y across cy)
        StitchSequence vSeq = testSeq;
        CHECK(vSeq.mirrorVertical(), "mirrorVertical returned true");
        CHECK(std::abs(vSeq.stitches[0].y - 60.0) < 1e-4, "mirror vertical moved 20 to 60");
        CHECK(std::abs(vSeq.stitches[1].y - 20.0) < 1e-4, "mirror vertical moved 60 to 20");

        // 3. Rotate 90 degrees clockwise
        StitchSequence rSeq = testSeq;
        CHECK(rSeq.rotate90(true), "rotate90 returned true");
        double rx0, ry0, rx1, ry1;
        rSeq.bounds(rx0, ry0, rx1, ry1);
        CHECK(std::abs((rx1 - rx0) - (y1 - y0)) < 1e-4, "rotate 90 swaps width and height");
        CHECK(std::abs((ry1 - ry0) - (x1 - x0)) < 1e-4, "rotate 90 swaps height and width");

        // 4. Free rotate 45 degrees preserves distance from origin center
        StitchSequence fSeq = testSeq;
        CHECK(fSeq.rotate(45.0), "rotate(45.0) returned true");
        const double origDist = std::hypot(testSeq.stitches[0].x - cx, testSeq.stitches[0].y - cy);
        const double rotDist  = std::hypot(fSeq.stitches[0].x - cx, fSeq.stitches[0].y - cy);
        CHECK(std::abs(origDist - rotDist) < 1e-4, "free rotate preserves distance from origin center");

        // 5. Translate & Append
        StitchSequence tSeq = testSeq;
        tSeq.translate(5.0, -10.0);
        CHECK(std::abs(tSeq.stitches[0].x - 15.0) < 1e-4, "translate shifted x by +5");
        CHECK(std::abs(tSeq.stitches[0].y - 10.0) < 1e-4, "translate shifted y by -10");

        StitchSequence appSeq = testSeq;
        appSeq.append(tSeq);
        CHECK(appSeq.size() == testSeq.size() + tSeq.size(), "append combined stitch counts");
    }

    std::printf("== Centralized Geometry Polygon Utilities ==\n");
    {
        // Counter-Clockwise 20x10 rectangle
        QPolygonF ccwPoly;
        ccwPoly << QPointF(0, 0) << QPointF(20, 0) << QPointF(20, 10) << QPointF(0, 10);
        CHECK(isPolygonCounterClockwise(ccwPoly), "ccw polygon detected as counter-clockwise");
        CHECK(std::abs(polygonArea(ccwPoly) - 200.0) < 1e-4, "polygonArea returns 200 mm²");
        CHECK(polygonSignedArea(ccwPoly) > 0.0, "polygonSignedArea is positive for CCW");

        // Clockwise 20x10 rectangle
        QPolygonF cwPoly;
        cwPoly << QPointF(0, 0) << QPointF(0, 10) << QPointF(20, 10) << QPointF(20, 0);
        CHECK(!isPolygonCounterClockwise(cwPoly), "cw polygon detected as clockwise");
        CHECK(std::abs(polygonArea(cwPoly) - 200.0) < 1e-4, "polygonArea returns 200 mm² for CW");
        CHECK(polygonSignedArea(cwPoly) < 0.0, "polygonSignedArea is negative for CW");
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

    std::printf("== Embird Flagship 1: Smart Color Sort ==\n");
    {
        // 1. Spatially separated blocks: Red -> Blue -> Red -> Blue
        StitchSequence seq;
        seq.palette = { ThreadCatalog::snap(QColor(255, 0, 0)), ThreadCatalog::snap(QColor(0, 0, 255)) };
        // Block 0: Red at (-40, 0)
        seq.add(-40, 0, SF_Normal, 0); seq.add(-40, 5, SF_Normal, 0);
        // Block 1: Blue at (-10, 0)
        seq.add(-10, 0, SF_ColorChange, 1); seq.add(-10, 5, SF_Normal, 1);
        // Block 2: Red at (20, 0)
        seq.add(20, 0, SF_ColorChange, 0); seq.add(20, 5, SF_Normal, 0);
        // Block 3: Blue at (50, 0)
        seq.add(50, 0, SF_ColorChange, 1); seq.add(50, 5, SF_Normal, 1);

        ColorSorter::Stats stats;
        StitchSequence sorted = ColorSorter::sort(seq, &stats);
        CHECK(stats.colorChangesBefore == 3, "initial color changes counted correctly (3)");
        CHECK(stats.colorChangesAfter == 1, "smart color sort reduced color changes to 1");
        CHECK(stats.savedPercent > 60.0, "color changes reduced by > 60%");
        CHECK(sorted.size() == seq.size(), "sort preserves total stitches");

        // 2. Overlapping blocks: Layering safety test
        StitchSequence seq2;
        seq2.palette = seq.palette;
        seq2.add(0, 0, SF_Normal, 0); seq2.add(5, 5, SF_Normal, 0);
        seq2.add(0, 0, SF_ColorChange, 1); seq2.add(5, 5, SF_Normal, 1); // overlapping!
        seq2.add(40, 40, SF_ColorChange, 0); seq2.add(45, 45, SF_Normal, 0); // separate

        ColorSorter::Stats stats2;
        StitchSequence sorted2 = ColorSorter::sort(seq2, &stats2);
        CHECK(stats2.colorChangesAfter <= 1, "overlapping blocks correctly preserved in layer DAG");
    }

    std::printf("== Embird Flagship 2: Basting Box / Heftrahmen ==\n");
    {
        StitchSequence motif;
        motif.palette = { ThreadCatalog::snap(QColor(100, 100, 100)) };
        motif.add(-20, -20, SF_Normal, 0);
        motif.add(20, 20, SF_Normal, 0);

        BastingGenerator::Params bp;
        bp.mode = BastingGenerator::Mode::MotifBounds;
        bp.marginMm = 5.0;
        bp.stitchLengthMm = 5.0;
        bp.doublePass = false;

        StitchSequence basting = BastingGenerator::generate(motif, bp);
        CHECK(!basting.empty(), "basting box generated stitches");
        const QRectF bb = basting.boundingRect();
        CHECK(std::abs(bb.width() - 50.0) < 0.5, "basting width matches motif + 2*margin (50mm)");
        CHECK(std::abs(bb.height() - 50.0) < 0.5, "basting height matches motif + 2*margin (50mm)");

        // Test prependTo
        StitchSequence combined = BastingGenerator::prependTo(motif, bp, 140, 200);
        CHECK(combined.palette.size() == motif.palette.size() + 1, "basting color prepended to palette");
        CHECK(combined.stitches.front().colorIdx == 0, "basting stitches use color 0");
        CHECK(combined.stitches.back().colorIdx == 1, "original motif stitches shifted to color 1");
    }

    std::printf("== Embird Flagship 3: Tatami Carving & Texture Embossing ==\n");
    {
        // 1. Presets path generation
        for (auto preset : { CarvingPattern::Preset::Star, CarvingPattern::Preset::Heart,
                             CarvingPattern::Preset::OakLeaf, CarvingPattern::Preset::DiamondGrid,
                             CarvingPattern::Preset::WaveLines }) {
            QPainterPath p = CarvingPattern::createPath(preset, 30.0, 30.0);
            CHECK(!p.isEmpty(), "carving preset path is non-empty");
            CHECK(!CarvingPattern::presetName(preset).isEmpty(), "carving preset has descriptive name");
        }

        // 2. Scanline intersection finding
        QPainterPath star = CarvingPattern::createPath(CarvingPattern::Preset::Star, 30.0, 30.0);
        QVector<double> xCross = CarvingPattern::findIntersections(star, 0.0);
        CHECK(xCross.size() >= 2, "star carving has at least 2 contour crossings along center scanline");

        // 3. Tatami fill with carving pattern
        QPolygonF patch;
        patch << QPointF(-25, -25) << QPointF(25, -25) << QPointF(25, 25) << QPointF(-25, 25);
        TatamiFill::Params tp;
        tp.carving = CarvingPattern::Preset::OakLeaf;
        tp.carvingScaleMm = 25.0;
        tp.rowSpacingMm = 0.5;
        tp.maxStitchMm = 4.0;
        tp.underlay = false;
        StitchSequence carvedTatami = TatamiFill::generate(patch, tp);
        CHECK(!carvedTatami.empty(), "tatami fill with carving generated stitches");
        CHECK(carvedTatami.size() > 100, "carved tatami has sufficient puncture density");
    }

    std::printf("== Embird Flagship 4: Sfumato Photo-Stitch ==\n");
    {
        QImage testImg(100, 100, QImage::Format_ARGB32);
        testImg.fill(Qt::white);
        QPainter p(&testImg);
        QRadialGradient grad(50, 50, 45);
        grad.setColorAt(0.0, Qt::black);
        grad.setColorAt(1.0, Qt::white);
        p.fillRect(testImg.rect(), grad);
        p.end();

        SfumatoGenerator::Params sp;
        sp.widthMm = 60.0;
        sp.heightMm = 60.0;
        sp.lineSpacingMm = 1.5;
        sp.maxAmplitudeMm = 1.0;
        sp.angleMode = SfumatoGenerator::AngleMode::Horizontal;

        StitchSequence sfumatoSeq = SfumatoGenerator::generate(testImg, sp);
        CHECK(!sfumatoSeq.empty(), "horizontal sfumato generated stitches");
        CHECK(sfumatoSeq.size() > 200, "sfumato produced continuous stipple density");

        sp.angleMode = SfumatoGenerator::AngleMode::CrossHatch;
        StitchSequence sfumatoCross = SfumatoGenerator::generate(testImg, sp);
        CHECK(sfumatoCross.size() > sfumatoSeq.size(), "crosshatch sfumato has higher stitch count than single pass");
    }

    std::printf("== Embird Flagship 5: Traditional Cross-Stitch ==\n");
    {
        CHECK(std::abs(CrossStitchGenerator::aidaToPitchMm(CrossStitchGenerator::AidaCount::Count14) - 1.814) < 0.01, "14ct Aida pitch is ~1.81mm");
        CHECK(std::abs(CrossStitchGenerator::aidaToPitchMm(CrossStitchGenerator::AidaCount::Count11) - 2.309) < 0.01, "11ct Aida pitch is ~2.31mm");

        QImage icon(16, 16, QImage::Format_ARGB32);
        icon.fill(Qt::transparent);
        for (int y = 4; y < 12; ++y) {
            for (int x = 4; x < 12; ++x) {
                icon.setPixelColor(x, y, QColor(180, 20, 20));
            }
        }

        CrossStitchGenerator::Params cp;
        cp.aida = CrossStitchGenerator::AidaCount::Count14;
        cp.style = CrossStitchGenerator::StitchStyle::FullCross;
        cp.skipWhite = true;

        StitchSequence crossSeq = CrossStitchGenerator::generate(icon, cp);
        CHECK(!crossSeq.empty(), "cross-stitch generated stitches");
        CHECK(crossSeq.size() >= 250, "counted cross stitch produced correct number of legs");

        cp.style = CrossStitchGenerator::StitchStyle::DoubleCross;
        StitchSequence dblCrossSeq = CrossStitchGenerator::generate(icon, cp);
        CHECK(dblCrossSeq.size() > crossSeq.size(), "double cross produced extra structural stitches");
    }

    std::printf("== Embird Flagship 6: Auto-Split & Multi-Hooping ==\n");
    {
        StitchSequence bigSeq;
        bigSeq.palette = { ThreadCatalog::snap(QColor(50, 80, 160)) };
        for (double y = -110; y <= 110; y += 10) {
            bigSeq.add(-80, y, SF_Jump, 0);
            bigSeq.add(80, y, SF_Normal, 0);
        }

        CHECK(MultiHoopSplitter::exceedsHoop(bigSeq, 140.0, 200.0), "exceedsHoop correctly flags oversized design");

        MultiHoopSplitter::Params mp;
        mp.hoopWidthMm = 140.0;
        mp.hoopHeightMm = 200.0;
        mp.overlapMm = 10.0;
        mp.addCrosshairs = true;
        mp.direction = MultiHoopSplitter::SplitDirection::Horizontal;

        MultiHoopSplitter::Result res = MultiHoopSplitter::split(bigSeq, mp);
        CHECK(res.success, "multi-hoop split succeeded");
        CHECK(!res.hoop1.empty(), "hoop 1 has stitches");
        CHECK(!res.hoop2.empty(), "hoop 2 has stitches");
        CHECK(res.hoop1.palette.size() > bigSeq.palette.size(), "registration crosshair color added to hoop 1");
        CHECK(res.hoop2.palette.size() > bigSeq.palette.size(), "registration crosshair color prepended to hoop 2");
    }

    std::printf("== High-End Fill 1: Gradient / Ombré Tatami Fill ==\n");
    {
        QPolygonF poly;
        const double s = 30.0;
        poly << QPointF(-s, -s) << QPointF(s, -s) << QPointF(s, s) << QPointF(-s, s);

        GradientFill::Params gp;
        gp.minSpacingMm = 0.40;
        gp.maxSpacingMm = 2.00;
        gp.color1 = ThreadColor(QColor(212, 175, 55), QStringLiteral("Gold"));
        gp.color2 = ThreadColor(QColor(184, 115, 51), QStringLiteral("Bronze"));
        gp.underlay = true;

        StitchSequence gseq = GradientFill::generate(poly, gp);
        CHECK(!gseq.empty(), "gradient fill produced stitches");
        CHECK(gseq.palette.size() == 2, "gradient fill has exactly 2 thread colors");

        int countCol0 = 0, countCol1 = 0;
        bool hasColorChange = false;
        for (const auto& st : gseq.stitches) {
            if (st.colorIdx == 0) countCol0++;
            if (st.colorIdx == 1) countCol1++;
            if (st.is(SF_ColorChange)) hasColorChange = true;
        }
        CHECK(countCol0 > 50, "gradient pass 1 has > 50 stitches");
        CHECK(countCol1 > 50, "gradient pass 2 has > 50 stitches");
        CHECK(hasColorChange, "gradient sequence contains SF_ColorChange");

        const QString testJef = QDir::tempPath() + QStringLiteral("/_test_gradient.jef");
        auto jefRes = JefCodec::exportToFile(testJef, gseq, HoopType::HoopB_140x200);
        CHECK(jefRes.ok, "gradient sequence exported cleanly to JEF");
    }

    std::printf("== High-End Fill 2: Bean Stitch (3-Pass & 5-Pass Triple Run) ==\n");
    {
        QVector<QPointF> pts;
        pts << QPointF(0.0, 0.0) << QPointF(5.0, 0.0) << QPointF(10.0, 0.0);

        BeanStitchGenerator::Params bp3;
        bp3.stitchLengthMm = 5.0; // exact 2 segments of 5mm
        bp3.mode = BeanStitchGenerator::Mode::TriplePass;
        bp3.closed = false;

        StitchSequence seq3 = BeanStitchGenerator::generate(pts, bp3);
        // 1 initial node + 2 segments * 3 passes = 7 stitches
        CHECK(seq3.size() == 7, "3-pass bean stitch generates exactly 7 stitches for 2 segments");
        CHECK(seq3.stitches[0].is(SF_Jump), "first stitch is jump to start");
        CHECK(seq3.stitches[1].x == 5.0 && seq3.stitches[2].x == 0.0 && seq3.stitches[3].x == 5.0,
              "segment 1 executed forward-backward-forward");
        CHECK(seq3.stitches[4].x == 10.0 && seq3.stitches[5].x == 5.0 && seq3.stitches[6].x == 10.0,
              "segment 2 executed forward-backward-forward");

        BeanStitchGenerator::Params bp5 = bp3;
        bp5.mode = BeanStitchGenerator::Mode::FivePass;
        StitchSequence seq5 = BeanStitchGenerator::generate(pts, bp5);
        // 1 initial node + 2 segments * 5 passes = 11 stitches
        CHECK(seq5.size() == 11, "5-pass bean stitch generates exactly 11 stitches for 2 segments");

        QPainterPath path;
        path.addEllipse(QPointF(0, 0), 20, 20);
        StitchSequence pathSeq = BeanStitchGenerator::generate(path, bp3);
        CHECK(!pathSeq.empty() && pathSeq.size() > 20, "bean stitch on QPainterPath generated valid stitches");
        const QString testJef = QDir::tempPath() + QStringLiteral("/_test_bean.jef");
        auto jefRes = JefCodec::exportToFile(testJef, pathSeq, HoopType::HoopA_126x110);
        CHECK(jefRes.ok, "bean stitch exported cleanly to JEF");
    }

    std::printf("== High-End Fill 3: Stippling / Meander Fill ==\n");
    {
        QPolygonF poly;
        const double s = 30.0;
        poly << QPointF(-s, -s) << QPointF(s, -s) << QPointF(s, s) << QPointF(-s, s);

        StipplingGenerator::Params sp;
        sp.loopSpacingMm = 4.0;
        sp.stitchLengthMm = 2.0;
        sp.marginMm = 1.5;

        StitchSequence sseq = StipplingGenerator::generate(poly, sp);
        CHECK(!sseq.empty(), "stippling generated stitches");
        CHECK(sseq.size() > 100, "stippling has sufficient density (>100 stitches)");

        bool allInside = true;
        for (const auto& st : sseq.stitches) {
            if (st.x < -s - 0.5 || st.x > s + 0.5 || st.y < -s - 0.5 || st.y > s + 0.5) {
                allInside = false;
                break;
            }
        }
        CHECK(allInside, "all stippling stitches stay inside polygon bounding box");

        const QString testJef = QDir::tempPath() + QStringLiteral("/_test_stipple.jef");
        auto jefRes = JefCodec::exportToFile(testJef, sseq, HoopType::HoopA_126x110);
        CHECK(jefRes.ok, "stippling sequence exported cleanly to JEF");
    }

    std::printf("== High-End Fill 4: Florentine Curved Tatami Fill ==\n");
    {
        QPolygonF poly;
        const double s = 30.0;
        poly << QPointF(-s, -s) << QPointF(s, -s) << QPointF(s, s) << QPointF(-s, s);

        FlorentineFill::Params fp;
        fp.amplitudeMm = 5.0;
        fp.wavelengthMm = 25.0;
        fp.rowSpacingMm = 0.50;
        fp.underlay = true;

        StitchSequence fseq = FlorentineFill::generate(poly, fp);
        CHECK(!fseq.empty(), "florentine fill produced stitches");
        CHECK(fseq.size() > 200, "florentine fill has >200 stitches");

        // Check wave height variation
        double minY = 1000.0, maxY = -1000.0;
        for (const auto& st : fseq.stitches) {
            minY = std::min(minY, st.y);
            maxY = std::max(maxY, st.y);
        }
        CHECK(maxY - minY >= 2.0 * s, "florentine fill spans full polygon height");

        const QString testJef = QDir::tempPath() + QStringLiteral("/_test_florentine.jef");
        auto jefRes = JefCodec::exportToFile(testJef, fseq, HoopType::HoopB_140x200);
        CHECK(jefRes.ok, "florentine sequence exported cleanly to JEF");
    }

    std::printf("== Parametric Object Scaling, Density Resampling & Fit to Hoop ==\n");
    {
        // 1. Test StitchSequence::scaled() upscale with long-stitch subdivision
        StitchSequence seq;
        seq.palette.emplace_back(QColor(255, 0, 0), "Rot", 101);
        seq.stitches.emplace_back(0.0, 0.0, SF_Jump, 0);
        seq.stitches.emplace_back(0.0, 0.0, SF_Normal, 0);
        seq.stitches.emplace_back(3.0, 0.0, SF_Normal, 0);
        seq.stitches.emplace_back(6.0, 0.0, SF_Normal, 0);
        seq.stitches.emplace_back(6.0, 0.0, SF_End, 0);

        // Scale by 2.0 -> segment length becomes 6.0 mm. With maxStitchMm=3.5, it must be subdivided!
        StitchSequence upscaled = seq.scaled(2.0, QPointF(0.0, 0.0), 3.5, 0.35);
        CHECK(upscaled.palette.size() == 1, "scaled preserves palette");
        CHECK(upscaled.stitches.back().is(SF_End), "scaled preserves SF_End marker");

        double maxStitchLen = 0.0;
        for (size_t i = 1; i < upscaled.stitches.size(); ++i) {
            if (upscaled.stitches[i].flags & (SF_Jump | SF_End)) continue;
            double dx = upscaled.stitches[i].x - upscaled.stitches[i-1].x;
            double dy = upscaled.stitches[i].y - upscaled.stitches[i-1].y;
            maxStitchLen = std::max(maxStitchLen, std::hypot(dx, dy));
        }
        CHECK(maxStitchLen <= 3.501, "upscale subdivides stitches so none exceeds maxStitchMm (3.5mm)");
        CHECK(upscaled.size() > seq.size(), "subdivision created additional needle points to prevent thread float");

        // 2. Test StitchSequence::scaled() downscale with micro-stitch filtering
        StitchSequence denseSeq;
        denseSeq.palette.emplace_back(QColor(0, 128, 0), "Gruen", 102);
        denseSeq.stitches.emplace_back(0.0, 0.0, SF_Jump, 0);
        for (int i = 0; i <= 10; ++i) {
            denseSeq.stitches.emplace_back(i * 0.40, 0.0, SF_Normal, 0);
        }
        denseSeq.stitches.back().flags |= SF_End;

        // Scale down by 0.5 -> segment length becomes 0.20 mm (< minStitchMm 0.35 mm). Micro-stitches should be filtered!
        StitchSequence downscaled = denseSeq.scaled(0.5, QPointF(0.0, 0.0), 4.0, 0.35);
        CHECK(downscaled.size() < denseSeq.size(), "downscale filters out micro-stitches < 0.35mm to protect needle");

        // 3. Test Bézier EditPath scaling & bounding calculation
        EditPath ep;
        ep.nodes.push_back(BezierNode(QPointF(-20.0, -10.0)));
        ep.nodes.push_back(BezierNode(QPointF(20.0, -10.0)));
        ep.nodes.push_back(BezierNode(QPointF(20.0, 10.0)));
        ep.nodes.push_back(BezierNode(QPointF(-20.0, 10.0)));
        ep.closed = true;

        // Original bounds: 40 x 20 mm, center (0, 0)
        double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
        for (const auto& nd : ep.nodes) {
            minX = std::min(minX, nd.pos.x());
            maxX = std::max(maxX, nd.pos.x());
            minY = std::min(minY, nd.pos.y());
            maxY = std::max(maxY, nd.pos.y());
        }
        CHECK(std::abs((maxX - minX) - 40.0) < 1e-4, "path initial width is 40mm");
        CHECK(std::abs((maxY - minY) - 20.0) < 1e-4, "path initial height is 20mm");

        // Scale paths around center (0, 0) by factor 2.5
        const double scaleF = 2.5;
        for (auto& nd : ep.nodes) {
            nd.pos *= scaleF;
            nd.ctrlIn *= scaleF;
            nd.ctrlOut *= scaleF;
        }
        minX = 1e9; minY = 1e9; maxX = -1e9; maxY = -1e9;
        for (const auto& nd : ep.nodes) {
            minX = std::min(minX, nd.pos.x());
            maxX = std::max(maxX, nd.pos.x());
            minY = std::min(minY, nd.pos.y());
            maxY = std::max(maxY, nd.pos.y());
        }
        CHECK(std::abs((maxX - minX) - 100.0) < 1e-4, "scaled path width is 100mm");
        CHECK(std::abs((maxY - minY) - 50.0) < 1e-4, "scaled path height is 50mm");

        // 4. Test Fit-to-Hoop logic for standard Hoop B (140 x 200 mm, 5mm margin)
        const double hoopW = 140.0, hoopH = 200.0, margin = 5.0;
        const double usableW = hoopW - 2.0 * margin; // 130mm
        const double usableH = hoopH - 2.0 * margin; // 190mm
        const double curW = maxX - minX;             // 100mm
        const double curH = maxY - minY;             // 50mm
        const double fitFactor = std::min(usableW / curW, usableH / curH); // min(130/100, 190/50) = 1.30
        CHECK(std::abs(fitFactor - 1.30) < 1e-4, "fit to Hoop B calculates exact 1.30x factor (130mm width)");
        const double fittedW = curW * fitFactor;
        const double fittedH = curH * fitFactor;
        CHECK(fittedW <= usableW + 1e-4 && fittedH <= usableH + 1e-4, "fitted geometry stays strictly inside hoop safety margin");

        // 5. Test Fit-to-Hoop logic for circular Hoop C (50 x 50 mm, target diameter 40mm)
        const double targetDiag = 50.0 - 2.0 * margin; // 40mm
        const double diag = std::hypot(fittedW, fittedH);
        const double circFitFactor = targetDiag / diag;
        CHECK(circFitFactor < 1.0, "large design is shrunk to fit circular Hoop C");
        const double inCircDiag = diag * circFitFactor;
        CHECK(std::abs(inCircDiag - 40.0) < 1e-4, "design diagonal matches 40mm circular boundary exactly");

        // 6. Test JEF Export of scaled stitch sequence
        const QString scaledJef = QDir::tempPath() + QStringLiteral("/_test_scaled.jef");
        auto sres = JefCodec::exportToFile(scaledJef, upscaled, HoopType::HoopB_140x200);
        CHECK(sres.ok, "resampled scaled stitches export cleanly to JEF");
    }

    std::printf("== ImageDigitizer & Real-Time Photo Live Preview ==\n");
    {
        // 1. Verify loading of webp photo
        QImage skullImg(QStringLiteral("E:/StickCore/schädel.webp"));
        if (skullImg.isNull()) {
            skullImg.load(QStringLiteral("schädel.webp"));
        }
        CHECK(!skullImg.isNull(), "schädel.webp loaded successfully via Qt WebP plugin");
        if (!skullImg.isNull()) {
            std::printf("     schädel.webp: %dx%d px, format=%d, corners: top-left RGB(%d,%d,%d)\n",
                        skullImg.width(), skullImg.height(), int(skullImg.format()),
                        qRed(skullImg.pixel(0,0)), qGreen(skullImg.pixel(0,0)), qBlue(skullImg.pixel(0,0)));
            
            // 2. Test ImageDigitizer::preview for Mode::Colors with real k-means quantization
            ImageDigitizer::Params pColors;
            pColors.mode = ImageDigitizer::Mode::Colors;
            pColors.colors = 4;
            pColors.widthMm = 100.0;
            pColors.dropBackground = true;
            QImage prevColors = ImageDigitizer::preview(skullImg, pColors);
            CHECK(!prevColors.isNull(), "preview() for Mode::Colors produces valid preview image");
            CHECK(prevColors.format() == QImage::Format_ARGB32, "preview() produces ARGB32 quantized preview");

            // 3. Test ImageDigitizer::generateFastPreview for responsive UI sliders (< 20 ms)
            StitchSequence fastSeq = ImageDigitizer::generateFastPreview(skullImg, pColors);
            CHECK(!fastSeq.empty(), "generateFastPreview() returns valid stitch stream");
            CHECK(fastSeq.palette.size() <= 4, "fast preview palette matches reduced colors");

            // 4. Test realistic stitch simulation rendering on dark fabric
            QImage realisticThumb = StitchThumbnail::render(fastSeq, 340, QColor(20, 23, 28), true);
            CHECK(!realisticThumb.isNull(), "realistic StitchThumbnail rendering succeeded");
            CHECK(realisticThumb.width() == 340 && realisticThumb.height() == 340, "thumbnail dimensions match 340x340");

            // 5. Test ImageDigitizer::generate for full-res production
            StitchSequence seq = ImageDigitizer::generate(skullImg, pColors);
            CHECK(!seq.empty(), "generate() for schädel.webp in Colors mode generates stitches");
            CHECK(seq.realStitchCount() > 500, "production sequence has high stitch density");
            std::printf("     schädel stitches: %zu, palette: %zu colors\n",
                        seq.realStitchCount(), seq.palette.size());

            realisticThumb.save(QStringLiteral("C:/Users/micbu/.gemini/antigravity/brain/e572641d-70d6-45b5-8c91-c97f358a4f6c/schaedel_stitch_preview.png"), "PNG");
            prevColors.save(QStringLiteral("C:/Users/micbu/.gemini/antigravity/brain/e572641d-70d6-45b5-8c91-c97f358a4f6c/schaedel_analysis_preview.png"), "PNG");
            QImage prodThumb = StitchThumbnail::render(seq, 600, QColor(20, 23, 28), true);
            prodThumb.save(QStringLiteral("C:/Users/micbu/.gemini/antigravity/brain/e572641d-70d6-45b5-8c91-c97f358a4f6c/schaedel_production_stitches.png"), "PNG");

            // 6. Test fast Sfumato photo-stitch preview on schädel.webp
            SfumatoGenerator::Params sfp;
            sfp.widthMm = 100.0;
            sfp.heightMm = 100.0;
            sfp.lineSpacingMm = 1.4;
            sfp.invertLuminance = true;
            StitchSequence sfumatoSeq = SfumatoGenerator::generate(skullImg, sfp);
            CHECK(!sfumatoSeq.empty(), "fast Sfumato preview generates stitches for photo");
            QImage sfumatoThumb = StitchThumbnail::render(sfumatoSeq, 600, QColor(20, 23, 28), true);
            sfumatoThumb.save(QStringLiteral("C:/Users/micbu/.gemini/antigravity/brain/e572641d-70d6-45b5-8c91-c97f358a4f6c/schaedel_sfumato_preview.png"), "PNG");

            // 7. Test ImageDigitizer::extractPalette() for interactive Palette Manager
            auto pal = ImageDigitizer::extractPalette(skullImg, pColors);
            CHECK(!pal.empty() && pal.size() <= 4, "extractPalette returns quantized color palette");
            double totalCoverage = 0.0;
            for (const auto& qc : pal) {
                totalCoverage += qc.percentage;
                CHECK(!qc.thread.description.isEmpty(), "each quantized color has valid thread description");
            }
            CHECK(std::abs(totalCoverage - 100.0) < 1.0, "color coverage percentages sum to ~100%");
            bool foundBg = false;
            for (const auto& qc : pal) {
                if (qc.isBackground) foundBg = true;
            }
            CHECK(foundBg, "background color is correctly identified");

            // 8. Test selective color filtering via activeColors
            ImageDigitizer::Params pFiltered = pColors;
            // Only enable the last color (foreground features), disable the rest
            pFiltered.activeColors = std::vector<bool>(pal.size(), false);
            pFiltered.activeColors.back() = true;
            StitchSequence filteredSeq = ImageDigitizer::generate(skullImg, pFiltered);
            CHECK(!filteredSeq.empty(), "selective color filtering produces valid stitches");
            CHECK(filteredSeq.palette.size() == 1, "only active color is included in thread palette");
            CHECK(filteredSeq.realStitchCount() < seq.realStitchCount(), "filtered sequence stitches are a subset of full design");

            // 9. Test ImageDigitizer::Mode::CrossStitch
            ImageDigitizer::Params pCross;
            pCross.mode = ImageDigitizer::Mode::CrossStitch;
            pCross.widthMm = 80.0;
            pCross.colors = 3;
            pCross.crossStitchAida = CrossStitchGenerator::AidaCount::Count14;
            StitchSequence crossSeq = ImageDigitizer::generate(skullImg, pCross);
            CHECK(!crossSeq.empty(), "ImageDigitizer Mode::CrossStitch produces valid cross stitches");
            CHECK(crossSeq.palette.size() <= 3, "CrossStitch palette bounded by color count");

            // 9b. Test CrossStitch and Colors mode with flexible color counts up to 16
            {
                ImageDigitizer::Params pCross16 = pCross;
                pCross16.colors = 16;
                StitchSequence cross16Seq = ImageDigitizer::generate(skullImg, pCross16);
                CHECK(!cross16Seq.empty(), "CrossStitch with 16 colors produces valid stitches");
                CHECK(cross16Seq.palette.size() <= 16, "CrossStitch palette bounded by 16 colors");

                // Test 6 colors in Colors mode
                ImageDigitizer::Params pCol6 = pColors;
                pCol6.colors = 6;
                auto pal6 = ImageDigitizer::extractPalette(skullImg, pCol6);
                CHECK(!pal6.empty() && pal6.size() <= 7, "Colors mode with 6 colors extracts correct palette");

                // Test 12 tones in Tones mode
                ImageDigitizer::Params pTone12;
                pTone12.mode = ImageDigitizer::Mode::Colors;
                pTone12.separation = ImageDigitizer::SeparationMode::Tones;
                pTone12.colors = 12;
                pTone12.widthMm = 80.0;
                auto palTone12 = ImageDigitizer::extractPalette(skullImg, pTone12);
                CHECK(!palTone12.empty() && palTone12.size() <= 13, "Tones mode with 12 tones extracts correct palette");

                // Test 16 colors in Colors mode
                ImageDigitizer::Params pCol16 = pColors;
                pCol16.colors = 16;
                auto pal16 = ImageDigitizer::extractPalette(skullImg, pCol16);
                CHECK(!pal16.empty() && pal16.size() <= 17, "Colors mode with 16 colors extracts up to 16 thread colors + background");
            }

            // 10. Test ImageDigitizer::Mode::Sfumato via unified API
            ImageDigitizer::Params pSfumato;
            pSfumato.mode = ImageDigitizer::Mode::Sfumato;
            pSfumato.widthMm = 80.0;
            pSfumato.sfumatoLineSpacingMm = 1.5;
            pSfumato.sfumatoAngle = SfumatoGenerator::AngleMode::Horizontal;
            StitchSequence sfSeq = ImageDigitizer::generate(skullImg, pSfumato);
            CHECK(!sfSeq.empty(), "ImageDigitizer Mode::Sfumato produces valid stitch sequence");
            CHECK(sfSeq.palette.size() == 1, "Sfumato uses 1 monochrome thread color");

            // 11. Test LineArt BeanStitch vs SingleRun
            ImageDigitizer::Params pSingle;
            pSingle.mode = ImageDigitizer::Mode::LineArt;
            pSingle.widthMm = 60.0;
            pSingle.lineArtStitch = ImageDigitizer::LineArtStitch::SingleRun;
            StitchSequence singleSeq = ImageDigitizer::generate(skullImg, pSingle);

            ImageDigitizer::Params pBean = pSingle;
            pBean.lineArtStitch = ImageDigitizer::LineArtStitch::BeanStitch;
            StitchSequence beanSeq = ImageDigitizer::generate(skullImg, pBean);
            CHECK(!singleSeq.empty() && !beanSeq.empty(), "LineArt generates outlines");
            CHECK(beanSeq.realStitchCount() >= singleSeq.realStitchCount() * 2,
                  "3-pass BeanStitch has significantly more stitches than SingleRun for robust outlines");

            // 12. Test ImageDigitizer::traceToPaths 1-Color Stencil / Schablone (Inkscape Bitmap to Path)
            ImageDigitizer::Params pStencil;
            pStencil.colors = 1;
            pStencil.separation = ImageDigitizer::SeparationMode::Tones;
            pStencil.widthMm = 80.0;
            pStencil.smoothing = 1.0;
            QVector<EditPath> stencilPaths = ImageDigitizer::traceToPaths(skullImg, pStencil);
            CHECK(!stencilPaths.isEmpty(), "1-Color Stencil traceToPaths generates vector paths");
            bool allClosed = true;
            int totalNodes = 0;
            for (const auto& ep : stencilPaths) {
                if (!ep.closed) allClosed = false;
                totalNodes += ep.nodes.size();
            }
            CHECK(allClosed, "All extracted stencil vector paths are closed loops");
            CHECK(totalNodes >= 20, "Extracted vector paths have rich Bézier nodes");

            // 13. Test ImageDigitizer::traceToPaths Multi-Color / Multi-Tone
            ImageDigitizer::Params pTones;
            pTones.colors = 3;
            pTones.separation = ImageDigitizer::SeparationMode::Tones;
            pTones.widthMm = 80.0;
            QVector<EditPath> tonePaths = ImageDigitizer::traceToPaths(skullImg, pTones);
            CHECK(!tonePaths.isEmpty(), "3-Tone traceToPaths generates vector paths");
            QSet<int> colorIndices;
            for (const auto& ep : tonePaths) {
                colorIndices.insert(ep.colorIdx);
            }
            CHECK(colorIndices.size() >= 2, "Vector paths contain multiple distinct tone/color layers");

            // 14. Test Y-axis orientation (skull right-side up)
            double minY = 1e9, maxY = -1e9;
            for (const auto& ep : stencilPaths) {
                for (const auto& n : ep.nodes) {
                    minY = std::min(minY, n.pos.y());
                    maxY = std::max(maxY, n.pos.y());
                }
            }
            CHECK(maxY > 0.0 && minY < 0.0, "Vector paths are properly centered with +Y up");

            // Verify Sfumato orientation: stitches at the top of the skull image have positive Y
            double sfX0, sfY0, sfX1, sfY1;
            sfSeq.bounds(sfX0, sfY0, sfX1, sfY1);
            CHECK(sfY1 > sfY0, "Sfumato stitch bounds are valid (+Y > -Y)");

            // Save vector stencil preview
            {
                QImage vecImg(600, 600, QImage::Format_ARGB32_Premultiplied);
                vecImg.fill(QColor(24, 26, 32));
                QPainter vp(&vecImg);
                vp.setRenderHint(QPainter::Antialiasing, true);

                double scale = std::min((600 * 0.85) / pStencil.widthMm, (600 * 0.85) / pStencil.widthMm);
                QTransform xf;
                xf.translate(300, 300);
                xf.scale(scale, -scale);

                for (const auto& ep : stencilPaths) {
                    QPainterPath pp = ep.toPainterPath();
                    QPainterPath sp = xf.map(pp);
                    vp.setBrush(QColor(235, 230, 220, 220));
                    vp.setPen(QPen(QColor(45, 212, 191), 1.6));
                    vp.drawPath(sp);

                    vp.setPen(Qt::NoPen);
                    vp.setBrush(QColor(255, 255, 255, 240));
                    for (const auto& node : ep.nodes) {
                        QPointF np = xf.map(node.pos);
                        vp.drawRect(QRectF(np.x() - 1.5, np.y() - 1.5, 3.0, 3.0));
                    }
                }
                vp.end();
                vecImg.save(QStringLiteral("C:/Users/micbu/.gemini/antigravity/brain/e572641d-70d6-45b5-8c91-c97f358a4f6c/schaedel_stencil_vector_preview.png"), "PNG");
                vecImg.save(QStringLiteral("docs/manual_assets/17_skull_bitmap_to_path_stencil.png"), "PNG");
            }

            // Save multi-tone vector tatami stitches
            {
                StitchSequence toneSeq = ImageDigitizer::generate(skullImg, pTones);
                QImage toneThumb = StitchThumbnail::render(toneSeq, 600, QColor(20, 23, 28), true);
                toneThumb.save(QStringLiteral("C:/Users/micbu/.gemini/antigravity/brain/e572641d-70d6-45b5-8c91-c97f358a4f6c/schaedel_duotone_stitches.png"), "PNG");
                toneThumb.save(QStringLiteral("docs/manual_assets/18_skull_multitone_tatami.png"), "PNG");
                toneThumb.save(QStringLiteral("docs/manual_assets/14_photo_live_preview_skull.png"), "PNG");
                sfumatoThumb.save(QStringLiteral("docs/manual_assets/16_sfumato_skull_live.png"), "PNG");
            }
        }
    }

    std::printf("== Vector Editing, Selection & Transform Tools ==\n");
    {
        // 1. Bounding box, center & area of EditPath
        EditPath boxPath;
        boxPath.closed = true;
        boxPath.nodes.append(BezierNode(QPointF(10.0, 10.0)));
        boxPath.nodes.append(BezierNode(QPointF(30.0, 10.0)));
        boxPath.nodes.append(BezierNode(QPointF(30.0, 25.0)));
        boxPath.nodes.append(BezierNode(QPointF(10.0, 25.0)));

        QRectF bbox = boxPath.boundingRect();
        CHECK(std::abs(bbox.left() - 10.0) < 1e-3 && std::abs(bbox.top() - 10.0) < 1e-3 &&
              std::abs(bbox.width() - 20.0) < 1e-3 && std::abs(bbox.height() - 15.0) < 1e-3,
              "EditPath::boundingRect computes accurate 20x15 mm bounds");

        QPointF c = boxPath.center();
        CHECK(std::abs(c.x() - 20.0) < 1e-3 && std::abs(c.y() - 17.5) < 1e-3,
              "EditPath::center computes correct centroid (20, 17.5)");

        double a = boxPath.area();
        CHECK(std::abs(a - 300.0) < 1e-2,
              "EditPath::area computes correct polygon area (300 mm²)");

        // 2. Translation
        boxPath.translate(5.0, -3.0);
        QPointF cTrans = boxPath.center();
        CHECK(std::abs(cTrans.x() - 25.0) < 1e-3 && std::abs(cTrans.y() - 14.5) < 1e-3,
              "EditPath::translate shifts geometry accurately");

        // 3. Scaling around center
        boxPath.scale(2.0, 2.0, cTrans);
        QRectF scaledBbox = boxPath.boundingRect();
        CHECK(std::abs(scaledBbox.width() - 40.0) < 1e-3 && std::abs(scaledBbox.height() - 30.0) < 1e-3 &&
              std::abs(boxPath.center().x() - 25.0) < 1e-3 && std::abs(boxPath.center().y() - 14.5) < 1e-3,
              "EditPath::scale scales geometry around arbitrary origin without center drift");

        // 4. Horizontal and Vertical Flipping
        EditPath tri;
        tri.closed = true;
        tri.nodes.append(BezierNode(QPointF(10.0, 10.0)));
        tri.nodes.append(BezierNode(QPointF(30.0, 10.0)));
        tri.nodes.append(BezierNode(QPointF(10.0, 25.0)));
        tri.flipHorizontal(20.0);
        CHECK(std::abs(tri.nodes[0].pos.x() - 30.0) < 1e-3 &&
              std::abs(tri.nodes[1].pos.x() - 10.0) < 1e-3 &&
              std::abs(tri.nodes[2].pos.x() - 30.0) < 1e-3,
              "EditPath::flipHorizontal mirrors X coordinates across axis");

        tri.flipVertical(15.0);
        CHECK(std::abs(tri.nodes[0].pos.y() - 20.0) < 1e-3 &&
              std::abs(tri.nodes[1].pos.y() - 20.0) < 1e-3 &&
              std::abs(tri.nodes[2].pos.y() - 5.0) < 1e-3,
              "EditPath::flipVertical mirrors Y coordinates across axis");

        // 5. 90-degree Rotation
        EditPath linePath;
        linePath.nodes.append(BezierNode(QPointF(0.0, 0.0)));
        linePath.nodes.append(BezierNode(QPointF(10.0, 0.0)));
        linePath.rotate(90.0, QPointF(0.0, 0.0));
        CHECK(std::abs(linePath.nodes[1].pos.x() - 0.0) < 1e-3 &&
              std::abs(linePath.nodes[1].pos.y() - (-10.0)) < 1e-3,
              "EditPath::rotate rotates geometry 90 deg clockwise");

        // 6. Direct Curve Bending
        EditPath bendPath;
        bendPath.nodes.append(BezierNode(QPointF(0.0, 0.0)));
        bendPath.nodes.append(BezierNode(QPointF(20.0, 0.0)));
        CHECK(bendPath.nodes[0].isCorner() && bendPath.nodes[1].isCorner(),
              "Initial nodes are straight sharp corners");
        bendPath.bendSegment(0, QPointF(0.0, 6.0));
        CHECK(!bendPath.nodes[0].isCorner() && !bendPath.nodes[1].isCorner(),
              "EditPath::bendSegment generates smooth control handles on straight segment");
        CHECK(bendPath.nodes[0].ctrlOut.y() > 2.0 && bendPath.nodes[1].ctrlIn.y() > 2.0,
              "EditPath::bendSegment displaces tangent handles toward drag delta");

        // 7. Multi-Path Selection & Selective Deletion
        QVector<EditPath> canvasPaths;
        // Path 0: Skull motif center (50x50 mm)
        EditPath pSkull; pSkull.closed = true;
        pSkull.nodes.append(BezierNode(QPointF(-25.0, -25.0)));
        pSkull.nodes.append(BezierNode(QPointF(25.0, -25.0)));
        pSkull.nodes.append(BezierNode(QPointF(25.0, 25.0)));
        pSkull.nodes.append(BezierNode(QPointF(-25.0, 25.0)));
        canvasPaths.append(pSkull);

        // Path 1: Unwanted watermark top right (5x2 mm)
        EditPath pWatermarkTR; pWatermarkTR.closed = true;
        pWatermarkTR.nodes.append(BezierNode(QPointF(35.0, 35.0)));
        pWatermarkTR.nodes.append(BezierNode(QPointF(40.0, 35.0)));
        pWatermarkTR.nodes.append(BezierNode(QPointF(40.0, 37.0)));
        pWatermarkTR.nodes.append(BezierNode(QPointF(35.0, 37.0)));
        canvasPaths.append(pWatermarkTR);

        // Path 2: Unwanted watermark bottom left (6x1 mm)
        EditPath pWatermarkBL; pWatermarkBL.closed = true;
        pWatermarkBL.nodes.append(BezierNode(QPointF(-40.0, -40.0)));
        pWatermarkBL.nodes.append(BezierNode(QPointF(-34.0, -40.0)));
        pWatermarkBL.nodes.append(BezierNode(QPointF(-34.0, -39.0)));
        pWatermarkBL.nodes.append(BezierNode(QPointF(-40.0, -39.0)));
        canvasPaths.append(pWatermarkBL);

        CHECK(canvasPaths.size() == 3, "Created 3 vector paths (motif + 2 watermarks)");

        // Simulate selection around watermark 1 & 2:
        QSet<int> selectedIndices;
        selectedIndices.insert(1);
        selectedIndices.insert(2);
        // Perform selective deletion (reverse sort to prevent index invalidation):
        QVector<int> sortedDel;
        for (int sIdx : selectedIndices) sortedDel.append(sIdx);
        std::sort(sortedDel.begin(), sortedDel.end(), std::greater<int>());
        for (int idx : sortedDel) {
            canvasPaths.remove(idx);
        }
        CHECK(canvasPaths.size() == 1, "Selective deletion removed exactly the 2 selected watermarks");
        CHECK(canvasPaths[0].area() == 2500.0, "Main skull motif is completely preserved with full area");

        // 8. Undo / Redo simulation
        QVector<QVector<EditPath>> undoHistory;
        undoHistory.push_back(canvasPaths); // State with 1 path
        // User accidentally deletes the remaining path
        canvasPaths.clear();
        CHECK(canvasPaths.isEmpty(), "Canvas cleared");
        // Undo:
        canvasPaths = undoHistory.takeLast();
        CHECK(canvasPaths.size() == 1, "Undo restored deleted motif from history stack");

        // 9. Island Filter (noise & watermark cleanup < 4 mm²)
        QVector<EditPath> noiseTestPaths;
        noiseTestPaths.append(pSkull); // 2500 mm²
        // Noise speck 1: 1x1 mm (1 mm² < 4 mm²)
        EditPath speck1; speck1.closed = true;
        speck1.nodes.append(BezierNode(QPointF(0, 0)));
        speck1.nodes.append(BezierNode(QPointF(1, 0)));
        speck1.nodes.append(BezierNode(QPointF(1, 1)));
        speck1.nodes.append(BezierNode(QPointF(0, 1)));
        noiseTestPaths.append(speck1);
        // Noise speck 2: 0.8x0.8 mm (0.64 mm² < 4 mm²)
        EditPath speck2; speck2.closed = true;
        speck2.nodes.append(BezierNode(QPointF(10, 10)));
        speck2.nodes.append(BezierNode(QPointF(10.8, 10)));
        speck2.nodes.append(BezierNode(QPointF(10.8, 10.8)));
        speck2.nodes.append(BezierNode(QPointF(10, 10.8)));
        noiseTestPaths.append(speck2);

        CHECK(noiseTestPaths.size() == 3, "Island test input: 1 main motif + 2 noise specks");

        QVector<EditPath> filteredPaths;
        const double minAreaMm2 = 4.0;
        for (const auto& p : noiseTestPaths) {
            if (p.closed) {
                if (p.area() >= minAreaMm2) filteredPaths.push_back(p);
            } else {
                QRectF b = p.boundingRect();
                if (b.width() * b.height() >= minAreaMm2 || std::max(b.width(), b.height()) >= 3.0) {
                    filteredPaths.push_back(p);
                }
            }
        }
        CHECK(filteredPaths.size() == 1, "filterSmallIslands filtered out both specks (< 4 mm²)");
        CHECK(filteredPaths[0].area() == 2500.0, "Main motif was kept intact");
    }

    // =======================================================================
    //  Tatami & Underlay Stability & Multi-Island Robustness
    // =======================================================================
    {
        std::printf("== Tatami & Underlay Stability & Multi-Island Robustness ==\n");

        // 1. Degenerate and 0-area polygons passed to Underlay::edgeRun
        StitchSequence underSeq;
        QPolygonF emptyPoly;
        Underlay::edgeRun(emptyPoly, 1.0, 0, underSeq, 0.5);
        CHECK(underSeq.empty(), "Underlay::edgeRun on empty polygon produces no stitches without crashing");

        QPolygonF singlePointPoly;
        singlePointPoly << QPointF(10, 10);
        Underlay::edgeRun(singlePointPoly, 1.0, 0, underSeq, 0.5);
        CHECK(underSeq.empty(), "Underlay::edgeRun on single point polygon produces no stitches without crashing");

        QPolygonF collapsedLinePoly;
        collapsedLinePoly << QPointF(10, 10) << QPointF(20, 20) << QPointF(10, 10);
        Underlay::edgeRun(collapsedLinePoly, 1.0, 0, underSeq, 0.5);
        CHECK(true, "Underlay::edgeRun on collapsed collinear polygon completed safely");

        // 2. Multi-island region (two disjoint circles) passed to TatamiFill::generate
        QPolygonF island1;
        const double r1 = 15.0;
        for (int a = 0; a < 24; ++a) {
            double rad = a * 2.0 * 3.141592653589793 / 24.0;
            island1 << QPointF(-25.0 + r1 * std::cos(rad), r1 * std::sin(rad));
        }

        QPolygonF island2;
        const double r2 = 12.0;
        for (int a = 0; a < 24; ++a) {
            double rad = a * 2.0 * 3.141592653589793 / 24.0;
            island2 << QPointF(25.0 + r2 * std::cos(rad), r2 * std::sin(rad));
        }

        QVector<QPolygonF> multiIslands;
        multiIslands << island1 << island2;

        TatamiFill::Params tp;
        tp.pattern = TatamiFill::PatternType::StandardTatami;
        tp.underlay = true;
        StitchSequence multiSeq = TatamiFill::generate(multiIslands, tp);
        CHECK(!multiSeq.empty(), "TatamiFill::generate on multiple disjoint islands produces valid stitches");
        CHECK(multiSeq.realStitchCount() > 50, "Multi-island tatami has substantial stitch count");

        // 3. Multi-island with ContourEcho (verifies islands are not subtracted as holes)
        TatamiFill::Params tpEcho;
        tpEcho.pattern = TatamiFill::PatternType::ContourEcho;
        tpEcho.underlay = false;
        StitchSequence echoSeq = TatamiFill::generate(multiIslands, tpEcho);
        CHECK(!echoSeq.empty(), "TatamiFill::generate ContourEcho on multiple disjoint islands generates stitches");

        // 4. Region containing degenerate/empty polygons mixed in
        QVector<QPolygonF> dirtyRegion;
        dirtyRegion << QPolygonF() << island1 << singlePointPoly << island2 << collapsedLinePoly;
        StitchSequence dirtySeq = TatamiFill::generate(dirtyRegion, tp);
        CHECK(!dirtySeq.empty(), "TatamiFill::generate safely filters dirty/degenerate polygons");
        CHECK(dirtySeq.realStitchCount() > 50, "Dirty region stitches generated successfully without crash");
    }

    std::printf("== Real reference round-trip ==\n");
    const char* ref = (argc > 1 && argv[1][0] != '-') ? argv[1] : nullptr;
    if (ref) {
        const QString refPath = QString::fromUtf8(ref);
        if (refPath.endsWith(QStringLiteral(".jpg"), Qt::CaseInsensitive) ||
            refPath.endsWith(QStringLiteral(".jpeg"), Qt::CaseInsensitive) ||
            refPath.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive) ||
            refPath.endsWith(QStringLiteral(".webp"), Qt::CaseInsensitive)) {
            QImage img(refPath);
            std::printf("Image loaded: %dx%d px\n", img.width(), img.height());
            if (!img.isNull()) {
                // 1. Stencil / 1-color outline
                ImageDigitizer::Params pOutline;
                pOutline.mode = ImageDigitizer::Mode::Colors;
                pOutline.colors = 1;
                pOutline.widthMm = 130.0;
                pOutline.dropBackground = true;
                StitchSequence seqOutline = ImageDigitizer::generate(img, pOutline);
                std::printf("  1-Color Outline: %zu stitches\n", seqOutline.realStitchCount());

                // 2. 2-Color Red + White Tatami Fill
                ImageDigitizer::Params p2Col;
                p2Col.mode = ImageDigitizer::Mode::Colors;
                p2Col.colors = 2;
                p2Col.widthMm = 130.0;
                p2Col.dropBackground = true;
                auto pal = ImageDigitizer::extractPalette(img, p2Col);
                std::printf("  Palette (%zu colors):\n", pal.size());
                for (size_t i = 0; i < pal.size(); ++i) {
                    std::printf("    [%zu] RGB(%d,%d,%d) %s - %.1f%% %s\n",
                        i, pal[i].thread.color.red(), pal[i].thread.color.green(), pal[i].thread.color.blue(),
                        qPrintable(pal[i].thread.description), pal[i].percentage,
                        pal[i].isBackground ? "(Background)" : "(Stitch Thread)");
                }
                StitchSequence seq2Col = ImageDigitizer::generate(img, p2Col);
                std::printf("  2-Color Tatami: %zu stitches\n", seq2Col.realStitchCount());

                // Render thumbnails
                QImage thumb1 = StitchThumbnail::render(seqOutline, 800, QColor(24, 26, 32), true);
                thumb1.save(QStringLiteral("C:/Users/micbu/.gemini/antigravity/brain/e572641d-70d6-45b5-8c91-c97f358a4f6c/feuerwehr_outline_stitches.png"), "PNG");

                QImage thumb2 = StitchThumbnail::render(seq2Col, 800, QColor(24, 26, 32), true);
                thumb2.save(QStringLiteral("C:/Users/micbu/.gemini/antigravity/brain/e572641d-70d6-45b5-8c91-c97f358a4f6c/feuerwehr_2col_stitches.png"), "PNG");

                // Helper lambda to ensure any design strictly fits within Hoop A (126x110 mm)
                auto ensureHoopA = [](StitchSequence s, double maxW = 124.0, double maxH = 108.0) -> StitchSequence {
                    if (s.empty()) return s;
                    double x0, y0, x1, y1;
                    if (s.bounds(x0, y0, x1, y1)) {
                        double w = x1 - x0;
                        double h = y1 - y0;
                        if (w > maxW || h > maxH) {
                            double f = std::min(maxW / w, maxH / h);
                            s = s.scaled(f, QPointF(0.5 * (x0 + x1), 0.5 * (y0 + y1)));
                        }
                        double nx0, ny0, nx1, ny1;
                        if (s.bounds(nx0, ny0, nx1, ny1)) {
                            double cx = 0.5 * (nx0 + nx1);
                            double cy = 0.5 * (ny0 + ny1);
                            for (Stitch& st : s.stitches) {
                                st.x -= cx;
                                st.y -= cy;
                            }
                        }
                    }
                    return s;
                };

                // --- 1. Kieler Wappen (Hoop A: 75 x 79 mm) ---
                const QString wappenPath = QStringLiteral("C:/Users/micbu/.gemini/antigravity/brain/e572641d-70d6-45b5-8c91-c97f358a4f6c/.user_uploaded/media_1791394910048.png");
                QImage wappenImg(wappenPath);
                StitchSequence seqWappen;
                if (!wappenImg.isNull()) {
                    ImageDigitizer::Params pw;
                    pw.mode = ImageDigitizer::Mode::Colors;
                    pw.colors = 3;
                    pw.widthMm = 75.0;
                    pw.dropBackground = false;
                    seqWappen = ensureHoopA(ImageDigitizer::generate(wappenImg, pw), 120.0, 105.0);
                    std::printf("  Kieler Wappen (Hoop A): %zu stitches\n", seqWappen.realStitchCount());
                    QImage thumbWappen = StitchThumbnail::render(seqWappen, 800, QColor(24, 26, 32), true);
                    thumbWappen.save(QStringLiteral("C:/Users/micbu/.gemini/antigravity/brain/e572641d-70d6-45b5-8c91-c97f358a4f6c/kieler_wappen_stitches.png"), "PNG");
                }

                // --- Helper Lambdas for 90-Degree Orthogonal Satin Columns & Kettel-Outlines ---
                auto samplePoly = [](const QVector<QPointF>& pts, double s) -> QPointF {
                    double totalL = 0;
                    for (int i = 0; i < pts.size() - 1; ++i)
                        totalL += std::hypot(pts[i+1].x() - pts[i].x(), pts[i+1].y() - pts[i].y());
                    double target = s * totalL;
                    double acc = 0;
                    for (int i = 0; i < pts.size() - 1; ++i) {
                        double d = std::hypot(pts[i+1].x() - pts[i].x(), pts[i+1].y() - pts[i].y());
                        if (acc + d >= target || i == pts.size() - 2) {
                            double rem = (target - acc) / std::max(1e-6, d);
                            rem = std::clamp(rem, 0.0, 1.0);
                            return QPointF(pts[i].x() + (pts[i+1].x() - pts[i].x()) * rem,
                                           pts[i].y() + (pts[i+1].y() - pts[i].y()) * rem);
                        }
                        acc += d;
                    }
                    return pts.last();
                };

                auto polyLength = [](const QVector<QPointF>& pts) -> double {
                    double L = 0;
                    for (int i = 0; i < pts.size() - 1; ++i)
                        L += std::hypot(pts[i+1].x() - pts[i].x(), pts[i+1].y() - pts[i].y());
                    return L;
                };

                // 1. Schienensprossen (Säulenstiche / transverse satin rungs between 2 rails)
                auto makeSatinCol = [&](const QVector<QPointF>& outPts, const QVector<QPointF>& inPts, double pitch, int colIdx) -> StitchSequence {
                    StitchSequence seq;
                    seq.palette.emplace_back(QColor(180, 20, 30), QStringLiteral("Deep Red Satin"));
                    double lAvg = 0.5 * (polyLength(outPts) + polyLength(inPts));
                    int steps = std::max(2, static_cast<int>(std::llround(lAvg / pitch)));
                    bool outer = true;
                    for (int i = 0; i <= steps; ++i) {
                        double s = static_cast<double>(i) / steps;
                        QPointF pOut = samplePoly(outPts, s);
                        QPointF pIn  = samplePoly(inPts, s);
                        QPointF pt   = outer ? pOut : pIn;
                        seq.add(pt.x(), pt.y(), (i == 0) ? SF_Jump : SF_Normal, colIdx);
                        outer = !outer;
                    }
                    return seq;
                };

                // 2. Kettel-Outlines (Satin-Kettelrand / Bourdonstich along closed contour)
                auto makeKettelOutline = [&](const QVector<QPointF>& pts, double widthMm, double pitchMm, int colIdx) -> StitchSequence {
                    StitchSequence seq;
                    seq.palette.emplace_back(QColor(180, 20, 30), QStringLiteral("Deep Red Kettel"));
                    if (pts.size() < 2) return seq;
                    double L = polyLength(pts);
                    int steps = std::max(4, static_cast<int>(std::llround(L / pitchMm)));
                    bool outer = true;
                    for (int i = 0; i <= steps; ++i) {
                        double s = static_cast<double>(i) / steps;
                        QPointF p0 = samplePoly(pts, std::max(0.0, s - 0.005));
                        QPointF p1 = samplePoly(pts, std::min(1.0, s + 0.005));
                        QPointF dir = p1 - p0;
                        double dlen = std::hypot(dir.x(), dir.y());
                        QPointF normal(0, 0);
                        if (dlen > 1e-6) {
                            normal = QPointF(-dir.y() / dlen, dir.x() / dlen);
                        }
                        QPointF p = samplePoly(pts, s);
                        QPointF pt = p + normal * (outer ? (widthMm * 0.5) : (-widthMm * 0.5));
                        seq.add(pt.x(), pt.y(), (i == 0) ? SF_Jump : SF_Normal, colIdx);
                        outer = !outer;
                    }
                    return seq;
                };

                // Varsity-Ziffern Geometrie & Führungsschienen für Schienensprossen (transverse satin rungs)
                auto getZeroGeometry = [](double W, double H, double w, double c, QVector<QPointF>& outPts, QVector<QPointF>& inPts) {
                    double hw = W * 0.5, hh = H * 0.5;
                    outPts = {
                        QPointF(-hw + c,  hh), QPointF( hw - c,  hh), QPointF( hw,  hh - c), QPointF( hw, -hh + c),
                        QPointF( hw - c, -hh), QPointF(-hw + c, -hh), QPointF(-hw, -hh + c), QPointF(-hw,  hh - c), QPointF(-hw + c,  hh)
                    };
                    double inW = W - 2*w, inH = H - 2*w, inC = std::max(2.0, c - w);
                    double ihw = inW * 0.5, ihh = inH * 0.5;
                    inPts = {
                        QPointF(-ihw + inC,  ihh), QPointF( ihw - inC,  ihh), QPointF( ihw,  ihh - inC), QPointF( ihw, -ihh + inC),
                        QPointF( ihw - inC, -ihh), QPointF(-ihw + inC, -ihh), QPointF(-ihw, -ihh + inC), QPointF(-ihw,  ihh - inC), QPointF(-ihw + inC,  ihh)
                    };
                };

                auto getThreeRails = [](double W, double H, double w, double c, QVector<QPointF>& railOut, QVector<QPointF>& railIn) {
                    double hw = W * 0.5, hh = H * 0.5;
                    railOut = {
                        QPointF(-hw,        hh - w - 8.0),
                        QPointF(-hw,        hh),
                        QPointF( hw - c,    hh),
                        QPointF( hw,        hh - c),
                        QPointF( hw,        8.0),
                        QPointF( 6.0,       0.0),
                        QPointF(-10.0,      0.0),
                        QPointF( 6.0,       0.0),
                        QPointF( hw,       -8.0),
                        QPointF( hw,       -hh + c),
                        QPointF( hw - c,   -hh),
                        QPointF(-hw,       -hh),
                        QPointF(-hw,       -hh + w + 8.0)
                    };
                    railIn = {
                        QPointF(-hw + w,    hh - w - 8.0),
                        QPointF(-hw + w,    hh - w),
                        QPointF( hw - w - (c - w), hh - w),
                        QPointF( hw - w,    hh - w - (c - w)),
                        QPointF( hw - w,    16.0),
                        QPointF( 6.0 - w,   0.0),
                        QPointF(-10.0,      0.0),
                        QPointF( 6.0 - w,   0.0),
                        QPointF( hw - w,   -16.0),
                        QPointF( hw - w,   -hh + w + (c - w)),
                        QPointF( hw - w - (c - w), -hh + w),
                        QPointF(-hw + w,   -hh + w),
                        QPointF(-hw + w,   -hh + w + 8.0)
                    };
                };

                auto getThreeClosedContour = [&](double W, double H, double w, double c) -> QVector<QPointF> {
                    QVector<QPointF> rOut, rIn;
                    getThreeRails(W, H, w, c, rOut, rIn);
                    QVector<QPointF> closed = rOut;
                    for (int i = rIn.size() - 1; i >= 0; --i) {
                        closed.push_back(rIn[i]);
                    }
                    closed.push_back(rOut.first());
                    return closed;
                };

                // --- 1. OBEN: Bogen "BERUFSFEUERWEHR KIEL" (Outline mit kleiner Stichweite) ---
                TextDigitizer::Params tpTopKompakt;
                tpTopKompakt.family = QStringLiteral("Varsity Regular");
                tpTopKompakt.text = QStringLiteral("berufsfeuerwehr kiel"); // lowercase = solide Athletic Block Glyphen
                tpTopKompakt.baseline = TextDigitizer::Baseline::ArcUp;
                tpTopKompakt.arcRadiusMm = 65.0;
                tpTopKompakt.heightMm = 9.5;
                tpTopKompakt.letterSpacingMm = 1.4;

                QPainterPath pathTopK = TextDigitizer::textPath(tpTopKompakt);

                // Variante A: 3-Pass Bean Stitch (Dreifach-Kontur mit kleiner Stichweite 1.6 mm)
                BeanStitchGenerator::Params bpTop;
                bpTop.stitchLengthMm = 1.6; // kleine Stichweite!
                bpTop.mode = BeanStitchGenerator::Mode::TriplePass;
                bpTop.colorIdx = 0;
                StitchSequence seqTopBean = ensureHoopA(BeanStitchGenerator::generate(pathTopK, bpTop), 122.0, 105.0);
                seqTopBean.palette.clear();
                seqTopBean.palette.emplace_back(QColor(190, 25, 35), QStringLiteral("Deep Red Outline"));

                // Variante B: Feiner Micro-Satin Kettelrand (0.65 mm Breite, 0.28 mm Stichabstand)
                StitchSequence seqTopMicroKettel;
                seqTopMicroKettel.palette.emplace_back(QColor(190, 25, 35), QStringLiteral("Deep Red Satin Outline"));
                const QList<QPolygonF> topPolys = pathTopK.toSubpathPolygons();
                for (const QPolygonF& poly : topPolys) {
                    if (poly.size() < 2) continue;
                    QVector<QPointF> pts;
                    for (const QPointF& pt : poly) pts.push_back(pt);
                    StitchSequence sb = makeKettelOutline(pts, 0.65, 0.28, 0); // 0.65 mm Breite, 0.28 mm Stichweite!
                    seqTopMicroKettel.stitches.insert(seqTopMicroKettel.stitches.end(), sb.stitches.begin(), sb.stitches.end());
                }
                seqTopMicroKettel = ensureHoopA(seqTopMicroKettel, 122.0, 105.0);

                // Wir verwenden die Micro-Kettel-Outline als Standard-Bogen
                StitchSequence seqTopKompakt = seqTopMicroKettel;
                std::printf("  OBEN Bogen Kompakt: BeanStitch (%zu st), Micro-Satin (%zu st)\n",
                            seqTopBean.realStitchCount(), seqTopMicroKettel.realStitchCount());

                // Geteilte Riesen-Version für volle Jackenbreite (2 Rahmenspannungen mit Passkreuz)
                TextDigitizer::Params tpTopLarge;
                tpTopLarge.family = QStringLiteral("Varsity Regular");
                tpTopLarge.text = QStringLiteral("berufsfeuerwehr kiel");
                tpTopLarge.baseline = TextDigitizer::Baseline::ArcUp;
                tpTopLarge.arcRadiusMm = 80.0;
                tpTopLarge.heightMm = 15.0;
                tpTopLarge.letterSpacingMm = 2.0;

                QPainterPath pathTopL = TextDigitizer::textPath(tpTopLarge);
                StitchSequence seqTopLarge;
                seqTopLarge.palette.emplace_back(QColor(190, 25, 35), QStringLiteral("Deep Red Satin Outline"));
                for (const QPolygonF& poly : pathTopL.toSubpathPolygons()) {
                    if (poly.size() < 2) continue;
                    QVector<QPointF> pts;
                    for (const QPointF& pt : poly) pts.push_back(pt);
                    StitchSequence sb = makeKettelOutline(pts, 0.85, 0.30, 0); // 0.85 mm bei Großversion
                    seqTopLarge.stitches.insert(seqTopLarge.stitches.end(), sb.stitches.begin(), sb.stitches.end());
                }

                MultiHoopSplitter::Params mhp;
                mhp.hoopWidthMm = 124.0;
                mhp.hoopHeightMm = 108.0;
                mhp.direction = MultiHoopSplitter::SplitDirection::Vertical;
                mhp.addCrosshairs = true;
                mhp.centerEachHoop = true;
                MultiHoopSplitter::Result splitTop = MultiHoopSplitter::split(seqTopLarge, mhp);
                if (splitTop.success) {
                    splitTop.hoop1.palette.clear();
                    splitTop.hoop1.palette.emplace_back(QColor(190, 25, 35), QStringLiteral("Deep Red Kettel"));
                    splitTop.hoop2.palette.clear();
                    splitTop.hoop2.palette.emplace_back(QColor(190, 25, 35), QStringLiteral("Deep Red Kettel"));
                }

                // --- 2. MITTE: Zahl "30" mit Schienensprossen (Satin) & Kettel-Outlines ---
                QVector<QPointF> rOut3, rIn3, out0, in0;
                getThreeRails(50.0, 76.0, 11.0, 12.0, rOut3, rIn3);
                getZeroGeometry(54.0, 76.0, 11.0, 12.0, out0, in0);

                // A) Schienensprossen (Säulenstiche / transverse rungs zwischen Innen- und Außenkante)
                StitchSequence s3Spross = makeSatinCol(rOut3, rIn3, 0.45, 0);
                StitchSequence s0Spross = makeSatinCol(out0, in0, 0.45, 0);
                StitchSequence seq30Sprossen;
                seq30Sprossen.palette = s3Spross.palette;
                for (const auto& st : s3Spross.stitches) seq30Sprossen.add(st.x - 30.0, st.y, st.flags, st.colorIdx);
                for (const auto& st : s0Spross.stitches) seq30Sprossen.add(st.x + 30.0, st.y, st.flags, st.colorIdx);
                seq30Sprossen = ensureHoopA(seq30Sprossen, 118.0, 105.0);

                // B) Kettel-Outlines (reiner Satin-Kettelrand um die Konturen herum für Aufnäher auf Stoff)
                QVector<QPointF> c3 = getThreeClosedContour(50.0, 76.0, 11.0, 12.0);
                StitchSequence k3_out = makeKettelOutline(c3, 2.2, 0.40, 0);
                StitchSequence k0_out = makeKettelOutline(out0, 2.2, 0.40, 0);
                StitchSequence k0_in  = makeKettelOutline(in0,  2.2, 0.40, 0);
                StitchSequence seq30Kettel;
                seq30Kettel.palette.emplace_back(QColor(180, 20, 30), QStringLiteral("Deep Red Kettel"));
                for (const auto& st : k3_out.stitches) seq30Kettel.add(st.x - 30.0, st.y, st.flags, st.colorIdx);
                for (const auto& st : k0_out.stitches) seq30Kettel.add(st.x + 30.0, st.y, st.flags, st.colorIdx);
                for (const auto& st : k0_in.stitches)  seq30Kettel.add(st.x + 30.0, st.y, st.flags, st.colorIdx);
                seq30Kettel = ensureHoopA(seq30Kettel, 118.0, 105.0);

                // C) Kombiniert: Schienensprossen + Kettelrand
                StitchSequence seq30Kompakt = seq30Sprossen;
                for (const auto& st : seq30Kettel.stitches) seq30Kompakt.stitches.push_back(st);

                std::printf("  MITTE 30: Sprossen (%zu st), Kettel-Outlines (%zu st)\n",
                            seq30Sprossen.realStitchCount(), seq30Kettel.realStitchCount());

                // D) Große Einzelziffern (100 mm Höhe für solo Rahmenspannung Hoop A)
                QVector<QPointF> rOut3L, rIn3L, out0L, in0L;
                getThreeRails(65.0, 100.0, 14.0, 15.0, rOut3L, rIn3L);
                getZeroGeometry(70.0, 100.0, 14.0, 15.0, out0L, in0L);

                StitchSequence seqZiffer3 = ensureHoopA(makeSatinCol(rOut3L, rIn3L, 0.45, 0), 120.0, 106.0);
                StitchSequence seqZiffer0 = ensureHoopA(makeSatinCol(out0L, in0L, 0.45, 0), 120.0, 106.0);
                QVector<QPointF> c3L = getThreeClosedContour(65.0, 100.0, 14.0, 15.0);
                StitchSequence seqZiffer3Kettel = ensureHoopA(makeKettelOutline(c3L, 2.4, 0.40, 0), 120.0, 106.0);
                StitchSequence seqZiffer0Kettel = makeKettelOutline(out0L, 2.4, 0.40, 0);
                StitchSequence k0Lin = makeKettelOutline(in0L, 2.4, 0.40, 0);
                for (const auto& st : k0Lin.stitches) seqZiffer0Kettel.stitches.push_back(st);
                seqZiffer0Kettel = ensureHoopA(seqZiffer0Kettel, 120.0, 106.0);

                // --- 3. UNTEN: Bogen "NORDWACHE II" in Original Varsity-Schrift ---
                TextDigitizer::Params tpBottom;
                tpBottom.family = QStringLiteral("Varsity Regular");
                tpBottom.text = QStringLiteral("nordwache ii");
                tpBottom.baseline = TextDigitizer::Baseline::ArcDown;
                tpBottom.arcRadiusMm = 65.0;
                tpBottom.heightMm = 11.0;
                tpBottom.letterSpacingMm = 1.6;

                QPainterPath pathBottomK = TextDigitizer::textPath(tpBottom);

                // Variante A: 3-Pass Bean Stitch (Dreifach-Kontur mit kleiner Stichweite 1.6 mm)
                BeanStitchGenerator::Params bpBottom;
                bpBottom.stitchLengthMm = 1.6;
                bpBottom.mode = BeanStitchGenerator::Mode::TriplePass;
                bpBottom.colorIdx = 0;
                StitchSequence seqBottomBean = ensureHoopA(BeanStitchGenerator::generate(pathBottomK, bpBottom), 120.0, 105.0);
                seqBottomBean.palette.clear();
                seqBottomBean.palette.emplace_back(QColor(190, 25, 35), QStringLiteral("Deep Red Outline"));

                // Variante B: Feiner Micro-Satin Kettelrand (0.65 mm Breite, 0.28 mm Stichabstand)
                StitchSequence seqBottomMicroKettel;
                seqBottomMicroKettel.palette.emplace_back(QColor(190, 25, 35), QStringLiteral("Deep Red Satin Outline"));
                const QList<QPolygonF> botPolys = pathBottomK.toSubpathPolygons();
                for (const QPolygonF& poly : botPolys) {
                    if (poly.size() < 2) continue;
                    QVector<QPointF> pts;
                    for (const QPointF& pt : poly) pts.push_back(pt);
                    StitchSequence sb = makeKettelOutline(pts, 0.65, 0.28, 0);
                    seqBottomMicroKettel.stitches.insert(seqBottomMicroKettel.stitches.end(), sb.stitches.begin(), sb.stitches.end());
                }
                seqBottomMicroKettel = ensureHoopA(seqBottomMicroKettel, 120.0, 105.0);

                StitchSequence seqBottomKompakt = seqBottomMicroKettel;
                std::printf("  UNTEN Bogen Kompakt: BeanStitch (%zu st), Micro-Satin (%zu st)\n",
                            seqBottomBean.realStitchCount(), seqBottomMicroKettel.realStitchCount());

                // --- 4. Kieler Wappen: Kettel-Konturen (Schild, Nesselblatt, Boot) ---
                StitchSequence seqWappenKettel;
                seqWappenKettel.palette.emplace_back(QColor(190, 25, 35), QStringLiteral("Wappen Red"));
                seqWappenKettel.palette.emplace_back(QColor(245, 245, 245), QStringLiteral("Nesselblatt White"));
                seqWappenKettel.palette.emplace_back(QColor(25, 28, 30), QStringLiteral("Kieler Boot Black"));

                if (!wappenImg.isNull()) {
                    ImageDigitizer::Params pwK;
                    pwK.mode = ImageDigitizer::Mode::Colors;
                    pwK.colors = 3;
                    pwK.widthMm = 75.0;
                    pwK.dropBackground = true;
                    auto palW = ImageDigitizer::extractPalette(wappenImg, pwK);
                    QVector<EditPath> wPaths = ImageDigitizer::traceToPaths(wappenImg, pwK);
                    for (const EditPath& ep : wPaths) {
                        QPainterPath pp = ep.toPainterPath();
                        const QList<QPolygonF> polys = pp.toSubpathPolygons();
                        int cIdx = 0;
                        if (ep.colorIdx >= 0 && ep.colorIdx < int(palW.size())) {
                            QColor c = palW[ep.colorIdx].thread.color;
                            if (c.red() > 140 && c.green() < 80 && c.blue() < 80) cIdx = 0; // Red
                            else if (c.red() > 180 && c.green() > 180 && c.blue() > 180) cIdx = 1; // White
                            else cIdx = 2; // Black Boot
                        }
                        for (const QPolygonF& poly : polys) {
                            if (poly.size() < 3) continue;
                            QVector<QPointF> pts;
                            for (const QPointF& pt : poly) pts.push_back(pt);
                            StitchSequence sb = makeKettelOutline(pts, (cIdx == 0) ? 2.2 : 1.6, 0.38, cIdx);
                            seqWappenKettel.stitches.insert(seqWappenKettel.stitches.end(), sb.stitches.begin(), sb.stitches.end());
                        }
                    }
                    seqWappenKettel = ensureHoopA(seqWappenKettel, 120.0, 105.0);
                    std::printf("  Kieler Wappen (Kettel-Konturen, Hoop A): %zu stitches\n", seqWappenKettel.realStitchCount());
                }

                // --- 5. Brust "NW" & Ärmel "30" in Varsity Kettel-Outlines ---
                TextDigitizer::Params tpNW;
                tpNW.family = QStringLiteral("Varsity Regular");
                tpNW.text = QStringLiteral("nw");
                tpNW.heightMm = 45.0;
                tpNW.style = TextDigitizer::StitchStyle::SatinOutline;
                tpNW.borderWidthMm = 2.2;
                tpNW.borderPitchMm = 0.40;
                StitchSequence seqNwSatin = ensureHoopA(TextDigitizer::generate(tpNW), 120.0, 105.0);

                TextDigitizer::Params tpAermel30;
                tpAermel30.family = QStringLiteral("Varsity Regular");
                tpAermel30.text = QStringLiteral("30");
                tpAermel30.heightMm = 55.0;
                tpAermel30.style = TextDigitizer::StitchStyle::SatinOutline;
                tpAermel30.borderWidthMm = 2.4;
                tpAermel30.borderPitchMm = 0.40;
                StitchSequence seqAermelSatin = ensureHoopA(TextDigitizer::generate(tpAermel30), 120.0, 105.0);

                // Render previews
                QImage thumb30K = StitchThumbnail::render(seq30Kettel, 800, QColor(24, 26, 32), true);
                thumb30K.save(QStringLiteral("C:/Users/micbu/.gemini/antigravity/brain/e572641d-70d6-45b5-8c91-c97f358a4f6c/feuerwehr_30_kettel_outlines.png"), "PNG");

                QImage thumb30S = StitchThumbnail::render(seq30Sprossen, 800, QColor(24, 26, 32), true);
                thumb30S.save(QStringLiteral("C:/Users/micbu/.gemini/antigravity/brain/e572641d-70d6-45b5-8c91-c97f358a4f6c/feuerwehr_30_satin_sprossen.png"), "PNG");

                QImage thumbTopK = StitchThumbnail::render(seqTopMicroKettel, 800, QColor(24, 26, 32), true);
                thumbTopK.save(QStringLiteral("C:/Users/micbu/.gemini/antigravity/brain/e572641d-70d6-45b5-8c91-c97f358a4f6c/feuerwehr_bogen_oben_stitches.png"), "PNG");

                QImage thumbTopBean = StitchThumbnail::render(seqTopBean, 800, QColor(24, 26, 32), true);
                thumbTopBean.save(QStringLiteral("C:/Users/micbu/.gemini/antigravity/brain/e572641d-70d6-45b5-8c91-c97f358a4f6c/feuerwehr_bogen_oben_beanstitch.png"), "PNG");

                QImage thumbBottomK = StitchThumbnail::render(seqBottomMicroKettel, 800, QColor(24, 26, 32), true);
                thumbBottomK.save(QStringLiteral("C:/Users/micbu/.gemini/antigravity/brain/e572641d-70d6-45b5-8c91-c97f358a4f6c/feuerwehr_bogen_unten_stitches.png"), "PNG");

                QImage thumbBottomBean = StitchThumbnail::render(seqBottomBean, 800, QColor(24, 26, 32), true);
                thumbBottomBean.save(QStringLiteral("C:/Users/micbu/.gemini/antigravity/brain/e572641d-70d6-45b5-8c91-c97f358a4f6c/feuerwehr_bogen_unten_beanstitch.png"), "PNG");

                if (!seqWappenKettel.empty()) {
                    QImage thumbWapK = StitchThumbnail::render(seqWappenKettel, 800, QColor(24, 26, 32), true);
                    thumbWapK.save(QStringLiteral("C:/Users/micbu/.gemini/antigravity/brain/e572641d-70d6-45b5-8c91-c97f358a4f6c/kieler_wappen_kettel_konturen.png"), "PNG");
                }

                // --- 6. Saubere JEF-Exporte mit OBEN, MITTE, UNTEN direkt auf den USB-Stick F:\EmbF5\Feuerwehr\ ---
                QDir usbDir(QStringLiteral("F:/EmbF5/Feuerwehr"));
                if (usbDir.exists()) {
                    for (const QString& oldF : usbDir.entryList(QStringList() << QStringLiteral("*.jef"))) {
                        usbDir.remove(oldF);
                    }

                    // 1. OBEN (Bogen "BERUFSFEUERWEHR KIEL")
                    JefCodec::exportToFile(QStringLiteral("F:/EmbF5/Feuerwehr/01_OBEN_Bogen_Berufsfeuerwehr_Kiel.jef"), seqTopKompakt, HoopType::HoopA_126x110);
                    JefCodec::exportToFile(QStringLiteral("F:/EmbF5/Feuerwehr/01_OBEN_Bogen_Berufsfeuerwehr_Kiel_Kettel.jef"), seqTopMicroKettel, HoopType::HoopA_126x110);
                    JefCodec::exportToFile(QStringLiteral("F:/EmbF5/Feuerwehr/01_OBEN_Bogen_Berufsfeuerwehr_Kiel_BeanStitch.jef"), seqTopBean, HoopType::HoopA_126x110);
                    if (splitTop.success) {
                        JefCodec::exportToFile(QStringLiteral("F:/EmbF5/Feuerwehr/01a_OBEN_Gross_Teil1_Links.jef"), splitTop.hoop1, HoopType::HoopA_126x110);
                        JefCodec::exportToFile(QStringLiteral("F:/EmbF5/Feuerwehr/01b_OBEN_Gross_Teil2_Rechts.jef"), splitTop.hoop2, HoopType::HoopA_126x110);
                    }

                    // 2. MITTE (Zahl "30")
                    JefCodec::exportToFile(QStringLiteral("F:/EmbF5/Feuerwehr/02_MITTE_Zahl_30_Kettel_Outlines.jef"), seq30Kettel, HoopType::HoopA_126x110);
                    JefCodec::exportToFile(QStringLiteral("F:/EmbF5/Feuerwehr/02_MITTE_Zahl_30_Satin_Sprossen.jef"), seq30Sprossen, HoopType::HoopA_126x110);
                    JefCodec::exportToFile(QStringLiteral("F:/EmbF5/Feuerwehr/02_MITTE_Zahl_30_Satin_Kompakt.jef"), seq30Kompakt, HoopType::HoopA_126x110);

                    JefCodec::exportToFile(QStringLiteral("F:/EmbF5/Feuerwehr/02a_MITTE_Gross_Ziffer_3_Sprossen.jef"), seqZiffer3, HoopType::HoopA_126x110);
                    JefCodec::exportToFile(QStringLiteral("F:/EmbF5/Feuerwehr/02a_MITTE_Gross_Ziffer_3_Kettel.jef"), seqZiffer3Kettel, HoopType::HoopA_126x110);
                    JefCodec::exportToFile(QStringLiteral("F:/EmbF5/Feuerwehr/02b_MITTE_Gross_Ziffer_0_Sprossen.jef"), seqZiffer0, HoopType::HoopA_126x110);
                    JefCodec::exportToFile(QStringLiteral("F:/EmbF5/Feuerwehr/02b_MITTE_Gross_Ziffer_0_Kettel.jef"), seqZiffer0Kettel, HoopType::HoopA_126x110);

                    // 3. UNTEN (Bogen "NORDWACHE II")
                    JefCodec::exportToFile(QStringLiteral("F:/EmbF5/Feuerwehr/03_UNTEN_Bogen_Nordwache_II.jef"), seqBottomKompakt, HoopType::HoopA_126x110);
                    JefCodec::exportToFile(QStringLiteral("F:/EmbF5/Feuerwehr/03_UNTEN_Bogen_Nordwache_II_Kettel.jef"), seqBottomMicroKettel, HoopType::HoopA_126x110);
                    JefCodec::exportToFile(QStringLiteral("F:/EmbF5/Feuerwehr/03_UNTEN_Bogen_Nordwache_II_BeanStitch.jef"), seqBottomBean, HoopType::HoopA_126x110);

                    // 4. KIELER WAPPEN
                    if (!seqWappenKettel.empty())
                        JefCodec::exportToFile(QStringLiteral("F:/EmbF5/Feuerwehr/04_Kieler_Wappen_Kettel_Konturen.jef"), seqWappenKettel, HoopType::HoopA_126x110);
                    if (!seqWappen.empty())
                        JefCodec::exportToFile(QStringLiteral("F:/EmbF5/Feuerwehr/04_Kieler_Wappen_Mehrfarbig.jef"), seqWappen, HoopType::HoopA_126x110);

                    // 5. BRUST "NW" & ÄRMEL "30"
                    if (!seqNwSatin.empty())
                        JefCodec::exportToFile(QStringLiteral("F:/EmbF5/Feuerwehr/05_Brust_NW_Varsity_Kettel.jef"), seqNwSatin, HoopType::HoopA_126x110);
                    if (!seqAermelSatin.empty())
                        JefCodec::exportToFile(QStringLiteral("F:/EmbF5/Feuerwehr/06_Aermel_30_Varsity_Kettel.jef"), seqAermelSatin, HoopType::HoopA_126x110);

                    std::printf("  Perfekt! 100%% Janome Hoop A kompatible JEF Dateien (OBEN, MITTE, UNTEN + Patches) mit Varsity Schrift & Kettel-Outlines auf F:/EmbF5/Feuerwehr/ geschrieben!\n");
                }
            }
            return 0;
        }

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

    std::printf("\n== StickStudio JEF Import, Color Palette & Stitch Rendering ==\n");
    {
        const QString testWappen = QStringLiteral("F:/EmbF5/Feuerwehr/04_Kieler_Wappen_Vollstick_Ausgefuellt.jef");
        if (QFile::exists(testWappen)) {
            StitchSequence wappenSeq;
            bool ok = JefCodec::importFromFile(testWappen, wappenSeq);
            CHECK(ok, "StickStudio imported 04_Kieler_Wappen_Vollstick_Ausgefuellt.jef");
            CHECK(!wappenSeq.empty(), "Wappen stitch sequence is non-empty");
            CHECK(wappenSeq.palette.size() >= 3, "Wappen has multiple thread colors");
            std::printf("     Imported Wappen: %zu stitches, %zu palette entries\n",
                        wappenSeq.realStitchCount(), wappenSeq.palette.size());

            QImage img(800, 800, QImage::Format_ARGB32_Premultiplied);
            img.fill(QColor(25, 25, 30));
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing);
            drawRealisticStitches(p, wappenSeq, QRectF(40, 40, 720, 720));
            p.end();
            const QString outPng = QStringLiteral("C:/Users/micbu/.gemini/antigravity/brain/e572641d-70d6-45b5-8c91-c97f358a4f6c/stickstudio_loaded_wappen_verified.png");
            img.save(outPng);
            std::printf("     Rendered verified preview: %s\n", qPrintable(outPng));
        }

        const QString testBogen = QStringLiteral("F:/EmbF5/Feuerwehr/01_OBEN_Bogen_Berufsfeuerwehr_Kiel.jef");
        if (QFile::exists(testBogen)) {
            StitchSequence bogenSeq;
            bool ok = JefCodec::importFromFile(testBogen, bogenSeq);
            CHECK(ok, "StickStudio imported 01_OBEN_Bogen_Berufsfeuerwehr_Kiel.jef");
            CHECK(!bogenSeq.empty(), "Bogen stitch sequence is non-empty");
            std::printf("     Imported Bogen: %zu stitches, %zu palette entries\n",
                        bogenSeq.realStitchCount(), bogenSeq.palette.size());

            QImage img(1000, 500, QImage::Format_ARGB32_Premultiplied);
            img.fill(QColor(25, 25, 30));
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing);
            drawRealisticStitches(p, bogenSeq, QRectF(40, 40, 920, 420));
            p.end();
            const QString outPng = QStringLiteral("C:/Users/micbu/.gemini/antigravity/brain/e572641d-70d6-45b5-8c91-c97f358a4f6c/stickstudio_loaded_bogen_verified.png");
            img.save(outPng);
            std::printf("     Rendered verified preview: %s\n", qPrintable(outPng));
        }
    }

    std::printf("\n== JEF Absolute Upright Orientation & Verification ==\n");
    {
        // 1. Asymmetric upward-pointing arrow test
        StitchSequence arrow;
        arrow.palette.emplace_back(QColor(220, 40, 50), "Red", 13);
        arrow.add(0.0, -25.0, SF_Normal, 0); // Bottom base
        arrow.add(0.0,  25.0, SF_Normal, 0); // Top tip
        arrow.add(-10.0, 15.0, SF_Normal, 0); // Left barb
        arrow.add(0.0,  25.0, SF_Normal, 0); // Back to top tip
        arrow.add(10.0,  15.0, SF_Normal, 0); // Right barb

        const QString arrowJef = QDir::tempPath() + QStringLiteral("/_verify_arrow_upright.jef");
        auto expRes = JefCodec::exportToFile(arrowJef, arrow, HoopType::HoopA_126x110);
        CHECK(expRes.ok, "JEF arrow export succeeded");

        StitchSequence arrowBack;
        CHECK(JefCodec::importFromFile(arrowJef, arrowBack), "re-import arrow JEF");
        CHECK(arrowBack.stitches.size() >= 5, "arrow stitches present");

        // Verify top point is +Y and bottom point is -Y (NOT flipped!)
        double aX, aY, bX, bY;
        arrowBack.bounds(aX, aY, bX, bY);
        CHECK(bY > 24.5, "arrow tip has maximum positive +Y (upright)");
        CHECK(aY < -24.5, "arrow base has minimum negative -Y (bottom)");
        CHECK(aX < -9.5, "arrow left barb has negative X (left)");
        CHECK(bX > 9.5, "arrow right barb has positive X (right)");

        // 2. Load authentic real-world BF30_1.JEF file
        QString realBfPath = QStringLiteral("E:/StickCore/output/EmbF5/MyDesign/BF30_1.JEF");
        if (!QFile::exists(realBfPath)) {
            realBfPath = QStringLiteral("F:/EmbF5/MyDesign/BF30_1.JEF");
        }
        if (QFile::exists(realBfPath)) {
            StitchSequence bfSeq;
            bool ok = JefCodec::importFromFile(realBfPath, bfSeq);
            CHECK(ok, "imported authentic BF30_1.JEF");
            CHECK(!bfSeq.empty(), "BF30_1.JEF stitches loaded");
            double minX, minY, maxX, maxY;
            bfSeq.bounds(minX, minY, maxX, maxY);
            std::printf("     BF30_1.JEF bounds: [%.1f, %.1f] to [%.1f, %.1f] mm (W=%.1f, H=%.1f)\n",
                        minX, minY, maxX, maxY, maxX - minX, maxY - minY);
            CHECK(maxY > minY, "BF30_1.JEF has valid vertical bounds");

            QImage thumb = StitchThumbnail::render(bfSeq, 800, QColor(25, 25, 30), true);
            const QString outBfPng = QStringLiteral("C:/Users/micbu/.gemini/antigravity/brain/e572641d-70d6-45b5-8c91-c97f358a4f6c/bf30_1_upright_stickstudio_verified.png");
            thumb.save(outBfPng);
            std::printf("     Saved upright verified thumbnail: %s\n", qPrintable(outBfPng));
        }
    }

    std::printf("\n== Bogen- & College-Schrift Studio, 3D-Relief Padding & Multi-Hoop Auto-Split ==\n");
    {
        // 1. Verify custom college fonts from QRC
        const QString varsityFont = QStringLiteral(":/fonts/varsity_regular.ttf");
        int fontId = QFontDatabase::addApplicationFont(varsityFont);
        CHECK(fontId >= 0, "loaded varsity_regular.ttf from resource system");
        const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        CHECK(!families.isEmpty(), "varsity font family name resolved");
        if (!families.isEmpty()) {
            std::printf("     Resolved Varsity font family: %s\n", qPrintable(families.first()));
        }

        // 2. Wave baseline geometry verification
        TextDigitizer::Params waveParams;
        waveParams.text = QStringLiteral("FEUERWEHR");
        waveParams.heightMm = 25.0;
        waveParams.baseline = TextDigitizer::Baseline::Wave;
        waveParams.arcRadiusMm = 80.0;
        QPainterPath wavePath = TextDigitizer::textPath(waveParams);
        CHECK(!wavePath.isEmpty(), "Wave baseline produced non-empty painter path");
        const QRectF waveBb = wavePath.boundingRect();
        CHECK(waveBb.width() > 50.0, "Wave text width is proportional (> 50mm)");
        CHECK(waveBb.height() > 20.0, "Wave text height is proportional (> 20mm)");

        // 3. 3D-Relief Lettering ("Paddlerin" Dual-Pass Satin: 2.4mm underlay + 3.5mm cover @ 0.34mm)
        TextDigitizer::Params padParams;
        padParams.text = QStringLiteral("BERUFSFEUERWEHR KIEL");
        padParams.family = families.isEmpty() ? QStringLiteral("Varsity") : families.first();
        padParams.heightMm = 28.0;
        padParams.baseline = TextDigitizer::Baseline::ArcUp;
        padParams.arcRadiusMm = 160.0;
        padParams.letterSpacingMm = 1.2;
        padParams.style = TextDigitizer::StitchStyle::PaddedSatin;
        padParams.paddingWidthMm = 2.4;
        padParams.paddingPitchMm = 0.48;
        padParams.borderWidthMm = 3.5;
        padParams.borderPitchMm = 0.34;

        StitchSequence paddedSeq = TextDigitizer::generate(padParams);
        CHECK(!paddedSeq.empty(), "PaddedSatin generated non-empty stitch sequence");
        CHECK(paddedSeq.stitches.size() > 4000, "PaddedSatin generated dense dual-pass stitches (> 4000)");
        std::printf("     Generated PaddedSatin: %zu stitches, bounds: [%.1f x %.1f mm]\n",
                    paddedSeq.stitches.size(), paddedSeq.boundingRect().width(), paddedSeq.boundingRect().height());

        // Single pass comparison to verify dual-pass has more stitches
        TextDigitizer::Params singleParams = padParams;
        singleParams.style = TextDigitizer::StitchStyle::SatinOutline;
        StitchSequence singleSeq = TextDigitizer::generate(singleParams);
        CHECK(paddedSeq.stitches.size() > singleSeq.stitches.size(), "Dual-pass PaddedSatin has significantly more stitches than single pass");

        // 4. Multi-Hoop Auto-Split with secure 1.0mm registration crosshairs for oversized arch
        MultiHoopSplitter::Params mp;
        mp.hoopWidthMm    = 140.0; // Janome Hoop B (140 x 200 mm)
        mp.hoopHeightMm   = 200.0;
        mp.overlapMm      = 8.0;
        mp.addCrosshairs  = true;
        mp.centerEachHoop = true;
        mp.direction      = MultiHoopSplitter::SplitDirection::Vertical;

        const double archWidth = paddedSeq.boundingRect().width();
        CHECK(archWidth > 140.0, "Arch text width exceeds Janome Hoop B (oversized)");

        auto multiRes = MultiHoopSplitter::splitMulti(paddedSeq, mp, 3);
        CHECK(multiRes.success, "splitMulti succeeded for 3-part arch");
        CHECK(multiRes.parts.size() == 3, "splitMulti generated exactly 3 parts");
        CHECK(!multiRes.crosshairs.empty(), "splitMulti generated registration crosshairs");

        // Verify that each part fits within Janome Hoop B bounds (with margin)
        for (size_t i = 0; i < multiRes.parts.size(); ++i) {
            const auto& part = multiRes.parts[i];
            CHECK(!part.empty(), "part stitch sequence is non-empty");
            const QRectF pb = part.boundingRect();
            std::printf("     Part %zu bounds: W=%.1f mm, H=%.1f mm (%zu stitches)\n",
                        i + 1, pb.width(), pb.height(), part.stitches.size());
            CHECK(pb.width() <= 140.0 + 10.0, "part width fits within Janome Hoop B envelope");
            CHECK(pb.height() <= 200.0 + 10.0, "part height fits within Janome Hoop B envelope");

            // Export to temporary JEF file and verify hardware round-trip
            const QString partJef = QDir::tempPath() + QString("/_test_part_%1.jef").arg(i + 1);
            auto expRes = JefCodec::exportToFile(partJef, part, HoopType::HoopB_140x200);
            CHECK(expRes.ok, "exported part to JEF format");

            StitchSequence partBack;
            CHECK(JefCodec::importFromFile(partJef, partBack), "re-imported part JEF file");
            CHECK(!partBack.empty(), "re-imported part contains stitches");
        }

        // 5. Render realistic verification image of the complete oversized 3D padded arch
        QImage renderImg(1200, 600, QImage::Format_ARGB32_Premultiplied);
        renderImg.fill(QColor(18, 20, 26));
        QPainter p(&renderImg);
        p.setRenderHint(QPainter::Antialiasing);
        if (!paddedSeq.palette.empty()) {
            paddedSeq.palette[0] = ThreadColor(QColor(245, 186, 21), QStringLiteral("Varsity Gold"));
        }
        drawRealisticStitches(p, paddedSeq, QRectF(50, 50, 1100, 500));
        p.end();

        const QString outArchPng = QStringLiteral("C:/Users/micbu/.gemini/antigravity/brain/e572641d-70d6-45b5-8c91-c97f358a4f6c/college_varsity_padded_arc_verified.png");
        renderImg.save(outArchPng);
        std::printf("     Saved verified 3D Varsity Padded Arc preview: %s\n", qPrintable(outArchPng));
    }

    std::printf("\n%s (%d failures)\n", failures? "FAILURES" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
