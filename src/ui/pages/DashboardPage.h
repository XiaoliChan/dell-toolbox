#pragma once

#include <QLabel>
#include <QWidget>

#include "core/ConfigStore.h"
#include "core/Controller.h"
#include "hal/HalInterfaces.h"

namespace dtb::ui {

class AreaChart;
class CircularGauge;

class DashboardPage : public QWidget {
    Q_OBJECT
public:
    explicit DashboardPage(HalSet hal, Controller* controller, ConfigStore* config, QWidget* parent = nullptr);
    void setAwccPaused(bool paused);

private slots:
    void onSnapshot(const SystemSnapshot& snapshot);

private:
    QWidget* buildHeaderCard();
    QWidget* buildTempCard(const QString& title);
    QWidget* buildFansCard();
    QWidget* buildBatteryCard();
    void applyTempStats(QLabel* big, QLabel* chip, QLabel* high, QLabel* avg, int value,
                        const QVector<int>& history);

    HalSet m_hal;
    QLabel* m_model = nullptr;
    QLabel* m_modelSub = nullptr;
    QLabel* m_statusChip = nullptr;
    QLabel* m_pausedBanner = nullptr;
    AreaChart* m_cpuChart = nullptr;
    AreaChart* m_gpuChart = nullptr;
    QLabel* m_cpuNow = nullptr;
    QLabel* m_cpuChip = nullptr;
    QLabel* m_cpuHigh = nullptr;
    QLabel* m_cpuAvg = nullptr;
    QLabel* m_gpuNow = nullptr;
    QLabel* m_gpuChip = nullptr;
    QLabel* m_gpuHigh = nullptr;
    QLabel* m_gpuAvg = nullptr;
    QLabel* m_cpuLoad = nullptr;
    QLabel* m_cpuPower = nullptr;
    QLabel* m_gpuLoad = nullptr;
    QLabel* m_gpuPower = nullptr;
    CircularGauge* m_cpuFanTemp = nullptr;
    CircularGauge* m_gpuFanTemp = nullptr;
    CircularGauge* m_cpuFanSpeed = nullptr;
    CircularGauge* m_gpuFanSpeed = nullptr;
    QLabel* m_batteryValue = nullptr;
    QLabel* m_batteryChip = nullptr;
    QLabel* m_batteryDetail = nullptr;
    QLabel* m_batHealth = nullptr;
    QLabel* m_batCycles = nullptr;
    QLabel* m_batFull = nullptr;
    QLabel* m_batRemain = nullptr;
    QLabel* m_batVolt = nullptr;
    QLabel* m_batRate = nullptr;
    QVector<int> m_cpuHist;
    QVector<int> m_gpuHist;
};

} // namespace dtb::ui
