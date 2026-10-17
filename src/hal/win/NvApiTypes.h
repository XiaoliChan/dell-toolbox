#pragma once
#include <cstddef>

// Minimal ABI declarations from NVIDIA's public nvapi.h/nvapi_interface.h:
// https://github.com/NVIDIA/nvapi
// Keep this header platform-neutral so ABI layout is checked by mock CI too.
namespace dtb::win {
// nvapi_QueryInterface ordinal IDs (published by NVIDIA in the open nvapi
// repo's samples and mirrored in open-source monitors).
constexpr unsigned int kIdInitialize = 0x0150E828;
constexpr unsigned int kIdUnload = 0xD22BDD7E;
constexpr unsigned int kIdEnumPhysicalGPUs = 0xE5AC921F;
constexpr unsigned int kIdGetDynamicPstatesInfoEx = 0x60DED2ED;
constexpr unsigned int kIdGetThermalSettings = 0xE3640A56;

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
    NvDynamicPstate util[8];
};
struct NvSensor {
    int controller;
    int defaultMinTemp;
    int defaultMaxTemp;
    int currentTemp;
    int target; // NVAPI_THERMAL_TARGET_GPU = 1
};
struct NvGpuThermalSettings {
    unsigned int version;
    unsigned int count;
    NvSensor sensor[3];
};
typedef int (*NvApi_GetDynamicPstatesInfoEx_t)(void*, NvGpuDynamicPstatesInfoEx*);
static_assert(sizeof(NvGpuDynamicPstatesInfoEx) == 72);
static_assert(sizeof(NvGpuThermalSettings) == 68);
static_assert(offsetof(NvSensor, currentTemp) == 12);
static_assert(offsetof(NvSensor, target) == 16);
typedef int (*NvApi_GetThermalSettings_t)(void*, unsigned int, NvGpuThermalSettings*);

} // namespace dtb::win
