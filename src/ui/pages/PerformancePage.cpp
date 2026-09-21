#include "ui/pages/PerformancePage.h"

#include <QDateTime>
#include <QComboBox>
#include <QInputDialog>
#include <QProcess>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include "ui/Theme.h"
#include "ui/widgets/Card.h"
#include "ui/widgets/WheelGuard.h"

namespace dtb::ui {

// Conservative G3 3590 ranges; to be calibrated on real hardware.
namespace {
constexpr int kPl1MinW = 35;
constexpr int kPl1MaxW = 60;
constexpr int kPl2MinW = 35;
constexpr int kPl2MaxW = 80;
constexpr int kGpuPptMinW = 60;
constexpr int kGpuPptMaxW = 115;
// "GUID: <guid> (<name>)" lines of powercfg /list and /getactivescheme
const QRegularExpression kPlanRe("GUID: ([0-9a-fA-F-]+)\\s+\\(([^)]+)\\)");
} // namespace

PerformancePage::PerformancePage(HalSet hal, Controller* controller, ConfigStore* config, QWidget* parent)
    : QWidget(parent), m_hal(hal), m_controller(controller), m_config(config) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(16);
    auto* title = new QLabel(tr("Performance"), this);
    title->setObjectName(QStringLiteral("pageTitle"));
    layout->addWidget(title);
    layout->addWidget(buildPowerPlanCard());

    // Manual power limits are opt-in through a named-profile flow:
    // New profile -> edit sliders -> Save (persists + records the override).
    m_profilesData = m_config->loadManualProfiles();
    auto* profileRow = new QHBoxLayout;
    profileRow->setContentsMargins(0, 0, 0, 0);
    m_newProfile = new QPushButton(tr("New profile"), this);
    profileRow->addWidget(m_newProfile);
    m_profileList = new QListWidget(this);
    m_profileList->setMaximumHeight(132); // ~4 rows; grows the dialog feel
    m_profileList->setUniformItemSizes(true);
    profileRow->addWidget(m_profileList);
    m_renameProfile = new QPushButton(tr("Rename"), this);
    profileRow->addWidget(m_renameProfile);
    m_deleteProfile = new QPushButton(tr("Delete"), this);
    profileRow->addWidget(m_deleteProfile);
    profileRow->addStretch(1);
    layout->addLayout(profileRow);

    m_manualCard = buildSlidersCard();
    m_manualCard->setVisible(!m_profilesData.isEmpty());
    layout->addWidget(m_manualCard);
    auto* saveRow = new QHBoxLayout;
    saveRow->setContentsMargins(0, 0, 0, 0);
    m_saveProfile = new QPushButton(tr("Save profile"), this);
    m_saveProfile->setVisible(!m_profilesData.isEmpty());
    saveRow->addWidget(m_saveProfile);
    saveRow->addStretch(1);
    layout->addLayout(saveRow);
    layout->addStretch(1);

