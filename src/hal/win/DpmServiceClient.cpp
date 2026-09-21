#include "hal/win/DpmServiceClient.h"

#include <QLoggingCategory>

#include <windows.h>
#include <objbase.h>

namespace dtb::win {
namespace {
Q_LOGGING_CATEGORY(lcDpm, "dtb.dpmservice")

constexpr wchar_t kProgId[] = L"Dell.CommandPowerManager.Service.ComponentService";

IDispatch* dispatchFromProgId(const wchar_t* progId) {
    CLSID clsid = {};
    if (FAILED(CLSIDFromProgID(progId, &clsid)))
        return nullptr;
    IDispatch* dispatch = nullptr;
    if (FAILED(CoCreateInstance(clsid, nullptr, CLSCTX_LOCAL_SERVER, IID_IDispatch,
                                reinterpret_cast<void**>(&dispatch))))
        return nullptr;
    return dispatch;
}
} // namespace

void DpmServiceClient::initialize() {
    IDispatch* dispatch = dispatchFromProgId(kProgId);
    if (!dispatch) {
        qCInfo(lcDpm) << "DPM ComponentService not present";
        return;
    }
    wchar_t* names[1] = {const_cast<wchar_t*>(L"ExecuteDACommand")};
    DISPID dispid = DISPID_UNKNOWN;
    if (FAILED(dispatch->GetIDsOfNames(IID_NULL, names, 1, LOCALE_USER_DEFAULT, &dispid))) {
        dispatch->Release();
        qCInfo(lcDpm) << "DPM ComponentService: ExecuteDACommand not found";
        return;
    }
    m_dispatch = dispatch;
    m_dispid = dispid;
    qCInfo(lcDpm) << "DPM ComponentService channel ready";
}

bool DpmServiceClient::execute(std::vector<unsigned char>& buffer) {
    auto* dispatch = static_cast<IDispatch*>(m_dispatch);
    if (!dispatch)
        return false;

    SAFEARRAYBOUND bound{static_cast<ULONG>(buffer.size()), 0};
    SAFEARRAY* psa = SafeArrayCreate(VT_UI1, 1, &bound);
    if (!psa)
        return false;
    memcpy(psa->pvData, buffer.data(), buffer.size());

    // byref so the service's response lands back in our array. For VT_BYREF
    // the VARIANT must hold a POINTER TO the safearray pointer, not the
    // safearray itself - getting this wrong crashes the marshaler.
    VARIANT arg;
    VariantInit(&arg);
    arg.vt = VT_BYREF | VT_ARRAY | VT_UI1;
    arg.pparray = &psa; // VT_BYREF|VT_ARRAY: pointer to the safearray pointer

    DISPPARAMS params = {&arg, nullptr, 1, 0}; // positional method argument
    EXCEPINFO excep = {};
    VARIANT result;
    VariantInit(&result);
    HRESULT hr = dispatch->Invoke(m_dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &params,
                                    &result, &excep, nullptr);
    if (hr == DISP_E_EXCEPTION && excep.bstrDescription) {
        qCWarning(lcDpm) << "ExecuteDACommand exception:"
                         << QString::fromWCharArray(excep.bstrDescription);
    }
    SysFreeString(excep.bstrDescription);
    SysFreeString(excep.bstrSource);
    VariantClear(&result);
    if (FAILED(hr)) {
        SafeArrayDestroy(psa);
        m_rejecting = true; // one rejection means all will be: stop size probes
        // Keep the log readable: warn on the first rejection only.
        if (!m_warnedInvoke) {
            qCWarning(lcDpm) << "ExecuteDACommand invoke failed 0x" << Qt::hex << hr
                             << "- DPM service rejects DA calls, using BFn WMI instead";
            m_warnedInvoke = true;
        }
        return false;
    }
    memcpy(buffer.data(), psa->pvData, buffer.size());
    SafeArrayDestroy(psa);
    return true;
}

} // namespace dtb::win
