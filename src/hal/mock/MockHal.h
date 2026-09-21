#pragma once

#include <QHash>
#include <QString>
#include <QVector>

#include <functional>

#include "hal/HalInterfaces.h"

namespace dtb {

// Programmable fake backend: one shared brain plus thin per-interface
// adapters (the interfaces collide on read()/available(), so MockHal itself
// cannot inherit them all). Serves reads from `state` — or, when a scenario
// is installed, from the snapshot it produced for the current tick — and
// records every write in `writes` for assertions.
class MockHal {
public:
    struct Write {
        enum class Kind { Mode, FanPercent, GpuPower, ChargeMode };
        Kind kind = Kind::Mode;
        ThermalMode mode = ThermalMode::Balanced;
        FanId fan = 0;
        int value = 0;
        QString svalue;
    };

    using Scenario = std::function<void(SystemSnapshot&, int tick)>;

    // Read-side state used when no scenario is installed.
    SystemSnapshot state;
    QVector<HalFanInfo> fanMap{{0x33, 0x01}, {0x32, 0x06}}; // G3 3590: CPU fan, GPU fan

    // Availability and write-success switches.
    bool thermalAvailable = true;
    bool batteryAvailable = true;
    bool gpuAvailable = true;
    bool chargeAvailable = true;
    bool thermalWritesSucceed = true;
    bool gpuWritesSucceed = true;
    bool chargeWritesSucceed = true;
    bool gModeSupported = true;
    std::optional<ThermalMode> externalMode; // served by readCurrentProfile

    QVector<Write> writes;

    HalSet makeSet(); // adapters forwarding to this object (not owned)

    void setScenario(Scenario s) { m_scenario = std::move(s); }
    // Drive the scenario from an internal timer (demo/Linux entry); the
    // controller then reads continuously-updated state.
    void enableAutoAdvance(int periodMs);
    void advanceScenario(); // pull the next snapshot from the scenario into `state`
    int tick() const { return m_tick; }

    // Backend operations shared by the adapters.
    int sensorTemp(SensorId id) const;
    int fanRpm(FanId id) const;
    bool doSetMode(ThermalMode mode);
    bool doSetFanPercent(FanId fan, int percent);
    bool doSetGpuPower(int watts);
    bool doSetChargeMode(const QString& cctkValue);
    QList<IChargeThresholdHAL::Mode> chargeModes() const;
    QString currentChargeMode() const { return m_chargeMode; }

private:
    Scenario m_scenario;
    int m_tick = 0;
    QString m_chargeMode = "adaptive";
};

} // namespace dtb