    connect(m_newProfile, &QPushButton::clicked, this, [this] {
        ConfigStore::ManualProfile p;
        p.name = tr("Profile %1").arg(m_profilesData.size() + 1);
        p.pl1W = m_pl1 ? m_pl1->value() : 45;
        p.pl2W = m_pl2 ? m_pl2->value() : 54;
        m_profilesData.append(p);
        m_config->saveManualProfiles(m_profilesData);
        refreshProfiles(p.name);
        m_manualCard->setVisible(true);
        m_saveProfile->setVisible(true);
        applyManual();
    });
    connect(m_renameProfile, &QPushButton::clicked, this, [this] {
        const int i = m_profileList->currentRow();
        if (i < 0 || i >= m_profilesData.size())
            return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Rename profile"), tr("Profile name"),
                                                   QLineEdit::Normal, m_profilesData[i].name, &ok);
        // The persisted format is "name|pl1|pl2" joined with ';': a separator
        // inside a name would corrupt the entry (silently dropped on load).
        QString safe = name;
        safe.remove(QLatin1Char('|')).remove(QLatin1Char(';'));
        if (ok && !safe.isEmpty()) {
            m_profilesData[i].name = safe;
            m_config->saveManualProfiles(m_profilesData);
            refreshProfiles(safe);
        }
    });
    connect(m_deleteProfile, &QPushButton::clicked, this, [this] {
        const int i = m_profileList->currentRow();
        if (i < 0 || i >= m_profilesData.size())
            return;
        m_profilesData.removeAt(i);
        m_config->saveManualProfiles(m_profilesData);
        refreshProfiles();
        m_manualCard->setVisible(!m_profilesData.isEmpty());
        m_saveProfile->setVisible(!m_profilesData.isEmpty());
    });
    // Selecting a row activates that profile right away.
    connect(m_profileList, &QListWidget::currentRowChanged, this, [this](int index) {
        if (index < 0 || index >= m_profilesData.size())
            return;
        m_pl1->setValue(m_profilesData[index].pl1W);
        m_pl2->setValue(m_profilesData[index].pl2W);
        applyManual();
        updateActiveLabel();
    });
    connect(m_saveProfile, &QPushButton::clicked, this, [this] {
        const int i = m_profileList->currentRow();
        if (i < 0 || i >= m_profilesData.size())
            return;
        m_profilesData[i].pl1W = m_pl1->value();
        m_profilesData[i].pl2W = m_pl2->value();
        m_config->saveManualProfiles(m_profilesData);
        refreshProfiles(m_profilesData[i].name);
        applyManual(); // records the override for the control loop
        updateActiveLabel();
    });

    refreshProfiles(); // persisted profiles must populate the combo at startup
    connect(controller, &Controller::configReloaded, this, [this] {
        // Reset-to-defaults wipes saved profiles; drop them from the UI too.
        m_profilesData = m_config->loadManualProfiles();
        refreshProfiles();
        m_manualCard->setVisible(!m_profilesData.isEmpty());
        m_saveProfile->setVisible(!m_profilesData.isEmpty());
    });
    WheelGuard::apply(this);
}

void PerformancePage::refreshProfiles(const QString& selectName) {
    QSignalBlocker block(m_profileList);
    m_profileList->clear();
    for (const ConfigStore::ManualProfile& p : m_profilesData)
        m_profileList->addItem(QStringLiteral("%1  -  %2 W sustained / %3 W boost").arg(p.name).arg(p.pl1W).arg(p.pl2W));
    int idx = -1;
    if (!selectName.isEmpty())
        for (int i = 0; i < m_profilesData.size(); ++i)
            if (m_profilesData[i].name == selectName)
                idx = i;
    if (idx < 0 && !m_profilesData.isEmpty())
        idx = 0;
    m_profileList->setCurrentRow(idx);
    if (idx >= 0) {
        m_pl1->setValue(m_profilesData[idx].pl1W);
        m_pl2->setValue(m_profilesData[idx].pl2W);
    }
}

QWidget* PerformancePage::buildPowerPlanCard() {
    auto* box = new Card(QStringLiteral("Operating system power plan"), this);
    auto* layout = box->bodyLayout();
    m_planCombo = new QComboBox(box);
    m_planCombo->setMinimumWidth(360);
    layout->addWidget(m_planCombo);
    layout->addStretch(1);

    struct Plan { QString guid; QString name; };
    QList<Plan> plans;
    QProcess list;
    list.start(QStringLiteral("powercfg"), {QStringLiteral("/list")});
    list.waitForFinished(3000);
    // powercfg prints in the console codepage, not UTF-8; decode locally so
    // localized plan names survive (GUIDs are ASCII either way).
    for (const QString& line : QString::fromLocal8Bit(list.readAllStandardOutput())
                                 .split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const auto m = kPlanRe.match(line.trimmed());
        if (m.hasMatch())
            plans.append({m.captured(1), m.captured(2)});
    }
    QString activeGuid;
    QProcess active;
    active.start(QStringLiteral("powercfg"), {QStringLiteral("/getactivescheme")});
    active.waitForFinished(3000);
    const auto am = kPlanRe.match(QString::fromLocal8Bit(active.readAllStandardOutput()));
    if (am.hasMatch())
        activeGuid = am.captured(1);

    for (const Plan& p : plans)
        m_planCombo->addItem(p.name, p.guid);
    const int idx = m_planCombo->findData(activeGuid);
    if (idx >= 0)
        m_planCombo->setCurrentIndex(idx);
    connect(m_planCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        const QString guid = m_planCombo->itemData(index).toString();
        if (guid.isEmpty())
            return;
        QProcess::startDetached(QStringLiteral("powercfg"), {QStringLiteral("/setactive"), guid});
    });
    // Track plan changes made outside the app (Windows settings, G-Mode...):
    // re-query every 5 s and re-select without firing the write-back.
    auto* planTimer = new QTimer(box);
    connect(planTimer, &QTimer::timeout, this, [this] {
        QProcess active;
        active.start(QStringLiteral("powercfg"), {QStringLiteral("/getactivescheme")});
        if (!active.waitForFinished(2000))
            return;
        const auto m = kPlanRe.match(QString::fromLocal8Bit(active.readAllStandardOutput()));
        if (!m.hasMatch())
            return;
        const int idx = m_planCombo->findData(m.captured(1));
        if (idx >= 0 && idx != m_planCombo->currentIndex()) {
            const QSignalBlocker block(m_planCombo);
            m_planCombo->setCurrentIndex(idx);
        }
    });
    planTimer->start(5000);
    box->setVisible(!plans.isEmpty());
    return box;
}

