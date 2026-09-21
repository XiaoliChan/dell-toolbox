#include <QtTest>

#include "hal/mock/MockHal.h"

using namespace dtb;

class TestMockHal : public QObject {
    Q_OBJECT

private slots:
    void scenarioDrivesReadings() {
        MockHal hal;
        hal.setScenario([](SystemSnapshot& s, int tick) {
            s.sensors.push_back({0x01, 70 + tick}); // 70, 71, 72...
            s.fans.push_back({0x33, 2000 + tick * 100});
        });
        HalSet set = hal.makeSet();
        hal.advanceScenario();
        QCOMPARE(set.thermal->sensorTemp(0x01), 70);
        QCOMPARE(set.thermal->fanRpm(0x33), 2000);
        hal.advanceScenario();
        QCOMPARE(set.thermal->sensorTemp(0x01), 71);
        QCOMPARE(set.thermal->fanRpm(0x33), 2100);
    }

    void fanEnumerationMapsG3590Layout() {
        MockHal hal;
        const auto fans = hal.makeSet().thermal->enumerateFans();
        QCOMPARE(fans.size(), 2);
        QCOMPARE(fans[0].id, 0x33);
        QCOMPARE(fans[0].sensorId, 0x01);
        QCOMPARE(fans[1].id, 0x32);
        QCOMPARE(fans[1].sensorId, 0x06);
    }

    void writesAreRecordedWithArgs() {
        MockHal hal;
        HalSet set = hal.makeSet();
        QVERIFY(set.thermal->setMode(ThermalMode::GMode));
        QVERIFY(set.thermal->setFanPercent(0x33, 55));
        QVERIFY(set.gpu->setPowerLimitW(80));

        QCOMPARE(hal.writes.size(), 3);
        QCOMPARE(hal.writes[0].kind, MockHal::Write::Kind::Mode);
        QCOMPARE(hal.writes[0].mode, ThermalMode::GMode);
        QCOMPARE(hal.writes[1].kind, MockHal::Write::Kind::FanPercent);
        QCOMPARE(hal.writes[1].fan, 0x33);
        QCOMPARE(hal.writes[1].value, 55);
        QCOMPARE(hal.writes[2].kind, MockHal::Write::Kind::GpuPower);
        QCOMPARE(hal.writes[2].value, 80);
    }

    void fanPercentIsClamped() {
        MockHal hal;
        HalSet set = hal.makeSet();
        set.thermal->setFanPercent(0x33, 150);
        set.thermal->setFanPercent(0x33, -5);
        QCOMPARE(hal.writes[0].value, 100);
        QCOMPARE(hal.writes[1].value, 0);
    }

    void availabilityGatesWrites() {
        MockHal hal;
        hal.thermalAvailable = false;
        HalSet set = hal.makeSet();
        QVERIFY(!set.thermal->available());
        QVERIFY(!set.thermal->setMode(ThermalMode::GMode));
        QVERIFY(hal.writes.size() > 0); // the attempt is still recorded
    }

    void chargeModesRoundTrip() {
        MockHal hal;
        HalSet set = hal.makeSet();
        QCOMPARE(set.charge->modes().size(), 5);
        QVERIFY(set.charge->setMode("express"));
        QCOMPARE(set.charge->currentMode(), QStringLiteral("express"));
        QCOMPARE(hal.writes.constLast().kind, MockHal::Write::Kind::ChargeMode);
        QCOMPARE(hal.writes.constLast().svalue, QStringLiteral("express"));
    }
};

QTEST_GUILESS_MAIN(TestMockHal)
#include "test_mockhal.moc"
