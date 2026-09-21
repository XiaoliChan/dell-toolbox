#include "core/ScenePolicy.h"

namespace dtb {

ScenePolicy::ScenePolicy(const SceneDetector* detector, SceneProfile profile)
    : m_detector(detector), m_profile(profile) {}

bool ScenePolicy::active(const SystemSnapshot&) { return m_detector && m_detector->inScene(); }

ControlTargets ScenePolicy::compute(const SystemSnapshot&) {
    ControlTargets t;
    t.mode = ThermalMode::GMode;
    if (m_profile.cpuPl1W > 0)
        t.cpuPl1W = m_profile.cpuPl1W;
    if (m_profile.cpuPl2W > 0)
        t.cpuPl2W = m_profile.cpuPl2W;
    if (m_profile.gpuPptW > 0)
        t.gpuPptW = m_profile.gpuPptW;
    return t;
}

void ScenePolicy::setProfile(SceneProfile p) { m_profile = p; }

} // namespace dtb
