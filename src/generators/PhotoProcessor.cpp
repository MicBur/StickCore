// ---------------------------------------------------------------------------
//  StickCore  –  PhotoProcessor.cpp
//
//  Native image-processing primitives (no OpenCV). Everything works on an
//  8-bit grayscale QImage (Format_Grayscale8) so the steps chain cleanly.
// ---------------------------------------------------------------------------
#include "generators/PhotoProcessor.h"

#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <vector>

namespace stick {

QImage PhotoProcessor::toGray(const QImage& src)
{
    if (src.isNull()) return QImage();
    // Qt's built-in conversion uses the standard luma weights.
    return src.convertToFormat(QImage::Format_Grayscale8);
}

QImage PhotoProcessor::boxBlur(const QImage& gray8, int radius)
{
    if (gray8.isNull() || radius < 1) return gray8;
    QImage in = gray8.format() == QImage::Format_Grayscale8
                    ? gray8 : gray8.convertToFormat(QImage::Format_Grayscale8);
    const int W = in.width(), H = in.height();
    const int win = 2 * radius + 1;

    // horizontal pass -> tmp
    std::vector<quint8> tmp(std::size_t(W) * H);
    for (int y = 0; y < H; ++y) {
        const uchar* line = in.constScanLine(y);
        int sum = 0;
        // prime the window at x = 0 (clamp edges)
        for (int k = -radius; k <= radius; ++k)
            sum += line[std::clamp(k, 0, W - 1)];
        for (int x = 0; x < W; ++x) {
            tmp[std::size_t(y) * W + x] = quint8(sum / win);
            const int xout = std::clamp(x - radius,     0, W - 1);
            const int xin  = std::clamp(x + radius + 1, 0, W - 1);
            sum += line[xin] - line[xout];
        }
    }

    // vertical pass -> out
    QImage out(W, H, QImage::Format_Grayscale8);
    for (int x = 0; x < W; ++x) {
        int sum = 0;
        for (int k = -radius; k <= radius; ++k)
            sum += tmp[std::size_t(std::clamp(k, 0, H - 1)) * W + x];
        for (int y = 0; y < H; ++y) {
            out.scanLine(y)[x] = quint8(sum / win);
            const int yout = std::clamp(y - radius,     0, H - 1);
            const int yin  = std::clamp(y + radius + 1, 0, H - 1);
            sum += tmp[std::size_t(yin) * W + x] - tmp[std::size_t(yout) * W + x];
        }
    }
    return out;
}

QImage PhotoProcessor::contrast(const QImage& gray8, double amount)
{
    if (gray8.isNull()) return gray8;
    QImage in = gray8.format() == QImage::Format_Grayscale8
                    ? gray8 : gray8.convertToFormat(QImage::Format_Grayscale8);
    // Precompute a lookup table: v' = clamp( (v-128)*amount + 128 )
    quint8 lut[256];
    for (int v = 0; v < 256; ++v) {
        const double nv = (v - 128.0) * amount + 128.0;
        lut[v] = quint8(std::clamp(int(std::lround(nv)), 0, 255));
    }
    QImage out = in.copy();
    const int W = out.width(), H = out.height();
    for (int y = 0; y < H; ++y) {
        uchar* line = out.scanLine(y);
        for (int x = 0; x < W; ++x) line[x] = lut[line[x]];
    }
    return out;
}

int PhotoProcessor::otsu(const QImage& gray8)
{
    if (gray8.isNull()) return 128;
    QImage in = gray8.format() == QImage::Format_Grayscale8
                    ? gray8 : gray8.convertToFormat(QImage::Format_Grayscale8);
    const int W = in.width(), H = in.height();

    long hist[256] = {0};
    for (int y = 0; y < H; ++y) {
        const uchar* line = in.constScanLine(y);
        for (int x = 0; x < W; ++x) hist[line[x]]++;
    }
    const long total = long(W) * H;
    if (total == 0) return 128;

    double sumAll = 0;
    for (int t = 0; t < 256; ++t) sumAll += double(t) * hist[t];

    double sumB = 0;      // weighted sum of background
    long   wB   = 0;      // background pixel count
    double maxVar = -1.0;
    int    lo = 128, hi = 128;   // range of t achieving the maximum variance
    for (int t = 0; t < 256; ++t) {
        wB += hist[t];
        if (wB == 0) continue;
        const long wF = total - wB;
        if (wF == 0) break;
        sumB += double(t) * hist[t];
        const double mB = sumB / wB;
        const double mF = (sumAll - sumB) / wF;
        const double var = double(wB) * double(wF) * (mB - mF) * (mB - mF);
        if (var > maxVar)       { maxVar = var; lo = hi = t; }
        else if (var == maxVar) { hi = t; }         // extend the plateau across the gap
    }
    // For a clean bimodal image the optimum is a plateau across the empty gap;
    // returning its midpoint gives a threshold that sits between the two peaks.
    return (lo + hi) / 2;
}

QImage PhotoProcessor::threshold(const QImage& gray8, int t, bool invert)
{
    if (gray8.isNull()) return gray8;
    QImage in = gray8.format() == QImage::Format_Grayscale8
                    ? gray8 : gray8.convertToFormat(QImage::Format_Grayscale8);
    const int W = in.width(), H = in.height();
    QImage out(W, H, QImage::Format_Grayscale8);
    const quint8 hi = invert ? 0   : 255;
    const quint8 lo = invert ? 255 : 0;
    for (int y = 0; y < H; ++y) {
        const uchar* si = in.constScanLine(y);
        uchar* so = out.scanLine(y);
        for (int x = 0; x < W; ++x) so[x] = (si[x] >= t) ? hi : lo;
    }
    return out;
}

QImage PhotoProcessor::posterize(const QImage& gray8, int levels)
{
    if (gray8.isNull()) return gray8;
    levels = std::clamp(levels, 2, 16);
    QImage in = gray8.format() == QImage::Format_Grayscale8
                    ? gray8 : gray8.convertToFormat(QImage::Format_Grayscale8);
    // Map each value to the nearest of `levels` evenly spaced tones (0..255).
    quint8 lut[256];
    for (int v = 0; v < 256; ++v) {
        int band = (v * levels) / 256;              // 0 .. levels-1
        if (band > levels - 1) band = levels - 1;
        lut[v] = quint8(band * 255 / (levels - 1));
    }
    QImage out = in.copy();
    const int W = out.width(), H = out.height();
    for (int y = 0; y < H; ++y) {
        uchar* line = out.scanLine(y);
        for (int x = 0; x < W; ++x) line[x] = lut[line[x]];
    }
    return out;
}

QImage PhotoProcessor::sobelEdges(const QImage& gray8, int threshold)
{
    if (gray8.isNull()) return gray8;
    QImage in = gray8.format() == QImage::Format_Grayscale8
                    ? gray8 : gray8.convertToFormat(QImage::Format_Grayscale8);
    const int W = in.width(), H = in.height();
    QImage out(W, H, QImage::Format_Grayscale8);
    out.fill(255);  // white background, edges will be drawn black

    auto at = [&](int x, int y) -> int {
        x = std::clamp(x, 0, W - 1);
        y = std::clamp(y, 0, H - 1);
        return in.constScanLine(y)[x];
    };

    for (int y = 0; y < H; ++y) {
        uchar* so = out.scanLine(y);
        for (int x = 0; x < W; ++x) {
            const int gx =
                -at(x-1,y-1) - 2*at(x-1,y) - at(x-1,y+1)
                +at(x+1,y-1) + 2*at(x+1,y) + at(x+1,y+1);
            const int gy =
                -at(x-1,y-1) - 2*at(x,y-1) - at(x+1,y-1)
                +at(x-1,y+1) + 2*at(x,y+1) + at(x+1,y+1);
            const int mag = int(std::lround(std::sqrt(double(gx*gx + gy*gy))));
            so[x] = (mag >= threshold) ? 0 : 255;
        }
    }
    return out;
}

std::vector<int> PhotoProcessor::filterSpeckles(const std::vector<int>& labels, int W, int H, int minArea)
{
    if (labels.size() != size_t(W) * H || minArea <= 1) return labels;

    std::vector<int> out = labels;
    std::vector<uint8_t> visited(size_t(W) * H, 0);

    std::vector<int> component;
    component.reserve(minArea + 10);

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const size_t idx = size_t(y) * W + x;
            if (visited[idx]) continue;

            const int curLabel = out[idx];
            component.clear();
            std::vector<std::pair<int, int>> queue;
            queue.push_back({x, y});
            visited[idx] = 1;

            std::vector<int> neighborLabels;

            size_t qHead = 0;
            while (qHead < queue.size()) {
                const int cx = queue[qHead].first;
                const int cy = queue[qHead].second;
                qHead++;
                component.push_back(cy * W + cx);

                static const int dx[4] = {0, 1, 0, -1};
                static const int dy[4] = {-1, 0, 1, 0};
                for (int d = 0; d < 4; ++d) {
                    const int nx = cx + dx[d];
                    const int ny = cy + dy[d];
                    if (nx >= 0 && nx < W && ny >= 0 && ny < H) {
                        const size_t nidx = size_t(ny) * W + nx;
                        if (out[nidx] == curLabel) {
                            if (!visited[nidx]) {
                                visited[nidx] = 1;
                                queue.push_back({nx, ny});
                            }
                        } else {
                            neighborLabels.push_back(out[nidx]);
                        }
                    }
                }
            }

            if (int(component.size()) < minArea && !neighborLabels.empty()) {
                std::sort(neighborLabels.begin(), neighborLabels.end());
                int bestNeighbor = neighborLabels[0];
                int bestCount = 0;
                int curCount = 0;
                int lastVal = neighborLabels[0];
                for (int nVal : neighborLabels) {
                    if (nVal == lastVal) {
                        curCount++;
                    } else {
                        if (curCount > bestCount) {
                            bestCount = curCount;
                            bestNeighbor = lastVal;
                        }
                        lastVal = nVal;
                        curCount = 1;
                    }
                }
                if (curCount > bestCount) {
                    bestNeighbor = lastVal;
                }

                for (int pidx : component) {
                    out[pidx] = bestNeighbor;
                }
            }
        }
    }
    return out;
}

