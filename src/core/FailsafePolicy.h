#pragma once

#include "core/Policy.h"
#include "core/Snapshot.h"

namespace dtb {

struct FailsafeParams {
    int cpuTripC = 95;
    int gpuTripC = 85;
    int triggerDelayS = 8;
    int releaseDelayS = 60;
    int releaseHysteresisC = 5;
};

// Last line of defence, always in the chain. Semantics inherited from tcc-g15:
// a CPU/GPU reading at or above its trip level for triggerDelayS forces
// G-mode; it is released only after every reading sits hysteresisC below its
// trip level for releaseDelayS. Missing/invalid readings count as dangerous.
class FailsafePolicy : public Policy {
public:
    explicit FailsafePolicy(FailsafeParams p = {});

    QString name() const override { return QStringLiteral("failsafe"); }
    PolicyPriority priority() const override { return PolicyPriority::Failsafe; }
    bool active(const SystemSnapshot&) override;
    ControlTargets compute(const SystemSnapshot&) override;

    // Advance trip/release timers. Must be called once per tick before the
    // chain is evaluated (the Controller does this).
    void onTick(const SystemSnapshot& s);

    bool tripped() const { return m_tripped; }
    bool justReleased() const { return m_justReleased; } // true on the release tick
    void setParams(FailsafeParams p);

    static constexpr SensorId kCpuSensor = 0x01;
    static constexpr SensorId kGpuSensor = 0x06;

private:
    // < temperature or nullopt when the reading is invalid/missing
    std::optional<int> cpuTemp(const SystemSnapshot& s) const;
    std::optional<int> gpuTemp(const SystemSnapshot& s) const;

    FailsafeParams m_params;
    bool m_tripped = false;
    bool m_justReleased = false;
    int m_streak = 0;
};

} // namespace dtb
