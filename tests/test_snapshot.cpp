#include <QtTest>

#include "core/Snapshot.h"

using namespace dtb;

class TestSnapshot : public QObject {
    Q_OBJECT

private slots:
    void mergeFromOverridesOnlyWhatHigherSets() {
        ControlTargets lo;
        lo.mode = ThermalMode::Balanced;
        lo.fanPercent.insert(0x33, 40);
        lo.cpuPl1W = 35;

        ControlTargets hi;
        hi.fanPercent.insert(0x33, 70);
        hi.fanPercent.insert(0x32, 90);
        hi.cpuPl2W = 80;

        lo.mergeFrom(hi);

        QCOMPARE(lo.mode.value_or(ThermalMode::Custom), ThermalMode::Balanced);
        QCOMPARE(lo.fanPercent.value(0x33), 70);
        QCOMPARE(lo.fanPercent.value(0x32), 90);
        QCOMPARE(lo.cpuPl1W.value_or(0), 35);
        QCOMPARE(lo.cpuPl2W.value_or(0), 80);
    }

    void anyDetectsSetFields() {
        ControlTargets t;
        QVERIFY(!t.any());
        t.cpuPl1W = 45;
        QVERIFY(t.any());
        t = {};
        t.fanPercent.insert(1, 50);
        QVERIFY(t.any());
        t = {};
        t.mode = ThermalMode::GMode;
        QVERIFY(t.any());
    }

    void tempOfReturnsValidSensorById() {
        SystemSnapshot s;
        s.sensors.push_back({0x01, 82});
        s.sensors.push_back({0x06, -1}); // read failed this tick
        QCOMPARE(s.tempOf(0x01).value_or(-999), 82);
        QVERIFY(!s.tempOf(0x06).has_value());
        QVERIFY(!s.tempOf(0x99).has_value());
    }
};

QTEST_GUILESS_MAIN(TestSnapshot)
#include "test_snapshot.moc"
