#include "hal/win/AwccConflict.h"

#include <windows.h>

#include <tlhelp32.h>

namespace dtb::win {

QStringList runningAwccProcs() {
    QStringList found;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return found;
    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            const QString name = QString::fromWCharArray(entry.szExeFile).toLower();
            if (name == QLatin1String("awcc.exe") || name == QLatin1String("awccservice.exe")
                || name == QLatin1String("awccscheduler.exe")
                || name.startsWith(QLatin1String("alienware")))
                found.append(name);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

} // namespace dtb::win
