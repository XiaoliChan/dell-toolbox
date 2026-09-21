#pragma once

#include "core/Snapshot.h"
#include "hal/win/DellAcpiChannel.h"
#include "hal/win/WmiConnection.h"

namespace dtb::win {

// AWCC thermal WMI backend. Every constant mirrors
// tcc-g15/src/Backend/AWCCWmiWrapper.py exactly; do not "clean them up".
class WinThermalHAL : public IThermalHAL {
public:
    WinThermalHAL();

    QVector<HalFanInfo> enumerateFans() override;
    int sensorTemp(SensorId id) override;
    int fanRpm(FanId id) override;
    bool setMode(ThermalMode mode) override;
    bool setFanPercent(FanId fan, int percent) override;
    bool available() override;
    bool supportsGMode() override { return m_gMode != TriState::No; }
    std::optional<ThermalMode> readCurrentProfile() override;

    std::optional<int> gameShiftState(); // EC G-Mode latch: 1 latched, 0 off
    bool toggleGameShift();

private:
    bool applyModeByte(int modeByte);
    std::optional<int> thermalInformationOp(int op, int id);
    std::optional<int> getFanSensorsOp(int op, int fan, int index = 0);

    WmiConnection m_wmi; // ROOT\WMI
    DellAcpiChannel m_acpi; // firmware-side profile readback (DPM's channel)
    bool m_acpiReady = false;
    int m_lastProfileCode = -1; // last raw AWCC profile code (log on change)
    int m_lastDaRaw = -1; // log DA raw-value transitions (G-Mode encoding hunt)
    QVector<HalFanInfo> m_fans; // discovered once
    bool m_enumerated = false;
    // Capability caches, inherited from tcc-g15's Balanced USTT patch idea:
    // first write of a mode probes whether the machine accepts it.
    enum class TriState { Unknown, Yes, No };
    TriState m_balancedUstt = TriState::Unknown; // write 0xA0 instead of 0x97?
    TriState m_gMode = TriState::Unknown;        // G-Mode supported at all?

    static constexpr SensorId kSensorFirst = 0x01;
    static constexpr SensorId kSensorLast = 0x30;
    static constexpr FanId kFanFirst = 0x31;
    static constexpr FanId kFanLast = 0x63;
    // AWCC thermal profile bytes (tcc-g15 WMI-AWCC-doc.md ThermalMode enum).
    static constexpr int kModeCustom = 0x00;
    // Read-side op codes, per the Linux alienware-wmi driver:
    static constexpr int kOpGetCurrentProfile = 0x0B; // Thermal_Information: active mode code
    static constexpr int kGameShiftGet = 0x02;    // GameShiftStatus GET
    static constexpr int kGameShiftToggle = 0x01; // GameShiftStatus TOGGLE (leaving/entering G-Mode)
    static constexpr int kModeQuietUstt = 0xA3;
    static constexpr int kModeCoolUstt = 0xA2;
    static constexpr int kModeBalancedLegacy = 0x97;
    static constexpr int kModeBalancedUstt = 0xA0;
    static constexpr int kModePerformanceUstt = 0xA1;
    static constexpr int kModeGMode = 0xAB;
};

} // namespace dtb::win