QWidget* PerformancePage::buildSlidersCard() {
    auto* box = new Card(QStringLiteral("Manual power limits"), this);
    auto* layout = box->bodyLayout();

    auto makeRow = [&](const QString& name, int min, int max, QSlider** slider, QLabel** value) {
        auto* row = new QWidget(box);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        auto* label = new QLabel(name, row);
        label->setMinimumWidth(120);
        *slider = new QSlider(Qt::Horizontal, row);
        (*slider)->setRange(min, max);
        *value = new QLabel(QString::number(min) + QStringLiteral(" W"), row);
        (*value)->setMinimumWidth(56);
        rowLayout->addWidget(label);
        rowLayout->addWidget(*slider, 1);
        rowLayout->addWidget(*value);
        layout->addWidget(row);
        connect(*slider, &QSlider::valueChanged, this, [this, slider, value](int v) {
            (*value)->setText(QString::number(v) + QStringLiteral(" W"));
        });
        connect(*slider, &QSlider::sliderReleased, this, [this] { applyManual(); });
    };

    makeRow(tr("CPU PL1 (sustained)"), kPl1MinW, kPl1MaxW, &m_pl1, &m_pl1Value);
    m_pl1->setValue(45);
    makeRow(tr("CPU PL2 (boost)"), kPl2MinW, kPl2MaxW, &m_pl2, &m_pl2Value);
    m_pl2->setValue(54);

    makeRow(tr("GPU power limit"), kGpuPptMinW, kGpuPptMaxW, &m_gpuPpt, &m_gpuPptValue);
    m_gpuPpt->setValue(kGpuPptMaxW);
    m_gpuPpt->setEnabled(false); // v1: GPU power writes are not wired to NVAPI yet
    m_gpuPptValue->setText(tr("n/a"));

    auto* caption = new QLabel(
        tr("These sliders record targets; hardware write-through lands in a future update and they "
           "do not change the CPU's power limits yet. Targets expire after %1 minutes.")
            .arg(QString::number(m_controller->manual()->expiresSec / 60)),
        box);
    wrapLabel(caption); // a bare QLabel's min width is the full line: overflowed narrow windows
    layout->addWidget(caption);
    m_activeLabel = new QLabel(box);
    m_activeLabel->setObjectName(QStringLiteral("cardCaption"));
    m_activeLabel->setStyleSheet(QStringLiteral("color:#4ade80;"));
    layout->addWidget(m_activeLabel);
    updateActiveLabel();
    return box;
}

void PerformancePage::updateActiveLabel() {
    if (!m_activeLabel || !m_profileList)
        return; // list is created after the sliders card during construction
    const int i = m_profileList->currentRow();
    if (i < 0 || i >= m_profilesData.size()) {
        m_activeLabel->setText(tr("No active profile - targets come from the automatic loop"));
        return;
    }
    m_activeLabel->setText(tr("Active: %1 (targets expire after %2 minutes)")
                               .arg(m_profilesData[i].name)
                               .arg(QString::number(m_controller->manual()->expiresSec / 60)));
}

void PerformancePage::applyManual() {
    ControlTargets t;
    t.cpuPl1W = m_pl1->value();
    t.cpuPl2W = m_pl2->value();
    if (m_gpuPpt->isEnabled())
        t.gpuPptW = m_gpuPpt->value();
    m_controller->manual()->set(t, QDateTime::currentMSecsSinceEpoch());
}

} // namespace dtb::ui
