// ---------------------------------------------------------------------------
//  StickCore  –  ImageDigitizer.cpp
// ---------------------------------------------------------------------------
#include "generators/ImageDigitizer.h"
#include "generators/PhotoProcessor.h"
#include "generators/OpenCvBridge.h"
#include "generators/SatinGenerator.h"
#include "generators/TatamiFill.h"
#include "generators/Underlay.h"
#include "generators/SfumatoGenerator.h"
#include "generators/CrossStitchGenerator.h"
#include "generators/BeanStitchGenerator.h"
#include "core/ThreadCatalog.h"

#include <QPainterPath>
#include <QPoint>
#include <QPointF>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <unordered_map>
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
QImage rgbAdjust(const QImage& src, double contrast, int blur, double brightness, bool invert)
{
    if (src.isNull()) return src;
    QImage img = src.convertToFormat(QImage::Format_RGB32);
    if (invert) img.invertPixels();

    // Pre-calculate 8-bit LUT for contrast & brightness
    quint8 lut[256];
    for (int v = 0; v < 256; ++v) {
        double nv = (v - 128.0) * contrast + 128.0;
        nv += brightness * 255.0;
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

    if (blur > 0) {
        if (OpenCvBridge::available()) {
            img = OpenCvBridge::bilateralDenoise(img, 5 + blur * 2, 40.0, 40.0);
        } else {
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
//  Dual-grid boundary extraction: extracts closed vector polygons
//  (outer boundaries and inner holes) for a specific label value.
// -------------------------------------------------------------------------
QVector<QPolygonF> extractPolygonsForLabel(const std::vector<int>& label, int want,
                                          int W, int H, double widthMm,
                                          double smoothing, double pullCompMm,
                                          double fillAngleDeg)
{
    QVector<QPolygonF> result;
    if (W < 2 || H < 2) return result;

    const int stride = W + 1;
    std::unordered_multimap<int, int> nextVertex;

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (label[std::size_t(y) * W + x] != want) continue;

            // Top edge: (x, y) -> (x+1, y)
            if (y == 0 || label[std::size_t(y - 1) * W + x] != want) {
                nextVertex.emplace(y * stride + x, y * stride + (x + 1));
            }
            // Bottom edge: (x+1, y+1) -> (x, y+1)
            if (y == H - 1 || label[std::size_t(y + 1) * W + x] != want) {
                nextVertex.emplace((y + 1) * stride + (x + 1), (y + 1) * stride + x);
            }
            // Left edge: (x, y+1) -> (x, y)
            if (x == 0 || label[std::size_t(y) * W + (x - 1)] != want) {
                nextVertex.emplace((y + 1) * stride + x, y * stride + x);
            }
            // Right edge: (x+1, y) -> (x+1, y+1)
            if (x == W - 1 || label[std::size_t(y) * W + (x + 1)] != want) {
                nextVertex.emplace(y * stride + (x + 1), (y + 1) * stride + (x + 1));
            }
        }
    }

    if (nextVertex.empty()) return result;

    const double mmPerPx = widthMm / W;
    const double cxPx = W * 0.5, cyPx = H * 0.5;
    auto toMm = [=](double gx, double gy) -> QPointF {
        return QPointF((gx - cxPx) * mmPerPx, (cyPx - gy) * mmPerPx);
    };

    const double rad = fillAngleDeg * 3.14159265358979323846 / 180.0;
    const QPointF stitchDir(std::cos(rad), std::sin(rad));

    while (!nextVertex.empty()) {
        auto it = nextVertex.begin();
        int start = it->first;
        int curr = it->second;
        nextVertex.erase(it);

        std::vector<int> loop;
        loop.push_back(start);
        loop.push_back(curr);

        int safety = 0;
        const int maxSteps = stride * (H + 1) * 4;
        while (curr != start && safety++ < maxSteps) {
            auto range = nextVertex.equal_range(curr);
            if (range.first == range.second) break; // dead end
            auto nxt = range.first;
            curr = nxt->second;
            nextVertex.erase(nxt);
            loop.push_back(curr);
        }

        if (curr == start && loop.size() >= 4) {
            QPolygonF poly;
            poly.reserve(int(loop.size()) - 1);
            for (size_t i = 0; i + 1 < loop.size(); ++i) {
                int vid = loop[i];
                int gx = vid % stride;
                int gy = vid / stride;
                poly << toMm(gx, gy);
            }

            // Douglas-Peucker vector smoothing
            if (smoothing > 0.05) {
                poly = PhotoProcessor::smoothPolygon(poly, smoothing * mmPerPx);
            }

            if (poly.size() >= 3) {
                // Apply Pull Compensation (Zugausgleich) along stitch direction
                if (pullCompMm > 0.01) {
                    // Compute signed area to know outer vs hole
                    double area = 0.0;
                    for (int i = 0; i < poly.size(); ++i) {
                        const QPointF& p1 = poly[i];
                        const QPointF& p2 = poly[(i + 1) % poly.size()];
                        area += (p1.x() * p2.y() - p2.x() * p1.y());
                    }
                    const double orientation = (area >= 0.0) ? 1.0 : -1.0;

                    QPolygonF pullPoly;
                    pullPoly.reserve(poly.size());
                    for (int i = 0; i < poly.size(); ++i) {
                        const QPointF& prev = poly[(i - 1 + poly.size()) % poly.size()];
                        const QPointF& next = poly[(i + 1) % poly.size()];
                        QPointF edge = next - prev;
                        double len = std::hypot(edge.x(), edge.y());
                        if (len > 1e-5) {
                            QPointF normal(-edge.y() / len, edge.x() / len);
                            normal *= orientation;
                            double align = std::abs(normal.x() * stitchDir.x() + normal.y() * stitchDir.y());
                            pullPoly << (poly[i] + normal * (pullCompMm * align));
                        } else {
                            pullPoly << poly[i];
                        }
                    }
                    poly = std::move(pullPoly);
                }

                result.push_back(std::move(poly));
            }
        }
    }

    return result;
}

// Moore contour tracing fallback for simple silhouette edges
std::vector<std::vector<QPoint>>
traceContours(const std::vector<quint8>& mask, int W, int H)
{
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
            if (fg(x - 1, y)) continue;

            std::vector<QPoint> c;
            const int sx = x, sy = y;
            int px = x, py = y;
            int b  = 6;
            c.push_back(QPoint(px, py));
            visited[std::size_t(py)*W + px] = 1;

            long it = 0;
            while (it++ < maxIter) {
                bool moved = false;
                for (int i = 1; i <= 8; ++i) {
                    const int d  = (b + i) & 7;
                    const int nx = px + dx[d], ny = py + dy[d];
                    if (fg(nx, ny)) {
                        b  = (d + 4) & 7;
                        px = nx; py = ny;
                        moved = true;
                        break;
                    }
                }
                if (!moved) break;
                if (px == sx && py == sy) break;
                c.push_back(QPoint(px, py));
                visited[std::size_t(py)*W + px] = 1;
            }
            if (c.size() >= 8) contours.push_back(std::move(c));
        }
    }
    return contours;
}

