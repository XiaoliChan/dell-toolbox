#include "core/BaselinePolicy.h"

namespace dtb {

BaselinePolicy::BaselinePolicy() = default;

void BaselinePolicy::setMode(ThermalMode m) { m_mode = m; }

void BaselinePolicy::setFanCurve(FanId fan, SensorId sensor, const FanCurve& curve) {
    FanConfig fc;
    fc.sensor = sensor;
    fc.curve = curve;
    m_fans.insert(fan, fc);
}

ControlTargets BaselinePolicy::compute(const SystemSnapshot& s) {
    ControlTargets t;
    t.mode = m_mode;
    // Outside Custom mode the BIOS owns the fan curve; only Custom curves ours.
    if (m_mode != ThermalMode::Custom)
        return t;
    for (auto it = m_fans.begin(); it != m_fans.end(); ++it) {
        FanConfig& fc = it.value();
        const auto temp = s.tempOf(fc.sensor);
        if (!temp) {
            t.fanPercent.insert(it.key(), 100); // unreadable sensor: run the fan flat out
            continue;
        }
        const bool falling = fc.lastTempC != INT_MIN && *temp < fc.lastTempC;
        t.fanPercent.insert(it.key(), fc.curve.percentFor(*temp, falling));
        fc.lastTempC = *temp;
    }
    return t;
}

} // namespace dtb
