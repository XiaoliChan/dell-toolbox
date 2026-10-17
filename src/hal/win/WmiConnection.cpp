#include "hal/win/WmiConnection.h"

#include <comdef.h>

#include <QLoggingCategory>

namespace dtb::win {
namespace {
Q_LOGGING_CATEGORY(lcWmi, "dtb.wmi")

std::optional<int> variantInt(const VARIANT& v) {
    switch (v.vt) {
    case VT_I4: return static_cast<int>(v.lVal);
    case VT_UI4: return static_cast<int>(v.ulVal);
    case VT_I2: return static_cast<int>(v.iVal);
    case VT_UI2: return static_cast<int>(v.uiVal);
    case VT_INT: return v.intVal;
    case VT_UINT: return static_cast<int>(v.uintVal);
    default: return std::nullopt;
    }
}
} // namespace

WmiConnection::WmiConnection(const wchar_t* namespace_) {
    connect(namespace_);
}

WmiConnection::~WmiConnection() {
    m_services.Reset();
    m_locator.Reset();
    if (m_comInitialized)
        CoUninitialize();
}

bool WmiConnection::connect(const wchar_t* namespace_) {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    m_comInitialized = SUCCEEDED(initialized);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE)
        return false;
    // One process-wide security blanket; RPC_E_TOO_LATE means already configured.
    const HRESULT security = CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
                         RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
    if (FAILED(security) && security != RPC_E_TOO_LATE)
        return false;
    HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_locator));
    if (FAILED(hr))
        return false;
    hr = m_locator->ConnectServer(_bstr_t(namespace_), nullptr, nullptr, nullptr, 0, nullptr, nullptr,
                                  m_services.GetAddressOf());
    if (FAILED(hr)) {
        m_locator.Reset();
        return false;
    }
    hr = CoSetProxyBlanket(m_services.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHN_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                      RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    if (FAILED(hr)) {
        m_services.Reset();
        return false;
    }
    return true;
}

std::optional<int> WmiConnection::callInstanceMethod(const wchar_t* className, const wchar_t* methodName,
                                                     int inArg) {
    if (!m_services)
        return std::nullopt;

    // Enumerate ALL instances: firmware images expose several, and only one
    // actually executes calls (enumeration order is unspecified, and binding
    // a dead instance yields silent failures). Try each until one answers.
    Microsoft::WRL::ComPtr<IEnumWbemClassObject> instances;
    HRESULT hr =
        m_services->CreateInstanceEnum(_bstr_t(className), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                       nullptr, instances.GetAddressOf());
    if (FAILED(hr))
        return std::nullopt;

    Microsoft::WRL::ComPtr<IWbemClassObject> classDef;
    m_services->GetObject(_bstr_t(className), 0, nullptr, classDef.GetAddressOf(), nullptr);
    if (!classDef)
        return std::nullopt;
    Microsoft::WRL::ComPtr<IWbemClassObject> inSig;
    hr = classDef->GetMethod(_bstr_t(methodName), 0, inSig.GetAddressOf(), nullptr);
    if (FAILED(hr) || !inSig)
        return std::nullopt;

    // Single input property (name differs across firmware generations).
    BSTR inName = nullptr;
    {
        SAFEARRAY* inNames = nullptr;
        hr = inSig->GetNames(nullptr, WBEM_FLAG_LOCAL_ONLY | WBEM_FLAG_NONSYSTEM_ONLY, nullptr, &inNames);
        if (FAILED(hr) || !inNames)
            return std::nullopt;
        LONG lo = 0, hi = 0;
        SafeArrayGetLBound(inNames, 1, &lo);
        SafeArrayGetUBound(inNames, 1, &hi);
        if (hi >= lo)
            SafeArrayGetElement(inNames, &lo, &inName);
        SafeArrayDestroy(inNames);
        if (!inName)
            return std::nullopt;
    }

    std::optional<int> result;
    Microsoft::WRL::ComPtr<IWbemClassObject> instance;
    ULONG returned = 0;
    while (SUCCEEDED(instances->Next(WBEM_INFINITE, 1, instance.ReleaseAndGetAddressOf(), &returned))
           && returned > 0) {
        VARIANT path;
        VariantInit(&path);
        if (FAILED(instance->Get(L"__PATH", 0, &path, nullptr, nullptr))
            || path.vt != VT_BSTR || !path.bstrVal) {
            VariantClear(&path);
            continue;
        }

        Microsoft::WRL::ComPtr<IWbemClassObject> inParams;
        if (SUCCEEDED(inSig->SpawnInstance(0, inParams.GetAddressOf()))) {
            variant_t v(static_cast<LONG>(inArg));
            if (SUCCEEDED(inParams->Put(inName, 0, &v, 0))) {
                Microsoft::WRL::ComPtr<IWbemClassObject> outParams;
                hr = m_services->ExecMethod(path.bstrVal, _bstr_t(methodName), 0, nullptr, inParams.Get(),
                                            outParams.GetAddressOf(), nullptr);
                if (SUCCEEDED(hr) && outParams) {
                    const auto value = firstOutValue(outParams.Get());
                    // -1 / 0xFFFFFFFF are the firmware's "unsupported" answers;
                    // treat them as failure and try the next instance.
                    if (value && *value != -1 && *value != 0xFFFFFFFF) {
                        result = value;
                        VariantClear(&path);
                        break;
                    }
                }
            }
        }
        VariantClear(&path);
    }
    SysFreeString(inName);
    return result;
}

