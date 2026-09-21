#pragma once

#include <QComboBox>
#include <QLabel>
#include <QRadioButton>
#include <QSlider>
#include <QVBoxLayout>
#include <QWidget>

#include "core/ConfigStore.h"
#include "core/Controller.h"
#include "hal/HalInterfaces.h"

namespace dtb::ui {

class BatteryPage : public QWidget {
    Q_OBJECT
public:
    explicit BatteryPage(HalSet hal, Controller* controller, ConfigStore* config, QWidget* parent = nullptr);

private slots:
    void onSnapshot(const SystemSnapshot& snapshot);
    void onModeChosen();
    void onCustomRangeChanged();

private:
    QWidget* buildStatusCard();
    QWidget* buildModesCard();
    QWidget* infoTile(const QString& caption, QLabel** valueOut);

    HalSet m_hal;
    Controller* m_controller; // not owned
    ConfigStore* m_config; // not owned
    QLabel* m_percent = nullptr;
    QLabel* m_state = nullptr;
    QLabel* m_health = nullptr;
    QLabel* m_cycles = nullptr;
    QLabel* m_design = nullptr;
    QLabel* m_full = nullptr;
    QLabel* m_voltage = nullptr;
    QLabel* m_power = nullptr;
    QLabel* m_vendor = nullptr;
    QLabel* m_device = nullptr;
    QLabel* m_serial = nullptr;
    QLabel* m_chemistry = nullptr;
    QWidget* m_modesCard = nullptr;
    QVBoxLayout* m_modesLayout = nullptr;
    QLabel* m_modesSource = nullptr;
    QButtonGroup* m_modeGroup = nullptr;
    QSlider* m_start = nullptr;
    QSlider* m_stop = nullptr;
    QLabel* m_rangeLabel = nullptr;
};

} // namespace dtb::ui
