#pragma once

#include "core/Snapshot.h"
#include "hal/win/WmiConnection.h"

namespace dtb::win {

// Battery readings from root\WMI MSBatteryClass — one class carries status
// plus static data (design/full capacity, cycles, identity). This is the same
// source Dell Power Manager-style tools use on G-series.
class WinBatteryHAL : public IBatteryHAL {
public:
    WinBatteryHAL();

    BatteryInfo read() override;
    bool available() override;

private:
    WmiConnection m_wmi; // ROOT\WMI
};

} // namespace dtb::win
