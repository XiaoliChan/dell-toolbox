#pragma once

#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QWidget>

#include "core/ConfigStore.h"
#include "core/Controller.h"
#include "hal/HalInterfaces.h"

namespace dtb::ui {

class PerformancePage : public QWidget {
    Q_OBJECT
public:
    explicit PerformancePage(HalSet hal, Controller* controller, ConfigStore* config, QWidget* parent = nullptr);

private slots:
    void applyManual();

private:
    QWidget* buildPowerPlanCard();
    QWidget* buildSlidersCard();
    void refreshProfiles(const QString& selectName = {});
    void updateActiveLabel();
    QWidget* m_manualCard = nullptr;
    QLabel* m_activeLabel = nullptr;
    QComboBox* m_profileCombo = nullptr;
    QPushButton* m_newProfile = nullptr;
    QPushButton* m_renameProfile = nullptr;
    QPushButton* m_deleteProfile = nullptr;
    QPushButton* m_saveProfile = nullptr;
    QList<ConfigStore::ManualProfile> m_profilesData;

    HalSet m_hal;
    Controller* m_controller; // not owned
    ConfigStore* m_config; // not owned
    QComboBox* m_planCombo = nullptr;
    QSlider* m_pl1 = nullptr;
    QLabel* m_pl1Value = nullptr;
    QSlider* m_pl2 = nullptr;
    QLabel* m_pl2Value = nullptr;
    QSlider* m_gpuPpt = nullptr;
    QLabel* m_gpuPptValue = nullptr;
};

} // namespace dtb::ui
