#include "ui/MainWindow.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QStackedWidget>
#include <QTimer>

#ifdef Q_OS_WIN
#include <dwmapi.h>
#endif
#include <QScrollArea>
#include <QVBoxLayout>

#include "core/AppInfo.h"
#include "ui/Names.h"
#include "ui/Theme.h"
#include "ui/widgets/NavIcons.h"

#ifdef Q_OS_WIN
#include "hal/win/AwccConflict.h"
#endif
#include "ui/pages/BatteryPage.h"
#include "ui/pages/DashboardPage.h"
#include "ui/pages/PerformancePage.h"
#include "ui/pages/SettingsPage.h"
#include "ui/pages/ThermalPage.h"

namespace dtb::ui {
namespace {
QScrollArea* wrapScrollable(QWidget* page) {
    auto* area = new QScrollArea;
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // vertical flow only
    area->setWidget(page);
    return area;
}
} // namespace

MainWindow::MainWindow(HalSet hal, Controller* controller, ConfigStore* config, QWidget* parent)
    : QMainWindow(parent), m_hal(hal), m_controller(controller), m_config(config) {
    setWindowTitle(QStringLiteral("Dell Toolbox"));
    resize(980, 640);

#ifdef Q_OS_WIN
    // Blend the native frame into the dark UI: dark title bar everywhere
    // (Win10 1809+), and on Win11 the caption/border tint to the app bg so
    // the window reads as one surface instead of a white strip on top.
    if (HWND hwnd = reinterpret_cast<HWND>(winId())) {
        const BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark)); // DWMWA_USE_IMMERSIVE_DARK_MODE
        const COLORREF caption = 0x00110e0d; // #0d0e11 (COLORREF is 0x00BBGGRR)
        DwmSetWindowAttribute(hwnd, 35, &caption, sizeof(caption)); // DWMWA_CAPTION_COLOR (Win11)
        const COLORREF border = 0x0027211f; // #1f2127 (COLORREF is 0x00BBGGRR)
        DwmSetWindowAttribute(hwnd, 34, &border, sizeof(border)); // DWMWA_BORDER_COLOR (Win11)
    }
#endif

    auto* central = new QWidget(this);
    auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* sidebar = new QWidget(central);
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(196);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(0, 18, 0, 18);
    sidebarLayout->setSpacing(6);
    auto* brand = new QLabel(QStringLiteral("Dell Toolbox"), sidebar);
    brand->setObjectName(QStringLiteral("brandText"));
    sidebarLayout->addWidget(brand, 0, Qt::AlignHCenter);
    sidebarLayout->addSpacing(14);

    m_stack = new QStackedWidget(central);
    m_dashboard = new DashboardPage(hal, controller, config, this);
    m_stack->addWidget(wrapScrollable(m_dashboard));
    m_stack->addWidget(wrapScrollable(new ThermalPage(hal, controller, config, this)));
    m_stack->addWidget(wrapScrollable(new PerformancePage(hal, controller, config, this)));
    m_stack->addWidget(wrapScrollable(new BatteryPage(hal, controller, config, this)));
    m_stack->addWidget(wrapScrollable(new SettingsPage(hal, controller, config, this)));

    m_nav = new QListWidget(sidebar);
    m_nav->setFocusPolicy(Qt::NoFocus);
    m_nav->setIconSize(QSize(20, 20));
    m_nav->setUniformItemSizes(true);
    m_nav->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // fixed height fits all five rows
    m_nav->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_nav->setFixedHeight(5 * 46 + 4);
    buildNav();
    sidebarLayout->addWidget(m_nav);
    sidebarLayout->addStretch(1);

    layout->addWidget(sidebar);
    layout->addWidget(m_stack, 1);
    setCentralWidget(central);

    buildTray();

    connect(m_controller, &Controller::snapshotUpdated, this, &MainWindow::onSnapshot);
    // Tray radios + notification bubble on every profile change.
    connect(m_controller, &Controller::thermalModeChanged, this, &MainWindow::onThermalModeChanged);
    // Charging mode changes get their own bubble (works in monitor mode too).
    connect(m_controller, &Controller::chargingModeChanged, this, &MainWindow::onChargingModeChanged);

    // Every page is built: round the combo popups (see Theme.h).
    softenComboPopups(this);