QImage PhotoProcessor::dropCornerBackground(const QImage& src, int tolerance)
{
    if (src.isNull()) return src;
    QImage img = src.convertToFormat(QImage::Format_ARGB32);
    const int W = img.width(), H = img.height();
    if (W < 4 || H < 4) return src;

    const QRgb c00 = img.pixel(0, 0);
    const QRgb cW0 = img.pixel(W - 1, 0);
    const QRgb c0H = img.pixel(0, H - 1);
    const QRgb cWH = img.pixel(W - 1, H - 1);

    auto colorDist = [](QRgb a, QRgb b) -> double {
        const int dr = qRed(a) - qRed(b);
        const int dg = qGreen(a) - qGreen(b);
        const int db = qBlue(a) - qBlue(b);
        return std::sqrt(double(dr*dr + dg*dg + db*db));
    };

    const double tol = double(tolerance);
    int matchingCorners = 1;
    if (colorDist(c00, cW0) <= tol) matchingCorners++;
    if (colorDist(c00, c0H) <= tol) matchingCorners++;
    if (colorDist(c00, cWH) <= tol) matchingCorners++;
    if (matchingCorners < 3) {
        // Corners are not a uniform background (e.g. design touches edges)
        return src;
    }

    std::vector<uint8_t> visited(size_t(W) * H, 0);
    std::vector<std::pair<int, int>> queue;

    auto pushIfBg = [&](int x, int y) {
        const size_t idx = size_t(y) * W + x;
        if (!visited[idx] && colorDist(img.pixel(x, y), c00) <= tol) {
            visited[idx] = 1;
            queue.push_back({x, y});
        }
    };

    pushIfBg(0, 0);
    if (colorDist(cW0, c00) <= tol) pushIfBg(W - 1, 0);
    if (colorDist(c0H, c00) <= tol) pushIfBg(0, H - 1);
    if (colorDist(cWH, c00) <= tol) pushIfBg(W - 1, H - 1);

    size_t head = 0;
    static const int dx[4] = {0, 1, 0, -1};
    static const int dy[4] = {-1, 0, 1, 0};

    while (head < queue.size()) {
        const auto [cx, cy] = queue[head++];
        const QRgb curCol = img.pixel(cx, cy);
        img.setPixel(cx, cy, qRgb(255, 255, 255)); // pure white background

        for (int d = 0; d < 4; ++d) {
            const int nx = cx + dx[d], ny = cy + dy[d];
            if (nx >= 0 && nx < W && ny >= 0 && ny < H) {
                const size_t nidx = size_t(ny) * W + nx;
                if (!visited[nidx]) {
                    if (colorDist(img.pixel(nx, ny), curCol) <= tol ||
                        colorDist(img.pixel(nx, ny), c00) <= tol) {
                        visited[nidx] = 1;
                        queue.push_back({nx, ny});
                    }
                }
            }
        }
    }
    return img;
}

