#include "hal/win/WinThermalHAL.h"

#include "core/Logger.h"

namespace dtb::win {

namespace {
// Full AWCC profile-code table, exactly the enum awcc_thermal_profile of the
// Linux alienware-wmi driver (the community reference for this firmware
// interface), bucketed into our six UI modes. 0x00 is deliberately unmapped:
// "Custom" here is our own fan-curve feature, never a firmware state we would
// want to adopt from a read.
std::optional<ThermalMode> modeFromProfileCode(int code) {
    switch (code) {
    case 0x96:
        return ThermalMode::Quiet; // legacy quiet
    case 0x97:
        return ThermalMode::Balanced; // legacy balanced
    case 0x98: // legacy balanced-performance
    case 0x99: // legacy performance
        return ThermalMode::Performance;
    case 0xA0:
        return ThermalMode::Balanced; // USTT balanced
    case 0xA1:
        return ThermalMode::Performance; // USTT balanced-performance
    case 0xA2:
        return ThermalMode::Cool; // USTT cool
    case 0xA3:
        return ThermalMode::Quiet; // USTT quiet
    case 0xA4:
        return ThermalMode::Performance; // USTT performance
    case 0xA5:
        return ThermalMode::Quiet; // USTT low power
    case 0xAB:
        return ThermalMode::GMode; // G-Mode
    default:
        return std::nullopt;
    }
}
} // namespace

WinThermalHAL::WinThermalHAL() {
    // The DA channel is optional (machines without it keep everything except
    // live profile readback).
    m_acpi.initialize();
    m_acpiReady = m_acpi.valid();
}

std::optional<ThermalMode> WinThermalHAL::readCurrentProfile() {
    // 1) AWCC native read of the active profile: Thermal_Information op 0x0B,
    //    arg 0 - the same call the Linux alienware-wmi driver uses for
    //    platform_profile_get. This is the only read path that sees G-Mode
    //    (0xAB) set from AWCC. Firmware failure value is 0xFFFFFFFF.
    if (const auto v = thermalInformationOp(kOpGetCurrentProfile, 0); v && *v != -1) {
        if (const auto mode = modeFromProfileCode(*v & 0xFF)) {
            if (*v != m_lastProfileCode) {
                dtbLog(info) << "AWCC current profile code: 0x" << Qt::hex << *v;
                m_lastProfileCode = *v;
            }
            return mode;
        }
    }
    // 2) G-Mode latched state. GameShiftStatus op 0x02 is a pure GET; op 0x01
    //    is a TOGGLE and must never be sent from a read path.
    if (const auto g = m_wmi.callInstanceMethod(L"AWCCWmiMethodFunction", L"GameShiftStatus", kGameShiftGet);
        g && *g != -1 && (*g & 0xFF) == 1) {
        return ThermalMode::GMode;
    }
    // 3) Dell ACPI "DA" channel fallback. It cannot express G-Mode (values
    //    0-8 only) but covers the four USTT modes when AWCC WMI read fails.
    if (!m_acpiReady)
        return std::nullopt;
    DellAcpiChannel::UsttModes modes;
    if (!m_acpi.getUsttModes(modes) || modes.error != 0)
        return std::nullopt;
    // Log raw transitions once: keeps teaching us what the DA side reports.
    if (modes.current != m_lastDaRaw) {
        dtbLog(info) << "DA current thermal mode byte:" << modes.current << "supported:0x"
                     << Qt::hex << modes.supported;
        m_lastDaRaw = modes.current;
    }
    switch (modes.current) {
    case 1:
        return ThermalMode::Balanced;
    case 2:
        return ThermalMode::Cool;
    case 4:
        return ThermalMode::Quiet;
    case 8:
        return ThermalMode::Performance;
    default:
        return std::nullopt; // 0 or vendor-specific states (e.g. G-Mode) are
                             // not representable on the DA side
    }
}

bool WinThermalHAL::available() { return m_wmi.valid(); }

std::optional<int> WinThermalHAL::thermalInformationOp(int op, int id) {
    return m_wmi.callInstanceMethod(L"AWCCWmiMethodFunction", L"Thermal_Information", ((id & 0xFF) << 8) | op);
}

std::optional<int> WinThermalHAL::getFanSensorsOp(int op, int fan, int index) {
    return m_wmi.callInstanceMethod(L"AWCCWmiMethodFunction", L"GetFanSensors",
                                    ((index & 0xFF) << 16) | ((fan & 0xFF) << 8) | op);
}

QVector<HalFanInfo> WinThermalHAL::enumerateFans() {
    if (m_enumerated)
        return m_fans;
    m_enumerated = true;
    if (!m_wmi.valid())
        return m_fans;
    for (FanId fan = kFanFirst; fan <= kFanLast; ++fan) {
        const auto count = getFanSensorsOp(1, fan); // GetFanRelatedSensorsCountById
        if (!count || *count <= 0)
            continue;
        const auto sensor = getFanSensorsOp(2, fan, 0); // first related sensor id
        if (sensor)
            m_fans.append({fan, *sensor});
    }
    return m_fans;
}

int WinThermalHAL::sensorTemp(SensorId id) {
    if (id < kSensorFirst || id > kSensorLast)
        return -1;
    const auto t = thermalInformationOp(4, id); // GetSensorTemperature
    return t.value_or(-1);
}

int WinThermalHAL::fanRpm(FanId id) {
    if (id < kFanFirst || id > kFanLast)
        return -1;
    const auto rpm = thermalInformationOp(5, id); // GetFanRPM
    return rpm.value_or(-1);
}

bool WinThermalHAL::applyModeByte(int modeByte) {
    const auto res = m_wmi.callInstanceMethod(L"AWCCWmiMethodFunction", L"Thermal_Control",
                                              ((modeByte & 0xFF) << 8) | 1);
    const bool ok = res && *res == 0;
    dtbLog(info) << "Thermal_Control mode 0x" << Qt::hex << modeByte << " -> " << (res ? *res : -1)
                 << (ok ? " (ok)" : " (REJECTED)");
    return ok;
}

// Game Shift is an EC LATCH on G-series, not just a thermal mode byte:
// while it latches, the EC re-asserts 0xAB seconds after anything else is
// written (the observed bounce). The kernel alienware-wmi driver mirrors
// this: read the latch (GameShiftStatus op 0x02) and TOGGLE it (op 0x01)
// whenever the target mode's G-state differs, then apply the mode byte.
std::optional<int> WinThermalHAL::gameShiftState() {
    // low byte of the reply: 1 = G-Mode latched, 0 = off; invalid on machines
    // without game shift (callers treat nullopt as "no latch support")
    return m_wmi.callInstanceMethod(L"AWCCWmiMethodFunction", L"GameShiftStatus", kGameShiftGet);
}

// Set the game shift latch to `want` (1 = G-Mode on, 0 = off).
//
// Semantics per tr1xem/AWCC (the Linux implementation validated on many
// G-series machines): GameShiftStatus op 0x01 ENGAGES the latch and op 0x00
// DISENGAGES it - a direct state set. The kernel driver's TOGGLE label for
// op 0x01 is a misreading: treating it as a toggle left the latch engaged
// when leaving G-Mode and the EC re-asserted 0xAB over the new mode write
// (the "bounces back to G-Mode" symptom). Verified read-back via op 0x02
// when the firmware supports it.
bool WinThermalHAL::setGameShift(int want) {
    const int op = want ? kGameShiftOn : kGameShiftOff;
    const auto r = m_wmi.callInstanceMethod(L"AWCCWmiMethodFunction", L"GameShiftStatus", op);
    if (!r)
        return false;
    if (const auto state = gameShiftState(); state && *state >= 0 && *state <= 1 && *state != want)
        dtbLog(warn) << "game shift: wanted" << want << "but reads" << *state;
    return true;
}

bool WinThermalHAL::setMode(ThermalMode mode) {
    if (!m_wmi.valid())
        return false;

    // Leave/enter the G-Mode latch first, or the EC fights the write.
    // Decision input is the CURRENT THERMAL MODE (op 0x0B - reliably
    // readable), exactly like tr1xem/AWCC: the latch read (op 0x02) may
    // return shapes we do not recognize, and gating on it silently skipped
    // the disengage (mode bounced back to G-Mode).
    if (const auto current = readCurrentProfile()) {
        if (*current == ThermalMode::GMode && mode != ThermalMode::GMode)
            setGameShift(0); // disengage, or the EC re-asserts 0xAB later
        else if (*current != ThermalMode::GMode && mode == ThermalMode::GMode)
            setGameShift(1); // engage
    }


    // Balanced has a legacy (0x97) and a USTT (0xA0) encoding; probe USTT once
    // and remember which the machine accepts (tcc-g15 semantics).
    if (mode == ThermalMode::Balanced) {
        if (m_balancedUstt == TriState::Unknown) {
            m_balancedUstt = applyModeByte(kModeBalancedUstt) ? TriState::Yes : TriState::No;
        }
        return applyModeByte(m_balancedUstt == TriState::Yes ? kModeBalancedUstt : kModeBalancedLegacy);
    }

    if (mode == ThermalMode::GMode) {
        // G3 3590 DOES support 0xAB (tcc-g15 proves it); no fallback — if the
        // write fails we surface the failure instead of guessing.
        const bool ok = applyModeByte(kModeGMode);
        m_gMode = ok ? TriState::Yes : TriState::No;
        return ok;
    }

    int value = kModeCustom;
    switch (mode) {
    case ThermalMode::Quiet:
        value = kModeQuietUstt;
        break;
    case ThermalMode::Cool:
        value = kModeCoolUstt;
        break;
    case ThermalMode::Performance:
        value = kModePerformanceUstt;
        break;
    default:
        value = kModeCustom;
        break;
    }
    return applyModeByte(value);
}

bool WinThermalHAL::setFanPercent(FanId fan, int percent) {
    if (fan < kFanFirst || fan > kFanLast || !m_wmi.valid())
        return false;
    const int spd = qBound(0, percent, 100);
    const auto res = m_wmi.callInstanceMethod(L"AWCCWmiMethodFunction", L"Thermal_Control",
                                              ((spd & 0xFF) << 16) | ((fan & 0xFF) << 8) | 2);
    return res && *res == 0;
}

} // namespace dtb::win
