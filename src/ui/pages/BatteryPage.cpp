#include "ui/pages/BatteryPage.h"

#include <QButtonGroup>
#include <QGridLayout>
#include "ui/Theme.h"
#include "ui/widgets/Card.h"
#include "ui/widgets/WheelGuard.h"
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

namespace dtb::ui {

BatteryPage::BatteryPage(HalSet hal, Controller* controller, ConfigStore* config, QWidget* parent)
    : QWidget(parent), m_hal(hal), m_controller(controller), m_config(config) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(16);
    auto* title = new QLabel(tr("Battery"), this);
    title->setObjectName(QStringLiteral("pageTitle"));
    layout->addWidget(title);
    layout->addWidget(buildStatusCard());

    if (m_hal.charge && m_hal.charge->available()) {
        layout->addWidget(buildModesCard(), 1);
    } else {
        auto* box = new Card(QStringLiteral("Charging modes"), this);
        auto* boxLayout = box->bodyLayout();
        boxLayout->addWidget(new QLabel(
            tr("Native Dell ACPI-WMI charging is unavailable on this machine and Dell Command | "
               "Configure was not found. Run dell-toolbox.exe --doctor and share the output - it "
               "lists which Dell interfaces this machine exposes."), box));
        wrapLabel(qobject_cast<QLabel*>(boxLayout->itemAt(boxLayout->count() - 1)->widget()));
        auto* link = new QLabel(
            QStringLiteral("<a href=\"https://www.dell.com/support\">dell.com/support</a> - "
                           "search for “Dell Command | Configure”"),
            box);
        link->setOpenExternalLinks(true);
        boxLayout->addWidget(link);
        layout->addWidget(box);
        layout->addStretch(1);
    }

    connect(controller, &Controller::snapshotUpdated, this, &BatteryPage::onSnapshot);
    WheelGuard::apply(this);
}

QWidget* BatteryPage::infoTile(const QString& caption, QLabel** valueOut) {
    auto* w = new QWidget(this);
    auto* l = new QVBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(2);
    *valueOut = new QLabel(QString::fromUtf8("-"), w);
    (*valueOut)->setObjectName(QStringLiteral("cardValue"));
    auto* cap = new QLabel(caption, w);
    cap->setObjectName(QStringLiteral("cardCaption"));
    l->addWidget(*valueOut);
    l->addWidget(cap);
    return w;
}

QWidget* BatteryPage::buildStatusCard() {
    auto* box = new Card(QStringLiteral("Battery information"), this);
    auto* outer = box->bodyLayout();
    outer->setSpacing(24);

    // Big percent + state on the left (DPM-style summary).
    auto* summary = new QWidget(box);
    auto* summaryLayout = new QVBoxLayout(summary);
    summaryLayout->setContentsMargins(0, 0, 0, 0);
    m_percent = new QLabel(QStringLiteral("--"), summary);
    m_percent->setStyleSheet(QStringLiteral("font-size:42px; font-weight:800; color:#ffffff;"));
    m_state = new QLabel(QStringLiteral(" "), summary);
    m_state->setObjectName(QStringLiteral("cardCaption"));
    summaryLayout->addWidget(m_percent);
    summaryLayout->addWidget(m_state);
    summaryLayout->addStretch(1);
    outer->addWidget(summary, 0);

    // Info tile grid on the right.
    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(36);
    grid->setVerticalSpacing(14);
    int r = 0, c = 0;
    QWidget* tiles[] = {infoTile(tr("Health"), &m_health), infoTile(tr("Cycle count"), &m_cycles),
                        infoTile(tr("Design capacity"), &m_design),
                        infoTile(tr("Full charge capacity"), &m_full),
                        infoTile(tr("Voltage"), &m_voltage), infoTile(tr("Power flow"), &m_power),
                        infoTile(tr("Manufacturer"), &m_vendor), infoTile(tr("Device"), &m_device),
                        infoTile(tr("Serial"), &m_serial), infoTile(tr("Chemistry"), &m_chemistry)};
    for (QWidget* tile : tiles) {
        grid->addWidget(tile, r, c);
        if (++c == 5) {
            c = 0;
            ++r;
        }
    }
    auto* gridHolder = new QWidget(box);
    gridHolder->setLayout(grid);
    outer->addWidget(gridHolder, 1);
    return box;
}

