#include <QTemporaryFile>
#include <QtTest>

#include "core/ConfigStore.h"
#include "core/FanCurve.h"

using namespace dtb;

namespace {
QString tempIniPath() {
    QTemporaryFile f;
    f.open();
    const QString path = f.fileName() + ".ini";
    f.close();
    return path;
}
} // namespace

class TestConfigStore : public QObject {
    Q_OBJECT

private slots:
    void freshFileGivesDefaults() {
        const QString path = tempIniPath();
        ConfigStore c(path);
        QCOMPARE(c.loadMode(), ThermalMode::Balanced);
        QCOMPARE(c.loadCurve(0x33), FanCurve::defaultCurve().points());

        const SceneParams sp = c.loadSceneParams();
        QCOMPARE(sp.gpuEnterThreshold, 60);
        QCOMPARE(sp.gpuExitThreshold, 55);
        QCOMPARE(sp.enterDebounceS, 10);
        QCOMPARE(sp.exitDebounceS, 30);
        QVERIFY(sp.gameProcesses.isEmpty());

        QCOMPARE(c.loadManualExpireMin(), 30);

        const DynamicProfile dp = c.loadDynamicProfile();
        const QPair<int, int> firstCpu(70, 45);
        const QPair<int, int> lastGpu(85, 100);
        QCOMPARE(dp.cpuPl1Table.first(), firstCpu);
        QCOMPARE(dp.gpuPptTable.last(), lastGpu);

        QVERIFY(!c.loadAutostart());
        QCOMPARE(c.loadLogLevel(), 1);
    }

    void roundtrip() {
        const QString path = tempIniPath();
        {
            ConfigStore c(path);
            c.saveMode(ThermalMode::Custom);
            c.saveCurve(0x33, {{45, 35}, {65, 55}, {80, 80}});
            SceneParams sp;
            sp.gpuEnterThreshold = 65;
            sp.gpuExitThreshold = 58;
            sp.enterDebounceS = 12;
            sp.exitDebounceS = 25;
            sp.gameProcesses = {"game.exe", "steam.exe"};
            c.saveSceneParams(sp);
            c.saveManualExpireMin(45);
            DynamicProfile dp;
            dp.cpuPl1Table = {{65, 40}, {75, 50}, {85, 60}};
            dp.gpuPptTable = {{70, 75}, {85, 95}};
            c.saveDynamicProfile(dp);
            c.saveAutostart(true);
            c.saveLogLevel(2);
        }
        ConfigStore c(path);
        QCOMPARE(c.loadMode(), ThermalMode::Custom);
        const QList<QPair<int, int>> savedCurve{{45, 35}, {65, 55}, {80, 80}};
        QCOMPARE(c.loadCurve(0x33), savedCurve);

        const SceneParams sp = c.loadSceneParams();
        QCOMPARE(sp.gpuEnterThreshold, 65);
        QCOMPARE(sp.gpuExitThreshold, 58);
        QCOMPARE(sp.enterDebounceS, 12);
        QCOMPARE(sp.exitDebounceS, 25);
        QCOMPARE(sp.gameProcesses, QStringList({"game.exe", "steam.exe"}));

        QCOMPARE(c.loadManualExpireMin(), 45);

        const DynamicProfile dp = c.loadDynamicProfile();
        const QList<QPair<int, int>> cpuTable{{65, 40}, {75, 50}, {85, 60}};
        const QList<QPair<int, int>> gpuTable{{70, 75}, {85, 95}};
        QCOMPARE(dp.cpuPl1Table, cpuTable);
        QCOMPARE(dp.gpuPptTable, gpuTable);

        QVERIFY(c.loadAutostart());
        QCOMPARE(c.loadLogLevel(), 2);
    }

    void corruptValuesFallBackToDefaults() {
        const QString path = tempIniPath();
        {
            QSettings raw(path, QSettings::IniFormat);
            raw.setValue("dtb/mode", 42);
            raw.setValue("dtb/fan/51/curve", "garbage,not:numbers");
            raw.setValue("dtb/dynamic/cpuPl1Table", "one:two");
            raw.sync();
        }
        ConfigStore c(path);
        QCOMPARE(c.loadMode(), ThermalMode::Balanced);
        QCOMPARE(c.loadCurve(0x33), FanCurve::defaultCurve().points());
        const QPair<int, int> firstCpu(70, 45);
        QCOMPARE(c.loadDynamicProfile().cpuPl1Table.first(), firstCpu);
    }

    void invalidCurveIsNotPersisted() {
        const QString path = tempIniPath();
        ConfigStore c(path);
        c.saveCurve(0x33, {{50, 40}, {50, 60}}); // duplicate x
        QCOMPARE(c.loadCurve(0x33), FanCurve::defaultCurve().points());
    }
};

QTEST_GUILESS_MAIN(TestConfigStore)
#include "test_configstore.moc"
