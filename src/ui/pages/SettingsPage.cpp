#include "ui/pages/SettingsPage.h"

#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include "core/AppInfo.h"
#include "core/Autostart.h"
#include "core/Logger.h"
#include "ui/widgets/Card.h"
#include "ui/widgets/WheelGuard.h"

namespace dtb::ui {

SettingsPage::SettingsPage(HalSet hal, Controller* controller, ConfigStore* config, QWidget* parent)
    : QWidget(parent), m_hal(hal), m_controller(controller), m_config(config) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(16);
    auto* title = new QLabel(tr("Settings"), this);
    title->setObjectName(QStringLiteral("pageTitle"));
    layout->addWidget(title);
    layout->addWidget(buildGeneralCard());
    layout->addWidget(buildAboutCard());
    layout->addStretch(1);
    // The log-level combo must not change under a scroll gesture, like every
    // other page's sliders/spin boxes/combos.
    WheelGuard::apply(this);
}

QWidget* SettingsPage::buildGeneralCard() {
    auto* box = new Card(QStringLiteral("General"), this);
    auto* form = new QGridLayout;
    form->setHorizontalSpacing(16);

    auto* autostartLabel = new QLabel(tr("Start with Windows"), box);
    auto* autostart = new QPushButton(m_config->loadAutostart() ? tr("Enabled") : tr("Disabled"), box);
    autostart->setObjectName(QStringLiteral("ghostButton"));
    autostart->setCheckable(true);
    autostart->setChecked(m_config->loadAutostart());
    connect(autostart, &QPushButton::toggled, this, &SettingsPage::onAutostartToggled);

    auto* minLabel = new QLabel(tr("Start minimized to tray"), box);
    auto* startMin = new QPushButton(m_config->loadStartMinimized() ? tr("Enabled") : tr("Disabled"), box);
    startMin->setObjectName(QStringLiteral("ghostButton"));
    startMin->setCheckable(true);
    startMin->setChecked(m_config->loadStartMinimized());
    connect(startMin, &QPushButton::toggled, this, [this, startMin](bool on) {
        startMin->setText(on ? tr("Enabled") : tr("Disabled"));
        m_config->saveStartMinimized(on);
    });
    // Reset to defaults clears this flag too; re-sync the toggle instead of
    // leaving a stale Enabled/Disabled state on the button.
    connect(m_controller, &Controller::configReloaded, startMin, [this, startMin] {
        const bool on = m_config->loadStartMinimized();
        QSignalBlocker block(startMin);
        startMin->setChecked(on);
        startMin->setText(on ? tr("Enabled") : tr("Disabled"));
    });

    auto* planSyncLabel = new QLabel(tr("G-Mode switches the Windows power plan"), box);
    auto* planSync = new QPushButton(m_config->loadPowerPlanSync() ? tr("Enabled") : tr("Disabled"), box);
    planSync->setObjectName(QStringLiteral("ghostButton"));
    planSync->setCheckable(true);
    planSync->setChecked(m_config->loadPowerPlanSync());
    connect(planSync, &QPushButton::toggled, this, [this, planSync](bool on) {
        planSync->setText(on ? tr("Enabled") : tr("Disabled"));
        m_config->savePowerPlanSync(on);
        m_controller->setPowerPlanSyncEnabled(on);
    });

    auto* logLabel = new QLabel(tr("Log level"), box);
    m_logLevel = new QComboBox(box);
    m_logLevel->addItems({tr("Debug"), tr("Info"), tr("Warning"), tr("Error")});
    m_logLevel->setCurrentIndex(qBound(0, m_config->loadLogLevel(), 3));
    connect(m_logLevel, &QComboBox::currentIndexChanged, this, &SettingsPage::onLogLevelChanged);

    auto* resetLabel = new QLabel(tr("Configuration"), box);
    auto* reset = new QPushButton(tr("Restore defaults"), box);
    connect(reset, &QPushButton::clicked, this, &SettingsPage::onResetDefaults);

    form->addWidget(autostartLabel, 0, 0);
    form->addWidget(autostart, 0, 1);
    form->addWidget(minLabel, 1, 0);
    form->addWidget(startMin, 1, 1);
    form->addWidget(planSyncLabel, 2, 0);
    form->addWidget(planSync, 2, 1);
    form->addWidget(logLabel, 3, 0);
    form->addWidget(m_logLevel, 3, 1);
    form->addWidget(resetLabel, 4, 0);
    form->addWidget(reset, 4, 1);
    form->setColumnStretch(2, 1);
    box->bodyLayout()->addLayout(form);
    return box;
}

QWidget* SettingsPage::buildAboutCard() {
    auto* box = new Card(QStringLiteral("About"), this);
    auto* layout = box->bodyLayout();
    auto* name = new QLabel(
        QStringLiteral("%1 %2").arg(QLatin1String(dtb::app::kName), QLatin1String(dtb::app::kVersion)), box);
    name->setObjectName(QStringLiteral("cardValue"));
    auto* license = new QLabel(
        QStringLiteral("GPLv3 - <a href=\"%1\">source</a>").arg(QLatin1String(dtb::app::kRepoUrl)), box);
    license->setOpenExternalLinks(true);
    layout->addWidget(name);
    layout->addWidget(license);
    return box;
}

void SettingsPage::onAutostartToggled(bool on) {
    QPushButton* button = qobject_cast<QPushButton*>(sender());
    if (button) {
        button->setText(on ? tr("Enabled") : tr("Disabled"));
        button->setDisabled(true);
        const bool ok = dtb::Autostart::setEnabled(on);
        button->setEnabled(true);
        if (!ok) {
            button->setChecked(false);
            button->setText(tr("Disabled"));
        }
    }
    m_config->saveAutostart(on);
}

void SettingsPage::onLogLevelChanged(int index) {
    m_config->saveLogLevel(index);
    dtbLog(info) << "log level set to" << index;
}

void SettingsPage::onResetDefaults() {
    m_config->resetToDefaults();
    m_logLevel->setCurrentIndex(1);
    // Re-reads mode/curves/boosts/scene/failsafe into the running loop and
    // emits configReloaded() so every page refreshes its controls.
    m_controller->reloadFromConfig();
}

} // namespace dtb::ui
