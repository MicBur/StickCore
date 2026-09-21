// ---------------------------------------------------------------------------
//  StickCore  –  ContourFill.cpp
// ---------------------------------------------------------------------------
#include "generators/ContourFill.h"

#include <QImage>
#include <QPainter>
#include <QRectF>
#include <QTransform>
#include <vector>
#include <algorithm>
#include <cmath>

#ifdef STICK_HAVE_OPENCV
#include <opencv2/imgproc.hpp>
#include <opencv2/core.hpp>
#endif

namespace stick {

bool ContourFill::available()
{
    return true; // Available via native Chamfer distance transform & contour tracer!
}

namespace {

struct Point2D { float x, y; };

static const int dx8[8] = { 1,  1,  0, -1, -1, -1, 0, 1 };
static const int dy8[8] = { 0,  1,  1,  1,  0, -1, -1, -1 };

void computeChamferDistance(const std::vector<uint8_t>& mask, int W, int H, std::vector<float>& dist)
{
    dist.assign(W * H, 1e9f);
    const float d1 = 1.0f;
    const float d2 = 1.41421356f;

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const int idx = y * W + x;
            if (mask[idx] == 0) {
                dist[idx] = 0.0f;
            } else {
                float m = dist[idx];
                if (x > 0) m = std::min(m, dist[idx - 1] + d1);
                if (y > 0) {
                    m = std::min(m, dist[idx - W] + d1);
                    if (x > 0)     m = std::min(m, dist[idx - W - 1] + d2);
                    if (x + 1 < W) m = std::min(m, dist[idx - W + 1] + d2);
                }
                dist[idx] = m;
            }
        }
    }

    for (int y = H - 1; y >= 0; --y) {
        for (int x = W - 1; x >= 0; --x) {
            const int idx = y * W + x;
            float m = dist[idx];
            if (x + 1 < W) m = std::min(m, dist[idx + 1] + d1);
            if (y + 1 < H) {
                m = std::min(m, dist[idx + W] + d1);
                if (x > 0)     m = std::min(m, dist[idx + W - 1] + d2);
                if (x + 1 < W) m = std::min(m, dist[idx + W + 1] + d2);
            }
            dist[idx] = m;
        }
    }
}

std::vector<std::vector<Point2D>> traceMooreContours(const std::vector<float>& dist, int W, int H, float L)
{
    std::vector<std::vector<Point2D>> loops;
    std::vector<uint8_t> bin(W * H, 0);
    for (int i = 0; i < W * H; ++i) bin[i] = (dist[i] >= L) ? 1 : 0;
    std::vector<uint8_t> visited(W * H, 0);

    for (int y = 1; y < H - 1; ++y) {
        for (int x = 1; x < W - 1; ++x) {
            const int idx = y * W + x;
            if (bin[idx] == 1 && visited[idx] == 0) {
                const bool isEdge = (bin[idx - 1] == 0 || bin[idx + 1] == 0 ||
                                     bin[idx - W] == 0 || bin[idx + W] == 0);
                if (!isEdge) continue;

                std::vector<Point2D> loop;
                int cx = x, cy = y;
                const int startX = x, startY = y;
                int dir = 0;
                int steps = 0;

                while (steps++ < 25000) {
                    visited[cy * W + cx] = 1;
                    loop.push_back({ (float)cx + 0.5f, (float)cy + 0.5f });

                    int nextDir = -1;
                    const int checkDir = (dir + 5) % 8;
                    for (int k = 0; k < 8; ++k) {
                        const int d = (checkDir + k) % 8;
                        const int nx = cx + dx8[d];
                        const int ny = cy + dy8[d];
                        if (nx >= 0 && nx < W && ny >= 0 && ny < H && bin[ny * W + nx] == 1) {
                            nextDir = d;
                            cx = nx; cy = ny; dir = d;
                            break;
                        }
                    }
                    if (nextDir == -1) break;
                    if (cx == startX && cy == startY && loop.size() >= 4) break;
                }
                if (loop.size() >= 6) loops.push_back(std::move(loop));
            }
        }
    }
    return loops;
}

} // namespace

