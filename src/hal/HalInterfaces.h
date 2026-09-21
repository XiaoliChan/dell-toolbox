#pragma once

#include <QList>
#include <QString>
#include <QVector>

#include "core/Snapshot.h"

namespace dtb {

struct HalFanInfo {
    FanId id = 0;
    SensorId sensorId = 0;
};

// Real implementations live in src/hal/win (WMI/NVAPI/PDH/cctk); the mock
// lives in src/hal/mock. Read methods return -1 when the value is unavailable
// this cycle; bool returns mean "write accepted".
class IThermalHAL {
public:
    virtual ~IThermalHAL() = default;
    virtual QVector<HalFanInfo> enumerateFans() = 0;
    virtual int sensorTemp(SensorId id) = 0;
    virtual int fanRpm(FanId id) = 0;
    virtual bool setMode(ThermalMode mode) = 0;
    virtual bool setFanPercent(FanId fan, int percent) = 0;
    virtual bool available() = 0;
    // Whether the machine accepts G-Mode (unknown until the first attempt on
    // the real backend; mocks can preset it). UI uses this for the hint banner.
    virtual bool supportsGMode() { return true; }
    // Live thermal profile as the firmware reports it right now (DPM's DA
    // GET). When present, this reflects changes made in AWCC/DPM externally.
    virtual std::optional<ThermalMode> readCurrentProfile() { return std::nullopt; }
};

class IBatteryHAL {
public:
    virtual ~IBatteryHAL() = default;
    virtual BatteryInfo read() = 0;
    virtual bool available() = 0;
};

class IGpuHAL {
public:
    virtual ~IGpuHAL() = default;
    virtual GpuInfo read() = 0;
    virtual bool setPowerLimitW(int watts) = 0;
    virtual bool available() = 0;
};

class ILoadHAL {
public:
    virtual ~ILoadHAL() = default;
    virtual int cpuLoadPercent() = 0;
    virtual QString foregroundProcess() = 0;
};

class IChargeThresholdHAL {
public:
    struct Mode {
        QString name;      // shown in the UI
        QString cctkValue; // value passed to cctk --PrimaryBattChargeCfg
    };
    virtual ~IChargeThresholdHAL() = default;
    virtual QList<Mode> modes() = 0;
    virtual QString currentMode() = 0;
    virtual bool setMode(const QString& cctkValue) = 0;
    virtual bool available() = 0;
    // Backend identity for the UI ("dell-acpi", "cctk", "mock").
    virtual QString sourceName() const { return QStringLiteral("mock"); }
};

struct HalSet {
    IThermalHAL* thermal = nullptr;
    IBatteryHAL* battery = nullptr;
    IGpuHAL* gpu = nullptr;
    ILoadHAL* load = nullptr;
    IChargeThresholdHAL* charge = nullptr;
};

} // namespace dtb