// Quantization helper shared across generate, preview and palette extraction
struct InternalQuantized {
    int W = 0, H = 0;
    int K = 0;
    std::vector<RGB> centers;
    std::vector<int> labels;
    std::vector<long> counts;
    int backgroundIdx = -1;
};

InternalQuantized runQuantization(const QImage& src, const ImageDigitizer::Params& p)
{
    InternalQuantized q;
    QImage img = analysisScaled(src, p.maxProcessPx);
    img = rgbAdjust(img, p.contrast, p.blur, p.brightness, p.invert);
    q.W = img.width();
    q.H = img.height();
    if (q.W < 2 || q.H < 2) return q;

    const int totalPx = q.W * q.H;
    q.labels.assign(totalPx, 0);

    // =======================================================================
    // 1. Single-Color Stencil / Silhouette Mode (p.colors == 1)
    // =======================================================================
    if (p.colors == 1) {
        q.K = 2;
        q.backgroundIdx = 1; // background is dropped
        q.centers.resize(2);

        QImage gray = PhotoProcessor::toGray(img);

        // Analyze border backdrop luminance
        double borderSum = 0;
        int borderCount = 0;
        const uchar* topRow = gray.constScanLine(0);
        const uchar* botRow = gray.constScanLine(q.H - 1);
        for (int x = 0; x < q.W; ++x) {
            borderSum += topRow[x] + botRow[x];
            borderCount += 2;
        }
        for (int y = 1; y < q.H - 1; ++y) {
            const uchar* row = gray.constScanLine(y);
            borderSum += row[0] + row[q.W - 1];
            borderCount += 2;
        }
        const double bgLum = (borderCount > 0) ? (borderSum / borderCount) : 210.0;
        const bool lightBg = (bgLum >= 128.0);

        int t = p.stencilThreshold;
        if (t < 0) {
            t = PhotoProcessor::otsu(gray);
        }

        long fgCount = 0;
        double sumR = 0, sumG = 0, sumB = 0;
        for (int y = 0; y < q.H; ++y) {
            const uchar* gLine = gray.constScanLine(y);
            const QRgb* rgbLine = reinterpret_cast<const QRgb*>(img.constScanLine(y));
            for (int x = 0; x < q.W; ++x) {
                const int idx = y * q.W + x;
                // If light background, dark pixels are foreground.
                // If dark background, light pixels are foreground (e.g. skull bone on dark background).
                const bool isFgPixel = lightBg ? (gLine[x] < t) : (gLine[x] >= t);
                const bool isFg = p.invert ? !isFgPixel : isFgPixel;
                if (isFg) {
                    q.labels[idx] = 0;
                    fgCount++;
                    sumR += qRed(rgbLine[x]);
                    sumG += qGreen(rgbLine[x]);
                    sumB += qBlue(rgbLine[x]);
                } else {
                    q.labels[idx] = 1;
                }
            }
        }

        q.labels = PhotoProcessor::filterSpeckles(q.labels, q.W, q.H, std::max(8, p.minSpeckleArea));

        q.counts.assign(2, 0);
        for (int l : q.labels) q.counts[l]++;

        if (fgCount > 0) {
            q.centers[0] = { sumR / fgCount, sumG / fgCount, sumB / fgCount };
            if (lum(q.centers[0]) > 200) q.centers[0] = { double(p.ink.red()), double(p.ink.green()), double(p.ink.blue()) };
        } else {
            q.centers[0] = { double(p.ink.red()), double(p.ink.green()), double(p.ink.blue()) };
        }
        q.centers[1] = { 245, 245, 245 };
        return q;
    }

    // =======================================================================
    // 2. Brightness Steps / Tones (Inkscape Multicolor Posterize)
    // =======================================================================
    if (p.separation == ImageDigitizer::SeparationMode::Tones) {
        const int numTones = std::clamp(p.colors, 2, 8);
        q.K = numTones + 1; // 0..numTones-1 are foreground tones, numTones is background
        q.backgroundIdx = numTones;
        q.centers.resize(q.K);

        QImage gray = PhotoProcessor::toGray(img);

        // Analyze border backdrop luminance
        double borderSum = 0;
        int borderCount = 0;
        const uchar* topRow = gray.constScanLine(0);
        const uchar* botRow = gray.constScanLine(q.H - 1);
        for (int x = 0; x < q.W; ++x) {
            borderSum += topRow[x] + botRow[x];
            borderCount += 2;
        }
        for (int y = 1; y < q.H - 1; ++y) {
            const uchar* row = gray.constScanLine(y);
            borderSum += row[0] + row[q.W - 1];
            borderCount += 2;
        }
        const double bgLum = (borderCount > 0) ? (borderSum / borderCount) : 210.0;
        const bool lightBg = (bgLum >= 128.0);

        const int otsuT = PhotoProcessor::otsu(gray);
        const int bgCutoff = p.dropBackground
            ? (lightBg ? int(std::min(245.0, bgLum - 12)) : int(std::max(12.0, bgLum + 12)))
            : (lightBg ? 256 : -1);

        for (int y = 0; y < q.H; ++y) {
            const uchar* gLine = gray.constScanLine(y);
            for (int x = 0; x < q.W; ++x) {
                const int idx = y * q.W + x;
                const int val = gLine[x];

                bool isBg = false;
                if (p.dropBackground) {
                    if (lightBg && val >= bgCutoff) isBg = true;
                    else if (!lightBg && val <= bgCutoff) isBg = true;
                }

                if (isBg) {
                    q.labels[idx] = numTones;
                } else {
                    double normVal = 0.0;
                    if (lightBg) {
                        normVal = std::clamp(double(val) / std::max(1, bgCutoff), 0.0, 0.999);
                    } else {
                        normVal = std::clamp(double(255 - val) / std::max(1, 255 - bgCutoff), 0.0, 0.999);
                    }
                    if (p.invert) normVal = 1.0 - normVal;
                    int tone = int(normVal * numTones);
                    tone = std::clamp(tone, 0, numTones - 1);
                    q.labels[idx] = tone;
                }
            }
        }

        q.labels = PhotoProcessor::filterSpeckles(q.labels, q.W, q.H, std::max(6, p.minSpeckleArea));

        q.counts.assign(q.K, 0);
        for (int l : q.labels) q.counts[l]++;

        // Compute color centers for each tone (dark shadow to bone/highlight)
        for (int k = 0; k < numTones; ++k) {
            double frac = (numTones > 1) ? double(k) / double(numTones - 1) : 0.0;
            int r = int(std::clamp(p.ink.red() + frac * (235 - p.ink.red()), 0.0, 255.0));
            int g = int(std::clamp(p.ink.green() + frac * (228 - p.ink.green()), 0.0, 255.0));
            int b = int(std::clamp(p.ink.blue() + frac * (210 - p.ink.blue()), 0.0, 255.0));
            q.centers[k] = { double(r), double(g), double(b) };
        }
        q.centers[numTones] = { bgLum, bgLum, bgLum };
        return q;
    }

    // =======================================================================
    // 3. K-Means Palette Separation (Color Artwork / Logos)
    // =======================================================================
    std::vector<RGB> px(std::size_t(q.W) * q.H);
    for (int y = 0; y < q.H; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < q.W; ++x)
            px[std::size_t(y)*q.W + x] = { double(qRed(line[x])), double(qGreen(line[x])), double(qBlue(line[x])) };
    }

    q.K = std::clamp(p.colors, 2, 16);
    q.centers.resize(q.K);
    for (int k = 0; k < q.K; ++k)
        q.centers[k] = px[std::size_t((k + 0.5) / q.K * px.size())];

    q.labels.assign(px.size(), 0);
    const int iterCount = 10;
    for (int iter = 0; iter < iterCount; ++iter) {
        for (std::size_t i = 0; i < px.size(); ++i) {
            double best = std::numeric_limits<double>::max(); int bi = 0;
            for (int k = 0; k < q.K; ++k){ double d = d2(px[i], q.centers[k]); if (d < best){best=d;bi=k;} }
            q.labels[i] = bi;
        }
        std::vector<RGB> sum(q.K, {0,0,0}); std::vector<long> cnt(q.K, 0);
        for (std::size_t i = 0; i < px.size(); ++i){
            sum[q.labels[i]].r += px[i].r; sum[q.labels[i]].g += px[i].g; sum[q.labels[i]].b += px[i].b; cnt[q.labels[i]]++;
        }
        for (int k = 0; k < q.K; ++k){
            if (cnt[k] > 0){ q.centers[k] = { sum[k].r/cnt[k], sum[k].g/cnt[k], sum[k].b/cnt[k] }; }
            else q.centers[k] = px[std::size_t(std::rand() % px.size())];
        }
    }

    q.labels = PhotoProcessor::filterSpeckles(q.labels, q.W, q.H, std::max(4, p.minSpeckleArea));

    q.counts.assign(q.K, 0);
    for (int l : q.labels) q.counts[l]++;

    if (p.dropBackground) {
        long tot = long(px.size());
        std::vector<long> borderCnt(q.K, 0);
        long borderTotal = 0;
        for (int x = 0; x < q.W; ++x) {
            borderCnt[q.labels[0 * q.W + x]]++;
            borderCnt[q.labels[(q.H - 1) * q.W + x]]++;
            borderTotal += 2;
        }
        for (int y = 1; y < q.H - 1; ++y) {
            borderCnt[q.labels[y * q.W + 0]]++;
            borderCnt[q.labels[y * q.W + (q.W - 1)]]++;
            borderTotal += 2;
        }
        int borderDominant = -1;
        long maxBorder = 0;
        for (int k = 0; k < q.K; ++k) {
            if (borderCnt[k] > maxBorder && borderCnt[k] >= borderTotal * 0.40 && q.counts[k] >= tot * 0.15) {
                maxBorder = borderCnt[k];
                borderDominant = k;
            }
        }
        if (borderDominant >= 0) {
            q.backgroundIdx = borderDominant;
        } else {
            for (int k = 0; k < q.K; ++k) {
                const double l = lum(q.centers[k]);
                if ((l > 220 || l < 35) && q.counts[k] > tot * 0.25) {
                    q.backgroundIdx = k;
                    break;
                }
            }
        }
    }

    return q;
}

} // namespace

