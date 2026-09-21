#pragma once

#include <QHash>
#include <QString>
#include <QVector>

#include <optional>

namespace dtb {

using FanId = int;    // AWCC fan ids: 0x31-0x63 (0x33 = CPU, 0x32 = GPU on G3 3590)
using SensorId = int; // AWCC sensor ids: 0x01-0x30 (0x01 = CPU, 0x06 = GPU on G3 3590)

// AWCC thermal profiles (tcc-g15 WMI doc): USTT variants are what AWCC shows
// on G-series (Quiet/Cool/Balanced/Performance); GMode exists on G15+ only.
enum class ThermalMode { Quiet, Cool, Balanced, Performance, GMode, Custom };

struct SensorReading {
    SensorId id = 0;
    int tempC = -1;
    bool valid() const { return tempC >= 0; }
};

struct FanReading {
    FanId id = 0;
    int rpm = -1;
    bool valid() const { return rpm >= 0; }
};

struct GpuInfo {
    int utilPercent = 0;
    int powerW = 0;
    int tempC = -1;
};

// All capacity values in mWh, voltage in mV, rate in mW; -1/empty = unknown.
// Mirrors root\WMI MSBatteryClass plus derived health.
struct BatteryInfo {
    bool present = false;
    bool acOnline = false;
    bool charging = false;
    int percent = -1;
    int healthPercent = -1;
    int cycleCount = -1;
    int designCapacityMWh = -1;
    int fullChargeCapacityMWh = -1;
    int remainingCapacityMWh = -1;
    int voltageMV = -1;
    int rateMW = 0;
    QString deviceName;
    QString vendor;
    QString serial;
    QString chemistry;
};

// One full sample of the machine, taken once per control tick.
// Convention for G3 3590 (two fans): sensors[0] = CPU, sensors[1] = GPU.
struct SystemSnapshot {
    qint64 tsMs = 0;
    QVector<SensorReading> sensors;
    QVector<FanReading> fans;
    int cpuLoadPercent = 0;
    GpuInfo gpu;
    BatteryInfo battery;
    QString foregroundProcess; // lowercased, path stripped

    std::optional<int> tempOf(SensorId id) const {
        for (const auto& s : sensors)
            if (s.id == id && s.valid())
                return s.tempC;
        return std::nullopt;
    }
};

// What the controller should apply this tick. Fields set by the
// highest-priority active policy win; the rest fall through to lower ones.
struct ControlTargets {
    std::optional<ThermalMode> mode;
    QHash<FanId, int> fanPercent;
    std::optional<int> cpuPl1W;
    std::optional<int> cpuPl2W;
    std::optional<int> gpuPptW;

    bool any() const {
        return mode.has_value() || !fanPercent.isEmpty() || cpuPl1W.has_value()
            || cpuPl2W.has_value() || gpuPptW.has_value();
    }

    // Fields `hi` sets override ours; fields it leaves unset are kept.
    void mergeFrom(const ControlTargets& hi) {
        if (hi.mode)
            mode = hi.mode;
        for (auto it = hi.fanPercent.constBegin(); it != hi.fanPercent.constEnd(); ++it)
            fanPercent.insert(it.key(), it.value());
        if (hi.cpuPl1W)
            cpuPl1W = hi.cpuPl1W;
        if (hi.cpuPl2W)
            cpuPl2W = hi.cpuPl2W;
        if (hi.gpuPptW)
            gpuPptW = hi.gpuPptW;
    }
};

} // namespace dtb
