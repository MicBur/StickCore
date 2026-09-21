// ---------------------------------------------------------------------------
//  StickCore  –  ImageDigitizer.cpp
// ---------------------------------------------------------------------------
#include "generators/ImageDigitizer.h"
#include "generators/PhotoProcessor.h"
#include "generators/OpenCvBridge.h"
#include "generators/SatinGenerator.h"
#include "core/ThreadCatalog.h"

#include <QPainterPath>
#include <QPoint>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

namespace stick {

namespace {

struct RGB { double r, g, b; };
inline double lum(const RGB& c){ return 0.2126*c.r + 0.7152*c.g + 0.0722*c.b; }
inline double d2(const RGB& a, const RGB& b){
    const double dr=a.r-b.r, dg=a.g-b.g, db=a.b-b.b; return dr*dr+dg*dg+db*db;
}

// Downscale the source to the analysis size (KeepAspectRatio, smooth).
QImage analysisScaled(const QImage& src, int maxPx)
{
    QImage img = src.convertToFormat(QImage::Format_RGB32);
    if (std::max(img.width(), img.height()) > maxPx)
        img = img.scaled(maxPx, maxPx, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return img;
}

// RGB contrast & blur adjustment preserving color channels in Format_RGB32:
QImage rgbAdjust(const QImage& src, double contrast, int blur, bool invert)
{
    if (src.isNull()) return src;
    QImage img = src.convertToFormat(QImage::Format_RGB32);
    if (invert) img.invertPixels();
    if (std::abs(contrast - 1.0) > 1e-3) {
        quint8 lut[256];
        for (int v = 0; v < 256; ++v) {
            const double nv = (v - 128.0) * contrast + 128.0;
            lut[v] = quint8(std::clamp(int(std::lround(nv)), 0, 255));
        }
        const int W = img.width(), H = img.height();
        for (int y = 0; y < H; ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < W; ++x) {
                QRgb c = line[x];
                line[x] = qRgb(lut[qRed(c)], lut[qGreen(c)], lut[qBlue(c)]);
            }
        }
    }
    if (blur > 0) {
        if (OpenCvBridge::available()) {
            img = OpenCvBridge::bilateralDenoise(img, 5 + blur * 2, 40.0, 40.0);
        } else {
            const int W = img.width(), H = img.height();
            const int sw = std::max(2, W / (1 + blur));
            const int sh = std::max(2, H / (1 + blur));
            img = img.scaled(sw, sh, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                     .scaled(W, H, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
    }
    return img.convertToFormat(QImage::Format_RGB32);
}

// Produce the grayscale analysis buffer (blur + contrast applied).
QImage grayAnalysis(const QImage& scaled, const ImageDigitizer::Params& p)
{
    QImage g = PhotoProcessor::toGray(scaled);
    if (p.blur > 0)         g = PhotoProcessor::boxBlur(g, p.blur);
    if (p.contrast != 1.0)  g = PhotoProcessor::contrast(g, p.contrast);
    return g;
}

// -------------------------------------------------------------------------
//  Moore-neighbour contour tracing on a binary mask (fg == non-zero).
//  Returns outer boundaries as ordered pixel-point loops (clockwise).
// -------------------------------------------------------------------------
std::vector<std::vector<QPoint>>
traceContours(const std::vector<quint8>& mask, int W, int H)
{
    // 8 neighbours, clockwise:   0=N 1=NE 2=E 3=SE 4=S 5=SW 6=W 7=NW
    static const int dx[8] = { 0, 1, 1, 1, 0,-1,-1,-1 };
    static const int dy[8] = {-1,-1, 0, 1, 1, 1, 0,-1 };
    auto fg = [&](int x, int y){
        return x >= 0 && y >= 0 && x < W && y < H && mask[std::size_t(y)*W + x] != 0;
    };

    std::vector<quint8> visited(std::size_t(W) * H, 0);
    std::vector<std::vector<QPoint>> contours;
    const long maxIter = long(W) * H * 8 + 64;

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (!fg(x, y)) continue;
            if (visited[std::size_t(y)*W + x]) continue;
            if (fg(x - 1, y)) continue;             // only start at a left edge

            std::vector<QPoint> c;
            const int sx = x, sy = y;
            int px = x, py = y;
            int b  = 6;                             // came from the West
            c.push_back(QPoint(px, py));
            visited[std::size_t(py)*W + px] = 1;

            long it = 0;
            bool ok = true;
            while (it++ < maxIter) {
                bool moved = false;
                for (int i = 1; i <= 8; ++i) {
                    const int d  = (b + i) & 7;
                    const int nx = px + dx[d], ny = py + dy[d];
                    if (fg(nx, ny)) {
                        b  = (d + 4) & 7;           // opposite = where we came from
                        px = nx; py = ny;
                        moved = true;
                        break;
                    }
                }
                if (!moved) break;                  // isolated pixel
                if (px == sx && py == sy) break;    // closed the loop
                c.push_back(QPoint(px, py));
                visited[std::size_t(py)*W + px] = 1;
            }
            if (!ok) continue;
            if (c.size() >= 8) contours.push_back(std::move(c));
        }
    }
    return contours;
}

// Raster-fill the pixels where labels[i] == want, as a tatami scan, into seq.
void rasterFillLabel(StitchSequence& seq,
                     const std::vector<int>& labels, int want,
                     int W, int H, int rowStep, double maxStitch,
                     std::function<double(double)> X,
                     std::function<double(double)> Y,
                     int colorIdx, bool firstColor,
                     double angleDeg = 0.0)
{
    const double rad = angleDeg * 3.14159265358979323846 / 180.0;
    const double cosA = std::cos(rad);
    const double sinA = std::sin(rad);

    auto rot = [cosA, sinA](double px, double py) -> std::pair<double, double> {
        return { px * cosA - py * sinA, px * sinA + py * cosA };
    };

    bool firstOfColor = true;
    bool reverse = false;
    for (int y = rowStep/2; y < H; y += rowStep, reverse = !reverse) {
        std::vector<std::pair<int,int>> runs;
        int x = 0;
        while (x < W) {
            if (labels[std::size_t(y)*W + x] == want) {
                int x0 = x; while (x < W && labels[std::size_t(y)*W + x] == want) ++x;
                runs.emplace_back(x0, x - 1);
            } else ++x;
        }
        if (reverse) std::reverse(runs.begin(), runs.end());
        for (auto& r : runs) {
            const double xa = X(reverse ? r.second + 1 : r.first);
            const double xb = X(reverse ? r.first     : r.second + 1);
            const double yy = Y(y);
            quint32 startFlag = firstOfColor ? SF_ColorChange : SF_Jump;
            if (firstColor && firstOfColor) startFlag = SF_Jump;
            auto p0 = rot(xa, yy);
            seq.add(p0.first, p0.second, startFlag, colorIdx);
            firstOfColor = false;
            const double span = std::abs(xb - xa);
            const int n = std::max(1, int(std::ceil(span / maxStitch)));
            for (int s = 1; s <= n; ++s) {
                const double curX = xa + (xb - xa) * (double(s)/n);
                auto ps = rot(curX, yy);
                seq.add(ps.first, ps.second, SF_Normal, colorIdx);
            }
        }
    }
}

} // namespace

// ===========================================================================
//  Colors mode (k-means) — unchanged behaviour, factored into a helper.
// ===========================================================================
static StitchSequence generateColors(const QImage& src, const ImageDigitizer::Params& p)
{
    StitchSequence seq;
    QImage img = analysisScaled(src, p.maxProcessPx);
    img = rgbAdjust(img, p.contrast, p.blur, p.invert);
    const int W = img.width(), H = img.height();
    if (W < 2 || H < 2) return seq;

    std::vector<RGB> px(std::size_t(W) * H);
    for (int y = 0; y < H; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < W; ++x)
            px[std::size_t(y)*W + x] = { double(qRed(line[x])), double(qGreen(line[x])), double(qBlue(line[x])) };
    }

