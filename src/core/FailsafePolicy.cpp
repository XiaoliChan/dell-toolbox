#include "core/FailsafePolicy.h"

#include <algorithm>

namespace dtb {

FailsafePolicy::FailsafePolicy(FailsafeParams p) : m_params(p) {}

void FailsafePolicy::setParams(FailsafeParams p) { m_params = p; }

std::optional<int> FailsafePolicy::cpuTemp(const SystemSnapshot& s) const {
    return s.tempOf(kCpuSensor);
}

std::optional<int> FailsafePolicy::gpuTemp(const SystemSnapshot& s) const {
    // Prefer the AWCC sensor; fall back to NVAPI's own reading.
    if (auto t = s.tempOf(kGpuSensor))
        return t;
    if (s.gpu.tempC >= 0)
        return s.gpu.tempC;
    return std::nullopt;
}

void FailsafePolicy::onTick(const SystemSnapshot& s) {
    m_justReleased = false;
    const auto cpu = cpuTemp(s);
    const auto gpu = gpuTemp(s);

    if (!m_tripped) {
        const bool dangerous = !cpu || *cpu >= m_params.cpuTripC || !gpu || *gpu >= m_params.gpuTripC;
        m_streak = dangerous ? m_streak + 1 : 0;
        if (m_streak >= std::max(1, m_params.triggerDelayS)) {
            m_tripped = true;
            m_streak = 0;
        }
    } else {
        const bool cool = cpu && *cpu < m_params.cpuTripC - m_params.releaseHysteresisC && gpu
            && *gpu < m_params.gpuTripC - m_params.releaseHysteresisC;
        m_streak = cool ? m_streak + 1 : 0;
        if (m_streak >= std::max(1, m_params.releaseDelayS)) {
            m_tripped = false;
            m_justReleased = true;
            m_streak = 0;
        }
    }
}

bool FailsafePolicy::active(const SystemSnapshot&) { return m_tripped; }

ControlTargets FailsafePolicy::compute(const SystemSnapshot&) {
    ControlTargets t;
    t.mode = ThermalMode::GMode;
    return t;
}

} // namespace dtb