std::optional<int> WmiConnection::firstOutValue(IWbemClassObject* outParams) {
    SAFEARRAY* propNames = nullptr;
    if (FAILED(outParams->GetNames(nullptr, WBEM_FLAG_LOCAL_ONLY | WBEM_FLAG_NONSYSTEM_ONLY, nullptr,
                                  &propNames))
        || !propNames)
        return std::nullopt;
    LONG lo = 0, hi = 0;
    SafeArrayGetLBound(propNames, 1, &lo);
    SafeArrayGetUBound(propNames, 1, &hi);
    std::optional<int> result;
    for (LONG i = lo; i <= hi; ++i) {
        BSTR pname = nullptr;
        if (SUCCEEDED(SafeArrayGetElement(propNames, &i, &pname)) && pname) {
            // ExecMethod synthesizes a ReturnValue property; the real answer
            // lives in the declared [out] params.
            if (_wcsicmp(pname, L"ReturnValue") != 0) {
                VARIANT val;
                VariantInit(&val);
                if (SUCCEEDED(outParams->Get(pname, 0, &val, nullptr, nullptr)))
                    result = variantInt(val);
                VariantClear(&val);
            }
            SysFreeString(pname);
            if (result)
                break;
        }
    }
    SafeArrayDestroy(propNames);
    return result;
}

bool WmiConnection::execQuery(const wchar_t* namespace_, const wchar_t* wql,
                              const std::function<void(IWbemClassObject*)>& fn) {
    if (!m_services)
        return false;
    Microsoft::WRL::ComPtr<IEnumWbemClassObject> results;
    HRESULT hr = m_services->ExecQuery(bstr_t("WQL"), _bstr_t(wql),
                                       WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr,
                                       results.GetAddressOf());
    if (FAILED(hr))
        return false;
    Microsoft::WRL::ComPtr<IWbemClassObject> obj;
    ULONG returned = 0;
    while (SUCCEEDED(results->Next(WBEM_INFINITE, 1, obj.ReleaseAndGetAddressOf(), &returned)) && returned > 0)
        fn(obj.Get());
    return true;
}

std::optional<int> WmiConnection::readIntProp(IWbemClassObject* obj, const wchar_t* prop) {
    VARIANT v;
    VariantInit(&v);
    if (FAILED(obj->Get(prop, 0, &v, nullptr, nullptr))) {
        VariantClear(&v);
        return std::nullopt;
    }
    const auto result = variantInt(v);
    VariantClear(&v);
    return result;
}

std::optional<bool> WmiConnection::readBoolProp(IWbemClassObject* obj, const wchar_t* prop) {
    VARIANT v;
    VariantInit(&v);
    if (FAILED(obj->Get(prop, 0, &v, nullptr, nullptr))) {
        VariantClear(&v);
        return std::nullopt;
    }
    if (v.vt == VT_BOOL) {
        const bool out = v.boolVal != VARIANT_FALSE;
        VariantClear(&v);
        return out;
    }
    VariantClear(&v);
    return std::nullopt;
}

std::optional<QString> WmiConnection::readStringProp(IWbemClassObject* obj, const wchar_t* prop) {
    VARIANT v;
    VariantInit(&v);
    if (FAILED(obj->Get(prop, 0, &v, nullptr, nullptr))) {
        VariantClear(&v);
        return std::nullopt;
    }
    if (v.vt == VT_BSTR && v.bstrVal) {
        QString out = QString::fromWCharArray(v.bstrVal, SysStringLen(v.bstrVal));
        VariantClear(&v);
        return out.trimmed();
    }
    VariantClear(&v);
    return std::nullopt;
}

} // namespace dtb::win
