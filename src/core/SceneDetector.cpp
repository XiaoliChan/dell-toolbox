#include "core/SceneDetector.h"

#include <algorithm>

namespace dtb {

SceneDetector::SceneDetector(SceneParams p) : m_params(std::move(p)) {}

void SceneDetector::setParams(SceneParams p) {
    m_params = std::move(p);
    if (!m_params.enabled) {
        // Disabling must leave the scene immediately: the control loop skips
        // onTick() while disabled, so without this reset the scene policy
        // would keep its last state (G-Mode + scene power targets) pinned
        // forever instead of falling back to the selected profile.
        m_inScene = false;
        m_streak = 0;
        m_trigger.clear();
    }
}

QString SceneDetector::normalizeProcessName(const QString& pathOrName) {
    QString s = pathOrName;
    const int slash = std::max(s.lastIndexOf(QLatin1Char('/')), s.lastIndexOf(QLatin1Char('\\')));
    if (slash >= 0)
        s = s.mid(slash + 1);
    return s.toLower();
}

QString SceneDetector::matchProcess(const QString& normalized) const {
    if (normalized.isEmpty())
        return {};
    for (const QString& entry : m_params.gameProcesses) {
        const QString want = normalizeProcessName(entry);
        if (!want.isEmpty() && want == normalized)
            return want;
    }
    return {};
}

void SceneDetector::onTick(const SystemSnapshot& s) {
    const QString proc = normalizeProcessName(s.foregroundProcess);
    const QString procHit = matchProcess(proc);
    const bool gpuEnter = s.gpu.utilPercent >= m_params.gpuEnterThreshold;
    const bool gpuSustain = s.gpu.utilPercent >= m_params.gpuExitThreshold;
    const int enterTicks = std::max(1, m_params.enterDebounceS);
    const int exitTicks = std::max(1, m_params.exitDebounceS);

    if (!m_inScene) {
        if (gpuEnter || !procHit.isEmpty())
            ++m_streak;
        else
            m_streak = 0;
        if (m_streak >= enterTicks) {
            m_inScene = true;
            m_streak = 0;
            m_trigger = procHit.isEmpty() ? QStringLiteral("gpu") : QStringLiteral("process:") + procHit;
        }
    } else {
        if (gpuSustain || !procHit.isEmpty())
            m_streak = 0;
        else
            ++m_streak;
        if (m_streak >= exitTicks) {
            m_inScene = false;
            m_trigger.clear();
            m_streak = 0;
        }
    }
}

} // namespace dtb