#ifdef Q_OS_WIN
    // Coexistence: pause our writers whenever the official controller runs.
    auto* watcher = new QTimer(this);
    auto checkAwcc = [this] {
        const bool awccRunning = !dtb::win::runningAwccProcs().isEmpty();
        // "Take control" lets the user override monitor mode (e.g. after
        // uninstalling AWCC left a lingering service running).
        const bool passive = awccRunning && m_config->loadAwccCoexist() && !m_config->loadForceControl();
        if (m_controller->passive() != passive)
            m_controller->setPassive(passive);
        if (m_dashboard)
            m_dashboard->setAwccPaused(passive);
    };
    connect(watcher, &QTimer::timeout, this, checkAwcc);
    watcher->start(4000);
    checkAwcc();
#endif
}

void MainWindow::buildNav() {
    const QStringList entries = {tr("Dashboard"), tr("Thermal"), tr("Performance"), tr("Battery"),
                                 tr("Settings")};
    const QList<int> iconKinds = {dtb::ui::NavIconKind::Dashboard, dtb::ui::NavIconKind::Thermal,
                                  dtb::ui::NavIconKind::Power, dtb::ui::NavIconKind::Battery,
                                  dtb::ui::NavIconKind::Settings};
    m_nav->clear();
    for (int i = 0; i < entries.size(); ++i) {
        QListWidgetItem* item = new QListWidgetItem(entries[i], m_nav);
        QIcon icon;
        // Request the pixmap at the exact displayed size: the source is a
        // 22-logical 4x canvas, so this is a single high-quality resample.
        icon.addPixmap(makeNavIcon(iconKinds[i], QColor("#9aa0ab")).pixmap(20, 20));
        icon.addPixmap(makeNavIcon(iconKinds[i], QColor("#6a9dff")).pixmap(20, 20), QIcon::Selected);
        item->setIcon(icon);
        item->setSizeHint(QSize(176, 40));
        item->setToolTip(entries[i]);
    }
    m_nav->setCurrentRow(0);
    connect(m_nav, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);
}

void MainWindow::buildTray() {
    m_tray = new QSystemTrayIcon(this);
    m_tray->setToolTip(QStringLiteral("Dell Toolbox"));
    paintTrayIcon(0, 0);

    auto* menu = new QMenu(this);
    m_trayMenu = menu;
    auto* modes = new QActionGroup(menu);
    const QList<QPair<QString, ThermalMode>> modeActions = {{tr("Balanced"), ThermalMode::Balanced},
                                                            {tr("G-Mode"), ThermalMode::GMode},
                                                            {tr("Custom"), ThermalMode::Custom}};
    for (const auto& entry : modeActions) {
        QAction* action = menu->addAction(entry.first);
        action->setCheckable(true);
        action->setChecked(m_config->loadMode() == entry.second);
        action->setData(static_cast<int>(entry.second)); // read by syncTrayModes
        modes->addAction(action);
        const ThermalMode mode = entry.second;
        connect(action, &QAction::triggered, this, [this, mode] { setModeFromUi(mode); });
    }
    menu->addSeparator();
    QAction* show = menu->addAction(tr("Show"), this, &MainWindow::showNormal);
    connect(show, &QAction::triggered, this, &MainWindow::activateWindow);
    menu->addAction(tr("Exit"), this, [] {
        QCoreApplication::exit(0);
    });

    m_tray->setContextMenu(menu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick)
            showNormal();
    });
    m_tray->show();
}

void MainWindow::setModeFromUi(ThermalMode mode) {
    m_config->saveMode(mode);
    m_controller->baseline()->setMode(mode);
    m_controller->forceNextModeWrite();
}

void MainWindow::syncTrayModes(ThermalMode current) {
    if (!m_trayMenu)
        return;
    // The mode radio actions sit before the separator and carry their
    // ThermalMode as data (see buildTray).
    const QList<QAction*> actions = m_trayMenu->actions();
    for (QAction* action : actions) {
        if (!action->data().isValid())
            break; // separator reached: mode actions are done
        action->setChecked(action->data().toInt() == static_cast<int>(current));
    }
}


