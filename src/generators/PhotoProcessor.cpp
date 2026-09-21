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

} // namespace stick
