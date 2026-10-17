#include "hal/win/WinSystemInfo.h"

#include "hal/win/WmiConnection.h"

namespace dtb::win {

QString systemModel() {
    WmiConnection cimv2(L"ROOT\\CIMV2");
    if (!cimv2.valid())
        return {};
    QString model;
    cimv2.execQuery(L"ROOT\\CIMV2", L"SELECT Model FROM Win32_ComputerSystem",
                    [&](IWbemClassObject* obj) {
                        if (model.isEmpty()) {
                            const auto m = WmiConnection::readStringProp(obj, L"Model");
                            model = m.value_or(QString());
                        }
                    });
    return model;
}

} // namespace dtb::win