// ===========================================================================
//  Mode and Style names
// ===========================================================================
QString ImageDigitizer::modeName(Mode m)
{
    switch (m) {
    case Mode::Colors:      return QStringLiteral("🎨 Vektor-Tatami (K-Means & Füllmuster)");
    case Mode::Sfumato:     return QStringLiteral("🌫️ Sfumato Photo-Stitch (Fotorealistischer Wellenstich)");
    case Mode::CrossStitch: return QStringLiteral("✖️ Traditioneller Kreuzstich (Aida-Raster)");
    case Mode::LineArt:     return QStringLiteral("✏️ LineArt & Kontur (Lauf- & Dreifachstich)");
    case Mode::Portrait:    return QStringLiteral("👤 Konterfei & Silhouette (Comic & Poster)");
    }
    return QStringLiteral("Vektor-Tatami");
}

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
//  Palette Extraction for UI Color Management
// ===========================================================================
std::vector<ImageDigitizer::QuantizedColor>
ImageDigitizer::extractPalette(const QImage& image, const Params& p)
{
    std::vector<QuantizedColor> palette;
    if (image.isNull()) return palette;

    InternalQuantized q = runQuantization(image, p);
    if (q.W < 2 || q.H < 2) return palette;

    const long totalPx = long(q.W) * q.H;
    palette.reserve(q.K);

    for (int k = 0; k < q.K; ++k) {
        QuantizedColor qc;
        qc.id = k;
        qc.rgb = QColor(int(std::round(q.centers[k].r)),
                        int(std::round(q.centers[k].g)),
                        int(std::round(q.centers[k].b)));
        qc.thread = ThreadCatalog::snap(qc.rgb, p.brand);
        qc.percentage = (totalPx > 0) ? (double(q.counts[k]) / double(totalPx) * 100.0) : 0.0;
        qc.isBackground = (k == q.backgroundIdx);

        if (!p.activeColors.empty() && size_t(k) < p.activeColors.size()) {
            qc.active = p.activeColors[k];
        } else {
            qc.active = !qc.isBackground;
        }

        palette.push_back(qc);
    }
    return palette;
}

