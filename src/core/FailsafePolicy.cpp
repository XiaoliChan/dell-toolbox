#include "core/FailsafePolicy.h"

#include "core/Logger.h"

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
        // A FAILED sensor read is not overheating - "no data" must never
        // trip the failsafe (flaky WMI reads used to force G-Mode out of
        // nowhere). Only genuinely hot readings count.
        const bool dangerous = (cpu && *cpu >= m_params.cpuTripC) || (gpu && *gpu >= m_params.gpuTripC);
        m_streak = dangerous ? m_streak + 1 : 0;
        if (m_streak >= std::max(1, m_params.triggerDelayS)) {
            m_tripped = true;
            m_streak = 0;
            dtbLog(warn) << "failsafe: TRIPPED (cpu" << (cpu ? QString::number(*cpu) : QStringLiteral("n/a"))
                         << "gpu" << (gpu ? QString::number(*gpu) : QStringLiteral("n/a")) << ")";
        }
    } else {
        const bool cool = cpu && *cpu < m_params.cpuTripC - m_params.releaseHysteresisC && gpu
            && *gpu < m_params.gpuTripC - m_params.releaseHysteresisC;
        m_streak = cool ? m_streak + 1 : 0;
        if (m_streak >= std::max(1, m_params.releaseDelayS)) {
            m_tripped = false;
            m_justReleased = true;
            m_streak = 0;
            dtbLog(info) << "failsafe: released";
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