void MainWindow::showToast(const QString& title, const QString& body) {
    if (!m_toast) {
        m_toast = new QLabel(this);
        m_toast->setObjectName(QStringLiteral("toast"));
        m_toast->setStyleSheet(QStringLiteral(
            "background:#15161b; color:#e9ebee; border:1px solid #4f8cff;"
            "border-radius:10px; padding:10px 14px;"));
        m_toast->setTextFormat(Qt::RichText);
        m_toast->setWordWrap(true);
        m_toast->setMaximumWidth(340);
        m_toast->hide();
        m_toastTimer = new QTimer(this);
        m_toastTimer->setSingleShot(true);
        connect(m_toastTimer, &QTimer::timeout, m_toast, &QWidget::hide);
    }
    m_toast->setText(QStringLiteral("<b>%1</b><br>%2").arg(title.toHtmlEscaped(), body.toHtmlEscaped()));
    m_toast->adjustSize();
    m_toast->move(width() - m_toast->width() - 24, 20);
    m_toast->show();
    m_toast->raise();
    m_toastTimer->start(3500);
}

void MainWindow::onThermalModeChanged(ThermalMode current, bool external) {
    syncTrayModes(current);
    QString name;
    switch (current) {
    case ThermalMode::Quiet:
        name = tr("Quiet");
        break;
    case ThermalMode::Cool:
        name = tr("Cool");
        break;
    case ThermalMode::Balanced:
        name = tr("Balanced");
        break;
    case ThermalMode::Performance:
        name = tr("Ultra Performance");
        break;
    case ThermalMode::GMode:
        name = tr("G-Mode");
        break;
    case ThermalMode::Custom:
        name = tr("Custom");
        break;
    }
    m_tray->showMessage(external ? tr("Profile changed externally") : tr("Profile applied"),
                        external ? tr("Now following %1.").arg(name)
                                 : tr("Switched to %1.").arg(name),
                        QSystemTrayIcon::Information, 3000);
    showToast(external ? tr("Profile changed externally") : tr("Profile applied"),
              external ? tr("Now following %1.").arg(name) : tr("Switched to %1.").arg(name));
}

void MainWindow::onChargingModeChanged(const QString& mode) {
    const QString name = chargingModeName(mode);
    m_tray->showMessage(tr("Charging mode"), tr("Switched to %1.").arg(name),
                        QSystemTrayIcon::Information, 3000);
    showToast(tr("Charging mode"), tr("Switched to %1.").arg(name));
}

void MainWindow::onSnapshot(const SystemSnapshot& s) {
    const auto cpu = s.tempOf(0x01);
    int gpu = s.tempOf(0x06).value_or(s.gpu.tempC);
    paintTrayIcon(cpu.value_or(0), gpu);
    m_tray->setToolTip(QStringLiteral("CPU %1°C  GPU %2°C").arg(cpu.value_or(0)).arg(gpu));
}

void MainWindow::paintTrayIcon(int cpuTempC, int gpuTempC) {
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(colors::kCard));
    painter.drawRoundedRect(0, 0, 32, 32, 6, 6);
    const auto tempColor = [](int t) {
        if (t >= 90)
            return QColor("#ff5252");
        if (t >= 80)
            return QColor("#ffd54f");
        return QColor(colors::kText);
    };
    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(10);
    painter.setFont(font);
    painter.setPen(tempColor(gpuTempC));
    painter.drawText(QRect(0, 1, 32, 14), Qt::AlignCenter, QString::number(gpuTempC));
    painter.setPen(tempColor(cpuTempC));
    painter.drawText(QRect(0, 16, 32, 14), Qt::AlignCenter, QString::number(cpuTempC));
    painter.end();
    m_tray->setIcon(pixmap);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // Closing hides to tray; Exit lives in the tray menu.
    hide();
    event->ignore();
    if (!m_trayNoticeShown && QSystemTrayIcon::isSystemTrayAvailable()) {
        m_trayNoticeShown = true;
        m_tray->showMessage(QStringLiteral("Dell Toolbox"),
                            tr("Still running in the tray. Use Exit to quit and restore Balanced mode."));
    }
}

} // namespace dtb::ui
