#include <QtTest>

#include "core/BaselinePolicy.h"
#include "core/DynamicPolicy.h"
#include "core/ManualOverride.h"

using namespace dtb;

namespace {
SystemSnapshot snapTemps(int cpuT, int gpuT) {
    SystemSnapshot s;
    s.sensors.push_back({0x01, cpuT});
    s.sensors.push_back({0x06, gpuT});
    return s;
}
} // namespace

class TestDynamic : public QObject {
    Q_OBJECT

private slots:
    void interpolateTableBoundaries() {
        const QList<QPair<int, int>> table{{70, 45}, {80, 54}, {85, 60}};
        QCOMPARE(DynamicPolicy::interpolate(table, 70), 45);
        QCOMPARE(DynamicPolicy::interpolate(table, 75), 50); // 45 + 9*0.5 = 49.5 -> 50
        QCOMPARE(DynamicPolicy::interpolate(table, 80), 54);
        QCOMPARE(DynamicPolicy::interpolate(table, 85), 60);
        QCOMPARE(DynamicPolicy::interpolate(table, 95), 60); // clamped above
        QCOMPARE(DynamicPolicy::interpolate(table, 40), 45); // clamped below
    }

    void activeOnlyInHotZone() {
        DynamicPolicy p;
        QVERIFY(!p.active(snapTemps(65, 60)));
        QVERIFY(p.active(snapTemps(70, 40))); // cpu reaches the zone
        QVERIFY(p.active(snapTemps(40, 71))); // gpu reaches the zone
    }

    void computeSetsPowerTargets() {
        DynamicPolicy p;
        SystemSnapshot s = snapTemps(75, 82);
        const ControlTargets t = p.compute(s);
        QCOMPARE(t.cpuPl1W.value_or(0), 50); // 45 + 9*0.5
        QCOMPARE(t.gpuPptW.value_or(0), 100); // 100 + 0*0.4 -> between 80:100 and 85:100
    }

    void manualOverrideExpiry() {
        ManualOverride m;
        QCOMPARE(m.expiresSec, 1800);
        ControlTargets t;
        t.cpuPl1W = 44;
        m.set(t, 1000);
        QVERIFY(m.active(1000 + 1799'000));
        QVERIFY(!m.active(1000 + 1800'000));
        QCOMPARE(m.targets().cpuPl1W.value_or(0), 44);
        m.clear();
        QVERIFY(!m.active(1000));
    }

    void baselineEmitsModeAndCustomFanSpeeds() {
        BaselinePolicy p;
        p.setMode(ThermalMode::Custom);
        p.setFanCurve(0x33, 0x01, FanCurve::defaultCurve());

        SystemSnapshot s = snapTemps(70, 40);
        ControlTargets t = p.compute(s);
        QCOMPARE(t.mode.value_or(ThermalMode::GMode), ThermalMode::Custom);
        QCOMPARE(t.fanPercent.value(0x33), 58); // default curve at 70C (see test_fancurve)

        // falling edge: 45 after 50 applies hysteresis
        p.compute(snapTemps(50, 40));
        t = p.compute(snapTemps(45, 40));
        QCOMPARE(t.fanPercent.value(0x33), 32); // evaluated at 42C
    }

    void baselineNonCustomModeLeavesFansToBios() {
        BaselinePolicy p;
        p.setMode(ThermalMode::Balanced);
        p.setFanCurve(0x33, 0x01, FanCurve::defaultCurve());
        ControlTargets t = p.compute(snapTemps(70, 40));
        QCOMPARE(t.mode.value_or(ThermalMode::Custom), ThermalMode::Balanced);
        QVERIFY(t.fanPercent.isEmpty());
    }

    void baselineInvalidSensorRunsFanFull() {
        BaselinePolicy p;
        p.setMode(ThermalMode::Custom);
        p.setFanCurve(0x33, 0x01, FanCurve::defaultCurve());
        SystemSnapshot empty;
        ControlTargets t = p.compute(empty);
        QCOMPARE(t.fanPercent.value(0x33), 100);
    }
};

QTEST_GUILESS_MAIN(TestDynamic)
#include "test_dynamic.moc"
