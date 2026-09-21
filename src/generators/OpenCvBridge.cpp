// ---------------------------------------------------------------------------
//  StickCore  –  OpenCvBridge.cpp
// ---------------------------------------------------------------------------
#include "generators/OpenCvBridge.h"

#ifdef STICK_HAVE_OPENCV

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>
#include <opencv2/photo.hpp>

#include <QFile>
#include <QTemporaryFile>
#include <QStandardPaths>
#include <QDir>
#include <algorithm>

namespace stick {

namespace {

// QImage -> BGR cv::Mat (deep copy, owns its data).
cv::Mat toBgr(const QImage& in)
{
    QImage img = in.convertToFormat(QImage::Format_RGB888);
    cv::Mat rgb(img.height(), img.width(), CV_8UC3,
                const_cast<uchar*>(img.constBits()),
                static_cast<size_t>(img.bytesPerLine()));
    cv::Mat bgr;
    cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);
    return bgr;              // cvtColor allocates fresh data
}

// QImage -> single channel gray cv::Mat (deep copy).
cv::Mat toGrayMat(const QImage& in)
{
    QImage img = in.convertToFormat(QImage::Format_Grayscale8);
    cv::Mat g(img.height(), img.width(), CV_8UC1,
              const_cast<uchar*>(img.constBits()),
              static_cast<size_t>(img.bytesPerLine()));
    return g.clone();
}

// 8UC1 cv::Mat -> grayscale QImage (deep copy).
QImage grayToQImage(const cv::Mat& m)
{
    QImage img(m.cols, m.rows, QImage::Format_Grayscale8);
    for (int y = 0; y < m.rows; ++y)
        std::memcpy(img.scanLine(y), m.ptr(y), size_t(m.cols));
    return img;
}

// BGR cv::Mat -> RGB QImage (deep copy).
QImage bgrToQImage(const cv::Mat& m)
{
    cv::Mat rgb;
    cv::cvtColor(m, rgb, cv::COLOR_BGR2RGB);
    QImage img(rgb.cols, rgb.rows, QImage::Format_RGB888);
    for (int y = 0; y < rgb.rows; ++y)
        std::memcpy(img.scanLine(y), rgb.ptr(y), size_t(rgb.cols) * 3);
    return img.copy();
}

// Remove isolated tiny ink blobs (background speckle) then thicken the ink by
// ~1px so thin detail survives the stitch row-sampling. ink: 0 = ink, 255 = bg.
void cleanInk(cv::Mat& ink, double minAreaDiv = 12000.0)
{
    cv::Mat fg;
    cv::bitwise_not(ink, fg);                     // ink = 255 for CC analysis
    cv::Mat labels, stats, cent;
    const int n = cv::connectedComponentsWithStats(fg, labels, stats, cent, 8);
    const double minArea = std::max(10.0, (double(ink.cols) * ink.rows) / minAreaDiv);
    cv::Mat keep = cv::Mat::zeros(fg.size(), CV_8UC1);
    for (int i = 1; i < n; ++i)
        if (stats.at<int>(i, cv::CC_STAT_AREA) >= minArea)
            keep.setTo(255, labels == i);
    cv::bitwise_not(keep, ink);
    cv::Mat k = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3,3));
    cv::erode(ink, ink, k);
}

// Load the bundled Haar cascade once (unpacked from the Qt resource to a temp
// file the first time, because CascadeClassifier reads from a path).
cv::CascadeClassifier& faceCascade()
{
    static cv::CascadeClassifier cc;
    static bool tried = false;
    if (!tried) {
        tried = true;
        QString path;
        // Prefer a system copy if present (dev build); else unpack the resource.
        const char* sys = "/usr/share/opencv4/haarcascades/haarcascade_frontalface_default.xml";
        if (QFile::exists(QString::fromLatin1(sys))) {
            path = QString::fromLatin1(sys);
        } else {
            QFile res(QStringLiteral(":/cv/haarcascade_frontalface_default.xml"));
            if (res.open(QIODevice::ReadOnly)) {
                const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
                const QString out = QDir(dir).filePath(QStringLiteral("stickcore_face.xml"));
                QFile o(out);
                if (o.open(QIODevice::WriteOnly)) { o.write(res.readAll()); o.close(); path = out; }
            }
        }
        if (!path.isEmpty()) cc.load(path.toStdString());
    }
    return cc;
}

} // namespace

bool OpenCvBridge::available() { return true; }

