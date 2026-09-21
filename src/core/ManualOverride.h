#pragma once

#include <QHash>

#include "core/Snapshot.h"

namespace dtb {

// A: the user's direct commands. Not part of the PolicyChain; the Controller
// applies these on top of the chain result while they are active, so sliders
// always win. Expires on its own so the machine never stays on a stale manual
// profile (mirrors the "resume auto" idea from AWCC's game shift).
class ManualOverride {
public:
    int expiresSec = 1800;

    void set(const ControlTargets& t, qint64 nowMs);
    void clear();
    bool active(qint64 nowMs) const;
    ControlTargets targets() const { return m_targets; }

private:
    ControlTargets m_targets;
    qint64 m_expiresMs = 0;
    bool m_set = false;
};

} // namespace dtb
