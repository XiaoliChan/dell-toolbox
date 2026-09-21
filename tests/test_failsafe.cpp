#include <QtTest>

#include "core/FailsafePolicy.h"

using namespace dtb;

namespace {
SystemSnapshot snapCpu(int cpuT, int gpuT = 40) {
    SystemSnapshot s;
    s.sensors.push_back({FailsafePolicy::kCpuSensor, cpuT});
    s.sensors.push_back({FailsafePolicy::kGpuSensor, gpuT});
    return s;
}
} // namespace

class TestFailsafe : public QObject {
    Q_OBJECT

private slots:
    void tripsAfterDelayAtCpu95() {
        FailsafePolicy p;
        SystemSnapshot s = snapCpu(95);
        for (int i = 0; i < 7; ++i)
            p.onTick(s);
        QVERIFY(!p.tripped());
        p.onTick(s);
        QVERIFY(p.tripped());
        QVERIFY(p.active(s));
        QCOMPARE(p.compute(s).mode.value_or(ThermalMode::Balanced), ThermalMode::GMode);
    }

    void tripsOnGpu85() {
        FailsafePolicy p;
        SystemSnapshot s = snapCpu(40, 86);
        for (int i = 0; i < 8; ++i)
            p.onTick(s);
        QVERIFY(p.tripped());
    }

    void dipResetsTripTimer() {
        FailsafePolicy p;
        for (int i = 0; i < 5; ++i)
            p.onTick(snapCpu(95));
        p.onTick(snapCpu(88)); // below trip level: timer resets
        for (int i = 0; i < 7; ++i)
            p.onTick(snapCpu(95));
        QVERIFY(!p.tripped());
    }

    void releasesAfterCooling() {
        FailsafePolicy p;
        for (int i = 0; i < 8; ++i)
            p.onTick(snapCpu(95));
        QVERIFY(p.tripped());
        const SystemSnapshot cool = snapCpu(80, 70);
        for (int i = 0; i < 59; ++i)
            p.onTick(cool);
        QVERIFY(p.tripped());
        QVERIFY(!p.justReleased());
        p.onTick(cool);
        QVERIFY(!p.tripped());
        QVERIFY(p.justReleased());
    }

    void missingTempsNeverTrip() {
        // Real-machine lesson (G3 3590): WMI sensor reads fail intermittently
        // and the old 'no data = dangerous' rule tripped the failsafe out of
        // nowhere, force-switching the machine to G-Mode. Only genuinely hot
        // readings may trip.
        FailsafePolicy p;
        SystemSnapshot bad; // no sensors at all
        for (int i = 0; i < 60; ++i)
            p.onTick(bad);
        QVERIFY(!p.tripped());
    }

    void gpuStillHotBlocksRelease() {
        FailsafePolicy p;
        for (int i = 0; i < 8; ++i)
            p.onTick(snapCpu(95));
        QVERIFY(p.tripped());
        for (int i = 0; i < 100; ++i)
            p.onTick(snapCpu(80, 82)); // gpu still >= 85-5=80
        QVERIFY(p.tripped());
    }
};

QTEST_GUILESS_MAIN(TestFailsafe)
#include "test_failsafe.moc"
