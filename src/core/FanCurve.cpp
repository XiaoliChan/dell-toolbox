#include "core/FanCurve.h"

#include <algorithm>

namespace dtb {

FanCurve FanCurve::defaultCurve() {
    FanCurve c;
    c.setPoints({{40, 30}, {60, 45}, {75, 65}, {85, 85}, {95, 100}});
    return c;
}

void FanCurve::setPoints(QList<QPair<int, int>> pts) {
    std::sort(pts.begin(), pts.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    if (pts.size() < 2) {
        m_points.clear();
        return;
    }
    for (int i = 0; i < pts.size(); ++i) {
        if (pts[i].second < 0 || pts[i].second > 100) {
            m_points.clear();
            return;
        }
        if (i > 0 && pts[i].first == pts[i - 1].first) {
            m_points.clear();
            return;
        }
    }
    m_points = std::move(pts);
}

const QList<QPair<int, int>>& FanCurve::points() const { return m_points; }

bool FanCurve::valid() const {
    if (m_points.size() < 2)
        return false;
    for (int i = 0; i < m_points.size(); ++i) {
        if (m_points[i].second < 0 || m_points[i].second > 100)
            return false;
        if (i > 0 && m_points[i].first <= m_points[i - 1].first)
            return false;
    }
    return true;
}

int FanCurve::percentFor(int tempC, bool fallingEdge) const {
    if (!valid())
        return 100; // a broken curve must never stall the fans
    const int t = fallingEdge ? tempC - hysteresisC : tempC;
    if (t <= m_points.first().first)
        return m_points.first().second;
    for (int i = 1; i < m_points.size(); ++i) {
        if (t <= m_points[i].first) {
            const int dx = m_points[i].first - m_points[i - 1].first;
            const int dy = m_points[i].second - m_points[i - 1].second;
            return m_points[i - 1].second + (dy * (t - m_points[i - 1].first) + dx / 2) / dx;
        }
    }
    return m_points.last().second;
}

} // namespace dtb
