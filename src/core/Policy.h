#pragma once

#include <QString>

#include "core/Snapshot.h"

namespace dtb {

enum class PolicyPriority { Baseline = 0, Dynamic = 1, Scene = 2, Manual = 3, Failsafe = 4 };

// One layer of the control decision. The chain evaluates policies from low to
// high priority; every active policy contributes targets, higher ones
// overriding fields the lower ones set.
class Policy {
public:
    virtual ~Policy() = default;
    virtual QString name() const = 0;
    virtual PolicyPriority priority() const = 0;
    virtual bool active(const SystemSnapshot& s) = 0;    // wants control this tick?
    virtual ControlTargets compute(const SystemSnapshot& s) = 0;
};

} // namespace dtb
