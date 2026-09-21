#pragma once

#include <wbemidl.h>
#include <wrl/client.h>

#include <functional>
#include <optional>

#include <QString>
#include <QVector>

#include "hal/HalInterfaces.h"

namespace dtb::win {

// One COM/WMI session. Thread affinity: create and use from the same thread
// (the controller loop); COM is initialized COINIT_MULTITHREADED anyway but
// the IWbemServices pointer is not marshalled across threads here.
class WmiConnection {
public:
    explicit WmiConnection(const wchar_t* namespace_ = L"ROOT\\WMI");
    ~WmiConnection();

    bool valid() const { return m_services != nullptr; }
    // Direct access for specialised channels (Dell ACPI-WMI BFn/BDat).
    IWbemServices* services() const { return m_services.Get(); }

    // Call a method on the first instance of a WMI class (e.g. the Dell AWCC
    // class). The single integer input is written to the method's first in
    // parameter; the first integer out parameter is returned. Returns nullopt
    // on any failure, -1 or 0xFFFFFFFF results — mirroring tcc-g15's _call().
    std::optional<int> callInstanceMethod(const wchar_t* className, const wchar_t* methodName, int inArg);

    // Run a WQL query; invoke fn for every result object.
    bool execQuery(const wchar_t* namespace_, const wchar_t* wql,
                   const std::function<void(IWbemClassObject*)>& fn);

    static std::optional<int> readIntProp(IWbemClassObject* obj, const wchar_t* prop);
    static std::optional<int> firstOutValue(IWbemClassObject* outParams);
    static std::optional<bool> readBoolProp(IWbemClassObject* obj, const wchar_t* prop);
    static std::optional<QString> readStringProp(IWbemClassObject* obj, const wchar_t* prop);

private:
    bool connect(const wchar_t* namespace_);

    Microsoft::WRL::ComPtr<IWbemLocator> m_locator;
    Microsoft::WRL::ComPtr<IWbemServices> m_services;
};

} // namespace dtb::win
