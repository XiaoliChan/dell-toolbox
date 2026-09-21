#include "core/PolicyChain.h"

#include <algorithm>

namespace dtb {

void PolicyChain::addPolicy(Policy* p) {
    m_policies.append(p);
    std::stable_sort(m_policies.begin(), m_policies.end(),
                     [](const Policy* a, const Policy* b) { return a->priority() < b->priority(); });
}

bool PolicyChain::evaluate(const SystemSnapshot& s, ControlTargets& out, QString& activeName) {
    out = ControlTargets{};
    activeName.clear();
    m_lastActive.clear();
    for (Policy* p : m_policies) {
        if (!p->active(s))
            continue;
        ControlTargets t = p->compute(s);
        out.mergeFrom(t);
        activeName = p->name();
        m_lastActive.append(p);
    }
    return !m_lastActive.isEmpty();
}

QList<QString> PolicyChain::activePolicyNames() const {
    QList<QString> names;
    for (const Policy* p : m_lastActive)
        names.append(p->name());
    return names;
}

} // namespace dtb
