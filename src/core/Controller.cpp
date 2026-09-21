#include "core/Controller.h"

#include <QDateTime>
#include <QProcess>

#include <functional>

#include "core/Logger.h"
#include "core/PowerCfg.h"

namespace dtb {

namespace {
// The firmware's profile readback can lag our own successful write by a tick
// or two; ignore adoptions inside this window so a slow read does not revert
// the mode we just applied.
constexpr qint64 kModeAdoptGraceMs = 5000;
} // namespace

Controller::Controller(HalSet hal, ConfigStore* cfg, QObject* parent)
    : QObject(parent), m_hal(hal), m_cfg(cfg), m_scene(m_cfg ? m_cfg->loadSceneParams() : SceneParams{}),
      m_scenePolicy(&m_scene), m_dynamic(m_cfg ? m_cfg->loadDynamicProfile() : DynamicProfile{}),
      m_failsafe(m_cfg ? m_cfg->loadFailsafeParams() : FailsafeParams{}) {
    if (m_cfg) {
        m_baseline.setMode(m_cfg->loadMode());
        m_manual.expiresSec = m_cfg->loadManualExpireMin() * 60;
    }
    if (m_hal.thermal) {
        for (const HalFanInfo& fi : m_hal.thermal->enumerateFans()) {
            FanCurve curve;
            if (m_cfg)
                curve.setPoints(m_cfg->loadCurve(fi.id));
            else
                curve = FanCurve::defaultCurve();
            m_baseline.setFanCurve(fi.id, fi.sensorId, curve);
            if (m_cfg)
                m_fanBoost[fi.id] = qBound(0, m_cfg->loadFanBoost(fi.id), 100);
        }
    }
    m_chain.addPolicy(&m_baseline);
    // The dynamic (C) policy stays out of the chain until CPU power-limit
    // writes are wired to hardware — otherwise it would only move targets no
    // one applies and the active-policy readout would be misleading.
    m_chain.addPolicy(&m_scenePolicy);
    m_chain.addPolicy(&m_failsafe);

    connect(&m_timer, &QTimer::timeout, this, &Controller::tickOnce);
}

Controller::~Controller() { stop(); }

void Controller::start(int periodMs) {
    m_timer.start(periodMs);
}

void Controller::stop() { m_timer.stop(); }

void Controller::reloadFromConfig() {
    if (!m_cfg)
        return;
    m_baseline.setMode(m_cfg->loadMode());
    m_manual.expiresSec = m_cfg->loadManualExpireMin() * 60;
    m_fanBoost.clear();
    if (m_hal.thermal) {
        for (const HalFanInfo& fi : m_hal.thermal->enumerateFans()) {
            FanCurve curve;
            curve.setPoints(m_cfg->loadCurve(fi.id));
            m_baseline.setFanCurve(fi.id, fi.sensorId, curve);
            m_fanBoost[fi.id] = qBound(0, m_cfg->loadFanBoost(fi.id), 100);
            m_fanForceRewrite.insert(fi.id); // pushed to hardware on next apply
        }
    }
    m_scene.setParams(m_cfg->loadSceneParams());
    m_failsafe.setParams(m_cfg->loadFailsafeParams());
    m_dynamic.setProfile(m_cfg->loadDynamicProfile());
    m_planSyncEnabled = m_cfg->loadPowerPlanSync();
    m_lastModeWritten.reset(); // force re-apply of the reloaded mode
    m_pendingModeWrite = true;
    m_pendingWriteAttempts = 0;
    emit configReloaded();
}

void Controller::setPassive(bool on) {
    m_passive = on;
    if (on)
        m_pendingModeWrite = false; // monitor mode sends nothing; nothing can be in flight
}

SystemSnapshot Controller::readSnapshot() {
    SystemSnapshot s;
    s.tsMs = m_clock();
    if (m_hal.thermal) {
        for (const HalFanInfo& fi : m_hal.thermal->enumerateFans()) {
            s.sensors.push_back({fi.sensorId, m_hal.thermal->sensorTemp(fi.sensorId)});
            s.fans.push_back({fi.id, m_hal.thermal->fanRpm(fi.id)});
        }
    }
    if (m_hal.gpu)
        s.gpu = m_hal.gpu->read();
    if (m_hal.battery)
        s.battery = m_hal.battery->read();
    if (m_hal.load) {
        s.cpuLoadPercent = m_hal.load->cpuLoadPercent();
        s.foregroundProcess = m_hal.load->foregroundProcess();
    }
    return s;
}

void Controller::adoptExternalProfile() {
    if (!m_hal.thermal)
        return;
    // A user-selected write is still queued or failing: never adopt over it,
    // otherwise the readback (still showing the previous profile) would revert
    // the user's selection before apply() ever sends it. Monitor mode sends
    // nothing of ours, so there is nothing to protect there - adoption must
    // keep following AWCC even after the user clicked a tray/radio mode.
    if (!m_passive) {
        if (m_pendingModeWrite)
            return;
        if (m_lastModeWriteMs != 0 && m_snapshot.tsMs - m_lastModeWriteMs < kModeAdoptGraceMs)
            return; // readback may lag our own recent write
    }
    const auto current = m_hal.thermal->readCurrentProfile();
    if (!current || *current == m_baseline.mode())
        return;
    m_baseline.setMode(*current);
    if (m_cfg)
        m_cfg->saveMode(*current);
    m_lastModeWritten = *current; // do not write it straight back
    m_failedMode.reset();
    dtbLog(info) << "thermal profile changed externally -> adopted";
    emit thermalModeChanged(*current, true);
}

void Controller::tickOnce() {
    m_snapshot = readSnapshot();
    adoptExternalProfile();
    // Charging mode polling every 3rd tick: WMI read, cheap enough at 1/3 Hz,
    // and drives the tray bubble plus the Dashboard/Battery displays.
    if (m_hal.charge && ++m_chargePollTick % 3 == 0) {
        const QString mode = m_hal.charge->currentMode();
        if (!mode.isEmpty() && mode != m_chargingMode) {
            const bool firstRead = m_chargingMode.isEmpty();
            m_chargingMode = mode;
            // The first successful read only seeds the state: the firmware did
            // not switch anything, so no "Switched to" bubble on app start.
            // UI reads the initial value through chargingMode().
            if (!firstRead)
                emit chargingModeChanged(mode);
        }
    }
    if (m_passive) { // monitor only: another app owns the hardware
        emit snapshotUpdated(m_snapshot);
        return;
    }
    if (m_scene.params().enabled)
        m_scene.onTick(m_snapshot);
    m_failsafe.onTick(m_snapshot);

    ControlTargets targets;
    QString active;
    m_chain.evaluate(m_snapshot, targets, active);

    const qint64 now = m_snapshot.tsMs;
    if (m_manual.active(now)) {
        targets.mergeFrom(m_manual.targets());
        active = QStringLiteral("manual");
    }

    apply(targets);

    emit snapshotUpdated(m_snapshot);
    if (active != m_activeName) {
        m_activeName = active;
        emit activePolicyChanged(m_activeName);
    }
}

// Thermal profile -> Windows power plan mapping (the published usage table):
//   G-Mode / Ultra Performance -> High Performance
//   Optimized / Quiet / Cool / Custom -> Balanced
// Plans are resolved from powercfg /list by template GUID or localized name
// (instance GUIDs are machine-specific), all calls asynchronous. A request
// arriving while a chain is in flight is REMEMBERED and re-applied when the
// chain completes - previously it was dropped, so a quick G-Mode ->
// Balanced -> G-Mode sequence left the plan stuck on Balanced.
void Controller::handlePlanSync(ThermalMode mode) {
    if (!m_planSyncEnabled)
        return;
    m_planWanted = mode;
    if (!m_planBusy)
        beginPlanChain();
}

void Controller::beginPlanChain() {
    m_planBusy = true;
    const bool wantsHigh = m_planWanted == ThermalMode::GMode || m_planWanted == ThermalMode::Performance;
    m_planAppliedHigh = wantsHigh;
    auto* list = new QProcess(this);
    // A failed start never emits finished(); release the gate or plan sync
    // stays dead for the rest of the session.
    connect(list, &QProcess::errorOccurred, this, [this, list](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            list->deleteLater();
            finishPlanChain();
        }
    });
    connect(list, &QProcess::finished, this, [this, list, wantsHigh] {
        list->deleteLater();
        QString target;
        for (const PowerPlanEntry& plan : parsePowerPlans(QString::fromLocal8Bit(list->readAllStandardOutput()))) {
            // powercfg's GUID casing is not guaranteed: match the well-known
            // template GUIDs case-insensitively or the fallback silently dies.
            const bool high = plan.guid.compare(QLatin1String("8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c"),
                                                Qt::CaseInsensitive) == 0
                              || plan.name.contains(QLatin1String("high"), Qt::CaseInsensitive)
                              || plan.name.contains(QString::fromUtf8("高性能"));
            const bool balanced = plan.guid.compare(QLatin1String("381b4222-f694-41f0-9685-ff5bb260df2e"),
                                                    Qt::CaseInsensitive) == 0
                                  || plan.name.contains(QLatin1String("balanc"), Qt::CaseInsensitive)
                                  || plan.name.contains(QString::fromUtf8("平衡"));
            if (wantsHigh ? high : balanced)
                target = plan.guid;
        }
        if (target.isEmpty()) {
            dtbLog(info) << "plan sync: no matching plan (wantsHigh" << wantsHigh << ")";
            finishPlanChain();
            return;
        }
        queryActivePlan([this, target](const QString& active) {
            if (active == target) {
                finishPlanChain();
                return;
            }
            setActivePlan(target, [this] { finishPlanChain(); });
        });
    });
    list->start(QStringLiteral("powercfg"), {QStringLiteral("/list")});
}

