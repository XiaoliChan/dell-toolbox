#pragma once

#include "core/Policy.h"
#include "core/SceneDetector.h"

namespace dtb {

// Power profile the scene (B) layer applies while a game is detected.
struct SceneProfile {
    int cpuPl1W = 54;
    int cpuPl2W = 60;
    int gpuPptW = 0; // 0 = leave the GPU limit alone
};

class ScenePolicy : public Policy {
public:
    ScenePolicy(const SceneDetector* detector, SceneProfile profile = {});

    QString name() const override { return QStringLiteral("scene"); }
    PolicyPriority priority() const override { return PolicyPriority::Scene; }
    bool active(const SystemSnapshot&) override;
    ControlTargets compute(const SystemSnapshot&) override;

    void setProfile(SceneProfile p);

private:
    const SceneDetector* m_detector;
    SceneProfile m_profile;
};

} // namespace dtb
