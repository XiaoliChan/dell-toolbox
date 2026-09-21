#pragma once

#include <QHash>

#include "core/FanCurve.h"
#include "core/Policy.h"

namespace dtb {

// The user's saved steady state: a thermal mode plus, in Custom mode, one
// curve per fan. Always active; every other layer only overrides it.
class BaselinePolicy : public Policy {
public:
    BaselinePolicy();

    QString name() const override { return QStringLiteral("baseline"); }
    PolicyPriority priority() const override { return PolicyPriority::Baseline; }
    bool active(const SystemSnapshot&) override { return true; }
    ControlTargets compute(const SystemSnapshot& s) override;

    void setMode(ThermalMode m);
    ThermalMode mode() const { return m_mode; }
    void setFanCurve(FanId fan, SensorId sensor, const FanCurve& curve);

private:
    struct FanConfig {
        SensorId sensor;
        FanCurve curve;
        int lastTempC = INT_MIN;
    };

    ThermalMode m_mode = ThermalMode::Balanced;
    QHash<FanId, FanConfig> m_fans;
};

} // namespace dtb
