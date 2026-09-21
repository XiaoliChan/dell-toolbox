#include <QTemporaryFile>
#include <QtTest>

#include "core/Controller.h"
#include "hal/mock/MockHal.h"

using namespace dtb;

namespace {
struct Rig {
    QTemporaryFile ini;
    QString iniPath;
    MockHal hal;
    HalSet set;
    std::unique_ptr<ConfigStore> cfg;
    std::unique_ptr<Controller> ctrl;
    qint64 fakeNow = 1'000'000;

    explicit Rig(MockHal::Scenario scenario = nullptr, ThermalMode mode = ThermalMode::Custom) {
        ini.open();
        iniPath = ini.fileName() + ".ini";
        ini.close();
        if (scenario)
            hal.setScenario(scenario);
        set = hal.makeSet();
        cfg = std::make_unique<ConfigStore>(iniPath);
        cfg->saveMode(mode); // Custom lets the baseline own the fan curves
        ctrl = std::make_unique<Controller>(set, cfg.get());
        ctrl->setClock([this] { return fakeNow; });
    }

    void tick(int n = 1) {
        for (int i = 0; i < n; ++i) {
            hal.advanceScenario();
            fakeNow += 1000;
            ctrl->tickOnce();
        }
    }
};

int countWrites(const QVector<MockHal::Write>& w, MockHal::Write::Kind k) {
    int n = 0;
    for (const auto& e : w)
        if (e.kind == k)
            ++n;
    return n;
}
bool hasModeWrite(const QVector<MockHal::Write>& w, ThermalMode m) {
    for (const auto& e : w)
        if (e.kind == MockHal::Write::Kind::Mode && e.mode == m)
            return true;
    return false;
}
} // namespace

class TestController : public QObject {
    Q_OBJECT

private slots:
    void constantLoadWritesFansOnce() {
        Rig rig([](SystemSnapshot& s, int) {
            s.sensors.push_back({0x01, 70});
            s.sensors.push_back({0x06, 40});
            s.gpu.utilPercent = 10;
        });
        rig.tick(10);
        QCOMPARE(countWrites(rig.hal.writes, MockHal::Write::Kind::FanPercent), 2); // both fans, first tick only
        QVERIFY(hasModeWrite(rig.hal.writes, ThermalMode::Custom));
        QCOMPARE(rig.ctrl->activePolicy(), QStringLiteral("baseline")); // dynamic is out of the chain until PL writes land
    }

    void sceneEntrySwitchesToGMode() {
        Rig rig([](SystemSnapshot& s, int) {
            s.sensors.push_back({0x01, 60});
            s.sensors.push_back({0x06, 50});
            s.gpu.utilPercent = 65; // over the 60% enter threshold
        });
        rig.tick(9);
        QCOMPARE(rig.ctrl->activePolicy(), QStringLiteral("baseline")); // debounce not elapsed
        rig.tick(1);
        QCOMPARE(rig.ctrl->activePolicy(), QStringLiteral("scene"));
        QVERIFY(hasModeWrite(rig.hal.writes, ThermalMode::GMode));
    }

    void manualOverrideBeatsScene() {
        Rig rig([](SystemSnapshot& s, int) {
            s.sensors.push_back({0x01, 60});
            s.sensors.push_back({0x06, 50});
            s.gpu.utilPercent = 65;
        });
        rig.tick(11);
        QCOMPARE(rig.ctrl->activePolicy(), QStringLiteral("scene"));

        ControlTargets t;
        t.mode = ThermalMode::Custom;
        t.fanPercent.insert(0x33, 90);
        rig.ctrl->manual()->set(t, rig.fakeNow);
        rig.tick(1);
        QCOMPARE(rig.ctrl->activePolicy(), QStringLiteral("manual"));
        QVERIFY(hasModeWrite(rig.hal.writes, ThermalMode::Custom));
        bool saw90 = false;
        for (const auto& e : rig.hal.writes)
            if (e.kind == MockHal::Write::Kind::FanPercent && e.fan == 0x33 && e.value == 90)
                saw90 = true;
        QVERIFY(saw90);
    }

    void failsafeTripsToGModeAndRecovers() {
        auto hot = [](SystemSnapshot& s, int tick) {
            s.sensors.push_back({0x01, tick < 8 ? 96 : 60});
            s.sensors.push_back({0x06, 50});
            s.gpu.utilPercent = 5;
        };
        Rig rig(hot);
        rig.tick(8);
        QCOMPARE(rig.ctrl->activePolicy(), QStringLiteral("failsafe"));
        QVERIFY(hasModeWrite(rig.hal.writes, ThermalMode::GMode));

        rig.tick(60); // 60 cool ticks release the failsafe
        QCOMPARE(rig.ctrl->activePolicy(), QStringLiteral("baseline"));
        QVERIFY(hasModeWrite(rig.hal.writes, ThermalMode::Custom)); // restored to the saved baseline
    }

    void adoptsExternalProfileChanges() {
        Rig rig([](SystemSnapshot& s, int) {
            s.sensors.push_back({0x01, 50});
            s.sensors.push_back({0x06, 40});
        });
        int adopted = -1;
        bool lastExternal = false;
        connect(rig.ctrl.get(), &Controller::thermalModeChanged,
                [&](ThermalMode m, bool external) {
                    adopted = static_cast<int>(m);
                    lastExternal = external;
                });
        // The machine reports G-Mode (set in AWCC): DT must adopt and save it.
        rig.hal.externalMode = ThermalMode::GMode;
        rig.tick(1);
        QCOMPARE(adopted, static_cast<int>(ThermalMode::GMode));
        QVERIFY(lastExternal); // firmware-reported, not our own write
        QCOMPARE(rig.cfg->loadMode(), ThermalMode::GMode);
        // No write-back: the adopted mode is cached as already-applied.
        QVERIFY(!hasModeWrite(rig.hal.writes, ThermalMode::GMode));
        // Clearing the external reading stops re-adopting.
        rig.hal.externalMode.reset();
        rig.tick(1);
        QCOMPARE(rig.cfg->loadMode(), ThermalMode::GMode);
    }

    void emitsSnapshotSignal() {
        Rig rig([](SystemSnapshot& s, int) { s.sensors.push_back({0x01, 50}); });
        int snapshots = 0;
        QString lastPolicy;
        connect(rig.ctrl.get(), &Controller::snapshotUpdated, [&](const SystemSnapshot&) { ++snapshots; });
        connect(rig.ctrl.get(), &Controller::activePolicyChanged, [&](const QString& n) { lastPolicy = n; });
        rig.tick(3);
        QCOMPARE(snapshots, 3);
        QCOMPARE(lastPolicy, QStringLiteral("baseline"));
    }
};

QTEST_GUILESS_MAIN(TestController)
#include "test_controller.moc"
