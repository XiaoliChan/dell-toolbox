#pragma once

#include <QList>
#include <QString>

#include "core/Policy.h"

namespace dtb {

// Ordered evaluation of all policies. Does not own the Policy objects.
class PolicyChain {
public:
    void addPolicy(Policy* p); // kept sorted by ascending priority
    // Merge every active policy's targets (low to high); activeName ends up as
    // the highest-priority active policy. Returns false if none is active.
    bool evaluate(const SystemSnapshot& s, ControlTargets& out, QString& activeName);
    QList<QString> activePolicyNames() const; // from the last evaluate() call

private:
    QList<Policy*> m_policies;
    QList<Policy*> m_lastActive;
};

} // namespace dtb
