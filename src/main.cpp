#include <QApplication>
#include <QIcon>
#include <QStringList>
#include <QRegularExpression>
#include <QTimer>
#include <cstdio>

#include "core/AppInfo.h"
#include "core/ConfigStore.h"
#include "core/Controller.h"
#include "core/Logger.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#ifdef Q_OS_WIN
#include <windows.h>

#include <shellapi.h>

#include <string>

namespace {
// Windows silently drops tray balloons/toasts for Win32 apps without an
// explicit AppUserModelID. Call it before anything creates a tray icon.
void ensureAppUserModelId() {
    using SetAumidFn = long(__stdcall*)(const wchar_t*);
    HMODULE shell = GetModuleHandleW(L"shell32.dll");
    if (!shell)
        shell = LoadLibraryW(L"shell32.dll");
    if (!shell)
        return;
    const auto set = reinterpret_cast<SetAumidFn>(
        reinterpret_cast<void*>(GetProcAddress(shell, "SetCurrentProcessExplicitAppUserModelID")));
    if (set)
        set(L"dtb.delltoolbox");
}

// Register a friendly display name and icon for the toast source. Without
// this, Windows shows the raw AppUserModelID ("dtb.delltoolbox") on every
// notification instead of "Dell Toolbox".
void registerToastIdentity() {
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    const std::wstring subkey = L"Software\\Classes\\AppUserModelId\\dtb.delltoolbox";
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                        &key, nullptr) != ERROR_SUCCESS)
        return;
    const std::wstring displayName = L"Dell Toolbox";
    RegSetValueExW(key, L"DisplayName", 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(displayName.c_str()),
                   static_cast<DWORD>((displayName.size() + 1) * sizeof(wchar_t)));
    const std::wstring iconUri = std::wstring(exePath) + L",0";
    RegSetValueExW(key, L"IconUri", 0, REG_SZ, reinterpret_cast<const BYTE*>(iconUri.c_str()),
                   static_cast<DWORD>((iconUri.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
}

// This is a GUI-subsystem executable: cmd.exe starts it without console
// handles, so --doctor/--version printf output would vanish. Reattach to the
// parent console when there is one (nothing to do when launched from Explorer).
void attachParentConsole() {
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
        return;
    FILE* dummy = nullptr;
    freopen_s(&dummy, "CONOUT$", "w", stdout);
    freopen_s(&dummy, "CONOUT$", "w", stderr);
    freopen_s(&dummy, "CONIN$", "r", stdin);
}
} // namespace

#include "hal/win/WinChargeHAL.h"
#include "hal/win/WmiConnection.h"
#include "hal/win/WinHalFactory.h"
#else
#include "hal/mock/MockHal.h"
#endif

namespace {
void printDoctor() {
#ifdef Q_OS_WIN
    // One-shot hardware triage; builds its own backend set.
    auto halOwner = dtb::win::createHalSet();
    const auto pass = [](bool ok, const char* name, const QString& note = {}) {
        std::printf("  %-24s: %s%s\n", name, ok ? "PASS" : "FAIL",
                    note.isEmpty() ? "" : (" (" + note + ")").toUtf8().constData());
    };
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
                                 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    pass(isAdmin, "administrator");
    const bool thermalOk = halOwner.set.thermal && halOwner.set.thermal->available();
    int fanCount = 0;
    if (thermalOk)
        fanCount = halOwner.set.thermal->enumerateFans().size();
    pass(thermalOk && fanCount > 0, "thermal (AWCC WMI)",
         thermalOk ? QStringLiteral("fans: %1").arg(fanCount) : QStringLiteral("WMI class missing"));
    if (thermalOk) {
        // Live readback of what the firmware says is active right now (G-Mode
        // set in AWCC must show up here - if it does not, send us the log).
        const auto cur = halOwner.set.thermal->readCurrentProfile();
        const char* modeName = "unknown (no readback)";
        if (cur) {
            switch (*cur) {
            case dtb::ThermalMode::Quiet:
                modeName = "Quiet";
                break;
            case dtb::ThermalMode::Cool:
                modeName = "Cool";
                break;
            case dtb::ThermalMode::Balanced:
                modeName = "Balanced";
                break;
            case dtb::ThermalMode::Performance:
                modeName = "Performance";
                break;
            case dtb::ThermalMode::GMode:
                modeName = "G-Mode";
                break;
            case dtb::ThermalMode::Custom:
                modeName = "Custom";
                break;
            }
        }
        std::printf("  %-24s: %s\n", "current thermal profile", modeName);
    }
    pass(halOwner.set.battery && halOwner.set.battery->available(), "battery (WMI)");
    pass(halOwner.set.gpu && halOwner.set.gpu->available(), "gpu (NVAPI)");
    pass(halOwner.set.load && halOwner.set.load->cpuLoadPercent() >= 0, "load (PDH)");
    auto* charge = dynamic_cast<dtb::win::WinChargeHAL*>(halOwner.charge.get());
    pass(halOwner.set.charge && halOwner.set.charge->available(), "charging modes",
         charge ? charge->sourceName() : QString());
    if (charge && !charge->available())
        std::printf("    reason: %s\n", charge->unavailableReason().toUtf8().constData());
    if (charge && charge->available()) {
        // What the firmware says the battery is doing right now (what official
        // DPM set - if this shows "unknown", send us the log).
        const QString cur = charge->currentMode();
        std::printf("  %-24s: %s\n", "current charging mode",
                    cur.isEmpty() ? "unknown (no readback)" : cur.toUtf8().constData());
    }
    {
        // Root\WMI inventory: which Dell/battery classes does this machine expose?
        dtb::win::WmiConnection wmi(L"root\\wmi");
        QStringList classes;
        wmi.execQuery(L"root\\wmi", L"SELECT * FROM meta_class", [&](IWbemClassObject* obj) {
            VARIANT v;
            VariantInit(&v);
            if (SUCCEEDED(obj->Get(L"__CLASS", 0, &v, nullptr, nullptr)) && v.vt == VT_BSTR && v.bstrVal) {
                const QString n = QString::fromWCharArray(v.bstrVal, SysStringLen(v.bstrVal));
                if (n.contains(QLatin1String("Battery"), Qt::CaseInsensitive)
                    || n.contains(QLatin1String("Charge"), Qt::CaseInsensitive)
                    || n.contains(QLatin1String("Thermal"), Qt::CaseInsensitive)
                    || n == QLatin1String("BFn") || n == QLatin1String("BDat")
                    || n == QLatin1String("WMI_Query") || n.contains(QLatin1String("AWCC")))
                    classes.append(n);
            }
            VariantClear(&v);
        });
        classes.sort();
        std::printf("  root\\WMI classes (battery/charge/thermal)\n");
        for (const QString& n : classes)
            std::printf("    %s\n", n.toUtf8().constData());
        if (classes.isEmpty())
            std::printf("    (none matched)\n");
    }
    std::fflush(stdout);
#else
    std::printf("doctor: mock backend\n");
    std::printf("  thermal (AWCC WMI)   : SKIPPED (mock)\n");
    std::printf("  battery              : SKIPPED (mock)\n");
    std::printf("  gpu (NVAPI)          : SKIPPED (mock)\n");
    std::printf("  load (PDH)           : SKIPPED (mock)\n");
    std::printf("  charge (cctk)        : SKIPPED (mock)\n");
    std::printf("  administrator        : SKIPPED (mock)\n");
#endif
}
} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    attachParentConsole();
    ensureAppUserModelId();
#endif
    QApplication::setApplicationName(dtb::app::kId);
#ifdef Q_OS_WIN
    registerToastIdentity();
#endif
    QApplication::setApplicationVersion(dtb::app::kVersion);
    QApplication::setOrganizationName(dtb::app::kId);
    const QStringList args = app.arguments();

    if (args.contains("--version")) {
        std::printf("%s %s\n", dtb::app::kName, dtb::app::kVersion);
        return 0;
    }
    if (args.contains("--doctor")) {
        printDoctor();
        return 0;
    }

    // Portable build: everything lives next to the executable.
    const QString base = QApplication::applicationDirPath();
    dtb::Logger::init(base + "/dell-toolbox.log",
                      args.contains("--verbose") ? dtb::Logger::debug : dtb::Logger::info);
    dtbLog(info) << dtb::app::kName << dtb::app::kVersion << "starting";

#ifdef Q_OS_WIN
    auto halOwner = dtb::win::createHalSet();
    dtb::HalSet hal = halOwner.set;
#else
    // Linux dev/demo mode: scripted fake hardware.
    auto halOwner = std::make_unique<dtb::MockHal>();
    halOwner->setScenario([](dtb::SystemSnapshot& s, int tick) {
        s.sensors.push_back({0x01, 55 + (tick % 20)});
        s.sensors.push_back({0x06, 45 + (tick % 25)});
        s.fans.push_back({0x33, 2400});
        s.fans.push_back({0x32, 2100});
        s.gpu.utilPercent = 30 + (tick % 40);
        s.gpu.powerW = 28 + (tick % 22);
        s.cpuLoadPercent = 20 + (tick % 30);
        s.battery.present = true;
        s.battery.acOnline = true;
        s.battery.percent = 88;
        s.battery.healthPercent = 92;
        s.battery.cycleCount = 143;
        s.battery.designCapacityMWh = 51000;
        s.battery.fullChargeCapacityMWh = 46900;
        s.battery.remainingCapacityMWh = 44880;
        s.battery.voltageMV = 12500;
        s.battery.rateMW = -12400;
        s.battery.deviceName = "DELL PN1VN95";
        s.battery.vendor = "SMP";
    });
    halOwner->enableAutoAdvance(1000);
    dtb::HalSet hal = halOwner->makeSet();
#endif

    dtbLog(info) << "HAL ready";
    dtb::ConfigStore config(base + "/dell-toolbox.ini");
    dtb::Controller controller(hal, &config);
    dtbLog(info) << "controller ready";
    dtb::ui::applyTheme(app);
    app.setWindowIcon(QIcon(QStringLiteral(":/res/icon-256.png")));
    dtb::ui::MainWindow window(hal, &controller, &config);
    dtbLog(info) << "ui ready";
    for (const QString& a : args) {
        if (a.startsWith(QStringLiteral("--window-size="))) {
            const QRegularExpression sizeRe("^--window-size=(\\d+)x(\\d+)$");
            const auto sm = sizeRe.match(a);
            if (sm.hasMatch())
                window.resize(sm.captured(1).toInt(), sm.captured(2).toInt());
        }
    }
    // Start minimized when requested via flag or the Settings toggle.
    const bool startMinimized = args.contains("--minimized") || config.loadStartMinimized();
    if (startMinimized)
        window.showMinimized();
    else
        window.show();
    controller.start(1000);

#ifndef Q_OS_WIN
    // Dev-only visual check: render the window to a PNG and exit (mock HAL).
    for (const QString& a : args) {
        if (a.startsWith(QStringLiteral("--screenshot-test"))) {
            const QString path = a.contains(QLatin1Char('=')) ? a.mid(a.indexOf(QLatin1Char('=')) + 1)
                                                              : QStringLiteral("/tmp/dtb-ui.png");
            int page = 0;
            const QString argName = a.section(QLatin1Char('='), 0, 0);
            const auto pm = QRegularExpression("^--screenshot-test(\\d*)$").match(argName);
            if (pm.hasMatch() && !pm.captured(1).isEmpty())
                page = pm.captured(1).toInt();
            QTimer::singleShot(2600, &app, [&app, &window, path, page] {
                if (page > 0)
                    window.showPageForScreenshot(page);
                QTimer::singleShot(250, &app, [&window, path] {
                    window.grab().save(path);
                    QApplication::exit(0);
                });
            });
            break;
        }
    }
#endif

    const int rc = app.exec();
    controller.stop();
    return rc;
}