StitchSequence ContourFill::generate(const QPainterPath& pathMm, const Params& p)
{
    StitchSequence seq;
    if (pathMm.elementCount() == 0) return seq;

    const QRectF bb = pathMm.boundingRect();
    if (bb.width() < 0.5 || bb.height() < 0.5) return seq;

    const double pad   = 2.0;                      // mm margin
    const double pxmm  = 12.0;                     // raster resolution (px per mm)
    const double minx = bb.left()  - pad, maxx = bb.right()  + pad;
    const double miny = bb.top()   - pad, maxy = bb.bottom() + pad;
    const int Wpx = int(std::ceil((maxx - minx) * pxmm));
    const int Hpx = int(std::ceil((maxy - miny) * pxmm));
    if (Wpx < 4 || Hpx < 4 || Wpx > 4000 || Hpx > 4000) return seq;

    // Rasterise glyphs (mm, +Y up) into mask
    QImage img(Wpx, Hpx, QImage::Format_Grayscale8);
    img.fill(0);
    {
        QPainter pr(&img);
        pr.setRenderHint(QPainter::Antialiasing, false);
        // mm -> px, flipping Y: x' = (x - minx)*pxmm, y' = (maxy - y)*pxmm
        pr.setTransform(QTransform(pxmm, 0, 0, -pxmm, -minx * pxmm, maxy * pxmm));
        pr.setBrush(Qt::white);
        pr.setPen(Qt::NoPen);
        pr.drawPath(pathMm);
        pr.end();
    }

#ifdef STICK_HAVE_OPENCV
    cv::Mat mask(Hpx, Wpx, CV_8UC1, img.bits(), img.bytesPerLine());
    cv::Mat bin;
    cv::threshold(mask, bin, 127, 255, cv::THRESH_BINARY);

    cv::Mat dist;
    cv::distanceTransform(bin, dist, cv::DIST_L2, 3);
    double maxDist = 0;
    cv::minMaxLoc(dist, nullptr, &maxDist);
    if (maxDist < 1.0) return seq;

    const double spacingPx = std::max(1.5, p.spacingMm * pxmm);
    const double runMm     = std::max(1.0, p.maxStitchMm);

    auto Xmm = [=](double xpx){ return minx + xpx / pxmm; };
    auto Ymm = [=](double ypx){ return maxy - ypx / pxmm; };

    std::vector<double> levels;
    if (p.outlineFirst) levels.push_back(0.6);
    for (double L = spacingPx * 0.5; L < maxDist; L += spacingPx)
        levels.push_back(L);

    for (double L : levels) {
        cv::Mat ring;
        cv::threshold(dist, ring, L, 255, cv::THRESH_BINARY);
        ring.convertTo(ring, CV_8UC1);
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(ring, contours, cv::RETR_CCOMP, cv::CHAIN_APPROX_SIMPLE);
        for (const auto& c : contours) {
            if (c.size() < 4) continue;
            if (cv::arcLength(c, true) < 3.0 * pxmm * p.spacingMm) continue;

            double px = c.front().x + 0.5, py = c.front().y + 0.5;
            seq.add(Xmm(px), Ymm(py), SF_Jump, p.colorIdx);
            double acc = 0.0;
            for (std::size_t i = 1; i <= c.size(); ++i) {
                const cv::Point& q = c[i % c.size()];
                const double qx = q.x + 0.5, qy = q.y + 0.5;
                const double dx = (qx - px) / pxmm, dy = (qy - py) / pxmm;
                acc += std::sqrt(dx*dx + dy*dy);
                if (acc >= runMm || i == c.size()) {
                    seq.add(Xmm(qx), Ymm(qy), SF_Normal, p.colorIdx);
                    acc = 0.0;
                }
                px = qx; py = qy;
            }
        }
    }
#else
    // High-performance native C++ Chamfer Distance Transform fallback
    std::vector<uint8_t> mask(Wpx * Hpx);
    for (int y = 0; y < Hpx; ++y) {
        const uchar* line = img.constScanLine(y);
        for (int x = 0; x < Wpx; ++x) {
            mask[y * Wpx + x] = (line[x] > 127) ? 255 : 0;
        }
    }

    std::vector<float> dist;
    computeChamferDistance(mask, Wpx, Hpx, dist);
    float maxDist = 0.0f;
    for (float d : dist) if (d > maxDist) maxDist = d;
    if (maxDist < 1.0f) return seq;

    const double spacingPx = std::max(1.5, p.spacingMm * pxmm);
    const double runMm     = std::max(1.0, p.maxStitchMm);

    auto Xmm = [=](double xpx){ return minx + xpx / pxmm; };
    auto Ymm = [=](double ypx){ return maxy - ypx / pxmm; };

    std::vector<double> levels;
    if (p.outlineFirst) levels.push_back(0.6);
    for (double L = spacingPx * 0.5; L < maxDist; L += spacingPx)
        levels.push_back(L);

    for (double L : levels) {
        auto loops = traceMooreContours(dist, Wpx, Hpx, (float)L);
        for (const auto& loop : loops) {
            if (loop.size() < 4) continue;
            seq.add(Xmm(loop[0].x), Ymm(loop[0].y), SF_Jump, p.colorIdx);
            double acc = 0.0;
            float px = loop[0].x, py = loop[0].y;
            for (std::size_t i = 1; i <= loop.size(); ++i) {
                const auto& pt = loop[i % loop.size()];
                const double dMm = std::hypot(pt.x - px, pt.y - py) / pxmm;
                acc += dMm;
                if (acc >= runMm || i == loop.size()) {
                    seq.add(Xmm(pt.x), Ymm(pt.y), SF_Normal, p.colorIdx);
                    acc = 0.0;
                }
                px = pt.x; py = pt.y;
            }
        }
    }
#endif

    return seq;
}

} // namespace stick