QImage OpenCvBridge::autoFaceCrop(const QImage& src, bool* found)
{
    if (found) *found = false;
    if (src.isNull()) return src;
    cv::CascadeClassifier& cc = faceCascade();
    if (cc.empty()) return src;

    cv::Mat gray = toGrayMat(src);
    cv::equalizeHist(gray, gray);
    std::vector<cv::Rect> faces;
    cc.detectMultiScale(gray, faces, 1.1, 5, 0, cv::Size(40, 40));
    if (faces.empty()) return src;

    // largest face
    cv::Rect f = *std::max_element(faces.begin(), faces.end(),
        [](const cv::Rect& a, const cv::Rect& b){ return a.area() < b.area(); });

    // widen for hair / chin / shoulders
    const int mx = int(f.width  * 0.35);
    const int myTop = int(f.height * 0.55);
    const int myBot = int(f.height * 0.45);
    int x0 = std::max(0, f.x - mx);
    int y0 = std::max(0, f.y - myTop);
    int x1 = std::min(src.width(),  f.x + f.width  + mx);
    int y1 = std::min(src.height(), f.y + f.height + myBot);
    if (x1 - x0 < 8 || y1 - y0 < 8) return src;

    if (found) *found = true;
    return src.copy(QRect(x0, y0, x1 - x0, y1 - y0));
}

QImage OpenCvBridge::adaptiveThreshold(const QImage& gray, int blockSize, int c, bool invert)
{
    if (gray.isNull()) return gray;
    cv::Mat g = toGrayMat(gray);
    cv::medianBlur(g, g, 5);
    if (blockSize < 3) blockSize = 3;
    if ((blockSize & 1) == 0) ++blockSize;
    cv::Mat out;
    cv::adaptiveThreshold(g, out, 255, cv::ADAPTIVE_THRESH_MEAN_C,
                          invert ? cv::THRESH_BINARY_INV : cv::THRESH_BINARY,
                          blockSize, c);
    return grayToQImage(out);
}

QImage OpenCvBridge::cannyEdges(const QImage& gray, int lo, int hi, int blurKsize)
{
    if (gray.isNull()) return gray;
    cv::Mat g = toGrayMat(gray);
    if (blurKsize >= 3) {
        if ((blurKsize & 1) == 0) ++blurKsize;
        cv::GaussianBlur(g, g, cv::Size(blurKsize, blurKsize), 0);
    }
    cv::Mat edges;
    cv::Canny(g, edges, lo, hi);
    cv::Mat inv;
    cv::bitwise_not(edges, inv);          // edges black on white
    return grayToQImage(inv);
}

QImage OpenCvBridge::stylize(const QImage& src)
{
    if (src.isNull()) return src;
    cv::Mat bgr = toBgr(src);
    cv::Mat out;
    cv::stylization(bgr, out, 60.0f, 0.45f);
    return bgrToQImage(out);
}

QImage OpenCvBridge::portraitComic(const QImage& gray, bool invert,
                                   double solidBias, int adaptiveBlock, int adaptiveC)
{
    if (gray.isNull()) return gray;
    cv::Mat g = toGrayMat(gray);
    if (invert) cv::bitwise_not(g, g);

    // edge-preserving smoothing so skin flattens but features stay
    cv::Mat bil;
    cv::bilateralFilter(g, bil, 9, 90, 90);

    // solid dark silhouette: everything below (biased) Otsu becomes ink
    cv::Mat tmp;
    const double t = cv::threshold(bil, tmp, 0, 255, cv::THRESH_BINARY + cv::THRESH_OTSU);
    cv::Mat solid;
    cv::threshold(bil, solid, t * solidBias, 255, cv::THRESH_BINARY);   // bg=255, ink=0

    // adaptive detail edges (eyes, nose, mouth, wrinkles, beard).
    // Block size scales with the image so the same detail appears at any
    // working resolution (adaptiveBlock <= 0 means "auto").
    if (adaptiveBlock <= 0) adaptiveBlock = std::max(g.cols, g.rows) / 22;
    if (adaptiveBlock < 5) adaptiveBlock = 5;
    if ((adaptiveBlock & 1) == 0) ++adaptiveBlock;
    cv::Mat med, adapt;
    cv::medianBlur(g, med, 5);
    cv::adaptiveThreshold(med, adapt, 255, cv::ADAPTIVE_THRESH_MEAN_C,
                          cv::THRESH_BINARY, adaptiveBlock, adaptiveC);

    // ink where EITHER is dark (bitwise AND on 0/255 masks = union of the 0s)
    cv::Mat ink;
    cv::bitwise_and(solid, adapt, ink);
    cleanInk(ink);
    return grayToQImage(ink);
}