    const int K = std::clamp(p.colors, 2, 16);
    std::vector<RGB> cen(K);
    for (int k = 0; k < K; ++k) cen[k] = px[std::size_t((k + 0.5) / K * px.size())];

    std::vector<int> label(px.size(), 0);
    for (int iter = 0; iter < 10; ++iter) {
        for (std::size_t i = 0; i < px.size(); ++i) {
            double best = std::numeric_limits<double>::max(); int bi = 0;
            for (int k = 0; k < K; ++k){ double d = d2(px[i], cen[k]); if (d < best){best=d;bi=k;} }
            label[i] = bi;
        }
        std::vector<RGB> sum(K, {0,0,0}); std::vector<long> cnt(K, 0);
        for (std::size_t i = 0; i < px.size(); ++i){
            sum[label[i]].r += px[i].r; sum[label[i]].g += px[i].g; sum[label[i]].b += px[i].b; cnt[label[i]]++;
        }
        for (int k = 0; k < K; ++k){
            if (cnt[k] > 0){ cen[k] = { sum[k].r/cnt[k], sum[k].g/cnt[k], sum[k].b/cnt[k] }; }
            else cen[k] = px[std::size_t(std::rand() % px.size())];
        }
    }

    // Clean isolated pixel noise so tiny micro-stitches and single-needle jumps disappear
    label = PhotoProcessor::filterSpeckles(label, W, H, 16);

