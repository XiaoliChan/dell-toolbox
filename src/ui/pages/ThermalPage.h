#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QHash>
#include <QLabel>
#include <QTimer>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

#include "core/ConfigStore.h"
#include "core/Controller.h"
#include "hal/HalInterfaces.h"
#include "ui/widgets/FanCurveEditor.h"

namespace dtb::ui {

class ThermalPage : public QWidget {
    Q_OBJECT
public:
    explicit ThermalPage(HalSet hal, Controller* controller, ConfigStore* config, QWidget* parent = nullptr);

private slots:
    void onModeAdopted(ThermalMode mode, bool external);
    void onModeSelectedWithMode(ThermalMode mode);
    void onCurveEdited(FanId fan, const QList<QPair<int, int>>& points);
    void onCurveModeChanged(int index);
    void onCurveReset();
    void onConfigReloaded();
    void onFanStyleChanged(int index);
    void saveBoosts();
    void onSceneParamsChanged();
    void onSceneReset();
    void onSnapshot(const SystemSnapshot& s);

private:
    QWidget* buildModeCard();
    QWidget* buildCurvesCard();
    QWidget* buildSceneCard();
    void addProfile(QButtonGroup* group, QVBoxLayout* layout, ThermalMode mode, const QString& title,
                    const QString& description);

    HalSet m_hal;
    Controller* m_controller; // not owned
    ConfigStore* m_config; // not owned
    QLabel* m_banner = nullptr;
    QButtonGroup* m_modeGroup = nullptr;
    QList<QRadioButton*> m_modeRadios;
    QList<ThermalMode> m_modeValues;
    QWidget* m_curvesCard = nullptr;
    QComboBox* m_curveMode = nullptr;
    QList<QPair<FanId, FanCurveEditor*>> m_curveEditors;
    QComboBox* m_fanStyle = nullptr; // custom mode: Easy (boost) / Advanced (curves)
    QWidget* m_easyBody = nullptr;
    QWidget* m_advanceBody = nullptr;
    QPushButton* m_curveReset = nullptr;
    QTimer* m_boostDebounce = nullptr;
    QHash<FanId, int> m_boostSaved; // last persisted boost per fan
    QList<QPair<FanId, QSlider*>> m_boostSliders;
    QList<QPair<FanId, QLabel*>> m_boostLabels;
    QCheckBox* m_sceneEnabled = nullptr;
    QPushButton* m_sceneReset = nullptr;
    QWidget* m_sceneBody = nullptr;
    QSpinBox* m_gpuEnter = nullptr;
    QSpinBox* m_gpuExit = nullptr;
    QSpinBox* m_enterDebounce = nullptr;
    QSpinBox* m_exitDebounce = nullptr;
    QTextEdit* m_processes = nullptr;
};

} // namespace dtb::ui
