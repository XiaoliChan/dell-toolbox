#include "core/ManualOverride.h"

namespace dtb {

void ManualOverride::set(const ControlTargets& t, qint64 nowMs) {
    m_targets = t;
    m_expiresMs = nowMs + qint64(expiresSec) * 1000;
    m_set = true;
}

void ManualOverride::clear() {
    m_set = false;
    m_targets = ControlTargets{};
    m_expiresMs = 0;
}

bool ManualOverride::active(qint64 nowMs) const { return m_set && nowMs < m_expiresMs; }

} // namespace dtb
