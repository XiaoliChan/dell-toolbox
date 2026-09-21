#pragma once

#include <QString>

#include "hal/HalInterfaces.h"

namespace dtb::win {

// CPU load via PDH with localized counter names, foreground process via the
// Win32 window/process APIs.
class WinLoadHAL : public ILoadHAL {
public:
    WinLoadHAL();
    ~WinLoadHAL() override;

    int cpuLoadPercent() override;
    QString foregroundProcess() override;

private:
    void* m_query = nullptr; // PDH_HQUERY
    void* m_counter = nullptr; // PDH_HCOUNTER
    bool m_ready = false;
};

} // namespace dtb::win
