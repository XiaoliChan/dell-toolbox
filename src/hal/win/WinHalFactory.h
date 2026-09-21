#pragma once

#include <memory>

#include "hal/HalInterfaces.h"

namespace dtb::win {

// Owns the real Windows backend objects and exposes them as a HalSet.
struct WinHalSet {
    HalSet set;
    std::unique_ptr<IThermalHAL> thermal;
    std::unique_ptr<IBatteryHAL> battery;
    std::unique_ptr<IGpuHAL> gpu;
    std::unique_ptr<ILoadHAL> load;
    std::unique_ptr<IChargeThresholdHAL> charge;
};

WinHalSet createHalSet();

} // namespace dtb::win
