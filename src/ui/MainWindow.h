#pragma once

#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QStackedWidget>
#include <QMenu>
#include <QSystemTrayIcon>

#include "core/ConfigStore.h"
#include "core/Controller.h"
#include "hal/HalInterfaces.h"

namespace dtb::ui {

class DashboardPage;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(HalSet hal, Controller* controller, ConfigStore* config, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildNav();
    void buildTray();
    void syncTrayModes(ThermalMode current);
    QLabel* m_toast = nullptr;
    QTimer* m_toastTimer = nullptr;
    void onThermalModeChanged(ThermalMode current, bool external);
    void onChargingModeChanged(const QString& mode);
    void showToast(const QString& title, const QString& body);
    void onSnapshot(const SystemSnapshot& s);
    void paintTrayIcon(int cpuTempC, int gpuTempC);

public:
    void setModeFromUi(ThermalMode mode);

#ifndef Q_OS_WIN
    // Dev-only offscreen screenshot harness.
    void showPageForScreenshot(int index) {
        if (m_nav)
            m_nav->blockSignals(true);
        m_stack->setCurrentIndex(index);
        if (m_nav) {
            m_nav->setCurrentRow(index);
            m_nav->blockSignals(false);
        }
    }
#endif

private:
    HalSet m_hal;
    Controller* m_controller; // not owned
    ConfigStore* m_config; // not owned
    QListWidget* m_nav = nullptr;
    DashboardPage* m_dashboard = nullptr;
    QStackedWidget* m_stack = nullptr;
    QSystemTrayIcon* m_tray = nullptr;
    QMenu* m_trayMenu = nullptr;
    bool m_trayNoticeShown = false;
};

} // namespace dtb::ui
