// ---------------------------------------------------------------------------
//  StickCore  –  PhotoProcessor.h
//
//  Lightweight image-processing for photo digitizing — implemented natively
//  (no OpenCV dependency) so the app stays self-contained and easy to install.
//  Provides the pieces needed to turn a photo into something stitchable:
//  grayscale, blur, contrast, Otsu auto-threshold, posterize (tone reduction)
//  and Sobel edge detection.
// ---------------------------------------------------------------------------
#pragma once

#include <QImage>

namespace stick {

class PhotoProcessor {
public:
    /// 8-bit grayscale copy.
    static QImage toGray(const QImage& src);

    /// Separable box blur (approximate Gaussian); radius in pixels.
    static QImage boxBlur(const QImage& gray8, int radius);

    /// Contrast around mid-grey: amount 1.0 = unchanged, >1 = more contrast.
    static QImage contrast(const QImage& gray8, double amount);

    /// Otsu's automatic threshold (0..255) for a grayscale image.
    static int otsu(const QImage& gray8);

    /// Binarise: output 255 where value >= t, else 0 (optionally inverted).
    static QImage threshold(const QImage& gray8, int t, bool invert = false);

    /// Reduce to N distinct grey levels (tone posterisation).
    static QImage posterize(const QImage& gray8, int levels);

    /// Sobel edges: output 0 (black) on edges, 255 elsewhere.
    static QImage sobelEdges(const QImage& gray8, int threshold);
};

} // namespace stick
