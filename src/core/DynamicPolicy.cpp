#include "core/DynamicPolicy.h"

#include <algorithm>

namespace dtb {

namespace {
// GPU reading: AWCC sensor first, NVAPI as fallback.
std::optional<int> gpuTempOf(const SystemSnapshot& s) {
    if (auto t = s.tempOf(0x06))
        return t;
    if (s.gpu.tempC >= 0)
        return s.gpu.tempC;
    return std::nullopt;
}
} // namespace

DynamicPolicy::DynamicPolicy(DynamicProfile p) : m_profile(std::move(p)) {}

void DynamicPolicy::setProfile(DynamicProfile p) { m_profile = std::move(p); }

int DynamicPolicy::interpolate(const QList<QPair<int, int>>& table, int tempC) {
    if (table.isEmpty())
        return 0;
    if (tempC <= table.first().first)
        return table.first().second;
    for (int i = 1; i < table.size(); ++i) {
        if (tempC <= table[i].first) {
            const int dx = table[i].first - table[i - 1].first;
            const int dy = table[i].second - table[i - 1].second;
            return table[i - 1].second + (dy * (tempC - table[i - 1].first) + dx / 2) / dx;
        }
    }
    return table.last().second;
}

bool DynamicPolicy::active(const SystemSnapshot& s) {
    int hotEnterC = INT_MAX;
    if (!m_profile.cpuPl1Table.isEmpty())
        hotEnterC = std::min(hotEnterC, m_profile.cpuPl1Table.first().first);
    if (!m_profile.gpuPptTable.isEmpty())
        hotEnterC = std::min(hotEnterC, m_profile.gpuPptTable.first().first);
    if (hotEnterC == INT_MAX)
        return false;

    const auto cpu = s.tempOf(0x01);
    const auto gpu = gpuTempOf(s);
    const int hottest = std::max(cpu.value_or(0), gpu.value_or(0));
    return hottest >= hotEnterC;
}

ControlTargets DynamicPolicy::compute(const SystemSnapshot& s) {
    ControlTargets t;
    const auto cpu = s.tempOf(0x01);
    const auto gpu = gpuTempOf(s);
    if (cpu && !m_profile.cpuPl1Table.isEmpty())
        t.cpuPl1W = interpolate(m_profile.cpuPl1Table, *cpu);
    if (gpu && !m_profile.gpuPptTable.isEmpty())
        t.gpuPptW = interpolate(m_profile.gpuPptTable, *gpu);
    return t;
}

} // namespace dtb
