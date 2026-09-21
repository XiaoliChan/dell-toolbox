#include <QtTest>

#include "core/FanCurve.h"

using namespace dtb;

class TestFanCurve : public QObject {
    Q_OBJECT

private slots:
    void defaultCurveIsValid() { QVERIFY(FanCurve::defaultCurve().valid()); }

    void interpolationMidpoint() {
        const auto c = FanCurve::defaultCurve();
        // Segment (60,45)-(75,65): 70C -> 45 + 20*(10/15) = 58.33 -> 58
        QCOMPARE(c.percentFor(70, false), 58);
    }

    void clampsBelowFirstAndAboveLastPoint() {
        const auto c = FanCurve::defaultCurve();
        QCOMPARE(c.percentFor(10, false), 30);
        QCOMPARE(c.percentFor(30, false), 30);
        QCOMPARE(c.percentFor(120, false), 100);
    }

    void fallingEdgeAppliesHysteresis() {
        auto c = FanCurve::defaultCurve();
        QCOMPARE(c.hysteresisC, 3);
        // Segment (40,30)-(60,45), dx=20 dy=15:
        // rising 45C -> 30 + (75+10)/20 = 34; falling 45C evaluates 42C -> 30 + (30+10)/20 = 32
        QCOMPARE(c.percentFor(45, false), 34);
        QCOMPARE(c.percentFor(45, true), 32);
    }

    void setPointsSortsByTemperature() {
        FanCurve c;
        c.setPoints({{95, 100}, {40, 30}, {60, 45}});
        QCOMPARE(c.points().size(), 3);
        QVERIFY(c.valid());
        QCOMPARE(c.points().first().first, 40);
    }

    void rejectsDuplicateX() {
        FanCurve c;
        c.setPoints({{50, 40}, {50, 60}});
        QVERIFY(!c.valid());
        QCOMPARE(c.percentFor(50, false), 100); // invalid curve fails fans open
    }

    void rejectsOutOfRangeY() {
        FanCurve c;
        c.setPoints({{40, 30}, {60, 101}});
        QVERIFY(!c.valid());
    }

    void rejectsSinglePoint() {
        FanCurve c;
        c.setPoints({{40, 30}});
        QVERIFY(!c.valid());
    }
};

QTEST_GUILESS_MAIN(TestFanCurve)
#include "test_fancurve.moc"
