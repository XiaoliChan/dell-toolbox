#include "hal/mock/MockHal.h"

#include <QCoreApplication>
#include <QTimer>

#include <algorithm>

namespace dtb {

namespace {
// Per-interface adapters: the HAL interfaces deliberately collide on
// read()/available(), so they cannot share one C++ base.
class MockThermal final : public IThermalHAL {
public:
    explicit MockThermal(MockHal* m) : m(m) {}
    QVector<HalFanInfo> enumerateFans() override { return m->fanMap; }
    int sensorTemp(SensorId id) override { return m->sensorTemp(id); }
    int fanRpm(FanId id) override { return m->fanRpm(id); }
    bool setMode(ThermalMode mode) override { return m->doSetMode(mode); }
    bool setFanPercent(FanId fan, int percent) override { return m->doSetFanPercent(fan, percent); }
    bool available() override { return m->thermalAvailable; }
    bool supportsGMode() override { return m->gModeSupported; }
    std::optional<ThermalMode> readCurrentProfile() override { return m->externalMode; }
    MockHal* m;
};

class MockBattery final : public IBatteryHAL {
public:
    explicit MockBattery(MockHal* m) : m(m) {}
    BatteryInfo read() override { return m->state.battery; }
    bool available() override { return m->batteryAvailable; }
    MockHal* m;
};

class MockGpu final : public IGpuHAL {
public:
    explicit MockGpu(MockHal* m) : m(m) {}
    GpuInfo read() override { return m->state.gpu; }
    bool setPowerLimitW(int watts) override { return m->doSetGpuPower(watts); }
    bool available() override { return m->gpuAvailable; }
    MockHal* m;
};

class MockLoad final : public ILoadHAL {
public:
    explicit MockLoad(MockHal* m) : m(m) {}
    int cpuLoadPercent() override { return m->state.cpuLoadPercent; }
    QString foregroundProcess() override { return m->state.foregroundProcess; }
    MockHal* m;
};

class MockCharge final : public IChargeThresholdHAL {
public:
    explicit MockCharge(MockHal* m) : m(m) {}
    QList<Mode> modes() override { return m->chargeModes(); }
    QString currentMode() override { return m->currentChargeMode(); }
    bool setMode(const QString& cctkValue) override { return m->doSetChargeMode(cctkValue); }
    bool available() override { return m->chargeAvailable; }
    MockHal* m;
};
} // namespace

HalSet MockHal::makeSet() {
    HalSet set;
    auto thermal = std::make_shared<MockThermal>(this);
    auto battery = std::make_shared<MockBattery>(this);
    auto gpu = std::make_shared<MockGpu>(this);
    auto load = std::make_shared<MockLoad>(this);
    auto charge = std::make_shared<MockCharge>(this);
    // HalSet carries raw pointers; ownership stays here so repeated makeSet
    // calls (one per test rig) never leak the adapters.
    m_setObjects = {thermal, battery, gpu, load, charge};
    set.thermal = thermal.get();
    set.battery = battery.get();
    set.gpu = gpu.get();
    set.load = load.get();
    set.charge = charge.get();
    return set;
}

MockHal::~MockHal() = default;

void MockHal::enableAutoAdvance(int periodMs) {
    // MockHal is not a QObject; the timer lives for the app lifetime (the
    // mock itself does too).
    auto* timer = new QTimer(qApp);
    QTimer::connect(timer, &QTimer::timeout, nullptr, [this] { advanceScenario(); });
    timer->start(periodMs);
    advanceScenario(); // first sample immediately
}

void MockHal::advanceScenario() {
    if (!m_scenario)
        return;
    state = SystemSnapshot{};
    m_scenario(state, m_tick);
    ++m_tick;
}

int MockHal::sensorTemp(SensorId id) const {
    const auto t = state.tempOf(id);
    return t.value_or(-1);
}

int MockHal::fanRpm(FanId id) const {
    for (const auto& f : state.fans)
        if (f.id == id && f.valid())
            return f.rpm;
    return -1;
}

bool MockHal::doSetMode(ThermalMode mode) {
    Write w;
    w.kind = Write::Kind::Mode;
    w.mode = mode;
    writes.append(w);
    return thermalAvailable && thermalWritesSucceed;
}

bool MockHal::doSetFanPercent(FanId fan, int percent) {
    Write w;
    w.kind = Write::Kind::FanPercent;
    w.fan = fan;
    w.value = std::clamp(percent, 0, 100); // out-of-range requests are clamped
    writes.append(w);
    return thermalAvailable && thermalWritesSucceed;
}

bool MockHal::doSetGpuPower(int watts) {
    Write w;
    w.kind = Write::Kind::GpuPower;
    w.value = watts;
    writes.append(w);
    return gpuAvailable && gpuWritesSucceed;
}

bool MockHal::doSetChargeMode(const QString& cctkValue) {
    Write w;
    w.kind = Write::Kind::ChargeMode;
    w.svalue = cctkValue;
    writes.append(w);
    if (chargeAvailable && chargeWritesSucceed)
        // currentMode() must return a bare mode key (like the real backends):
        // "custom:55-80" would match no radio chargeKey in the UI.
        m_chargeMode = cctkValue.section(QLatin1Char(':'), 0, 0);
    return chargeAvailable && chargeWritesSucceed;
}

QList<IChargeThresholdHAL::Mode> MockHal::chargeModes() const {
    // Mirrors Dell Command Configure --PrimaryBattChargeCfg values.
    return {{"Adaptive", "adaptive"},
            {"Standard", "standard"},
            {"Express", "express"},
            {"Primarily AC Use", "primacuse"},
            {"Custom", "custom"}};
}

} // namespace dtb