// ===========================================================================
//  Colors mode (Vector-Tatami) — High-End Implementation
// ===========================================================================
static StitchSequence generateVectorTatami(const QImage& src, const ImageDigitizer::Params& p)
{
    StitchSequence seq;
    InternalQuantized q = runQuantization(src, p);
    if (q.W < 2 || q.H < 2) return seq;

    // Determine which colors are active
    std::vector<int> order;
    for (int k = 0; k < q.K; ++k) {
        bool isActive = (k != q.backgroundIdx);
        if (!p.activeColors.empty() && size_t(k) < p.activeColors.size()) {
            isActive = p.activeColors[k];
        }
        if (isActive && q.counts[k] > 0) {
            order.push_back(k);
        }
    }

    // Sort order from dark to light for natural embroidery depth
    std::sort(order.begin(), order.end(), [&](int a, int b){
        return lum(q.centers[a]) < lum(q.centers[b]);
    });
    if (order.empty()) return seq;

    for (size_t oi = 0; oi < order.size(); ++oi) {
        int colorK = order[oi];
        double angle = p.fillAngleDeg;
        if (p.multiAngle) {
            angle = p.fillAngleDeg + (oi * 45.0);
        }

        // 1. Vectorize boundaries & holes into clean polygons with pull compensation
        QVector<QPolygonF> region = extractPolygonsForLabel(
            q.labels, colorK, q.W, q.H, p.widthMm,
            p.smoothing, p.pullCompMm, angle);

        if (region.isEmpty()) continue;

        // 2. Generate world-class Tatami fill with chosen pattern and underlay
        TatamiFill::Params tp;
        tp.pattern      = p.tatamiPattern;
        tp.rowSpacingMm = (p.densityMm > 0.05) ? p.densityMm : 0.40;
        tp.maxStitchMm  = (p.maxStitchMm > 0.5) ? p.maxStitchMm : 4.0;
        tp.fillAngleDeg = angle;
        tp.underlay     = p.underlay;
        tp.colorIdx     = int(oi);

        StitchSequence colStitches = TatamiFill::generate(region, tp);
        if (!colStitches.stitches.empty()) {
            if (oi > 0) {
                colStitches.stitches.front().flags = SF_ColorChange;
            }
            seq.stitches.insert(seq.stitches.end(),
                                colStitches.stitches.begin(),
                                colStitches.stitches.end());
        }
    }

    // Populate palette
    for (int k : order) {
        const QColor c(int(std::round(q.centers[k].r)),
                       int(std::round(q.centers[k].g)),
                       int(std::round(q.centers[k].b)));
        seq.palette.push_back(ThreadCatalog::snap(c, p.brand));
    }

    // Optional raised satin border around motif boundary
    if (p.satinBorder && q.backgroundIdx >= 0) {
        std::vector<quint8> fgMask(size_t(q.W) * q.H, 0);
        for (size_t i = 0; i < q.labels.size(); ++i) {
            if (q.labels[i] != q.backgroundIdx) fgMask[i] = 255;
        }
        auto rawContours = traceContours(fgMask, q.W, q.H);
        const double mmPerPx = p.widthMm / q.W;
        const double cxPx = q.W * 0.5, cyPx = q.H * 0.5;

        for (const auto& c : rawContours) {
            if (c.size() < 6) continue;
            QPolygonF poly;
            for (const auto& pt : c)
                poly << QPointF((pt.x() - cxPx) * mmPerPx, (cyPx - pt.y()) * mmPerPx);
            poly = PhotoProcessor::smoothPolygon(poly, 0.8 * mmPerPx);
            if (poly.size() >= 3) {
                QPainterPath cPath;
                cPath.addPolygon(poly);
                cPath.closeSubpath();
                StitchSequence border = SatinGenerator::fromCenterline(
                    cPath, p.borderWidthMm, 0.40, int(order.size()));
                if (!border.stitches.empty()) {
                    border.stitches.front().flags = SF_ColorChange;
                    seq.stitches.insert(seq.stitches.end(), border.stitches.begin(), border.stitches.end());
                }
            }
        }
        seq.palette.push_back(ThreadCatalog::snap(QColor(25, 25, 30), p.brand));
    }

    return seq;
}

