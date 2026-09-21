#include "hal/win/WinLoadHAL.h"

#include <pdh.h>
#include <windows.h>

#include <QLoggingCategory>
#include <QString>

namespace dtb::win {
namespace {
Q_LOGGING_CATEGORY(lcLoad, "dtb.load")

QString pdhCounterPath() {
    // Resolve the localized names for "Processor" and "% Processor Time"
    // (perf indices 238 and 6); fall back to the English literals.
    wchar_t processor[128] = L"Processor";
    wchar_t percentTime[128] = L"% Processor Time";
    wchar_t buffer[128] = {};
    DWORD size = sizeof(buffer);
    if (PdhLookupPerfNameByIndexW(nullptr, 238, buffer, &size) == ERROR_SUCCESS && buffer[0])
        wcscpy_s(processor, buffer);
    size = sizeof(buffer);
    if (PdhLookupPerfNameByIndexW(nullptr, 6, buffer, &size) == ERROR_SUCCESS && buffer[0])
        wcscpy_s(percentTime, buffer);
    return QString("\\%1(_Total)\\%2").arg(QString::fromWCharArray(processor),
                                           QString::fromWCharArray(percentTime));
}
} // namespace

WinLoadHAL::WinLoadHAL() {
    if (PdhOpenQueryW(nullptr, 0, static_cast<PDH_HQUERY*>(&m_query)) != ERROR_SUCCESS)
        return;
    const QString path = pdhCounterPath();
    if (PdhAddEnglishCounterW(static_cast<PDH_HQUERY>(m_query),
                              reinterpret_cast<LPCWSTR>(path.utf16()), 0,
                              static_cast<PDH_HCOUNTER*>(&m_counter))
        != ERROR_SUCCESS) {
        PdhCloseQuery(static_cast<PDH_HQUERY>(m_query));
        m_query = nullptr;
        return;
    }
    m_ready = true;
}

WinLoadHAL::~WinLoadHAL() {
    if (m_query)
        PdhCloseQuery(static_cast<PDH_HQUERY>(m_query));
}

int WinLoadHAL::cpuLoadPercent() {
    if (!m_ready)
        return 0;
    if (PdhCollectQueryData(static_cast<PDH_HQUERY>(m_query)) != ERROR_SUCCESS)
        return 0;
    PDH_FMT_COUNTERVALUE value = {};
    if (PdhGetFormattedCounterValue(static_cast<PDH_HCOUNTER>(m_counter), PDH_FMT_LONG, nullptr, &value)
        != ERROR_SUCCESS)
        return 0;
    return static_cast<int>(value.longValue);
}

QString WinLoadHAL::foregroundProcess() {
    const HWND hwnd = GetForegroundWindow();
    if (!hwnd)
        return {};
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid)
        return {};
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return {};
    wchar_t path[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    QString result;
    if (QueryFullProcessImageNameW(process, 0, path, &size)) {
        const QString full = QString::fromWCharArray(path, size);
        const int slash = qMax(full.lastIndexOf(QLatin1Char('/')), full.lastIndexOf(QLatin1Char('\\')));
        result = slash >= 0 ? full.mid(slash + 1) : full;
    }
    CloseHandle(process);
    return result.toLower();
}

} // namespace dtb::win
