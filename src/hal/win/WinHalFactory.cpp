#include "hal/win/WinHalFactory.h"

#include "hal/win/WinBatteryHAL.h"
#include "hal/win/WinChargeHAL.h"
#include "hal/win/WinGpuHAL.h"
#include "hal/win/WinLoadHAL.h"
#include "hal/win/WinThermalHAL.h"

namespace dtb::win {

WinHalSet createHalSet() {
    WinHalSet out;
    out.thermal = std::make_unique<WinThermalHAL>();
    out.battery = std::make_unique<WinBatteryHAL>();
    out.gpu = std::make_unique<WinGpuHAL>();
    out.load = std::make_unique<WinLoadHAL>();
    out.charge = std::make_unique<WinChargeHAL>();
    out.set.thermal = out.thermal.get();
    out.set.battery = out.battery.get();
    out.set.gpu = out.gpu.get();
    out.set.load = out.load.get();
    out.set.charge = out.charge.get();
    return out;
}

} // namespace dtb::win