// ===========================================================================
//  Bitmap to Path (Inkscape-like Vector Trace)
// ===========================================================================
QVector<EditPath> ImageDigitizer::traceToPaths(const QImage& image, const Params& p)
{
    QVector<EditPath> paths;
    if (image.isNull()) return paths;

    InternalQuantized q = runQuantization(image, p);
    if (q.W < 2 || q.H < 2) return paths;

    std::vector<int> order;
    for (int k = 0; k < q.K; ++k) {
        bool isActive = (k != q.backgroundIdx);
        if (!p.activeColors.empty() && size_t(k) < p.activeColors.size()) {
            isActive = p.activeColors[k];
        }
        if (isActive && q.counts[k] > 0) {
            order.push_back(k);
        }
    }

    std::sort(order.begin(), order.end(), [&](int a, int b){
        return lum(q.centers[a]) < lum(q.centers[b]);
    });

    for (size_t oi = 0; oi < order.size(); ++oi) {
        int colorK = order[oi];
        QColor col(int(std::round(q.centers[colorK].r)),
                   int(std::round(q.centers[colorK].g)),
                   int(std::round(q.centers[colorK].b)));

        QVector<QPolygonF> region = extractPolygonsForLabel(
            q.labels, colorK, q.W, q.H, p.widthMm,
            p.smoothing, 0.0, 0.0);

        for (const QPolygonF& poly : region) {
            if (poly.size() < 3) continue;

            EditPath ep;
            ep.closed = true;
            ep.colorIdx = int(oi);
            ep.color = col;

            const int n = poly.size();
            ep.nodes.reserve(n);
            for (int i = 0; i < n; ++i) {
                const QPointF& prev = poly[(i - 1 + n) % n];
                const QPointF& curr = poly[i];
                const QPointF& next = poly[(i + 1) % n];

                QPointF dPrev = curr - prev;
                QPointF dNext = next - curr;
                double lenPrev = std::hypot(dPrev.x(), dPrev.y());
                double lenNext = std::hypot(dNext.x(), dNext.y());

                BezierNode node(curr);
                double dot = (lenPrev > 1e-4 && lenNext > 1e-4)
                    ? (dPrev.x() * dNext.x() + dPrev.y() * dNext.y()) / (lenPrev * lenNext)
                    : 1.0;

                if (dot < 0.45) { // sharp corner (> ~63°)
                    node.ctrlIn = curr;
                    node.ctrlOut = curr;
                    node.smooth = false;
                } else {
                    QPointF tan = next - prev;
                    double tanLen = std::hypot(tan.x(), tan.y());
                    if (tanLen > 1e-4) {
                        QPointF uTan = tan / tanLen;
                        node.ctrlIn  = curr - uTan * (lenPrev * 0.33);
                        node.ctrlOut = curr + uTan * (lenNext * 0.33);
                        node.smooth  = true;
                    }
                }
                ep.nodes.push_back(node);
            }
            paths.push_back(std::move(ep));
        }
    }
    return paths;
}

