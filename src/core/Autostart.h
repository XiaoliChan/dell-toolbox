#pragma once

namespace dtb {

// Start-at-login via Task Scheduler (schtasks), mirroring the tcc-g15
// template in res/autostart-task.xml. Non-Windows builds are no-ops.
class Autostart {
public:
    static bool enabled();
    static bool setEnabled(bool on);
};

} // namespace dtb