namespace {
double perpendicularDist(const QPointF& p, const QPointF& a, const QPointF& b) {
    const double dx = b.x() - a.x(), dy = b.y() - a.y();
    const double len2 = dx * dx + dy * dy;
    if (len2 < 1e-8) return std::hypot(p.x() - a.x(), p.y() - a.y());
    const double cross = std::abs((p.y() - a.y()) * dx - (p.x() - a.x()) * dy);
    return cross / std::sqrt(len2);
}

void rdpRecursive(const QVector<QPointF>& pts, int i0, int i1, double eps, QVector<bool>& keep) {
    if (i1 <= i0 + 1) return;
    double maxDist = 0.0;
    int maxIdx = i0;
    for (int i = i0 + 1; i < i1; ++i) {
        const double d = perpendicularDist(pts[i], pts[i0], pts[i1]);
        if (d > maxDist) {
            maxDist = d;
            maxIdx = i;
        }
    }
    if (maxDist > eps) {
        keep[maxIdx] = true;
        rdpRecursive(pts, i0, maxIdx, eps, keep);
        rdpRecursive(pts, maxIdx, i1, eps, keep);
    }
}
} // namespace

QPolygonF PhotoProcessor::smoothPolygon(const QPolygonF& poly, double epsilon)
{
    if (poly.size() < 4 || epsilon <= 0.01) return poly;
    QVector<QPointF> pts;
    pts.reserve(poly.size());
    for (const auto& pt : poly) pts.push_back(pt);

    QVector<bool> keep(pts.size(), false);
    keep[0] = true;
    keep[pts.size() - 1] = true;

    rdpRecursive(pts, 0, int(pts.size()) - 1, epsilon, keep);

    QPolygonF res;
    for (int i = 0; i < pts.size(); ++i) {
        if (keep[i]) res << pts[i];
    }
    return res;
}

} // namespace stick