    std::vector<long> cnt(K, 0);
    for (int l : label) cnt[l]++;

    int background = -1;
    if (p.dropBackground) {
        long tot = long(px.size());
        std::vector<long> borderCnt(K, 0);
        long borderTotal = 0;
        for (int x = 0; x < W; ++x) {
            borderCnt[label[0 * W + x]]++;
            borderCnt[label[(H - 1) * W + x]]++;
            borderTotal += 2;
        }
        for (int y = 1; y < H - 1; ++y) {
            borderCnt[label[y * W + 0]]++;
            borderCnt[label[y * W + (W - 1)]]++;
            borderTotal += 2;
        }
        int borderDominant = -1;
        long maxBorder = 0;
        for (int k = 0; k < K; ++k) {
            if (borderCnt[k] > maxBorder && borderCnt[k] >= borderTotal * 0.40 && cnt[k] >= tot * 0.15) {
                maxBorder = borderCnt[k];
                borderDominant = k;
            }
        }
        if (borderDominant >= 0) {
            background = borderDominant;
        } else {
            for (int k = 0; k < K; ++k) {
                const double l = lum(cen[k]);
                if ((l > 220 || l < 35) && cnt[k] > tot * 0.25) {
                    background = k;
                    break;
                }
            }
        }
    }

    std::vector<int> order;
    for (int k = 0; k < K; ++k) if (k != background && cnt[k] > 0) order.push_back(k);
    std::sort(order.begin(), order.end(), [&](int a, int b){ return lum(cen[a]) < lum(cen[b]); });
    if (order.empty()) return seq;

    const double mmPerPx = p.widthMm / W;
    const double cxPx = W * 0.5, cyPx = H * 0.5;
    auto X = [=](double xpx){ return (xpx - cxPx) * mmPerPx; };
    auto Y = [=](double ypx){ return (cyPx - ypx) * mmPerPx; };
    const int rowStep = std::max(1, int(std::llround(p.densityMm / mmPerPx)));

    for (std::size_t oi = 0; oi < order.size(); ++oi) {
        double angle = p.fillAngleDeg;
        if (p.multiAngle) {
            angle = p.fillAngleDeg + (oi * 45.0);
        }
        rasterFillLabel(seq, label, order[oi], W, H, rowStep, p.maxStitchMm,
                        X, Y, int(oi), oi == 0, angle);
    }

    for (int k : order) {
        const QColor c(int(std::round(cen[k].r)), int(std::round(cen[k].g)), int(std::round(cen[k].b)));
        seq.palette.push_back(ThreadCatalog::snap(c, p.brand));
    }

    // Optional raised satin border around the motif boundary (patch edge)
    if (p.satinBorder && background >= 0) {
        std::vector<quint8> fgMask(size_t(W) * H, 0);
        for (size_t i = 0; i < label.size(); ++i) {
            if (label[i] != background) fgMask[i] = 255;
        }
        auto rawContours = traceContours(fgMask, W, H);
        for (const auto& c : rawContours) {
            if (c.size() < 6) continue;
            QPolygonF poly;
            for (const auto& pt : c) poly << QPointF(X(pt.x()), Y(pt.y()));
            poly = PhotoProcessor::smoothPolygon(poly, 0.8);
            if (poly.size() >= 3) {
                QPainterPath cPath;
                cPath.addPolygon(poly);
                cPath.closeSubpath();
                StitchSequence border = SatinGenerator::fromCenterline(cPath, p.borderWidthMm, 0.40, int(order.size()));
                seq.stitches.insert(seq.stitches.end(), border.stitches.begin(), border.stitches.end());
            }
        }
        seq.palette.push_back(ThreadCatalog::snap(QColor(25, 25, 30), p.brand));
    }

