#pragma once

#include <QComboBox>
#include <QLabel>
#include <QWidget>

#include "core/ConfigStore.h"
#include "core/Controller.h"
#include "hal/HalInterfaces.h"

namespace dtb::ui {

class SettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(HalSet hal, Controller* controller, ConfigStore* config, QWidget* parent = nullptr);

private slots:
    void onAutostartToggled(bool on);
    void onLogLevelChanged(int index);
    void onResetDefaults();

private:
    QWidget* buildGeneralCard();
    QWidget* buildAboutCard();

    HalSet m_hal;
    Controller* m_controller; // not owned
    ConfigStore* m_config; // not owned
    QComboBox* m_logLevel = nullptr;
};

} // namespace dtb::ui