void Controller::finishPlanChain() {
    m_planBusy = false;
    // Honor a newer request that arrived while this chain was in flight.
    if (m_planSyncEnabled) {
        const bool wantsHigh = m_planWanted == ThermalMode::GMode || m_planWanted == ThermalMode::Performance;
        if (wantsHigh != m_planAppliedHigh)
            beginPlanChain();
    }
}

void Controller::queryActivePlan(const std::function<void(const QString&)>& done) {
    auto* proc = new QProcess(this);
    connect(proc, &QProcess::errorOccurred, this, [this, proc, done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            proc->deleteLater();
            done(QString()); // active plan unknown; caller proceeds without it
        }
    });
    connect(proc, &QProcess::finished, this, [this, proc, done] {
        proc->deleteLater();
        const auto plans = parsePowerPlans(QString::fromLocal8Bit(proc->readAllStandardOutput()));
        done(plans.isEmpty() ? QString() : plans.first().guid);
    });
    proc->start(QStringLiteral("powercfg"), {QStringLiteral("/getactivescheme")});
}

void Controller::setActivePlan(const QString& guid, const std::function<void()>& done) {
    auto* proc = new QProcess(this);
    connect(proc, &QProcess::errorOccurred, this, [this, proc, done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            proc->deleteLater();
            done(); // nothing was switched; still release the busy gate
        }
    });
    connect(proc, &QProcess::finished, this, [this, proc, done] {
        proc->deleteLater();
        done();
    });
    proc->start(QStringLiteral("powercfg"), {QStringLiteral("/setactive"), guid});
}

