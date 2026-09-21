#pragma once

#include <vector>

namespace dtb::win {

// Client for Dell Power Manager's privileged COM+ service
// ("Dell.CommandPowerManager.Service.ComponentService"). This is how Dell
// Power Manager itself issues DA calls (ExecuteDACommand) without touching
// the BFn WMI driver. Present on machines where DPM (or its service package)
// is installed. IDispatch-based; no type library needed at build time.
class DpmServiceClient {
public:
    void initialize();
    bool valid() const { return m_dispatch != nullptr; }

    // Run one DA call through the service; buffer is in/out.
    bool execute(std::vector<unsigned char>& buffer);

    // True once an Invoke has been rejected by the service. A rejection is
    // size-independent, so callers can stop buffer-size probing immediately.
    bool rejecting() const { return m_rejecting; }

private:
    void* m_dispatch = nullptr; // IDispatch*, cast in the .cpp
    int m_dispid = -1;          // ExecuteDACommand
    bool m_warnedInvoke = false; // one warning, then debug-level invoke failures
    bool m_rejecting = false;    // set on first Invoke-level rejection
};

} // namespace dtb::win
