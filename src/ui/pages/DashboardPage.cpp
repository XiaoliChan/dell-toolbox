#include "ui/pages/DashboardPage.h"

#include <QHBoxLayout>
#include <QStyle>
#include <QPushButton>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include "hal/win/AwccConflict.h"
#include "hal/win/WinSystemInfo.h"
#endif

#include "ui/Theme.h"
#include "ui/widgets/AreaChart.h"
#include "ui/widgets/Card.h"
#include "ui/widgets/CircularGauge.h"

namespace dtb::ui {

DashboardPage::DashboardPage(HalSet hal, Controller* controller, ConfigStore* config, QWidget* parent)
    : QWidget(parent), m_hal(hal) {
    Q_UNUSED(controller);
    Q_UNUSED(config);
    auto* layout = new QGridLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setHorizontalSpacing(16);
    layout->setVerticalSpacing(16);
    layout->addWidget(buildHeaderCard(), 0, 0, 1, 2);
    // Row 1 is reserved for the AWCC-paused banner (see setAwccPaused); the
    // banner must own its own row or it paints on top of the header card.
    layout->addWidget(buildTempCard(QStringLiteral("CPU temperature")), 2, 0);
    layout->addWidget(buildTempCard(QStringLiteral("GPU temperature")), 2, 1);
    layout->addWidget(buildFansCard(), 3, 0);
    layout->addWidget(buildBatteryCard(), 3, 1);
    layout->setRowStretch(4, 1);
    layout->setColumnStretch(0, 1);
    layout->setColumnStretch(1, 1);

    connect(controller, &Controller::snapshotUpdated, this, &DashboardPage::onSnapshot);
    connect(controller, &Controller::thermalModeChanged, this, &DashboardPage::onThermalModeChanged);
    connect(controller, &Controller::activePolicyChanged, this, &DashboardPage::onPolicyChanged);
    connect(controller, &Controller::chargingModeChanged, this, &DashboardPage::onChargingModeChanged);
}

void DashboardPage::setAwccPaused(bool paused) {
    if (!m_pausedBanner) {
        m_pausedBanner = new QLabel(this);
        wrapLabel(m_pausedBanner);
        m_pausedBanner->setStyleSheet(QStringLiteral(
            "background:#12233c; color:#6a9dff; border:1px solid #1d3a66; border-radius:10px;"
            "padding:10px 14px; font-weight:600;"));
        auto* lay = qobject_cast<QGridLayout*>(layout());
        lay->addWidget(m_pausedBanner, 1, 0, 1, 2);
    }
    m_pausedBanner->setText(
        tr("Monitoring only - AWCC is running and re-asserts its own thermal profile (writes get "
           "snapped back). Dell Toolbox pauses its writes and resumes automatically when AWCC "
           "closes."));
    m_pausedBanner->setVisible(paused);
}

QWidget* DashboardPage::buildHeaderCard() {
    auto* card = new Card(QString(), this);
    card->headerLayout()->itemAt(0)->widget()->hide(); // no title on the banner

    auto* identity = new QWidget(card);
    auto* il = new QVBoxLayout(identity);
    il->setContentsMargins(0, 0, 0, 0);
    il->setSpacing(2);
    m_model = new QLabel(QStringLiteral("Dell System"), identity);
    m_model->setStyleSheet(QStringLiteral("font-size:20px; font-weight:800; color:#ffffff;"));
    m_modelSub = new QLabel(tr("Fan & thermal control via the AWCC WMI interface"), identity);
    m_modelSub->setObjectName(QStringLiteral("cardCaption"));
    il->addWidget(m_model);
    il->addWidget(m_modelSub);
    card->headerLayout()->insertWidget(0, identity);

    m_statusChip = new QLabel(card);
    if (m_hal.thermal && m_hal.thermal->available()) {
        m_statusChip->setText(QStringLiteral("AWCC WMI"));
        m_statusChip->setObjectName(QStringLiteral("chipGood"));
    } else {
        m_statusChip->setText(QStringLiteral("No thermal HAL"));
        m_statusChip->setObjectName(QStringLiteral("chipBad"));
    }
    card->headerLayout()->addWidget(m_statusChip);

#ifdef Q_OS_WIN
    const QString model = dtb::win::systemModel();
    if (!model.isEmpty())
        m_model->setText(QStringLiteral("Dell %1").arg(model));
#endif
    return card;
}

QWidget* DashboardPage::buildTempCard(const QString& title) {
    auto* card = new Card(title, this);
    const bool cpu = title.startsWith(QLatin1String("CPU"));
    auto*& chart = cpu ? m_cpuChart : m_gpuChart;
    auto*& now = cpu ? m_cpuNow : m_gpuNow;
    auto*& chip = cpu ? m_cpuChip : m_gpuChip;
    auto*& high = cpu ? m_cpuHigh : m_gpuHigh;
    auto*& avg = cpu ? m_cpuAvg : m_gpuAvg;
    auto*& load = cpu ? m_cpuLoad : m_gpuLoad;
    auto*& power = cpu ? m_cpuPower : m_gpuPower;

    auto* row = new QWidget(card);
    auto* rl = new QHBoxLayout(row);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(18);
    chart = new AreaChart(row);
    // 170 keeps two cards side by side inside a 948px window next to the
    // 196px sidebar; the chart stretches with the window.
    chart->setMinimumSize(170, 150);
    rl->addWidget(chart, 1);

    auto* stats = new QWidget(row);
    auto* sl = new QVBoxLayout(stats);
    sl->setContentsMargins(0, 0, 0, 0);
    sl->setSpacing(2);
    sl->addStretch(1);
    auto* nowCap = new QLabel(QStringLiteral("Current"), stats);
    nowCap->setObjectName(QStringLiteral("statCaption"));
    now = new QLabel(QStringLiteral("--"), stats);
    now->setStyleSheet(QStringLiteral("font-size:30px; font-weight:800; color:#ffffff;"));
    chip = new QLabel(QStringLiteral("-"), stats);
    chip->setObjectName(QStringLiteral("chip"));
    chip->setAlignment(Qt::AlignHCenter);
    high = new QLabel(QStringLiteral("Highest -"), stats);
    high->setObjectName(QStringLiteral("statCaption"));
    avg = new QLabel(QStringLiteral("Average -"), stats);
    avg->setObjectName(QStringLiteral("statCaption"));
    load = new QLabel(QStringLiteral("Load -"), stats);
    load->setObjectName(QStringLiteral("statCaption"));
    power = new QLabel(QStringLiteral("Power -"), stats);
    power->setObjectName(QStringLiteral("statCaption"));
    sl->addWidget(nowCap);
    sl->addWidget(now);
    sl->addWidget(chip, 0, Qt::AlignLeft);
    sl->addSpacing(8);
    sl->addWidget(high);
    sl->addWidget(avg);
    sl->addWidget(load);
    sl->addWidget(power);
    sl->addStretch(1);
    rl->addWidget(stats, 0);
    card->bodyLayout()->addWidget(row);
    return card;
}

QWidget* DashboardPage::buildFansCard() {
    auto* card = new Card(QStringLiteral("Thermal & performance"), this);
    auto* row = new QWidget(card);
    auto* rl = new QHBoxLayout(row);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(8);
    auto fanHalf = [&](const QString& tempTitle, const QString& speedTitle, CircularGauge** temp,
                       CircularGauge** speed) {
        auto* w = new QWidget(row);
        auto* l = new QHBoxLayout(w);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(8);
        *temp = new CircularGauge(tempTitle, 110, QStringLiteral("°C"), w);
        (*temp)->setAbsoluteColors(true);
        // Percent (op 6) reads 0 while the BIOS owns the fans; RPM is the
        // honest live figure. Ring fill is relative to a 7000 RPM ceiling.
        *speed = new CircularGauge(speedTitle, 7000, QStringLiteral("RPM"), w);
        l->addWidget(*temp, 1);
        l->addWidget(*speed, 1);
        return w;
    };
    rl->addWidget(fanHalf(QStringLiteral("CPU temp"), QStringLiteral("CPU fan"), &m_cpuFanTemp, &m_cpuFanSpeed), 1);
    rl->addWidget(fanHalf(QStringLiteral("GPU temp"), QStringLiteral("GPU fan"), &m_gpuFanTemp, &m_gpuFanSpeed), 1);
    card->bodyLayout()->addWidget(row);
    // Current thermal profile + the policy currently driving the loop.
    auto* chips = new QHBoxLayout;
    chips->setContentsMargins(0, 6, 0, 0);
    chips->setSpacing(8);
    auto chipCaption = [](const QString& text) {
        auto* l = new QLabel(text);
        l->setObjectName(QStringLiteral("statCaption"));
        return l;
    };
    chips->addWidget(chipCaption(QStringLiteral("Thermal")));
    m_thermalChip = new QLabel(QStringLiteral("-"), card);
    m_thermalChip->setObjectName(QStringLiteral("chip"));
    chips->addWidget(m_thermalChip);
    chips->addSpacing(16);
    chips->addWidget(chipCaption(QStringLiteral("Policy")));
    m_policyChip = new QLabel(QStringLiteral("-"), card);
    m_policyChip->setObjectName(QStringLiteral("chip"));
    chips->addWidget(m_policyChip);
    chips->addStretch(1);
    card->bodyLayout()->addLayout(chips);
    return card;
}

QWidget* DashboardPage::buildBatteryCard() {
    auto* card = new Card(QStringLiteral("Battery"), this);
    auto* body = new QWidget(card);
    auto* bl = new QVBoxLayout(body);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(10);
    auto* top = new QWidget(body);
    auto* tl = new QHBoxLayout(top);
    tl->setContentsMargins(0, 0, 0, 0);
    tl->setSpacing(12);
    m_batteryValue = new QLabel(QStringLiteral("--"), top);
    m_batteryValue->setStyleSheet(QStringLiteral("font-size:34px; font-weight:800; color:#ffffff;"));
    m_batteryChip = new QLabel(QStringLiteral("-"), top);
    m_batteryChip->setObjectName(QStringLiteral("chip"));
    tl->addWidget(m_batteryValue, 0, Qt::AlignBottom);
    tl->addWidget(m_batteryChip, 0, Qt::AlignBottom);
    tl->addStretch(1);
    bl->addWidget(top);
    m_batteryDetail = new QLabel(QStringLiteral(" "), body);
    m_batteryDetail->setObjectName(QStringLiteral("cardCaption"));
    wrapLabel(m_batteryDetail);
    bl->addWidget(m_batteryDetail);
    auto* chargeRow = new QHBoxLayout;
    chargeRow->setContentsMargins(0, 2, 0, 0);
    chargeRow->addWidget(new QLabel(tr("Charging mode"), card));
    m_chargeMode = new QLabel(QStringLiteral("-"), card);
    m_chargeMode->setStyleSheet(QStringLiteral("font-weight:600; color:#e9ebee;"));
    chargeRow->addWidget(m_chargeMode);
    chargeRow->addStretch(1);
    bl->addLayout(chargeRow);

    // Detail grid: the figures people actually judge a battery by.
    auto* grid = new QGridLayout;
    grid->setContentsMargins(0, 2, 0, 0);
    grid->setHorizontalSpacing(18);
    grid->setVerticalSpacing(8);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    auto stat = [](const QString& caption, QLabel** value) {
        auto* w = new QWidget;
        auto* l = new QVBoxLayout(w);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(1);
        auto* cap = new QLabel(caption, w);
        cap->setObjectName(QStringLiteral("statCaption"));
        *value = new QLabel(QStringLiteral("-"), w);
        (*value)->setStyleSheet(QStringLiteral("font-size:13px; font-weight:600; color:#e9ebee;"));
        (*value)->setTextInteractionFlags(Qt::TextSelectableByMouse);
        l->addWidget(cap);
        l->addWidget(*value);
        return w;
    };
    grid->addWidget(stat(tr("Health"), &m_batHealth), 0, 0);
    grid->addWidget(stat(tr("Cycle count"), &m_batCycles), 0, 1);
    grid->addWidget(stat(tr("Full charge capacity"), &m_batFull), 1, 0);
    grid->addWidget(stat(tr("Remaining capacity"), &m_batRemain), 1, 1);
    grid->addWidget(stat(tr("Voltage"), &m_batVolt), 2, 0);
    grid->addWidget(stat(tr("Power"), &m_batRate), 2, 1);
    bl->addLayout(grid);
    bl->addStretch(1);
    card->bodyLayout()->addWidget(body);
    return card;
}

void DashboardPage::applyTempStats(QLabel* big, QLabel* chip, QLabel* high, QLabel* avg, int value,
                                   const QVector<int>& history) {
    big->setText(QString::number(value) + QStringLiteral("°C"));
    int hi = value, sum = 0, n = 0;
    for (int v : history) {
        hi = qMax(hi, v);
        sum += v;
        ++n;
    }
    high->setText(QStringLiteral("Highest %1°C").arg(n ? hi : value));
    avg->setText(QStringLiteral("Average %1°C").arg(n ? qRound(double(sum) / n) : value));
    QString grade = QStringLiteral("Normal");
    QString style = QStringLiteral("chipGood");
    if (value >= 90) {
        grade = QStringLiteral("Critical");
        style = QStringLiteral("chipBad");
    } else if (value >= 80) {
        grade = QStringLiteral("Hot");
        style = QStringLiteral("chipWarn");
    }
    chip->setText(grade);
    chip->setObjectName(style);
    chip->style()->unpolish(chip);
    chip->style()->polish(chip);
}

void DashboardPage::onSnapshot(const SystemSnapshot& s) {
    const int cpu = s.tempOf(0x01).value_or(0);
    const int gpu = s.tempOf(0x06).value_or(s.gpu.tempC > 0 ? s.gpu.tempC : 0);
    m_cpuChart->push(cpu);
    m_gpuChart->push(gpu);
    m_cpuHist.append(cpu);
    m_gpuHist.append(gpu);
    while (m_cpuHist.size() > 90)
        m_cpuHist.removeFirst();
    while (m_gpuHist.size() > 90)
        m_gpuHist.removeFirst();
    applyTempStats(m_cpuNow, m_cpuChip, m_cpuHigh, m_cpuAvg, cpu, m_cpuHist);
    applyTempStats(m_gpuNow, m_gpuChip, m_gpuHigh, m_gpuAvg, gpu, m_gpuHist);
    // Live utilisation next to the thermals (voltage arrives with NVAPI).
    m_cpuLoad->setText(QStringLiteral("Load %1%").arg(s.cpuLoadPercent >= 0 ? s.cpuLoadPercent : 0));
    m_gpuLoad->setText(QStringLiteral("Load %1%").arg(s.gpu.utilPercent >= 0 ? s.gpu.utilPercent : 0));
    m_gpuPower->setText(s.gpu.powerW > 0 ? QStringLiteral("Power %1 W").arg(s.gpu.powerW)
                                         : QStringLiteral("Power -"));
    // CPU power: a real RAPL read needs a kernel driver (that is what AWCC's
    // OC service is). Until that decision, show a load-derived estimate -
    // honestly marked with a tilde; capped at the 45 W sustained limit.
    if (s.cpuLoadPercent >= 0)
        m_cpuPower->setText(QStringLiteral("Power ~%1 W").arg(qBound(5, 8 + int(s.cpuLoadPercent * 0.4), 45)));
    else
        m_cpuPower->setText(QStringLiteral("Power -"));

    auto rpmOf = [&s](FanId id) {
        for (const auto& f : s.fans)
            if (f.id == id && f.valid())
                return f.rpm;
        return 0;
    };
    m_cpuFanTemp->setValue(s.tempOf(0x01).value_or(0));
    m_gpuFanTemp->setValue(gpu);
    m_cpuFanSpeed->setValue(rpmOf(0x33));
    m_gpuFanSpeed->setValue(rpmOf(0x32));

    const BatteryInfo& b = s.battery;
    if (!b.present) {
        m_batteryValue->setText(tr("No battery"));
        m_batteryChip->setText(QStringLiteral("AC"));
        m_batteryChip->setObjectName(QStringLiteral("chip"));
        m_batteryDetail->setText(QString());
        for (QLabel* cell : {m_batHealth, m_batCycles, m_batFull, m_batRemain, m_batVolt, m_batRate})
            cell->setText(QStringLiteral("-"));
    } else {
        m_batteryValue->setText(b.percent >= 0 ? QString::number(b.percent) + QStringLiteral("%")
                                               : QStringLiteral("-"));
        m_batteryChip->setText(b.charging ? tr("Charging") : (b.acOnline ? tr("Plugged in") : tr("On battery")));
        m_batteryChip->setObjectName(b.charging ? QStringLiteral("chipInfo") : QStringLiteral("chipGood"));
        m_batteryDetail->setText(b.charging ? tr("Charging")
                                            : (b.acOnline ? tr("Plugged in, not charging") : tr("On battery")));
        m_batHealth->setText(b.healthPercent >= 0 ? tr("%1%").arg(b.healthPercent) : QStringLiteral("-"));
        m_batCycles->setText(b.cycleCount >= 0 ? QString::number(b.cycleCount) : QStringLiteral("-"));
        m_batFull->setText(b.fullChargeCapacityMWh > 0
                               ? tr("%1 Wh").arg(b.fullChargeCapacityMWh / 1000.0, 0, 'f', 1)
                               : QStringLiteral("-"));
        m_batRemain->setText(b.remainingCapacityMWh > 0
                                 ? tr("%1 Wh").arg(b.remainingCapacityMWh / 1000.0, 0, 'f', 1)
                                 : QStringLiteral("-"));
        m_batVolt->setText(b.voltageMV > 0 ? tr("%1 V").arg(b.voltageMV / 1000.0, 0, 'f', 2)
                                           : QStringLiteral("-"));
        if (b.rateMW != 0)
            m_batRate->setText(tr("%1%2 W").arg(b.rateMW > 0 ? QStringLiteral("+") : QStringLiteral("-"))
                                   .arg(qAbs(b.rateMW) / 1000.0, 0, 'f', 1));
        else
            m_batRate->setText(tr("Idle"));
    }
    m_batteryChip->style()->unpolish(m_batteryChip);
    m_batteryChip->style()->polish(m_batteryChip);
}

void DashboardPage::onThermalModeChanged(ThermalMode mode, bool external) {
    Q_UNUSED(external);
    QString name;
    switch (mode) {
    case ThermalMode::Quiet:
        name = tr("Quiet");
        break;
    case ThermalMode::Cool:
        name = tr("Cool");
        break;
    case ThermalMode::Balanced:
        name = tr("Optimized");
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
    m_thermalChip->setText(name);
}

void DashboardPage::onPolicyChanged(const QString& policy) {
    m_policyChip->setText(policy);
}

void DashboardPage::onChargingModeChanged(const QString& mode) {
    m_chargeMode->setText(mode);
}

} // namespace dtb::ui