void Controller::apply(const ControlTargets& t) {
    if (!m_hal.thermal) {
        // Nothing to write through; targets still recorded in the snapshot path.
        return;
    }
    // Per-fan minimum speed (Easy boost): raise curve targets up to it.
    ControlTargets targets = t;
    for (auto it = targets.fanPercent.begin(); it != targets.fanPercent.end(); ++it) {
        const int boost = m_fanBoost.value(it.key(), 0);
        if (boost > it.value())
            it.value() = boost;
    }
    // Mode changes are rare and safe: always written immediately.
    if (targets.mode && targets.mode != m_lastModeWritten) {
        if (m_hal.thermal->setMode(*targets.mode)) {
            m_lastModeWritten = targets.mode;
            m_pendingModeWrite = false; // the queued user write has landed
            m_pendingWriteAttempts = 0;
            m_lastModeWriteMs = m_snapshot.tsMs;
            m_failedMode.reset();
            handlePlanSync(*targets.mode); // AWCC-aligned: G-Mode -> High Performance plan
            emit thermalModeChanged(*targets.mode, false); // UI + tray re-sync
        } else {
            m_failedMode = targets.mode; // retried next tick; UI shows the banner
            // Bound the retry so a permanently rejected mode (unsupported
            // G-Mode, WMI gone) cannot block external adoption forever: after
            // ~5 failed ticks give up and let the readback win (honest revert,
            // the banner already tells the user the write failed).
            if (++m_pendingWriteAttempts >= 5)
                m_pendingModeWrite = false;
        }
    }
    // Fan percentages: dead zone plus a minimum interval between writes,
    // because rapid WMI fan writes can stutter the machine.
    if (!targets.fanPercent.isEmpty()) {
        bool dirty = false;
        for (auto it = targets.fanPercent.constBegin(); it != targets.fanPercent.constEnd(); ++it) {
            // A queued force rewrite (boost change) bypasses the dead zone:
            // without it, releasing boost to 0 with a 0% curve target produces
            // |0 - (-1)| = 1 < 2 and the release never reaches the hardware.
            if (m_fanForceRewrite.contains(it.key())) {
                dirty = true;
                break;
            }
            const int last = m_lastFanPercent.value(it.key(), -1);
            if (qAbs(it.value() - last) >= kFanDeadZonePercent) {
                dirty = true;
                break;
            }
        }
        if (dirty && (m_lastFanWriteMs == 0 || m_snapshot.tsMs - m_lastFanWriteMs >= kMinWriteIntervalMs)) {
            for (auto it = targets.fanPercent.constBegin(); it != targets.fanPercent.constEnd(); ++it) {
                if (m_hal.thermal->setFanPercent(it.key(), it.value())) {
                    m_lastFanPercent.insert(it.key(), it.value());
                    m_fanForceRewrite.remove(it.key());
                }
            }
            m_lastFanWriteMs = m_snapshot.tsMs;
        }
    }
    // GPU power limit: same minimum-interval throttle.
    if (targets.gpuPptW && m_hal.gpu && m_hal.gpu->available()) {
        if (m_lastGpuWriteMs == 0 || m_snapshot.tsMs - m_lastGpuWriteMs >= kMinWriteIntervalMs) {
            if (m_hal.gpu->setPowerLimitW(*targets.gpuPptW))
                m_lastGpuWriteMs = m_snapshot.tsMs;
        }
    }
    // CPU PL1/PL2 targets are intentionally not applied in v1: the AWCC
    // analysis (docs/awcc-analysis/00-inventory.md) shows Dell routes them
    // through the Intel XTU service, which is a separate integration task.
}

} // namespace dtb
