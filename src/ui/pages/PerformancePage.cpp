#include "ui/pages/PerformancePage.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QInputDialog>
#include <QProcess>
#include <QPointer>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include "core/PowerCfg.h"
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

    // Manual power limits are opt-in through named profiles, kept in their
    // own visible card: the list shows everything saved, the buttons act on
    // the selection, sliders edit, Save persists draft targets only.
    m_profilesData = m_config->loadManualProfiles();
    auto* profilesCard = new Card(QStringLiteral("Custom power profiles"), this);
    auto* pbody = profilesCard->bodyLayout();
    m_profileList = new QListWidget(profilesCard);
    m_profileList->setObjectName(QStringLiteral("profileList"));
    m_profileList->setMaximumHeight(132);
    m_profileList->setUniformItemSizes(true);
    pbody->addWidget(m_profileList);
    auto* buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    m_newProfile = new QPushButton(tr("New profile"), profilesCard);
    buttonRow->addWidget(m_newProfile);
    buttonRow->addStretch(1);
    m_renameProfile = new QPushButton(tr("Rename"), profilesCard);
    buttonRow->addWidget(m_renameProfile);
    m_deleteProfile = new QPushButton(tr("Delete"), profilesCard);
    buttonRow->addWidget(m_deleteProfile);
    pbody->addLayout(buttonRow);
    auto* hint = new QLabel(
        tr("Draft targets only — CPU/GPU power-limit writes are not supported. Select a profile to edit and save it."),
        profilesCard);
    pbody->addWidget(hint);
    wrapLabel(hint);
    layout->addWidget(profilesCard);

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
    // Selecting a row only loads saved draft targets.
    connect(m_profileList, &QListWidget::currentRowChanged, this, [this](int index) {
        if (index < 0 || index >= m_profilesData.size())
            return;
        m_pl1->setValue(m_profilesData[index].pl1W);
        m_pl2->setValue(m_profilesData[index].pl2W);
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
    // Refresh the draft caption while selection signals are blocked.
    updateActiveLabel();
}

void PerformancePage::updatePowerPlans(const QVector<PowerPlanEntry>& plans, const QString& activeGuid) {
    // Never replace the popup model while the user navigates it.
    if (m_planCombo->view()->isVisible() || plans.isEmpty())
        return;
    const QSignalBlocker block(m_planCombo);
    m_planCombo->clear();
    for (const auto& p : plans)
        m_planCombo->addItem(p.name, p.guid);
    int activeIndex = -1;
    for (int i = 0; i < m_planCombo->count(); ++i)
        if (m_planCombo->itemData(i).toString().compare(activeGuid, Qt::CaseInsensitive) == 0) {
            activeIndex = i;
            break;
        }
    m_planCombo->setCurrentIndex(activeIndex);
}

void PerformancePage::refreshPowerPlans() {
    if (m_planRefreshBusy || m_planCombo->view()->isVisible())
        return;
    m_planRefreshBusy = true;
    auto* list = new QProcess(this);
    auto* timeout = new QTimer(list);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, list, &QProcess::kill);
    timeout->start(10000);
    connect(list, &QProcess::errorOccurred, this, [this, list](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            m_planRefreshBusy = false;
            list->deleteLater();
        }
    });
    connect(list, &QProcess::finished, this, [this, list](int code, QProcess::ExitStatus status) {
        list->deleteLater();
        m_planRefreshBusy = false;
        if (code != 0 || status != QProcess::NormalExit)
            return; // retain the last successful read on errors
        const QString output = QString::fromLocal8Bit(list->readAllStandardOutput());
        QString activeGuid;
        for (const QString& line : output.split(QLatin1Char('\n'))) {
            if (!line.trimmed().endsWith(QLatin1Char('*')))
                continue;
            const auto active = parsePowerPlans(line);
            if (!active.isEmpty())
                activeGuid = active.first().guid;
        }
        updatePowerPlans(parsePowerPlans(output), activeGuid);
    });
    list->start(m_controller->powerCfgProgram(), {QStringLiteral("/list")});
}

QWidget* PerformancePage::buildPowerPlanCard() {
    auto* box = new Card(QStringLiteral("Operating system power plan"), this);
    auto* layout = box->bodyLayout();
    m_planCombo = new QComboBox(box);
    m_planCombo->setObjectName(QStringLiteral("powerPlanCombo"));
    layout->addWidget(m_planCombo);
    connect(m_planCombo, &QComboBox::activated, this, [this](int index) {
        const QString guid = m_planCombo->itemData(index).toString();
        if (guid.isEmpty() || m_controller->passive())
            return;
        const QPointer<PerformancePage> guard(this);
        m_controller->setActivePlan(guid, [guard](bool) { if (guard) guard->refreshPowerPlans(); });
    });
    m_planCombo->setEnabled(!m_controller->passive());
    connect(m_controller, &Controller::snapshotUpdated, this, [this] {
        m_planCombo->setEnabled(!m_controller->passive());
    });
    refreshPowerPlans();
    auto* planTimer = new QTimer(box);
    connect(planTimer, &QTimer::timeout, this, &PerformancePage::refreshPowerPlans);
    planTimer->start(5000);
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
        tr("Saved drafts only. These sliders do not change CPU or GPU power limits."),
        box);
    wrapLabel(caption); // a bare QLabel's min width is the full line: overflowed narrow windows
    layout->addWidget(caption);
    m_activeLabel = new QLabel(box);
    m_activeLabel->setObjectName(QStringLiteral("cardCaption"));

    layout->addWidget(m_activeLabel);
    updateActiveLabel();
    return box;
}

void PerformancePage::updateActiveLabel() {
    if (!m_activeLabel || !m_profileList)
        return; // list is created after the sliders card during construction
    const int i = m_profileList->currentRow();
    if (i < 0 || i >= m_profilesData.size()) {
        m_activeLabel->setText(tr("No draft profile selected"));
        return;
    }
    m_activeLabel->setText(tr("Draft: %1 — not applied to hardware").arg(m_profilesData[i].name));
}

} // namespace dtb::ui
