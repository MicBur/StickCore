// ---------------------------------------------------------------------------
//  StickCore  –  OpenCvBridge.h
//
//  Optional bridge to OpenCV. When the app is built WITH OpenCV
//  (STICK_HAVE_OPENCV defined by CMake), these give higher-quality photo
//  analysis than the native PhotoProcessor: real face detection / auto-crop,
//  Canny edges, adaptive threshold, contour extraction and a comic stylise.
//
//  When built WITHOUT OpenCV, available() returns false and every call is a
//  no-op that returns its input (or an empty result), so callers can fall back
//  to the native PhotoProcessor cleanly.
// ---------------------------------------------------------------------------
#pragma once

#include <QImage>
#include <QPolygonF>
#include <QVector>

namespace stick {

class OpenCvBridge {
public:
    /// True when the app was compiled with OpenCV support.
    static bool available();

    /// Detect the largest frontal face and return a head-and-shoulders crop.
    /// If no face is found (or OpenCV is absent) returns src unchanged and sets
    /// *found = false.
    static QImage autoFaceCrop(const QImage& src, bool* found = nullptr);

    /// Adaptive (local mean) threshold on a grayscale image. Foreground (ink)
    /// comes out as 0, background as 255. blockSize is forced odd and >= 3.
    static QImage adaptiveThreshold(const QImage& gray, int blockSize, int c, bool invert);

    /// Canny edges: edges drawn 0 (black) on a 255 (white) field. blurKsize is
    /// forced odd; 0 disables the pre-blur.
    static QImage cannyEdges(const QImage& gray, int lo, int hi, int blurKsize);

    /// Comic-style stylise (edge-preserving smoothing + quantised colour).
    static QImage stylize(const QImage& src);

    /// Edge-preserving bilateral smoothing for color images before color quantization.
    static QImage bilateralDenoise(const QImage& src, int d = 7, double sigmaColor = 50.0, double sigmaSpace = 50.0);

    /// High-quality single-tone "Konterfei" mask for a photo portrait: combines
    /// a solid dark silhouette (bilateral + Otsu) with adaptive detail edges so
    /// the result is both solid AND shows facial features. Returns a grayscale
    /// mask with ink = 0 (dark) and background = 255.
    static QImage portraitComic(const QImage& gray, bool invert,
                                double solidBias = 0.94,
                                int adaptiveBlock = 0 /* 0 = auto from size */,
                                int adaptiveC = 10);

    // --- Alternative single-tone portrait styles (all return ink=0/bg=255) ---
    /// Solid silhouette only (bilateral + Otsu) — bold, reduced. (Variant A)
    static QImage silhouette(const QImage& gray, bool invert, double bias = 1.0);
    /// Fine adaptive line drawing, no solid fill. (Variant B)
    static QImage sketch(const QImage& gray, bool invert);
    /// Detailed, photo-like (CLAHE local contrast + bilateral + Otsu). (Variant F)
    static QImage detailed(const QImage& gray, bool invert);
    /// Stylised comic look (edge-preserving stylise then Otsu). Needs colour.
    static QImage stylizedMask(const QImage& colorSrc, bool invert);

    /// Extract contours from a binary mask (foreground = dark, < 128). Returns
    /// external contours as pixel-space polylines, dropping any shorter than
    /// minLenPx of perimeter.
    static QVector<QPolygonF> findContours(const QImage& mask, double minLenPx);

    /// Removes background around logos, motifs and clipart based on corner colors
    /// (e.g. white paper or solid backdrop), returning an image where background is made pure white.
    static QImage removeBackground(const QImage& src, double tolerance = 28.0);

    /// Smooths raw raster contours using Douglas-Peucker polygon approximation,
    /// turning pixel staircases into clean vectors for embroidery.
    static QVector<QPolygonF> smoothContours(const QVector<QPolygonF>& contours, double epsilonPx = 1.2);
};

} // namespace stick
