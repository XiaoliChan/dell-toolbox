#pragma once

#include <QString>
#include <vector>

#include "hal/win/DpmServiceClient.h"
#include "hal/win/WmiConnection.h"

namespace dtb::win {

// Dell ACPI-WMI calling interface ("DA" channel) — the same path Dell Power
// Manager uses. Protocol (recovered from DPM 3.18.0 binaries, see
// docs/awcc-analysis/02-oc-semantics.md §6):
//   root\wmi WMI_Query.QDATA  -> 16-byte descriptor: "DELL", " WMI", version,
//                                buffer length
//   root\wmi BDat instance with Bytes = call buffer, passed as the Data
//   parameter of BFn.DoBFn on the instance whose InstanceName is
//   "ACPI\PNP0C14\0_0"; the returned Bytes hold the BIOS response.
class DellAcpiChannel {
public:
    // Battery charge-mode byte values used inside the DA buffer.
    enum ChargeMode : unsigned char {
        ChargeStandard = 1,
        ChargeExpress = 2,
        ChargePredominatelyAc = 3,
        ChargeAuto = 4,
        ChargeCustom = 5,
        ChargeLongLife = 6,
        ChargeAdvanced = 7,
    };
    // Capability bits returned by the BIOS (BatteryInformationBitMasks).
    enum Capability : unsigned int {
        CapStandard = 1u,
        CapExpress = 2u,
        CapPredominatelyAc = 4u,
        CapAuto = 8u,
        CapCustom = 16u,
        CapLongLife = 32u,
        CapAdvanced = 64u,
    };

    struct ChargeState {
        int mode = -1; // current ChargeMode, -1 unknown
        int start = -1;
        int stop = -1;
        unsigned int capability = 0;
        int allowableStart = -1;
        int allowableStop = -1;
        int granularity = -1;
        unsigned int error = 0; // cbRes1 of the response; 0 == success
    };

    void initialize();
    bool valid() const { return m_valid; }
    int bufferLength() const { return m_bufferLength; }
    QString transport() const { return m_transport; } // "dpm-service" | "bfn-wmi"

    // Run one DA call: buffer in/out. Returns false on any WMI-level failure.
    bool execute(std::vector<unsigned char>& buffer);

    struct UsttModes {
        unsigned char current = 0; // bit0=Balanced/Optimized, bit1=Cool, bit2=Quiet, bit3=Ultra
        unsigned char supported = 0;
        unsigned int error = 0; // cbRes1; 0 == success
    };

    // Read the firmware's live thermal-profile state (the same GET Dell Power
    // Manager polls; this is how external changes become visible to us).
    bool getUsttModes(UsttModes& out);

    bool getChargeState(ChargeState& out); // battery 1
    // start/stop are only written when mode == ChargeCustom.
    bool setChargeMode(unsigned char mode, unsigned char start = 0, unsigned char stop = 0);

private:
    WmiConnection m_wmi{L"root\\wmi"};
    DpmServiceClient m_service;
    QString m_transport;
    bool m_valid = false;
    int m_bufferLength = 128;
};

} // namespace dtb::win