// ===========================================================================
//  Sfumato Mode Delegate
// ===========================================================================
static StitchSequence generateSfumatoMode(const QImage& src, const ImageDigitizer::Params& p)
{
    SfumatoGenerator::Params sp;
    sp.widthMm         = p.widthMm;
    const double aspect = (src.width() > 0) ? (double(src.height()) / src.width()) : 1.0;
    sp.heightMm        = p.widthMm * aspect;
    sp.lineSpacingMm   = (p.sfumatoLineSpacingMm > 0.2) ? p.sfumatoLineSpacingMm : 1.4;
    sp.maxAmplitudeMm  = (p.sfumatoAmplitudeMm > 0.1) ? p.sfumatoAmplitudeMm : 0.9;
    sp.angleMode       = p.sfumatoAngle;
    sp.contrast        = p.contrast;
    sp.brightness      = p.brightness;
    sp.invertLuminance = p.invert;
    sp.threadColor     = p.sfumatoColor;
    return SfumatoGenerator::generate(src, sp);
}

// ===========================================================================
//  Cross-Stitch Mode Delegate
// ===========================================================================
static StitchSequence generateCrossStitchMode(const QImage& src, const ImageDigitizer::Params& p)
{
    CrossStitchGenerator::Params cp;
    cp.aida      = p.crossStitchAida;
    cp.style     = p.crossStitchStyle;
    cp.maxColors = std::clamp(p.colors, 2, 16);
    cp.skipWhite = p.dropBackground;
    return CrossStitchGenerator::generate(src, cp);
}

