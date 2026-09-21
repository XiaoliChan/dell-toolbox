#include "hal/win/DellAcpiChannel.h"

#include <QLoggingCategory>

#include <atlcomcli.h>
#include <comdef.h>

namespace dtb::win {
namespace {
Q_LOGGING_CATEGORY(lcAcpi, "dtb.dellacpi")

constexpr wchar_t kInstanceName[] = L"ACPI\\PNP0C14\\0_0";

// DA buffer offsets (packed, little endian) — see the docs reference above.
namespace buf {
constexpr int kClass = 0;       // u16
constexpr int kSelector = 2;    // u16
constexpr int kCommand = 4;     // u8
constexpr int kBatteryNumber = 5; // u8
constexpr int kChargeMode = 8;  // u8 (SET header)
constexpr int kStart = 9;       // u8 (SET header)
constexpr int kStop = 10;       // u8 (SET header)
constexpr int kSetCbRes1 = 20;  // u32 (SET header, same standard-interface slot as GET)
constexpr int kGetCbArg2 = 8;   // u32 (GET header)
constexpr int kGetCbRes1 = 20;  // u32 (GET header)
constexpr int kGetCurrentMode = 24; // u8 (GET header)
constexpr int kGetStart = 25;   // u8
constexpr int kGetStop = 26;    // u8
constexpr int kGetCapability = 28; // u32
constexpr int kGetAllowStart = 32; // u8
constexpr int kGetAllowStop = 33;  // u8
constexpr int kGetGranularity = 34; // u8
constexpr int kGetArgAttrib = 36;  // u32
constexpr int kGetBLength = 40;    // u32
// DA_CALL_STANDARD_INTERFACE offsets (used by the USTT calls)
constexpr int kStdCbArg1 = 4;   // u32
constexpr int kStdCbRes1 = 20;  // u32
constexpr int kStdCbRes2 = 24;  // u32
constexpr int kStdCbRes3 = 28;  // u32
constexpr int kStdSize = 44;
constexpr unsigned short kClassThermal = 17;
constexpr unsigned short kSelectorUstt = 19;
constexpr unsigned int kUsttGet = 0; // cbArg1 command: 0 = get tables
constexpr unsigned int kUsttSet = 1; // cbArg1 command: 1 = set mode

constexpr int kSetHeaderSize = 44;
constexpr int kGetHeaderSize = 44;
constexpr unsigned int kRequestMarker = 0xFFFFFFFDu;
constexpr unsigned short kClassBattery = 8;
constexpr unsigned short kSelectorGet = 18; // GetBatteryInfoEx
constexpr unsigned short kSelectorSet = 19; // SetBatteryInfoEx
constexpr unsigned char kCommandSet = 0;
constexpr unsigned char kCommandGet = 3;
} // namespace buf

std::vector<unsigned char> variantToBytes(const VARIANT& v) {
    std::vector<unsigned char> out;
    if (v.vt != (VT_ARRAY | VT_UI1) || !v.parray)
        return out;
    SAFEARRAY* psa = v.parray;
    LONG lo = 0, hi = 0;
    SafeArrayGetLBound(psa, 1, &lo);
    SafeArrayGetUBound(psa, 1, &hi);
    if (hi < lo)
        return out;
    out.resize(hi - lo + 1);
    unsigned char* data = nullptr;
    if (SUCCEEDED(SafeArrayAccessData(psa, reinterpret_cast<void**>(&data)))) {
        memcpy(out.data(), data, out.size());
        SafeArrayUnaccessData(psa);
    }
    return out;
}
} // namespace

void DellAcpiChannel::initialize() {
    // Preferred transport: Dell Power Manager's own privileged service (no
    // driver prerequisites beyond DPM being installed).
    m_service.initialize();
    if (m_service.valid()) {
        m_valid = true;
        m_transport = QStringLiteral("dpm-service");
        // The service validates the buffer length against its own DA
        // descriptor; probe the usual sizes once with a harmless GET.
        // An Invoke-level rejection (some COM+ installs refuse foreign
        // callers outright) is size-independent: stop probing immediately
        // and let the BFn WMI path take over - probing all nine sizes just
        // spams the log.
        const int candidates[] = {128, 256, 512, 1024, 2048, 4096, 6144, 8192, 64};
        for (int size : candidates) {
            m_bufferLength = size;
            DellAcpiChannel::UsttModes probeModes;
            if (getUsttModes(probeModes) && probeModes.error == 0) {
                qCInfo(lcAcpi) << "Dell ACPI channel via DPM service, buffer" << m_bufferLength;
                return;
            }
            if (m_service.rejecting())
                break;
        }
        // Service present but no size worked - keep probing the WMI path too.
        m_valid = false;
    }

    if (!m_wmi.valid())
        return;
    // The BFn class is the actual requirement; WMI_Query only refines the
    // buffer length. Some firmwares expose BFn without a well-formed
    // WMI_Query instance, so BFn presence alone enables the channel.
    Microsoft::WRL::ComPtr<IEnumWbemClassObject> probe;
    bool hasBFn = SUCCEEDED(m_wmi.services()->CreateInstanceEnum(
                                _bstr_t(L"BFn"), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                nullptr, probe.GetAddressOf()))
               && [&] {
                   Microsoft::WRL::ComPtr<IWbemClassObject> o;
                   ULONG n = 0;
                   return SUCCEEDED(probe->Next(WBEM_INFINITE, 1, o.ReleaseAndGetAddressOf(), &n)) && n > 0;
               }();
    if (!hasBFn) {
        qCInfo(lcAcpi) << "Dell ACPI-WMI: BFn class not present";
        return;
    }
    m_valid = true;
    // Reset whatever the service size-probing above may have left behind; the
    // QDATA descriptor below (when present) is the authoritative length.
    m_bufferLength = 128;
    // Capability check: WMI_Query instance whose QDATA descriptor starts with
    // vendor "DELL" and signature " WMI"; descriptor offsets (Pack=1 struct):
    // 0 u32 vendor, 4 u32 signature, 8 u32 version, 12 u32 buffer length.
    // Per the official WmiDescriptor.Initialize (decomp): the instance must be
    // ACPI\PNP0C14\0_0 AND QDATA is an EMBEDDED OBJECT whose "Bytes" property
    // carries the descriptor - reading QDATA directly yields VT_UNKNOWN, which
    // is why the scan here silently found nothing for so long.
    bool haveQdata = false;
    auto onInstance = [&](IWbemClassObject* obj) {
        VARIANT name;
        VariantInit(&name);
        if (FAILED(obj->Get(L"InstanceName", 0, &name, nullptr, nullptr))) {
            VariantClear(&name);
            return;
        }
        const bool rightInstance = name.vt == VT_BSTR && name.bstrVal
                                && _wcsicmp(name.bstrVal, kInstanceName) == 0;
        VariantClear(&name);
        if (!rightInstance)
            return;
        VARIANT qdata;
        VariantInit(&qdata);
        if (FAILED(obj->Get(L"QDATA", 0, &qdata, nullptr, nullptr)))
            return;
        std::vector<unsigned char> bytes;
        if (qdata.vt == VT_UNKNOWN && qdata.punkVal) {
            Microsoft::WRL::ComPtr<IWbemClassObject> qobj;
            if (SUCCEEDED(qdata.punkVal->QueryInterface(IID_PPV_ARGS(qobj.GetAddressOf())))) {
                VARIANT b;
                VariantInit(&b);
                if (SUCCEEDED(qobj->Get(L"Bytes", 0, &b, nullptr, nullptr)))
                    bytes = variantToBytes(b);
                VariantClear(&b);
            }
        }
        VariantClear(&qdata);
        if (bytes.size() < 16)
            return;
        if (memcmp(bytes.data(), "DELL", 4) != 0 || memcmp(bytes.data() + 4, " WMI", 4) != 0)
            return;
        const unsigned int len = bytes[12] | (bytes[13] << 8) | (bytes[14] << 16)
                               | (unsigned int)(bytes[15] << 24);
        if (len >= 44 && len <= 65536) {
            m_bufferLength = static_cast<int>(len);
            haveQdata = true;
            qCInfo(lcAcpi) << "QDATA descriptor: version" << bytes[8] << "bufferLength" << len;
        } else {
            qCWarning(lcAcpi) << "QDATA descriptor has unusable buffer length:" << len;
        }
    };
    Microsoft::WRL::ComPtr<IEnumWbemClassObject> it;
    if (FAILED(m_wmi.services()->CreateInstanceEnum(_bstr_t(L"WMI_Query"),
                                                    WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                                    nullptr, it.GetAddressOf())))
        return;
    Microsoft::WRL::ComPtr<IWbemClassObject> obj;
    ULONG got = 0;
    while (!haveQdata && SUCCEEDED(it->Next(WBEM_INFINITE, 1, obj.ReleaseAndGetAddressOf(), &got)) && got > 0)
        onInstance(obj.Get());
    if (!haveQdata) {
        // No well-formed QDATA descriptor: discover the buffer length the
        // firmware actually registered by probing with a harmless battery GET
        // (battery, not thermal: the thermal GET reports an error while
        // G-Mode is active, which would defeat the probe).
        static const int kCandidates[] = {6144, 4096, 2048, 1024, 512, 256, 128};
        for (int size : kCandidates) {
            m_bufferLength = size;
            DellAcpiChannel::ChargeState probeState;
            if (getChargeState(probeState) && probeState.error == 0 && probeState.mode != -1) {
                qCInfo(lcAcpi) << "BFn buffer length probed:" << size;
                break;
            }
        }
    }
    if (m_valid) {
        m_transport = QStringLiteral("bfn-wmi");
        qCInfo(lcAcpi) << "Dell ACPI-WMI channel ready via BFn, buffer" << m_bufferLength
                       << (haveQdata ? "(qdata)" : "(probed/default)");
    }
}

bool DellAcpiChannel::execute(std::vector<unsigned char>& buffer) {
    if (!m_valid || buffer.size() < 44) {
        qCWarning(lcAcpi) << "DA execute: channel invalid or buffer short";
        return false;
    }
    if (m_transport == QLatin1String("dpm-service"))
        return m_service.execute(buffer);
    buffer.resize(m_bufferLength);

    // Every failure point logs its step and HRESULT: the BFn path has never
    // been observed succeeding on a real machine yet, and a silent false
    // gives nothing to debug from.
    auto fail = [](const char* step, HRESULT hr) {
        qCWarning(lcAcpi) << "DA execute failed at" << step << "hr=0x" << Qt::hex
                          << static_cast<unsigned int>(hr);
        return false;
    };

    HRESULT hr;
    // BDat instance with Bytes = buffer
    Microsoft::WRL::ComPtr<IWbemClassObject> bdatClass, bdatInst;
    hr = m_wmi.services()->GetObject(_bstr_t(L"BDat"), 0, nullptr, bdatClass.GetAddressOf(), nullptr);
    if (FAILED(hr) || !bdatClass)
        return fail("GetObject(BDat)", hr);
    hr = bdatClass->SpawnInstance(0, bdatInst.GetAddressOf());
    if (FAILED(hr))
        return fail("SpawnInstance(BDat)", hr);
    {
        SAFEARRAYBOUND bound{static_cast<ULONG>(buffer.size()), 0};
        SAFEARRAY* psa = SafeArrayCreate(VT_UI1, 1, &bound);
        if (!psa)
            return fail("SafeArrayCreate", E_OUTOFMEMORY);
        memcpy(psa->pvData, buffer.data(), buffer.size());
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_ARRAY | VT_UI1;
        v.parray = psa;
        hr = bdatInst->Put(L"Bytes", 0, &v, 0);
        VariantClear(&v);
        if (FAILED(hr))
            return fail("Put(BDat.Bytes)", hr);
    }

    // BFn instance ACPI\PNP0C14\0_0
    Microsoft::WRL::ComPtr<IEnumWbemClassObject> it;
    hr = m_wmi.services()->CreateInstanceEnum(_bstr_t(L"BFn"),
                                              WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr,
                                              it.GetAddressOf());
    if (FAILED(hr))
        return fail("CreateInstanceEnum(BFn)", hr);

    Microsoft::WRL::ComPtr<IWbemClassObject> bfnClass;
    hr = m_wmi.services()->GetObject(_bstr_t(L"BFn"), 0, nullptr, bfnClass.GetAddressOf(), nullptr);
    if (FAILED(hr))
        return fail("GetObject(BFn)", hr);
    Microsoft::WRL::ComPtr<IWbemClassObject> inSig;
    hr = bfnClass->GetMethod(_bstr_t(L"DoBFn"), 0, inSig.GetAddressOf(), nullptr);
    if (FAILED(hr) || !inSig)
        return fail("GetMethod(DoBFn)", hr);

    Microsoft::WRL::ComPtr<IWbemClassObject> obj;
    ULONG got = 0;
    while (SUCCEEDED(it->Next(WBEM_INFINITE, 1, obj.ReleaseAndGetAddressOf(), &got)) && got > 0) {
        VARIANT name;
        VariantInit(&name);
        if (FAILED(obj->Get(L"InstanceName", 0, &name, nullptr, nullptr))) {
            VariantClear(&name);
            continue;
        }
        // Exact match only, like the official client: a relaxed prefix would
        // also hit sibling PNP0C14 instances (e.g. 2_0) whose DoBFn belongs to
        // a different firmware table.
        const bool match = name.vt == VT_BSTR && name.bstrVal
                        && _wcsicmp(name.bstrVal, kInstanceName) == 0;
        VariantClear(&name);
        if (!match)
            continue;

        Microsoft::WRL::ComPtr<IWbemClassObject> inInst;
        hr = inSig->SpawnInstance(0, inInst.GetAddressOf());
        if (FAILED(hr))
            return fail("SpawnInstance(DoBFn in)", hr);
        // Embedded object parameter: VT_UNKNOWN wrapping IWbemClassObject.
        VARIANT dv;
        VariantInit(&dv);
        dv.vt = VT_UNKNOWN;
        dv.punkVal = bdatInst.Get();
        hr = inInst->Put(_bstr_t(L"Data"), 0, &dv, 0);
        if (FAILED(hr))
            return fail("Put(Data)", hr);

        VARIANT path;
        VariantInit(&path);
        if (FAILED(obj->Get(L"__PATH", 0, &path, nullptr, nullptr)))
            return fail("Get(__PATH)", hr);
        Microsoft::WRL::ComPtr<IWbemClassObject> outParams;
        hr = m_wmi.services()->ExecMethod(path.bstrVal, _bstr_t(L"DoBFn"), 0, nullptr, inInst.Get(),
                                          outParams.GetAddressOf(), nullptr);
        VariantClear(&path);
        if (FAILED(hr) || !outParams)
            return fail("ExecMethod(DoBFn)", hr);

        VARIANT outData;
        VariantInit(&outData);
        hr = outParams->Get(_bstr_t(L"Data"), 0, &outData, nullptr, nullptr);
        if (FAILED(hr))
            return fail("Get(out Data)", hr);
        // outData is VT_UNKNOWN wrapping an IWbemClassObject (BDat instance).
        std::vector<unsigned char> result;
        if (outData.vt == VT_UNKNOWN && outData.punkVal) {
            Microsoft::WRL::ComPtr<IWbemClassObject> outObj;
            if (SUCCEEDED(outData.punkVal->QueryInterface(IID_PPV_ARGS(outObj.GetAddressOf())))) {
                VARIANT bytes;
                VariantInit(&bytes);
                if (SUCCEEDED(outObj->Get(L"Bytes", 0, &bytes, nullptr, nullptr))) {
                    result = variantToBytes(bytes);
                    VariantClear(&bytes);
                }
            }
        }
        VariantClear(&outData);
        if (result.empty())
            return fail("Decode(out Data empty)", S_OK);
        const size_t n = (std::min)(result.size(), buffer.size());
        memcpy(buffer.data(), result.data(), n);
        return true;
    }
    qCWarning(lcAcpi) << "DA execute failed: no matching BFn instance";
    return false;
}

bool DellAcpiChannel::getUsttModes(UsttModes& out) {
    std::vector<unsigned char> b(m_bufferLength, 0);
    auto put16 = [&](int off, unsigned short v) { memcpy(b.data() + off, &v, 2); };
    auto put32 = [&](int off, unsigned int v) { memcpy(b.data() + off, &v, 4); };
    put16(buf::kClass, buf::kClassThermal);
    put16(buf::kSelector, buf::kSelectorUstt);
    put32(buf::kStdCbArg1, buf::kUsttGet);
    put32(buf::kStdCbRes1, buf::kRequestMarker);

    if (!execute(b))
        return false;
    auto u32 = [&](int off) {
        unsigned int v = 0;
        memcpy(&v, b.data() + off, 4);
        return v;
    };
    out.error = u32(buf::kStdCbRes1);
    if (out.error != 0)
        return true;
    out.supported = static_cast<unsigned char>(u32(buf::kStdCbRes2) & 0xFF);
    out.current = static_cast<unsigned char>(u32(buf::kStdCbRes3) & 0xFF);
    return true;
}

bool DellAcpiChannel::getChargeState(ChargeState& out) {
    // The GET carries the 44-byte header plus a trailing 36-byte output
    // template written below (offsets 44..79): a smaller buffer (the size
    // probe can settle on 64) cannot hold it and the writes below would
    // overrun the vector.
    if (m_bufferLength < 80)
        return false;
    std::vector<unsigned char> b(m_bufferLength, 0);
    auto put16 = [&](int off, unsigned short v) { memcpy(b.data() + off, &v, 2); };
    auto put32 = [&](int off, unsigned int v) { memcpy(b.data() + off, &v, 4); };
    put16(buf::kClass, buf::kClassBattery);
    put16(buf::kSelector, buf::kSelectorGet);
    b[buf::kCommand] = buf::kCommandGet;
    b[buf::kBatteryNumber] = 1; // first battery (official range: 1..16)
    put32(buf::kGetCbArg2, 44); // sizeof(DA_CALL_STANDARD_INTERFACE)
    put32(buf::kGetCbRes1, buf::kRequestMarker);
    put32(buf::kGetArgAttrib, 0x100);
    // The firmware validates a trailing output TEMPLATE after the 44-byte
    // header (official GetBatteryPropertyInformation): BLength = sizeof the
    // expected struct (DA_CALL_ADVANCED_CHARGE_CONFIGURATION, 36), the first
    // u32 of the template = BLength-4, then the ASCII signature "DSCI"
    // repeated to fill. Getting any of this wrong yields
    // InterfaceBufferFormatError (-5).
    constexpr unsigned int kAdvChargeConfigSize = 36;
    put32(buf::kGetBLength, kAdvChargeConfigSize);
    put32(44, kAdvChargeConfigSize - 4);
    for (int i = 48; i + 4 <= 44 + (int)kAdvChargeConfigSize; i += 4)
        memcpy(b.data() + i, "DSCI", 4);

    if (!execute(b))
        return false;
    auto u32 = [&](int off) {
        unsigned int v = 0;
        memcpy(&v, b.data() + off, 4);
        return v;
    };
    out.error = u32(buf::kGetCbRes1);
    if (out.error != 0)
        return true; // BIOS-level error surfaced through .error
    out.mode = b[buf::kGetCurrentMode];
    out.start = b[buf::kGetStart];
    out.stop = b[buf::kGetStop];
    out.capability = u32(buf::kGetCapability);
    out.allowableStart = b[buf::kGetAllowStart];
    out.allowableStop = b[buf::kGetAllowStop];
    out.granularity = b[buf::kGetGranularity];
    return true;
}

bool DellAcpiChannel::setChargeMode(unsigned char mode, unsigned char start, unsigned char stop) {
    std::vector<unsigned char> b(m_bufferLength, 0);
    auto put16 = [&](int off, unsigned short v) { memcpy(b.data() + off, &v, 2); };
    auto put32 = [&](int off, unsigned int v) { memcpy(b.data() + off, &v, 4); };
    put16(buf::kClass, buf::kClassBattery);
    put16(buf::kSelector, buf::kSelectorSet);
    b[buf::kCommand] = buf::kCommandSet;
    b[buf::kBatteryNumber] = 1;
    b[buf::kChargeMode] = mode;
    b[buf::kStart] = start;
    b[buf::kStop] = stop;
    put32(buf::kSetCbRes1, buf::kRequestMarker);

    if (!execute(b))
        return false;
    unsigned int err = 0;
    memcpy(&err, b.data() + buf::kSetCbRes1, 4);
    return err == 0;
}

} // namespace dtb::win