    return seq;
}

// ===========================================================================
//  Portrait mode — silhouette (1 tone) or a few grey tones (posterise).
// ===========================================================================
static StitchSequence generatePortrait(const QImage& src, const ImageDigitizer::Params& p)
{
    using PS = ImageDigitizer::PortraitStyle;
    StitchSequence seq;
    const bool useOcv     = OpenCvBridge::available();
    const bool singleTone = (p.portraitStyle != PS::Tones);
    // Detail-bearing single-tone styles want more pixels so fine facial features
    // survive; the plain silhouette is fine at the normal analysis resolution.
    const bool hires = singleTone && useOcv && p.portraitStyle != PS::Silhouette;
    const int procPx = hires ? std::max(p.maxProcessPx, 440) : p.maxProcessPx;
    QImage scaled = analysisScaled(src, procPx);
    const int W = scaled.width(), H = scaled.height();
    if (W < 2 || H < 2) return seq;

    QImage g = grayAnalysis(scaled, p);
    const int tones = singleTone ? 1 : std::clamp(p.tones, 2, 6);

    // Build a per-pixel layer label: 0 = background (fabric), 1..tones = ink.
    std::vector<int> label(std::size_t(W) * H, 0);

    if (singleTone) {
        QImage m;
        if (useOcv) {
            switch (p.portraitStyle) {
            case PS::Silhouette: m = OpenCvBridge::silhouette(g, p.invert); break;
            case PS::Sketch:     m = OpenCvBridge::sketch(g, p.invert); break;
            case PS::Detailed:   m = OpenCvBridge::detailed(g, p.invert); break;
            case PS::Stylized:   m = OpenCvBridge::stylizedMask(scaled, p.invert); break;
            case PS::Comic:
            default:             m = OpenCvBridge::portraitComic(g, p.invert); break;
            }
        }
        if (m.isNull() || m.size() != g.size()) {
            // native fallback: global Otsu silhouette
            const int t = (p.threshold >= 0) ? p.threshold : PhotoProcessor::otsu(g);
            m = PhotoProcessor::threshold(g, t, p.invert);   // ink dark (0)
        }
        for (int y = 0; y < H; ++y) {
            const uchar* line = m.constScanLine(y);
            for (int x = 0; x < W; ++x)
                label[std::size_t(y)*W + x] = (line[x] < 128) ? 1 : 0;   // ink = dark
        }
    } else {
        QImage q = PhotoProcessor::posterize(g, tones + 1);   // tones+1 bands
        // band 0 = darkest ... band tones = lightest (= fabric background).
        // If inverted, flip which end is background.
        for (int y = 0; y < H; ++y) {
            const uchar* line = q.constScanLine(y);
            for (int x = 0; x < W; ++x) {
                int band = int(std::lround(line[x] / 255.0 * tones)); // 0..tones
                band = std::clamp(band, 0, tones);
                if (p.invert) band = tones - band;
                // band == tones -> background; otherwise ink layer (1..tones)
                label[std::size_t(y)*W + x] = (band >= tones) ? 0 : (band + 1);
            }
        }
    }

    const double mmPerPx = p.widthMm / W;
    const double cxPx = W * 0.5, cyPx = H * 0.5;
    auto X = [=](double xpx){ return (xpx - cxPx) * mmPerPx; };
    auto Y = [=](double ypx){ return (cyPx - ypx) * mmPerPx; };
    const double density = (p.densityMm > 0.05) ? p.densityMm : 0.45;
    const int rowStep = std::max(1, int(std::llround(density / mmPerPx)));

    // Fill darkest layer first.
    for (int layer = 1; layer <= tones; ++layer)
        rasterFillLabel(seq, label, layer, W, H, rowStep, p.maxStitchMm,
                        X, Y, layer - 1, layer == 1);

    // Palette: darkest = ink, lighter tones = blends toward mid-grey.
    for (int layer = 1; layer <= tones; ++layer) {
        double f = (tones == 1) ? 0.0 : double(layer - 1) / double(tones - 1);
        const int base = p.ink.value();
        const int v = int(std::round(base + f * (200 - base)));
        QColor c = QColor::fromHsv(p.ink.hue() < 0 ? 0 : p.ink.hue(),
                                   p.ink.saturation(), std::clamp(v, 0, 255));
        if (p.ink.saturation() == 0) c = QColor(v, v, v);
        seq.palette.push_back(ThreadCatalog::snap(c, p.brand));
    }
    return seq;
}

// ===========================================================================
//  LineArt mode — trace outlines of the threshold mask into run stitches.
// ===========================================================================
static StitchSequence generateLineArt(const QImage& src, const ImageDigitizer::Params& p)
{
    StitchSequence seq;
    QImage scaled = analysisScaled(src, p.maxProcessPx);
    const int W = scaled.width(), H = scaled.height();
    if (W < 2 || H < 2) return seq;

    QImage g = grayAnalysis(scaled, p);

    // Contours as pixel-space polylines. With OpenCV we use Canny + findContours
    // (captures facial features, clean lines); otherwise a Moore boundary trace
    // of the Otsu silhouette.
    std::vector<std::vector<QPoint>> contours;
    if (OpenCvBridge::available()) {
        QImage edges = OpenCvBridge::cannyEdges(g, 60, 160, 3);   // edges dark on white
        const double minLenPx = std::max(6.0, W * 0.06);
        const auto polys = OpenCvBridge::findContours(edges, minLenPx);
        for (const auto& poly : polys) {
            std::vector<QPoint> c; c.reserve(poly.size());
            for (const QPointF& pt : poly) c.emplace_back(int(pt.x()), int(pt.y()));
            if (c.size() >= 4) contours.push_back(std::move(c));
        }
    } else {
        const int t = (p.threshold >= 0) ? p.threshold : PhotoProcessor::otsu(g);
        std::vector<quint8> mask(std::size_t(W) * H, 0);
        for (int y = 0; y < H; ++y) {
            const uchar* line = g.constScanLine(y);
            for (int x = 0; x < W; ++x) {
                const bool dark = line[x] < t;
                const bool fg = p.invert ? !dark : dark;
                mask[std::size_t(y)*W + x] = fg ? 1 : 0;
            }
        }
        contours = traceContours(mask, W, H);
    }
    if (contours.empty()) return seq;

    const double mmPerPx = p.widthMm / W;
    const double cxPx = W * 0.5, cyPx = H * 0.5;
    auto X = [=](double xpx){ return (xpx - cxPx) * mmPerPx; };
    auto Y = [=](double ypx){ return (cyPx - ypx) * mmPerPx; };
    const double run = std::max(1.0, p.runMm);

    bool first = true;
    for (const auto& c : contours) {
        // Walk the contour, emitting a stitch every ~run mm of arc length.
        double acc = 0.0;
        const QPoint& p0 = c.front();
        seq.add(X(p0.x() + 0.5), Y(p0.y() + 0.5), first ? SF_Jump : SF_Jump, 0);
        first = false;
        double px = p0.x() + 0.5, py = p0.y() + 0.5;
        for (std::size_t i = 1; i <= c.size(); ++i) {
            const QPoint& q = c[i % c.size()];
            const double qx = q.x() + 0.5, qy = q.y() + 0.5;
            double dx = (qx - px) * mmPerPx, dy = (qy - py) * mmPerPx;
            acc += std::sqrt(dx*dx + dy*dy);
            if (acc >= run || i == c.size()) {
                seq.add(X(qx), Y(qy), SF_Normal, 0);
                acc = 0.0;
            }
            px = qx; py = qy;
        }
    }

    seq.palette.push_back(ThreadCatalog::snap(p.ink, p.brand));
    return seq;
}

// ===========================================================================
QString ImageDigitizer::portraitStyleName(PortraitStyle s)
{
    switch (s) {
    case PortraitStyle::Comic:      return QStringLiteral("Comic (Silhouette + Detail)");
    case PortraitStyle::Silhouette: return QStringLiteral("Silhouette (kräftig, einfarbig)");
    case PortraitStyle::Sketch:     return QStringLiteral("Zeichnung (feine Linien)");
    case PortraitStyle::Detailed:   return QStringLiteral("Detailliert (fotoartig)");
    case PortraitStyle::Stylized:   return QStringLiteral("Stilisiert (Comic-Glättung)");
    case PortraitStyle::Tones:      return QStringLiteral("Graustufen (2–5 Töne)");
    }
    return QStringLiteral("Comic");
}

// ===========================================================================
StitchSequence ImageDigitizer::generate(const QImage& image, const Params& p)
{
    if (image.isNull()) return StitchSequence();
    QImage src = image;
    if (p.autoFaceCrop && OpenCvBridge::available())
        src = OpenCvBridge::autoFaceCrop(image);
    switch (p.mode) {
    case Mode::Portrait: return generatePortrait(src, p);
    case Mode::LineArt:  return generateLineArt(src, p);
    case Mode::Colors:
    default:             return generateColors(src, p);
    }
}

StitchSequence ImageDigitizer::generateFastPreview(const QImage& image, const Params& p)
{
    if (image.isNull()) return StitchSequence();
    Params fastP = p;
    fastP.maxProcessPx = std::min(p.maxProcessPx, 130);
    fastP.densityMm    = std::max(p.densityMm, 0.55);
    return generate(image, fastP);
}

// ===========================================================================
//  Preview: what the analysis stage "sees" (for the settings dialog).
// ===========================================================================
QImage ImageDigitizer::preview(const QImage& image, const Params& p)
{
    if (image.isNull()) return QImage();
    QImage source = image;
    if (p.autoFaceCrop && OpenCvBridge::available())
        source = OpenCvBridge::autoFaceCrop(image);
    const bool hiresPrev = (p.mode == Mode::Portrait &&
                            p.portraitStyle != PortraitStyle::Tones &&
                            p.portraitStyle != PortraitStyle::Silhouette &&
                            OpenCvBridge::available());
    QImage scaled = analysisScaled(source, hiresPrev ? std::max(p.maxProcessPx, 440) : p.maxProcessPx);
    const int W = scaled.width(), H = scaled.height();
    if (W < 2 || H < 2) return QImage();

    if (p.mode == Mode::Colors) {
        QImage img = rgbAdjust(scaled, p.contrast, p.blur, p.invert);

        std::vector<RGB> px(std::size_t(W) * H);
        for (int y = 0; y < H; ++y) {
            const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
            for (int x = 0; x < W; ++x)
                px[std::size_t(y)*W + x] = { double(qRed(line[x])), double(qGreen(line[x])), double(qBlue(line[x])) };
        }

        const int K = std::clamp(p.colors, 2, 16);
        std::vector<RGB> cen(K);
        for (int k = 0; k < K; ++k) cen[k] = px[std::size_t((k + 0.5) / K * px.size())];

        std::vector<int> label(px.size(), 0);
        for (int iter = 0; iter < 8; ++iter) {
            for (std::size_t i = 0; i < px.size(); ++i) {
                double best = std::numeric_limits<double>::max(); int bi = 0;
                for (int k = 0; k < K; ++k){ double d = d2(px[i], cen[k]); if (d < best){best=d;bi=k;} }
                label[i] = bi;
            }
            std::vector<RGB> sum(K, {0,0,0}); std::vector<long> cnt(K, 0);
            for (std::size_t i = 0; i < px.size(); ++i){
                sum[label[i]].r += px[i].r; sum[label[i]].g += px[i].g; sum[label[i]].b += px[i].b; cnt[label[i]]++;
            }
            for (int k = 0; k < K; ++k){
                if (cnt[k] > 0){ cen[k] = { sum[k].r/cnt[k], sum[k].g/cnt[k], sum[k].b/cnt[k] }; }
                else cen[k] = px[std::size_t(std::rand() % px.size())];
            }
        }
        label = PhotoProcessor::filterSpeckles(label, W, H, 16);

        std::vector<long> cnt(K, 0);
        for (int l : label) cnt[l]++;

        int background = -1;
        if (p.dropBackground) {
            long tot = long(px.size());
            std::vector<long> borderCnt(K, 0);
            long borderTotal = 0;
            for (int x = 0; x < W; ++x) {
                borderCnt[label[0 * W + x]]++;
                borderCnt[label[(H - 1) * W + x]]++;
                borderTotal += 2;
            }
            for (int y = 1; y < H - 1; ++y) {
                borderCnt[label[y * W + 0]]++;
                borderCnt[label[y * W + (W - 1)]]++;
                borderTotal += 2;
            }
            int borderDominant = -1;
            long maxBorder = 0;
            for (int k = 0; k < K; ++k) {
                if (borderCnt[k] > maxBorder && borderCnt[k] >= borderTotal * 0.40 && cnt[k] >= tot * 0.15) {
                    maxBorder = borderCnt[k];
                    borderDominant = k;
                }
            }
            if (borderDominant >= 0) {
                background = borderDominant;
            } else {
                for (int k = 0; k < K; ++k) {
                    const double l = lum(cen[k]);
                    if ((l > 220 || l < 35) && cnt[k] > tot * 0.25) {
                        background = k;
                        break;
                    }
                }
            }
        }

        std::vector<QRgb> snappedColors(K);
        for (int k = 0; k < K; ++k) {
            const QColor c(int(std::round(cen[k].r)), int(std::round(cen[k].g)), int(std::round(cen[k].b)));
            snappedColors[k] = ThreadCatalog::snap(c, p.brand).color.rgb();
        }

        QImage out(W, H, QImage::Format_ARGB32);
        for (int y = 0; y < H; ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
            for (int x = 0; x < W; ++x) {
                int l = label[std::size_t(y)*W + x];
                if (l == background) {
                    const bool checker = ((x / 8) + (y / 8)) % 2 == 0;
                    line[x] = checker ? qRgba(35, 40, 48, 255) : qRgba(25, 28, 34, 255);
                } else {
                    line[x] = snappedColors[l];
                }
            }
        }
        return out;
    }

    QImage g = grayAnalysis(scaled, p);

    if (p.mode == Mode::LineArt) {
        if (OpenCvBridge::available())
            return OpenCvBridge::cannyEdges(g, 60, 160, 3).convertToFormat(QImage::Format_RGB32);
        const int t = (p.threshold >= 0) ? p.threshold : PhotoProcessor::otsu(g);
        QImage mask(W, H, QImage::Format_Grayscale8);
        for (int y = 0; y < H; ++y) {
            const uchar* gi = g.constScanLine(y);
            uchar* mo = mask.scanLine(y);
            for (int x = 0; x < W; ++x) {
                const bool dark = gi[x] < t;
                const bool fg = p.invert ? !dark : dark;
                mo[x] = fg ? 0 : 255;   // outlines drawn dark
            }
        }
        return PhotoProcessor::sobelEdges(mask, 64).convertToFormat(QImage::Format_RGB32);
    }

    // Portrait preview
    using PS = ImageDigitizer::PortraitStyle;
    QImage out(W, H, QImage::Format_Grayscale8);
    if (p.portraitStyle != PS::Tones) {
        QImage m;
        if (OpenCvBridge::available()) {
            switch (p.portraitStyle) {
            case PS::Silhouette: m = OpenCvBridge::silhouette(g, p.invert); break;
            case PS::Sketch:     m = OpenCvBridge::sketch(g, p.invert); break;
            case PS::Detailed:   m = OpenCvBridge::detailed(g, p.invert); break;
            case PS::Stylized:   m = OpenCvBridge::stylizedMask(scaled, p.invert); break;
            case PS::Comic:
            default:             m = OpenCvBridge::portraitComic(g, p.invert); break;
            }
        }
        if (m.isNull() || m.size() != g.size()) {
            const int t = (p.threshold >= 0) ? p.threshold : PhotoProcessor::otsu(g);
            m = PhotoProcessor::threshold(g, t, p.invert);
        }
        return m.convertToFormat(QImage::Format_RGB32);
    } else {
        const int tones = std::clamp(p.tones, 2, 6);
        out = PhotoProcessor::posterize(g, tones + 1);
        if (p.invert) {
            for (int y = 0; y < H; ++y) {
                uchar* line = out.scanLine(y);
                for (int x = 0; x < W; ++x) line[x] = 255 - line[x];
            }
        }
    }
    return out.convertToFormat(QImage::Format_RGB32);
}

} // namespace stick