// ===========================================================================
//  LineArt mode with Running Stitch or Bean Stitch (Triple / Five-Pass)
// ===========================================================================
static StitchSequence generateLineArtMode(const QImage& src, const ImageDigitizer::Params& p)
{
    StitchSequence seq;
    QImage scaled = analysisScaled(src, p.maxProcessPx);
    const int W = scaled.width(), H = scaled.height();
    if (W < 2 || H < 2) return seq;

    QImage g = grayAnalysis(scaled, p);

    std::vector<std::vector<QPoint>> contours;
    if (OpenCvBridge::available()) {
        const int hi = std::clamp(p.edgeThreshold * 2, 60, 240);
        const int lo = std::clamp(p.edgeThreshold, 20, 120);
        QImage edges = OpenCvBridge::cannyEdges(g, lo, hi, 3);
        const double minLenPx = std::max(6.0, W * 0.04);
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
    auto toMm = [=](double px, double py) -> QPointF {
        return QPointF((px - cxPx) * mmPerPx, (cyPx - py) * mmPerPx);
    };

    if (p.lineArtStitch == ImageDigitizer::LineArtStitch::BeanStitch ||
        p.lineArtStitch == ImageDigitizer::LineArtStitch::FivePass) {
        // High-end Bean Stitch (Triple or 5-Pass)
        BeanStitchGenerator::Params bp;
        bp.stitchLengthMm = (p.runMm > 0.5) ? p.runMm : 2.5;
        bp.mode = (p.lineArtStitch == ImageDigitizer::LineArtStitch::FivePass)
                      ? BeanStitchGenerator::Mode::FivePass
                      : BeanStitchGenerator::Mode::TriplePass;
        bp.colorIdx = 0;
        bp.closed = true;

        for (const auto& c : contours) {
            QPolygonF poly;
            poly.reserve(int(c.size()));
            for (const auto& pt : c) poly << toMm(pt.x(), pt.y());
            poly = PhotoProcessor::smoothPolygon(poly, 0.6 * mmPerPx);
            if (poly.size() >= 3) {
                StitchSequence bs = BeanStitchGenerator::generate(poly, bp);
                seq.stitches.insert(seq.stitches.end(), bs.stitches.begin(), bs.stitches.end());
            }
        }
    } else {
        // Standard Single-Pass Running Stitch
        const double run = std::max(1.0, p.runMm);
        bool first = true;
        for (const auto& c : contours) {
            double acc = 0.0;
            const QPoint& p0 = c.front();
            seq.add(toMm(p0.x(), p0.y()).x(), toMm(p0.x(), p0.y()).y(),
                    first ? SF_Jump : SF_Jump, 0);
            first = false;
            double px = p0.x(), py = p0.y();
            for (std::size_t i = 1; i <= c.size(); ++i) {
                const QPoint& q = c[i % c.size()];
                double dx = (q.x() - px) * mmPerPx, dy = (q.y() - py) * mmPerPx;
                acc += std::hypot(dx, dy);
                if (acc >= run || i == c.size()) {
                    QPointF mmp = toMm(q.x(), q.y());
                    seq.add(mmp.x(), mmp.y(), SF_Normal, 0);
                    acc = 0.0;
                }
                px = q.x(); py = q.y();
            }
        }
    }

    seq.palette.push_back(ThreadCatalog::snap(p.ink, p.brand));
    return seq;
}

// ===========================================================================
//  Portrait mode — silhouette or posterized tones with Vector-Tatami
// ===========================================================================
static StitchSequence generatePortraitMode(const QImage& src, const ImageDigitizer::Params& p)
{
    using PS = ImageDigitizer::PortraitStyle;
    StitchSequence seq;
    const bool useOcv = OpenCvBridge::available();
    const bool singleTone = (p.portraitStyle != PS::Tones);
    const bool hires = singleTone && useOcv && p.portraitStyle != PS::Silhouette;
    const int procPx = hires ? std::max(p.maxProcessPx, 440) : p.maxProcessPx;
    QImage scaled = analysisScaled(src, procPx);
    const int W = scaled.width(), H = scaled.height();
    if (W < 2 || H < 2) return seq;

    QImage g = grayAnalysis(scaled, p);
    const int tones = singleTone ? 1 : std::clamp(p.tones, 2, 6);

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
            const int t = (p.threshold >= 0) ? p.threshold : PhotoProcessor::otsu(g);
            m = PhotoProcessor::threshold(g, t, p.invert);
        }
        for (int y = 0; y < H; ++y) {
            const uchar* line = m.constScanLine(y);
            for (int x = 0; x < W; ++x)
                label[std::size_t(y)*W + x] = (line[x] < 128) ? 1 : 0;
        }
    } else {
        QImage q = PhotoProcessor::posterize(g, tones + 1);
        for (int y = 0; y < H; ++y) {
            const uchar* line = q.constScanLine(y);
            for (int x = 0; x < W; ++x) {
                int band = int(std::lround(line[x] / 255.0 * tones));
                band = std::clamp(band, 0, tones);
                if (p.invert) band = tones - band;
                label[std::size_t(y)*W + x] = (band >= tones) ? 0 : (band + 1);
            }
        }
    }

    for (int layer = 1; layer <= tones; ++layer) {
        QVector<QPolygonF> region = extractPolygonsForLabel(
            label, layer, W, H, p.widthMm, p.smoothing, p.pullCompMm, p.fillAngleDeg);
        if (region.isEmpty()) continue;

        TatamiFill::Params tp;
        tp.pattern      = p.tatamiPattern;
        tp.rowSpacingMm = (p.densityMm > 0.05) ? p.densityMm : 0.45;
        tp.maxStitchMm  = p.maxStitchMm;
        tp.fillAngleDeg = p.fillAngleDeg;
        tp.underlay     = p.underlay;
        tp.colorIdx     = layer - 1;

        StitchSequence layerStitches = TatamiFill::generate(region, tp);
        if (!layerStitches.stitches.empty()) {
            if (layer > 1) {
                layerStitches.stitches.front().flags = SF_ColorChange;
            }
            seq.stitches.insert(seq.stitches.end(),
                                layerStitches.stitches.begin(),
                                layerStitches.stitches.end());
        }
    }

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
//  Main Generation Dispatcher
// ===========================================================================
StitchSequence ImageDigitizer::generate(const QImage& image, const Params& p)
{
    if (image.isNull()) return StitchSequence();
    QImage src = image;
    if (p.autoFaceCrop && OpenCvBridge::available())
        src = OpenCvBridge::autoFaceCrop(image);

    switch (p.mode) {
    case Mode::Sfumato:     return generateSfumatoMode(src, p);
    case Mode::CrossStitch: return generateCrossStitchMode(src, p);
    case Mode::LineArt:     return generateLineArtMode(src, p);
    case Mode::Portrait:    return generatePortraitMode(src, p);
    case Mode::Colors:
    default:                return generateVectorTatami(src, p);
    }
}

