#include "hal/win/WinGpuHAL.h"

#include <QLoggingCategory>
#include <QString>

#include <windows.h>

namespace dtb::win {
namespace {
Q_LOGGING_CATEGORY(lcGpu, "dtb.gpu")

// nvapi_QueryInterface ordinal IDs (published by NVIDIA in the open nvapi
// repo's samples and mirrored in open-source monitors).
constexpr unsigned int kIdInitialize = 0x0150E828;
constexpr unsigned int kIdEnumPhysicalGPUs = 0xE5AC921F;
constexpr unsigned int kIdGetDynamicPstatesInfoEx = 0x60DED2ED;
constexpr unsigned int kIdGetThermalSettings = 0x363D1D3F;

typedef void* (*NvApi_QueryInterface_t)(unsigned int);
typedef int (*NvApi_Initialize_t)();
typedef int (*NvApi_EnumPhysicalGPUs_t)(void** handles, unsigned int* count);

struct NvDynamicPstate {
    unsigned int present;
    unsigned int percentage;
};
struct NvGpuDynamicPstatesInfoEx {
    unsigned int version;
    unsigned int flags;
    NvDynamicPstate util[32];
    NvDynamicPstate current;
    unsigned int flags2;
};
struct NvSensor {
    unsigned int sensorId; // 1 = GPU core
    int currentTemp;
    int defaultMaxTemp;
    int maxTemp;
    int flags;
};
struct NvGpuThermalSettings {
    unsigned int version;
    unsigned int count;
    NvSensor sensor[32];
};
typedef int (*NvApi_GetDynamicPstatesInfoEx_t)(void*, NvGpuDynamicPstatesInfoEx*);
typedef int (*NvApi_GetThermalSettings_t)(void*, NvGpuThermalSettings*);

template <typename Fn>
Fn query(void* module, unsigned int id) {
    static NvApi_QueryInterface_t queryInterface = nullptr;
    if (!queryInterface)
        queryInterface = reinterpret_cast<NvApi_QueryInterface_t>(GetProcAddress(static_cast<HMODULE>(module), "nvapi_QueryInterface"));
    if (!queryInterface)
        return nullptr;
    return reinterpret_cast<Fn>(queryInterface(id));
}
} // namespace

WinGpuHAL::WinGpuHAL() = default;

WinGpuHAL::~WinGpuHAL() = default;

bool WinGpuHAL::available() { return ensureLoaded(); }

bool WinGpuHAL::ensureLoaded() {
    if (m_loaded)
        return m_gpuHandle != nullptr;
    if (m_loadFailed)
        return false;
    m_loaded = true;

    HMODULE module = LoadLibraryW(L"nvapi64.dll");
    if (!module) {
        m_loadFailed = true;
        return false; // no NVIDIA driver present; not an error on hybrid GPUs
    }
    m_nvapi = module;

    auto init = query<NvApi_Initialize_t>(module, kIdInitialize);
    if (!init || init() != 0) {
        m_loadFailed = true;
        return false;
    }
    auto enumerate = query<NvApi_EnumPhysicalGPUs_t>(module, kIdEnumPhysicalGPUs);
    if (!enumerate) {
        m_loadFailed = true;
        return false;
    }
    void* handles[64] = {};
    unsigned int count = 0;
    if (enumerate(handles, &count) != 0 || count == 0) {
        m_loadFailed = true;
        return false;
    }
    m_gpuHandle = handles[0];
    return true;
}

GpuInfo WinGpuHAL::read() {
    GpuInfo info;
    if (!ensureLoaded())
        return info;

    auto pstates = query<NvApi_GetDynamicPstatesInfoEx_t>(m_nvapi, kIdGetDynamicPstatesInfoEx);
    if (pstates) {
        NvGpuDynamicPstatesInfoEx ex = {};
        ex.version = sizeof(ex) | 0x10000; // MAKE_NVAPI_VERSION(..., 1)
        if (pstates(m_gpuHandle, &ex) == 0 && ex.util[0].present)
            info.utilPercent = static_cast<int>(ex.util[0].percentage);
    }
    auto thermal = query<NvApi_GetThermalSettings_t>(m_nvapi, kIdGetThermalSettings);
    if (thermal) {
        NvGpuThermalSettings ts = {};
        ts.version = sizeof(ts) | 0x20000; // MAKE_NVAPI_VERSION(..., 2)
        if (thermal(m_gpuHandle, &ts) == 0 && ts.count >= 1 && ts.sensor[0].sensorId == 1)
            info.tempC = ts.sensor[0].currentTemp;
    }
    info.powerW = readPowerW();
    return info;
}

// Power draw comes from NVML (nvml.dll ships with the NVIDIA driver), the
// same source HWiNFO-style monitors use. NVAPI's power surface is unstable
// across driver versions, so it is not used here.
int WinGpuHAL::readPowerW() {
    if (m_nvmlFailed)
        return -1;
    if (!m_nvml) {
        m_nvml = LoadLibraryW(L"nvml.dll");
        if (!m_nvml) {
            m_nvmlFailed = true;
            return -1;
        }
    }
    typedef int (*NvmlInitV2_t)();
    typedef int (*NvmlDeviceGetHandleByIndexV2_t)(unsigned int, void**);
    typedef int (*NvmlDeviceGetPowerUsage_t)(void*, unsigned int*);
    auto init = reinterpret_cast<NvmlInitV2_t>(GetProcAddress(static_cast<HMODULE>(m_nvml), "nvmlInit_v2"));
    auto handle = reinterpret_cast<NvmlDeviceGetHandleByIndexV2_t>(
        GetProcAddress(static_cast<HMODULE>(m_nvml), "nvmlDeviceGetHandleByIndex_v2"));
    auto power = reinterpret_cast<NvmlDeviceGetPowerUsage_t>(
        GetProcAddress(static_cast<HMODULE>(m_nvml), "nvmlDeviceGetPowerUsage"));
    if (!init || !handle || !power) {
        m_nvmlFailed = true;
        return -1;
    }
    // nvmlInit_v2 is latched per instance but retried until it succeeds: a
    // function-local static would hard-disable power reporting for the whole
    // session if the driver was not ready at the very first read.
    if (!m_nvmlInited) {
        if (init() != 0)
            return -1;
        m_nvmlInited = true;
    }
    void* device = nullptr;
    if (handle(0, &device) != 0)
        return -1;
    unsigned int milliwatts = 0;
    if (power(device, &milliwatts) != 0)
        return -1;
    return static_cast<int>(milliwatts / 1000);
}

bool WinGpuHAL::setPowerLimitW(int watts) {
    Q_UNUSED(watts);
    return false; // v1: read-only GPU backend
}

} // namespace dtb::win
