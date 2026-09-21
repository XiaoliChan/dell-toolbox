#pragma once

#include <QList>
#include <QPair>

namespace dtb {

// Temperature-to-percent fan curve with linear interpolation and a
// falling-edge hysteresis so fans spin down late and never oscillate.
class FanCurve {
public:
    static FanCurve defaultCurve(); // {(40,30),(60,45),(75,65),(85,85),(95,100)}
    void setPoints(QList<QPair<int, int>> pts); // sorted by tempC; invalid input rejected
    const QList<QPair<int, int>>& points() const;
    int percentFor(int tempC, bool fallingEdge) const;
    bool valid() const; // >=2 points, x strictly increasing, y in [0,100]

    int hysteresisC = 3;

private:
    QList<QPair<int, int>> m_points;
};

} // namespace dtb
