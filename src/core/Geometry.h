// ---------------------------------------------------------------------------
//  StickCore  –  Geometry.h
//
//  Small geometry toolkit: 2-D vector helpers plus an arc-length
//  re-parameterisation of a QPainterPath.  "Arc-length re-parameterisation"
//  means: given a path we build a lookup table so that we can ask for the
//  point at a fraction s in [0,1] of the *true length* (not of Qt's control
//  parameter), which is what a satin column needs to stay evenly spaced.
// ---------------------------------------------------------------------------
#pragma once

#include <QPainterPath>
#include <QPointF>
#include <QVector>
#include <cmath>

namespace stick {

// --- free vector helpers on QPointF -----------------------------------------
inline double dot(const QPointF& a, const QPointF& b)      { return a.x()*b.x() + a.y()*b.y(); }
inline double length(const QPointF& a)                     { return std::hypot(a.x(), a.y()); }
inline QPointF normalized(const QPointF& a)
{
    const double l = length(a);
    return (l > 1e-9) ? QPointF(a.x()/l, a.y()/l) : QPointF(0.0, 0.0);
}
/// Left-hand normal (90° CCW rotation) of a 2-D vector.
inline QPointF perpLeft(const QPointF& a)  { return QPointF(-a.y(),  a.x()); }
inline QPointF perpRight(const QPointF& a) { return QPointF( a.y(), -a.x()); }

// ---------------------------------------------------------------------------
//  ArcLengthCurve
//
//  Densely samples a QPainterPath once, stores cumulative lengths and offers
//  O(log n) lookup of position and tangent by normalised arc length.
// ---------------------------------------------------------------------------
class ArcLengthCurve {
public:
    ArcLengthCurve() = default;

    /// Build from a path. samplesPerUnit controls the flattening density
    /// (samples per mm of straight approximation). Values around 4-8 are ample.
    explicit ArcLengthCurve(const QPainterPath& path, double flatnessMm = 0.10)
    {
        build(path, flatnessMm);
    }

    void build(const QPainterPath& path, double flatnessMm = 0.10)
    {
        m_pts.clear();
        m_cum.clear();
        m_totalLen = 0.0;

        if (path.elementCount() == 0) return;

        // Sample the path uniformly in Qt's percent parameter, then rebuild a
        // true cumulative-length table from those samples. Sample count scales
        // with the rough length so curved rails stay smooth. This avoids
        // relying on any particular toSubpathPolygons() overload.
        const double approxLen = std::max(path.length(), 1.0);
        int n = static_cast<int>(std::ceil(approxLen / std::max(flatnessMm, 0.02)));
        n = std::min(std::max(n, 8), 20000);

        m_pts.reserve(n + 1);
        for (int i = 0; i <= n; ++i)
            m_pts.push_back(path.pointAtPercent(static_cast<double>(i) / n));

        m_cum.resize(m_pts.size());
        m_cum[0] = 0.0;
        for (int i = 1; i < m_pts.size(); ++i)
            m_cum[i] = m_cum[i-1] + stick::length(m_pts[i] - m_pts[i-1]);

        m_totalLen = m_cum.isEmpty() ? 0.0 : m_cum.back();
    }

    bool   isValid()  const { return m_pts.size() >= 2 && m_totalLen > 1e-6; }
    double length()   const { return m_totalLen; }
    int    sampleCount() const { return m_pts.size(); }

    /// Point at normalised arc length s in [0,1].
    QPointF pointAt(double s) const
    {
        if (!isValid()) return m_pts.isEmpty() ? QPointF() : m_pts.front();
        return pointAtLen(clamp01(s) * m_totalLen);
    }

    /// Unit tangent (direction of travel) at normalised arc length s.
    QPointF tangentAt(double s) const
    {
        if (!isValid()) return QPointF(1.0, 0.0);
        const double target = clamp01(s) * m_totalLen;
        const double eps    = std::max(m_totalLen * 1e-4, 1e-4);
        const QPointF pa = pointAtLen(std::max(0.0,        target - eps));
        const QPointF pb = pointAtLen(std::min(m_totalLen, target + eps));
        return normalized(pb - pa);
    }

    /// Unit normal (left-hand) at normalised arc length s.
    QPointF normalAt(double s) const { return perpLeft(tangentAt(s)); }

private:
    static double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

    QPointF pointAtLen(double target) const
    {
        // Binary search for the segment containing 'target'.
        int lo = 0, hi = m_cum.size() - 1;
        if (target <= 0.0)          return m_pts.front();
        if (target >= m_totalLen)   return m_pts.back();
        while (hi - lo > 1) {
            const int mid = (lo + hi) / 2;
            if (m_cum[mid] < target) lo = mid; else hi = mid;
        }
        const double segLen = m_cum[hi] - m_cum[lo];
        const double f = (segLen > 1e-9) ? (target - m_cum[lo]) / segLen : 0.0;
        return m_pts[lo] + (m_pts[hi] - m_pts[lo]) * f;
    }

    QVector<QPointF> m_pts;
    QVector<double>  m_cum;
    double           m_totalLen = 0.0;
};

} // namespace stick
