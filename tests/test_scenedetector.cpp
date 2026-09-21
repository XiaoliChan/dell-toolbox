#include <QtTest>

#include "core/SceneDetector.h"
#include "core/ScenePolicy.h"

using namespace dtb;

namespace {
SystemSnapshot snap(int gpuUtil, const QString& proc = {}) {
    SystemSnapshot s;
    s.gpu.utilPercent = gpuUtil;
    s.foregroundProcess = proc;
    return s;
}
} // namespace

class TestSceneDetector : public QObject {
    Q_OBJECT

private slots:
    void entersAfterGpuDebounce() {
        SceneDetector d(SceneParams{});
        for (int i = 0; i < 9; ++i)
            d.onTick(snap(62));
        QVERIFY(!d.inScene());
        d.onTick(snap(62));
        QVERIFY(d.inScene());
        QCOMPARE(d.trigger(), QStringLiteral("gpu"));
    }

    void dipResetsEnterStreak() {
        SceneDetector d(SceneParams{});
        for (int i = 0; i < 5; ++i)
            d.onTick(snap(62));
        d.onTick(snap(50));
        for (int i = 0; i < 4; ++i)
            d.onTick(snap(62));
        QVERIFY(!d.inScene()); // 4 consecutive only, debounce is 10
    }

    void exitsAfterFallBelowExitThreshold() {
        SceneDetector d(SceneParams{});
        for (int i = 0; i < 10; ++i)
            d.onTick(snap(62));
        QVERIFY(d.inScene());
        for (int i = 0; i < 29; ++i)
            d.onTick(snap(50));
        QVERIFY(d.inScene());
        d.onTick(snap(50));
        QVERIFY(!d.inScene());
        QVERIFY(d.trigger().isEmpty());
    }

    void sustainsBetweenExitAndEnterThresholds() {
        SceneDetector d(SceneParams{});
        for (int i = 0; i < 10; ++i)
            d.onTick(snap(62));
        for (int i = 0; i < 100; ++i)
            d.onTick(snap(58)); // below enter(60) but above exit(55)
        QVERIFY(d.inScene());
    }

    void processMatchEntersScene() {
        SceneParams p;
        p.gameProcesses = {"game.exe"};
        SceneDetector d(p);
        for (int i = 0; i < 10; ++i)
            d.onTick(snap(0, "C:/x/Game.EXE")); // case + path ignored
        QVERIFY(d.inScene());
        QCOMPARE(d.trigger(), QStringLiteral("process:game.exe"));
    }

    void processMatchSustainsScene() {
        SceneParams p;
        p.gameProcesses = {"game.exe"};
        SceneDetector d(p);
        for (int i = 0; i < 10; ++i)
            d.onTick(snap(0, "C:/x/Game.EXE"));
        for (int i = 0; i < 100; ++i)
            d.onTick(snap(0, "game.exe"));
        QVERIFY(d.inScene());
    }

    void policyFollowsDetector() {
        SceneDetector d(SceneParams{});
        SceneProfile prof;
        prof.cpuPl1W = 50;
        prof.cpuPl2W = 70;
        prof.gpuPptW = 90;
        ScenePolicy p(&d, prof);

        SystemSnapshot s;
        QVERIFY(!p.active(s));
        for (int i = 0; i < 10; ++i)
            d.onTick(snap(62));
        QVERIFY(p.active(s));

        const ControlTargets t = p.compute(s);
        QCOMPARE(t.mode.value_or(ThermalMode::Balanced), ThermalMode::GMode);
        QCOMPARE(t.cpuPl1W.value_or(0), 50);
        QCOMPARE(t.cpuPl2W.value_or(0), 70);
        QCOMPARE(t.gpuPptW.value_or(0), 90);
    }
};

QTEST_GUILESS_MAIN(TestSceneDetector)
#include "test_scenedetector.moc"
