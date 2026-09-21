#pragma once

#include <QList>
#include <QPair>

#include "core/Policy.h"

namespace dtb {

// Power tables for the C layer: linear interpolation over (tempC, watts).
struct DynamicProfile {
    QList<QPair<int, int>> cpuPl1Table{{70, 45}, {80, 54}, {85, 60}};
    QList<QPair<int, int>> gpuPptTable{{70, 80}, {80, 100}, {85, 100}}; // last entry bumped to GPU max by the UI
};

// C: nudges power limits as temperatures climb through the hot zone. Only
// active once the hottest reading reaches the first table temperature.
class DynamicPolicy : public Policy {
public:
    explicit DynamicPolicy(DynamicProfile p = {});

    QString name() const override { return QStringLiteral("dynamic"); }
    PolicyPriority priority() const override { return PolicyPriority::Dynamic; }
    bool active(const SystemSnapshot& s) override;
    ControlTargets compute(const SystemSnapshot& s) override;

    void setProfile(DynamicProfile p);

    static int interpolate(const QList<QPair<int, int>>& table, int tempC);

private:
    DynamicProfile m_profile;
};

} // namespace dtb
