#pragma once

#include <QString>

#include "core/Snapshot.h"
#include "hal/HalInterfaces.h"

namespace dtb::win {

// NVIDIA GPU readings via nvapi64.dll loaded at runtime (nothing to link).
// Interface IDs are the published ones used by open-source monitors; doctor
// verifies them on first contact. Power-limit writes are not implemented in
// v1 (the public NVAPI surface for laptop power limits is unstable across
// driver versions).
class WinGpuHAL : public IGpuHAL {
public:
    WinGpuHAL();

    GpuInfo read() override;
    bool setPowerLimitW(int watts) override;
    bool available() override;

    ~WinGpuHAL();

private:
    bool ensureLoaded();
    int readPowerW(); // NVML, loaded on demand; -1 when unavailable
    void* m_nvapi = nullptr; // module handle
    void* m_gpuHandle = nullptr;
    bool m_loaded = false;
    bool m_loadFailed = false;
    void* m_nvml = nullptr; // module handle
    bool m_nvmlFailed = false;
    bool m_nvmlInited = false; // nvmlInit_v2 succeeded (retried until then)
};

} // namespace dtb::win