QWidget* BatteryPage::buildModesCard() {
    auto* box = new Card(QStringLiteral("Charging modes"), this);
    auto* layout = box->bodyLayout();
    m_modesSource = new QLabel(box);
    m_modesSource->setObjectName(QStringLiteral("cardCaption"));
    m_modesSource->setText(tr("Backend: %1").arg(m_hal.charge->sourceName()));
    layout->addWidget(m_modesSource);

    m_modeGroup = new QButtonGroup(box);
    const QString current = m_hal.charge->currentMode();
    const QList<IChargeThresholdHAL::Mode> modes = m_hal.charge->modes();
    int index = 0;
    for (const IChargeThresholdHAL::Mode& mode : modes) {
        auto* radio = new QRadioButton(mode.name, box);
        radio->setChecked(mode.cctkValue == current);
        m_modeGroup->addButton(radio, index++);
        layout->addWidget(radio);
        if (mode.cctkValue == "custom") {
            auto* rangeRow = new QWidget(box);
            auto* rangeLayout = new QHBoxLayout(rangeRow);
            rangeLayout->setContentsMargins(24, 0, 0, 0);
            m_start = new QSlider(Qt::Horizontal, rangeRow);
            m_start->setRange(50, 95);
            m_start->setValue(55);
            m_stop = new QSlider(Qt::Horizontal, rangeRow);
            m_stop->setRange(55, 100);
            m_stop->setValue(80);
            m_rangeLabel = new QLabel(QStringLiteral("55% – 80%"), rangeRow);
            m_rangeLabel->setMinimumWidth(90);
            rangeLayout->addWidget(new QLabel(tr("Start"), rangeRow));
            rangeLayout->addWidget(m_start, 1);
            rangeLayout->addWidget(new QLabel(tr("Stop"), rangeRow));
            rangeLayout->addWidget(m_stop, 1);
            rangeLayout->addWidget(m_rangeLabel);
            layout->addWidget(rangeRow);
            connect(m_start, &QSlider::valueChanged, this, &BatteryPage::onCustomRangeChanged);
            connect(m_stop, &QSlider::valueChanged, this, &BatteryPage::onCustomRangeChanged);
            // The thresholds only make sense while Custom is the active mode.
            rangeRow->setVisible(radio->isChecked());
            connect(radio, &QRadioButton::toggled, rangeRow, &QWidget::setVisible);
        }
    }
    connect(m_modeGroup, &QButtonGroup::idClicked, this, &BatteryPage::onModeChosen);
    layout->addStretch(1);
    m_modesCard = box;
    m_modesLayout = layout;
    return box;
}

void BatteryPage::onSnapshot(const SystemSnapshot& s) {
    const BatteryInfo& b = s.battery;
    if (!b.present) {
        m_percent->setText(tr("No battery"));
        m_state->setText(b.acOnline ? tr("AC power") : QString::fromUtf8(" "));
        m_health->setText(QString::fromUtf8("-"));
        m_cycles->setText(QString::fromUtf8("-"));
        m_design->setText(QString::fromUtf8("-"));
        m_full->setText(QString::fromUtf8("-"));
        m_voltage->setText(QString::fromUtf8("-"));
        m_power->setText(QString::fromUtf8("-"));
        m_vendor->setText(QString::fromUtf8("-"));
        m_device->setText(QString::fromUtf8("-"));
        m_serial->setText(QString::fromUtf8("-"));
        m_chemistry->setText(QString::fromUtf8("-"));
        return;
    }
    m_percent->setText(b.percent >= 0 ? QString::number(b.percent) + QStringLiteral("%")
                                      : QString::fromUtf8("-"));
    m_state->setText(b.charging ? tr("Charging")
                                : (b.acOnline ? tr("Plugged in, not charging") : tr("On battery")));

    // Dell Power Manager health wording: >=80% Good, >=50% Fair, else Poor.
    if (b.healthPercent >= 0) {
        const QString grade = b.healthPercent >= 80 ? tr("Good")
                                                   : (b.healthPercent >= 50 ? tr("Fair") : tr("Poor"));
        m_health->setText(QStringLiteral("%1% (%2)").arg(b.healthPercent).arg(grade));
    }
    m_cycles->setText(b.cycleCount >= 0 ? QString::number(b.cycleCount) : QString::fromUtf8("-"));
    m_design->setText(b.designCapacityMWh > 0
                          ? QStringLiteral("%1 Wh").arg(b.designCapacityMWh / 1000.0, 0, 'f', 1)
                          : QString::fromUtf8("-"));
    m_full->setText(b.fullChargeCapacityMWh > 0
                        ? QStringLiteral("%1 Wh").arg(b.fullChargeCapacityMWh / 1000.0, 0, 'f', 1)
                        : QString::fromUtf8("-"));
    m_voltage->setText(b.voltageMV > 0 ? QStringLiteral("%1 V").arg(b.voltageMV / 1000.0, 0, 'f', 2)
                                       : QString::fromUtf8("-"));
    m_power->setText(b.rateMW != 0 ? QStringLiteral("%1%2 W")
                                         .arg(b.rateMW > 0 ? QStringLiteral("+") : QString())
                                         .arg(qAbs(b.rateMW) / 1000.0, 0, 'f', 1)
                                   : tr("Idle")); // full battery on AC: genuinely 0 W
    m_vendor->setText(b.vendor.isEmpty() ? QString::fromUtf8("-") : b.vendor);
    m_device->setText(b.deviceName.isEmpty() ? QString::fromUtf8("-") : b.deviceName);
    m_serial->setText(b.serial.isEmpty() ? QString::fromUtf8("-") : b.serial);
    m_chemistry->setText(b.chemistry.isEmpty() ? QString::fromUtf8("-") : b.chemistry);
}

void BatteryPage::onCustomRangeChanged() {
    if (m_stop->value() - m_start->value() < 5) {
        if (QObject::sender() == m_start)
            m_start->setValue(m_stop->value() - 5);
        else
            m_stop->setValue(m_start->value() + 5);
        return;
    }
    m_rangeLabel->setText(QString::number(m_start->value()) + QStringLiteral("% – ")
                          + QString::number(m_stop->value()) + QStringLiteral("%"));
}

void BatteryPage::onModeChosen() {
    const int id = m_modeGroup->checkedId();
    const QList<IChargeThresholdHAL::Mode> modes = m_hal.charge->modes();
    if (id < 0 || id >= modes.size())
        return;
    QString value = modes[id].cctkValue;
    if (value == "custom" && m_start && m_stop)
        value += QStringLiteral(":%1-%2").arg(m_start->value()).arg(m_stop->value());
    m_hal.charge->setMode(value);
}

} // namespace dtb::ui
