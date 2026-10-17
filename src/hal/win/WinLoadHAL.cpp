#include "hal/win/WinLoadHAL.h"

#include <pdh.h>
#include <windows.h>

#include <QLoggingCategory>
#include <QString>

namespace dtb::win {
namespace {
Q_LOGGING_CATEGORY(lcLoad, "dtb.load")

} // namespace

WinLoadHAL::WinLoadHAL() {
    if (PdhOpenQueryW(nullptr, 0, static_cast<PDH_HQUERY*>(&m_query)) != ERROR_SUCCESS)
        return;
    const QString path = QStringLiteral("\\Processor(_Total)\\% Processor Time");
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
        return -1;
    if (PdhCollectQueryData(static_cast<PDH_HQUERY>(m_query)) != ERROR_SUCCESS)
        return -1;
    PDH_FMT_COUNTERVALUE value = {};
    if (PdhGetFormattedCounterValue(static_cast<PDH_HCOUNTER>(m_counter), PDH_FMT_LONG, nullptr, &value)
        != ERROR_SUCCESS)
        return -1;
    if (value.CStatus != PDH_CSTATUS_VALID_DATA && value.CStatus != PDH_CSTATUS_NEW_DATA)
        return -1;
    return qBound(0, static_cast<int>(value.longValue), 100);
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
