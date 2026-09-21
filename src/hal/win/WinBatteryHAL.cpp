#include "hal/win/WinBatteryHAL.h"

#include <windows.h>
#include <setupapi.h>

#include <vector>

namespace dtb::win {
namespace {
// Battery-driver cycle count, mirroring Dell Power Manager's PlatformAPI:
// BATTERY_INFORMATION.CycleCount is read via IOCTLs on the battery PDO.
// MSBatteryClass.CycleCount is a separate counter that Dell firmware often
// leaves at 0.
constexpr ULONG kIoctlBatteryQueryTag = 0x00290040;         // CTL_CODE(0x29, 0x10, METHOD_BUFFERED, FILE_ANY_ACCESS)
constexpr ULONG kIoctlBatteryQueryInformation = 0x00294044; // CTL_CODE(0x29, 0x11, METHOD_BUFFERED, FILE_READ_ACCESS)

std::optional<int> queryCycleCountFromDriver() {
    // GUID_DEVCLASS_BATTERY
    const GUID batteryClass = {0x72631e54, 0x78a4, 0x11d0, {0xbc, 0xf7, 0x00, 0xaa, 0x00, 0xb7, 0xb3, 0x2a}};
    HDEVINFO devInfo =
        SetupDiGetClassDevsW(&batteryClass, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE)
        return std::nullopt;
    std::optional<int> found;
    for (DWORD i = 0; !found && i < 8; ++i) {
        SP_DEVICE_INTERFACE_DATA ifData = {};
        ifData.cbSize = sizeof(ifData);
        if (!SetupDiEnumDeviceInterfaces(devInfo, nullptr, &batteryClass, i, &ifData))
            break;
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, nullptr, 0, &needed, nullptr);
        if (needed == 0)
            continue;
        std::vector<BYTE> buf(needed, 0);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, detail, needed, nullptr, nullptr))
            continue;
        HANDLE handle = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            continue;
        ULONG tag = 0;
        DWORD returned = 0;
        if (DeviceIoControl(handle, kIoctlBatteryQueryTag, nullptr, 0, &tag, sizeof(tag), &returned, nullptr) &&
            tag != 0) {
            struct QueryInfo {
                ULONG tag;
                ULONG level; // 0 = BatteryInformation
                LONG atRate;
            } query{tag, 0, 0};
            struct BatteryInformation {
                ULONG capabilities;
                UCHAR technology;
                UCHAR reserved[3];
                ULONG chemistry;
                ULONG designedCapacity;
                ULONG fullChargedCapacity;
                ULONG defaultAlert1;
                ULONG defaultAlert2;
                ULONG criticalBias;
                ULONG cycleCount;
            } info = {};
            static_assert(sizeof(BatteryInformation) == 36);
            if (DeviceIoControl(handle, kIoctlBatteryQueryInformation, &query, sizeof(query), &info, sizeof(info),
                                &returned, nullptr))
                found = static_cast<int>(info.cycleCount);
        }
        CloseHandle(handle);
    }
    SetupDiDestroyDeviceInfoList(devInfo);
    return found;
}
} // namespace

WinBatteryHAL::WinBatteryHAL() = default;

bool WinBatteryHAL::available() { return m_wmi.valid(); }

BatteryInfo WinBatteryHAL::read() {
    BatteryInfo info;
    m_wmi.execQuery(L"ROOT\\WMI", L"SELECT * FROM MSBatteryClass", [&](IWbemClassObject* obj) {
        info.present = true;
        const auto online = WmiConnection::readBoolProp(obj, L"PowerOnline");
        const auto charging = WmiConnection::readBoolProp(obj, L"Charging");
        const auto discharging = WmiConnection::readBoolProp(obj, L"Discharging");
        const auto designed = WmiConnection::readIntProp(obj, L"DesignedCapacity");
        const auto full = WmiConnection::readIntProp(obj, L"FullChargedCapacity");
        const auto remaining = WmiConnection::readIntProp(obj, L"RemainingCapacity");
        const auto cycles = WmiConnection::readIntProp(obj, L"CycleCount");
        const auto voltage = WmiConnection::readIntProp(obj, L"Voltage");
        const auto chargeRate = WmiConnection::readIntProp(obj, L"ChargeRate");
        const auto dischargeRate = WmiConnection::readIntProp(obj, L"DischargeRate");
        const auto deviceName = WmiConnection::readStringProp(obj, L"DeviceName");
        const auto vendor = WmiConnection::readStringProp(obj, L"ManufactureName");
        const auto serial = WmiConnection::readStringProp(obj, L"SerialNumber");
        const auto chemistry = WmiConnection::readStringProp(obj, L"Chemistry");

        if (online)
            info.acOnline = info.acOnline || *online;
        info.charging = info.charging || (charging && *charging);
        if (designed && *designed > 0)
            info.designCapacityMWh = *designed;
        if (full && *full > 0)
            info.fullChargeCapacityMWh = *full;
        if (remaining && *remaining >= 0)
            info.remainingCapacityMWh = *remaining;
        if (cycles && *cycles > 0) // 0 is a stub value on Dell firmware
            info.cycleCount = *cycles;
        if (voltage && *voltage > 0)
            info.voltageMV = *voltage;
        if (chargeRate && *chargeRate > 0)
            info.rateMW = *chargeRate;
        else if (dischargeRate && *dischargeRate > 0)
            info.rateMW = -*dischargeRate;
        if (deviceName && !deviceName->isEmpty())
            info.deviceName = *deviceName;
        if (vendor && !vendor->isEmpty())
            info.vendor = *vendor;
        if (serial && !serial->isEmpty())
            info.serial = *serial;
        if (chemistry && !chemistry->isEmpty())
            info.chemistry = *chemistry;
    });

    if (!info.present)
        return info;

    // Cycle count: prefer the driver value (official-aligned); fall back to
    // the Dell BatteryCycleCount WMI class. A count of 0 on a worn battery is
    // a stub, so unknown stays -1 and the UI shows "-".
    if (info.cycleCount <= 0) {
        if (const auto viaDriver = queryCycleCountFromDriver())
            info.cycleCount = *viaDriver;
        else
            m_wmi.execQuery(L"ROOT\\WMI", L"SELECT * FROM BatteryCycleCount", [&](IWbemClassObject* obj) {
                const auto v = WmiConnection::readIntProp(obj, L"CycleCount");
                if (v && *v > 0 && info.cycleCount <= 0)
                    info.cycleCount = *v;
            });
    }

    // Percentage from capacities (matches Dell Power Manager's math). When the
    // firmware does not report them, percent stays -1 and the UI shows "-"
    // (MSBatteryClass has no estimated-charge property to fall back to).
    if (info.remainingCapacityMWh >= 0 && info.fullChargeCapacityMWh > 0)
        info.percent = qBound(0, qRound(info.remainingCapacityMWh * 100.0 / info.fullChargeCapacityMWh), 100);
    if (info.designCapacityMWh > 0 && info.fullChargeCapacityMWh > 0)
        info.healthPercent = qBound(0, qRound(info.fullChargeCapacityMWh * 100.0 / info.designCapacityMWh), 100);
    return info;
}

} // namespace dtb::win
