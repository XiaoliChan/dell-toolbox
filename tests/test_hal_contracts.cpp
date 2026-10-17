#include <QtTest>
#include "hal/mock/MockHal.h"
#include "hal/ModeWriteProbe.h"
#include "hal/win/NvApiTypes.h"

class TestHalContracts : public QObject {
    Q_OBJECT
private slots:
    void transientMatchDoesNotConfirmRetention() {
        dtb::ModeWriteProbe probe;
        using Result = dtb::ModeWriteProbe::Result;
        QCOMPARE(probe.observe(true), Result::Pending);
        QCOMPARE(probe.observe(false), Result::Pending);
        QCOMPARE(probe.observe(false), Result::Rejected);
        probe.reset();
        QCOMPARE(probe.observe(false), Result::Pending);
        QCOMPARE(probe.observe(true), Result::Pending);
        QCOMPARE(probe.observe(true), Result::Retained);
    }
    void repeatedMakeSetKeepsEarlierAdaptersAlive() {
        dtb::MockHal mock;
        const auto first = mock.makeSet();
        const auto second = mock.makeSet();
        QVERIFY(first.thermal->setMode(dtb::ThermalMode::Custom));
        QVERIFY(second.thermal->setFanPercent(0x33, 40));
        QCOMPARE(mock.writes.size(), 2);
    }
    void timerDiesWithMock() {
        int calls = 0;
        {
            dtb::MockHal mock;
            mock.setScenario([&](dtb::SystemSnapshot&, int) { ++calls; });
            mock.enableAutoAdvance(1);
            QTRY_VERIFY_WITH_TIMEOUT(calls > 1, 1000);
        }
        const int stopped = calls;
        QTest::qWait(20);
        QCOMPARE(calls, stopped);
    }
    void restartingTimerDoesNotLeaveOldTimer() {
        dtb::MockHal mock;
        mock.enableAutoAdvance(1);
        int calls = 0;
        mock.setScenario([&](dtb::SystemSnapshot&, int) { ++calls; });
        mock.enableAutoAdvance(60000);
        QCOMPARE(calls, 1);
        QTest::qWait(30);
        QCOMPARE(calls, 1);
    }
    void nvapiAbiMatchesPublishedHeader() {
        QCOMPARE(sizeof(dtb::win::NvGpuThermalSettings), size_t(68));
        QCOMPARE(sizeof(dtb::win::NvGpuDynamicPstatesInfoEx), size_t(72));
        QCOMPARE(dtb::win::kIdGetThermalSettings, 0xE3640A56u);
        dtb::win::NvApi_GetThermalSettings_t call = [](void*, unsigned int index,
                                                       dtb::win::NvGpuThermalSettings* out) {
            out->sensor[0].currentTemp = 57;
            return index == 0 ? 0 : -1;
        };
        dtb::win::NvGpuThermalSettings out{};
        QCOMPARE(call(nullptr, 0, &out), 0);
        QCOMPARE(out.sensor[0].currentTemp, 57);
    }
};
QTEST_GUILESS_MAIN(TestHalContracts)
#include "test_hal_contracts.moc"