QImage OpenCvBridge::silhouette(const QImage& gray, bool invert, double bias)
{
    if (gray.isNull()) return gray;
    cv::Mat g = toGrayMat(gray);
    if (invert) cv::bitwise_not(g, g);
    cv::Mat bil; cv::bilateralFilter(g, bil, 9, 90, 90);
    cv::Mat tmp;
    const double t = cv::threshold(bil, tmp, 0, 255, cv::THRESH_BINARY + cv::THRESH_OTSU);
    cv::Mat ink;
    cv::threshold(bil, ink, t * bias, 255, cv::THRESH_BINARY);   // bg=255, ink=0
    cleanInk(ink, 6000.0);   // solid shapes: clear more speckle
    return grayToQImage(ink);
}

QImage OpenCvBridge::sketch(const QImage& gray, bool invert)
{
    if (gray.isNull()) return gray;
    cv::Mat g = toGrayMat(gray);
    if (invert) cv::bitwise_not(g, g);
    int block = std::max(g.cols, g.rows) / 22; if (block < 5) block = 5;
    if ((block & 1) == 0) ++block;
    cv::Mat med, adapt;
    cv::medianBlur(g, med, 5);
    cv::adaptiveThreshold(med, adapt, 255, cv::ADAPTIVE_THRESH_MEAN_C,
                          cv::THRESH_BINARY, block, 9);
    cleanInk(adapt);
    return grayToQImage(adapt);
}

QImage OpenCvBridge::detailed(const QImage& gray, bool invert)
{
    if (gray.isNull()) return gray;
    cv::Mat g = toGrayMat(gray);
    if (invert) cv::bitwise_not(g, g);
    cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(2.5, cv::Size(8, 8));
    cv::Mat cl; clahe->apply(g, cl);
    cv::Mat bil; cv::bilateralFilter(cl, bil, 9, 75, 75);
    cv::Mat ink;
    cv::threshold(bil, ink, 0, 255, cv::THRESH_BINARY + cv::THRESH_OTSU);
    cleanInk(ink, 9000.0);
    return grayToQImage(ink);
}

QImage OpenCvBridge::stylizedMask(const QImage& colorSrc, bool invert)
{
    if (colorSrc.isNull()) return colorSrc;
    cv::Mat bgr = toBgr(colorSrc);
    cv::Mat st; cv::stylization(bgr, st, 60.0f, 0.45f);
    cv::Mat g; cv::cvtColor(st, g, cv::COLOR_BGR2GRAY);
    if (invert) cv::bitwise_not(g, g);
    cv::Mat ink;
    cv::threshold(g, ink, 0, 255, cv::THRESH_BINARY + cv::THRESH_OTSU);
    cleanInk(ink, 9000.0);
    return grayToQImage(ink);
}

QVector<QPolygonF> OpenCvBridge::findContours(const QImage& mask, double minLenPx)
{
    QVector<QPolygonF> result;
    if (mask.isNull()) return result;
    cv::Mat g = toGrayMat(mask);
    // foreground = dark; make a 0/255 mask with fg = 255 for findContours
    cv::Mat bin;
    cv::threshold(g, bin, 128, 255, cv::THRESH_BINARY_INV);
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(bin, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
    for (auto& c : contours) {
        if (c.size() < 2) continue;
        const double len = cv::arcLength(c, true);
        if (len < minLenPx) continue;
        QPolygonF poly;
        poly.reserve(int(c.size()));
        for (auto& p : c) poly << QPointF(p.x, p.y);
        result.push_back(std::move(poly));
    }
    return result;
}

} // namespace stick

#else   // ---------------- no OpenCV: safe fallbacks --------------------------

namespace stick {

bool    OpenCvBridge::available() { return false; }
QImage  OpenCvBridge::autoFaceCrop(const QImage& src, bool* found) { if (found) *found = false; return src; }
QImage  OpenCvBridge::adaptiveThreshold(const QImage& gray, int, int, bool) { return gray; }
QImage  OpenCvBridge::cannyEdges(const QImage& gray, int, int, int) { return gray; }
QImage  OpenCvBridge::stylize(const QImage& src) { return src; }
QImage  OpenCvBridge::portraitComic(const QImage& gray, bool, double, int, int) { return gray; }
QImage  OpenCvBridge::silhouette(const QImage& gray, bool, double) { return gray; }
QImage  OpenCvBridge::sketch(const QImage& gray, bool) { return gray; }
QImage  OpenCvBridge::detailed(const QImage& gray, bool) { return gray; }
QImage  OpenCvBridge::stylizedMask(const QImage& src, bool) { return src; }
QVector<QPolygonF> OpenCvBridge::findContours(const QImage&, double) { return {}; }

} // namespace stick

#endif
