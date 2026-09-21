#include <QtTest>

#include "core/Policy.h"
#include "core/PolicyChain.h"

using namespace dtb;

namespace {
// Scripted stand-in: always active (or never) and returns fixed targets.
class FakePolicy : public Policy {
public:
    FakePolicy(QString n, PolicyPriority pr, bool on, ControlTargets t)
        : m_name(std::move(n)), m_pri(pr), m_on(on), m_t(t) {}
    QString name() const override { return m_name; }
    PolicyPriority priority() const override { return m_pri; }
    bool active(const SystemSnapshot&) override { return m_on; }
    ControlTargets compute(const SystemSnapshot&) override { return m_t; }

private:
    QString m_name;
    PolicyPriority m_pri;
    bool m_on;
    ControlTargets m_t;
};
} // namespace

class TestPolicyChain : public QObject {
    Q_OBJECT

private slots:
    void mergesFieldsAcrossPriorities() {
        ControlTargets base;
        base.mode = ThermalMode::Balanced;
        base.fanPercent.insert(0x33, 40);
        ControlTargets scene;
        scene.fanPercent.insert(0x32, 80);
        scene.cpuPl1W = 54;

        FakePolicy baseline("baseline", PolicyPriority::Baseline, true, base);
        FakePolicy sceneP("scene", PolicyPriority::Scene, true, scene);
        PolicyChain chain;
        chain.addPolicy(&sceneP); // added out of order on purpose
        chain.addPolicy(&baseline);

        ControlTargets out;
        QString active;
        QVERIFY(chain.evaluate(SystemSnapshot{}, out, active));
        QCOMPARE(active, "scene");
        QCOMPARE(out.mode.value_or(ThermalMode::Custom), ThermalMode::Balanced); // from baseline
        QCOMPARE(out.fanPercent.value(0x33), 40);                                // from baseline
        QCOMPARE(out.fanPercent.value(0x32), 80);                                // from scene
        QCOMPARE(out.cpuPl1W.value_or(0), 54);                                   // from scene
        const QList<QString> expectedActives{"baseline", "scene"};
        QCOMPARE(chain.activePolicyNames(), expectedActives);
    }

    void higherPriorityOverridesSameFan() {
        ControlTargets low;
        low.fanPercent.insert(0x33, 40);
        ControlTargets high;
        high.fanPercent.insert(0x33, 90);
        FakePolicy lowP("low", PolicyPriority::Baseline, true, low);
        FakePolicy highP("high", PolicyPriority::Failsafe, true, high);
        PolicyChain chain;
        chain.addPolicy(&lowP);
        chain.addPolicy(&highP);

        ControlTargets out;
        QString active;
        QVERIFY(chain.evaluate(SystemSnapshot{}, out, active));
        QCOMPARE(out.fanPercent.value(0x33), 90);
        QCOMPARE(active, "high");
    }

    void noActivePolicyReturnsFalse() {
        FakePolicy idle("idle", PolicyPriority::Dynamic, false, ControlTargets{});
        PolicyChain chain;
        chain.addPolicy(&idle);
        ControlTargets out;
        QString active = "stale";
        QVERIFY(!chain.evaluate(SystemSnapshot{}, out, active));
        QVERIFY(active.isEmpty());
        QVERIFY(!out.any());
    }
};

QTEST_GUILESS_MAIN(TestPolicyChain)
#include "test_policychain.moc"