// ===========================================================================
//  Fast Preview Generation (< 20 ms)
// ===========================================================================
StitchSequence ImageDigitizer::generateFastPreview(const QImage& image, const Params& p)
{
    if (image.isNull()) return StitchSequence();
    Params fastP = p;
    fastP.maxProcessPx = std::min(p.maxProcessPx, 160);
    fastP.densityMm    = std::max(p.densityMm, 0.55);
    fastP.underlay     = false; // Skip underlay in interactive preview for instant speed
    fastP.sfumatoLineSpacingMm = std::max(p.sfumatoLineSpacingMm, 1.8);
    return generate(image, fastP);
}

// ===========================================================================
//  Preview: what the machine-analysis stage sees
// ===========================================================================
QImage ImageDigitizer::preview(const QImage& image, const Params& p)
{
    if (image.isNull()) return QImage();
    QImage source = image;
    if (p.autoFaceCrop && OpenCvBridge::available())
        source = OpenCvBridge::autoFaceCrop(image);

    if (p.mode == Mode::Colors) {
        InternalQuantized q = runQuantization(source, p);
        if (q.W < 2 || q.H < 2) return QImage();

        std::vector<QRgb> snappedColors(q.K);
        for (int k = 0; k < q.K; ++k) {
            const QColor c(int(std::round(q.centers[k].r)),
                           int(std::round(q.centers[k].g)),
                           int(std::round(q.centers[k].b)));
            snappedColors[k] = ThreadCatalog::snap(c, p.brand).color.rgb();
        }

        std::vector<bool> active(q.K, true);
        for (int k = 0; k < q.K; ++k) {
            if (k == q.backgroundIdx) active[k] = false;
            if (!p.activeColors.empty() && size_t(k) < p.activeColors.size()) {
                active[k] = p.activeColors[k];
            }
        }

        QImage out(q.W, q.H, QImage::Format_ARGB32);
        for (int y = 0; y < q.H; ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
            for (int x = 0; x < q.W; ++x) {
                int l = q.labels[std::size_t(y)*q.W + x];
                if (!active[l]) {
                    const bool checker = ((x / 8) + (y / 8)) % 2 == 0;
                    line[x] = checker ? qRgba(35, 40, 48, 255) : qRgba(25, 28, 34, 255);
                } else {
                    line[x] = snappedColors[l];
                }
            }
        }
        return out;
    }

    if (p.mode == Mode::Sfumato) {
        SfumatoGenerator::Params sp;
        sp.contrast = p.contrast;
        sp.brightness = p.brightness;
        sp.invertLuminance = p.invert;
        return SfumatoGenerator::preprocessImage(source, sp).convertToFormat(QImage::Format_RGB32);
    }

    if (p.mode == Mode::CrossStitch) {
        // Quick downscaled preview of the Aida grid blocks
        const int maxD = 96;
        QImage grid = source.scaled(maxD, maxD, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        return grid.convertToFormat(QImage::Format_RGB32);
    }

    QImage scaled = analysisScaled(source, p.maxProcessPx);
    const int W = scaled.width(), H = scaled.height();
    if (W < 2 || H < 2) return QImage();
    QImage g = grayAnalysis(scaled, p);

    if (p.mode == Mode::LineArt) {
        if (OpenCvBridge::available()) {
            const int hi = std::clamp(p.edgeThreshold * 2, 60, 240);
            const int lo = std::clamp(p.edgeThreshold, 20, 120);
            return OpenCvBridge::cannyEdges(g, lo, hi, 3).convertToFormat(QImage::Format_RGB32);
        }
        const int t = (p.threshold >= 0) ? p.threshold : PhotoProcessor::otsu(g);
        QImage mask(W, H, QImage::Format_Grayscale8);
        for (int y = 0; y < H; ++y) {
            const uchar* gi = g.constScanLine(y);
            uchar* mo = mask.scanLine(y);
            for (int x = 0; x < W; ++x) {
                const bool dark = gi[x] < t;
                const bool fg = p.invert ? !dark : dark;
                mo[x] = fg ? 0 : 255;
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

