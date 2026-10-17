#include "core/Controller.h"

#include <QDateTime>
#include <QProcess>
#include <QRegularExpression>

#include <functional>

#include "core/Logger.h"
#include "core/PowerCfg.h"

namespace dtb {

namespace {
// A bare "12345678-1234-..." GUID (duplicatescheme prints "Power Scheme GUID: <guid> (name)").
const QRegularExpression kGuidOnlyRe("([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})");
// Well-known Windows plan template GUIDs; instance GUIDs are machine-specific.
// powercfg's GUID casing is not guaranteed - compare case-insensitively.
const QLatin1String kHighPerfPlanGuid("8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c");
const QLatin1String kBalancedPlanGuid("381b4222-f694-41f0-9685-ff5bb260df2e");
// Stock display names for the paths that never see the localized /list entry.
const QLatin1String kHighPerfPlanName("High performance");
const QLatin1String kBalancedPlanName("Balanced");
// The firmware's profile readback can lag our own successful write by a tick
// or two; ignore adoptions inside this window so a slow read does not revert
// the mode we just applied.
constexpr qint64 kModeAdoptGraceMs = 5000;

// Bounded, single-completion subprocess. FailedToStart does not emit finished;
// a hung powercfg must not pin the chain gate for the rest of the session.
void runPowerCfg(QObject* owner, const QString& program, const QStringList& args,
                 std::function<void(bool, const QByteArray&)> done) {
    auto* proc = new QProcess(owner);
    auto* timeout = new QTimer(proc);
    timeout->setSingleShot(true);
    QObject::connect(timeout, &QTimer::timeout, proc, [proc] { proc->kill(); });
    QObject::connect(proc, &QProcess::errorOccurred, owner, [proc, done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            proc->deleteLater();
            done(false, {});
        }
    });
    QObject::connect(proc, &QProcess::finished, owner, [proc, timeout, done] {
        timeout->stop();
        const bool ok = proc->exitStatus() == QProcess::NormalExit && proc->exitCode() == 0;
        const auto output = proc->readAllStandardOutput();
        proc->deleteLater();
        done(ok, output);
    });
    proc->start(program, args);
    timeout->start(10000);
}

} // namespace

Controller::Controller(HalSet hal, ConfigStore* cfg, QObject* parent)
    : QObject(parent), m_hal(hal), m_cfg(cfg), m_scene(m_cfg ? m_cfg->loadSceneParams() : SceneParams{}),
      m_scenePolicy(&m_scene), m_dynamic(m_cfg ? m_cfg->loadDynamicProfile() : DynamicProfile{}) {
    if (m_cfg) {
        m_baseline.setMode(m_cfg->loadMode());
        m_manual.expiresSec = m_cfg->loadManualExpireMin() * 60;
        m_planSyncEnabled = m_cfg->loadPowerPlanSync();
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

    connect(&m_timer, &QTimer::timeout, this, &Controller::tickOnce);
}

Controller::~Controller() {
    stop();
    // Disconnect before member teardown: a killed subprocess can emit finished
    // synchronously and must not start a replacement chain during destruction.
    for (auto* proc : findChildren<QProcess*>(QString(), Qt::FindDirectChildrenOnly)) {
        disconnect(proc, nullptr, this, nullptr);
        if (proc->state() != QProcess::NotRunning) {
            proc->kill();
            proc->waitForFinished(1000);
        }
    }
}

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
    m_dynamic.setProfile(m_cfg->loadDynamicProfile());
    ++m_planGeneration;
    m_planQueuedRequest = false;
    m_planSyncEnabled = m_cfg->loadPowerPlanSync();
    if (m_hal.thermal)
        m_hal.thermal->cancelPendingModeWrite();
    m_manual.clear();
    m_manualProfileName.clear();
    m_failedMode.reset();
    m_adoptStreak = 0;
    m_lastFanPercent.clear();
    m_lastFanWriteMs = 0;
    m_lastModeWritten.reset(); // force re-apply of the reloaded mode
    m_pendingModeWrite = true;
    m_pendingWriteAttempts = 0;
    emit configReloaded();
}

void Controller::setPassive(bool on) {
    if (m_passive == on)
        return;
    m_passive = on;
    ++m_planGeneration;
    m_planQueuedRequest = false;
    m_adoptStreak = 0;
    m_lastModeWritten.reset();
    m_lastFanPercent.clear();
    m_lastFanWriteMs = 0;
    m_failedMode.reset();
    if (on) {
        if (m_hal.thermal)
            m_hal.thermal->cancelPendingModeWrite();
        m_pendingModeWrite = false; // monitor mode sends nothing; nothing can be in flight
        m_planActiveName.clear(); // the policy chip falls back to "AWCC (monitor)"
    }
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
        if (m_pendingModeWrite || m_hal.thermal->hasPendingModeWrite())
            return;
        if (m_lastModeWriteMs != 0 && m_snapshot.tsMs - m_lastModeWriteMs < kModeAdoptGraceMs)
            return; // readback may lag our own recent write
    }
    const auto current = m_hal.thermal->readCurrentProfile();
    if (!current || *current == m_baseline.mode()
        || (!m_passive && current == m_lastModeWritten)) {
        m_adoptStreak = 0;
        return;
    }
    // While actively owning Custom fan control, transient firmware table
    // readback must not discard the user's curve configuration. Monitor mode
    // does not own it and must follow external changes normally.
    if (!m_passive && m_baseline.mode() == ThermalMode::Custom) {
        m_adoptStreak = 0;
        return;
    }
    // The firmware readback is flaky right after writes (observed: a
    // different mode returned seconds after a successful write, flipping the
    // app off the user's choice). Adopt only a readback that is STABLE for 3
    // consecutive ticks - real external changes (AWCC) are stable, glitches
    // are not.
    if (m_adoptStreak == 0 || m_adoptCandidate != *current) {
        m_adoptCandidate = *current;
        m_adoptStreak = 1;
        return;
    }
    if (++m_adoptStreak < 3)
        return;
    m_adoptStreak = 0;
    m_baseline.setMode(*current);
    if (m_cfg)
        m_cfg->saveMode(*current);
    m_lastFanPercent.clear();
    m_lastModeWritten = *current; // do not write it straight back
    m_failedMode.reset();
    dtbLog(info) << "thermal profile changed externally -> adopted" << int(*current);
    emit thermalModeChanged(*current, true);
}

void Controller::tickOnce() {
    m_snapshot = readSnapshot();
    adoptExternalProfile();
    // Keep the Dashboard plan chip live: re-read the ACTIVE plan name every
    // 5 s (the chip used to update only on mode writes - stale after manual
    // plan changes in Windows or the Performance page).
    if (++m_planNameTick % 5 == 0)
        refreshActivePlanName();
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

// Every asynchronous stage is tied to a generation. A newer mode, disabling
// sync, or entering monitor mode invalidates all not-yet-issued mutations.
void Controller::setPowerPlanSyncEnabled(bool on) {
    if (m_planSyncEnabled == on)
        return;
    m_planSyncEnabled = on;
    ++m_planGeneration;
    m_planQueuedRequest = false;
    if (on && !m_passive)
        handlePlanSync(m_lastModeWritten.value_or(m_baseline.mode()));
}

void Controller::handlePlanSync(ThermalMode mode) {
    ++m_planGeneration;
    m_planWanted = mode;
    if (!m_planSyncEnabled || m_passive) {
        m_planQueuedRequest = false;
        return;
    }
    m_planQueuedRequest = true;
    if (!m_planBusy)
        beginPlanChain();
}

void Controller::beginPlanChain() {
    if (!m_planSyncEnabled || m_passive) {
        m_planQueuedRequest = false;
        return;
    }
    m_planQueuedRequest = false;
    m_planBusy = true;
    const auto generation = m_planGeneration;
    const bool reportOnly = m_planWanted == ThermalMode::Custom;
    const bool wantsHigh = m_planWanted == ThermalMode::GMode || m_planWanted == ThermalMode::Performance;
    runPowerCfg(this, m_powerCfgProgram, {QStringLiteral("/list")}, [this, generation, reportOnly, wantsHigh](bool ok, const QByteArray& output) {
        if (generation != m_planGeneration || !ok) {
            finishPlanChain();
            return;
        }
        const auto plans = parsePowerPlans(QString::fromLocal8Bit(output));
        if (reportOnly) {
            queryActivePlan([this, generation, plans](const QString& active) {
                if (generation == m_planGeneration) {
                    for (const auto& plan : plans)
                        if (plan.guid.compare(active, Qt::CaseInsensitive) == 0)
                            m_planActiveName = plan.name;
                }
                finishPlanChain();
            });
            return;
        }
        QString target;
        QString targetName;
        for (const auto& plan : plans) {
            const bool high = plan.guid.compare(kHighPerfPlanGuid, Qt::CaseInsensitive) == 0
                              || plan.name.contains(QLatin1String("high"), Qt::CaseInsensitive)
                              || plan.name.contains(QString::fromUtf8("高性能"));
            const bool balanced = plan.guid.compare(kBalancedPlanGuid, Qt::CaseInsensitive) == 0
                                  || plan.name.contains(QLatin1String("balanc"), Qt::CaseInsensitive)
                                  || plan.name.contains(QString::fromUtf8("平衡"));
            if (wantsHigh ? high : balanced) {
                target = plan.guid;
                targetName = plan.name;
            }
        }
        auto activate = [this, generation](const QString& guid, const QString& name) {
            if (generation != m_planGeneration) {
                finishPlanChain();
                return;
            }
            applyActivePlan(guid, [this, generation, name](bool success) {
                if (success && generation == m_planGeneration)
                    m_planActiveName = name;
                finishPlanChain();
            });
        };
        if (target.isEmpty()) {
            if (!wantsHigh) {
                finishPlanChain();
                return;
            }
            if (!m_mintedHighGuid.isEmpty()) {
                activate(m_mintedHighGuid, kHighPerfPlanName);
                return;
            }
            // The generation was checked above; no disabled/passive request
            // can create a persistent OS scheme through this continuation.
            runPowerCfg(this, m_powerCfgProgram, {QStringLiteral("-duplicatescheme"), kHighPerfPlanGuid},
                        [this, generation, activate](bool success, const QByteArray& output) {
                const auto match = kGuidOnlyRe.match(QString::fromLocal8Bit(output));
                if (success && match.hasMatch())
                    m_mintedHighGuid = match.captured(1);
                if (!success || !match.hasMatch() || generation != m_planGeneration) {
                    finishPlanChain();
                    return;
                }
                activate(m_mintedHighGuid, kHighPerfPlanName);
            });
            return;
        }
        queryActivePlan([this, generation, target, targetName, activate](const QString& active) {
            if (generation != m_planGeneration) {
                finishPlanChain();
                return;
            }
            if (active.compare(target, Qt::CaseInsensitive) == 0) {
                m_planActiveName = targetName;
                finishPlanChain();
            } else {
                activate(target, targetName);
            }
        });
    });
}

void Controller::finishPlanChain() {
    m_planBusy = false;
    if (m_planQueuedRequest && m_planSyncEnabled && !m_passive)
        beginPlanChain();
}

void Controller::refreshActivePlanName() {
    if (m_planBusy || m_passive)
        return;
    m_planBusy = true;
    const auto generation = m_planGeneration;
    runPowerCfg(this, m_powerCfgProgram, {QStringLiteral("/list")}, [this, generation](bool ok, const QByteArray& output) {
        if (!ok || generation != m_planGeneration) {
            finishPlanChain();
            return;
        }
        const auto plans = parsePowerPlans(QString::fromLocal8Bit(output));
        queryActivePlan([this, generation, plans](const QString& active) {
            if (generation == m_planGeneration) {
                for (const auto& plan : plans)
                    if (plan.guid.compare(active, Qt::CaseInsensitive) == 0)
                        m_planActiveName = plan.name;
            }
            finishPlanChain();
        });
    });
}

void Controller::queryActivePlan(const std::function<void(const QString&)>& done) {
    runPowerCfg(this, m_powerCfgProgram, {QStringLiteral("/getactivescheme")}, [done](bool ok, const QByteArray& output) {
        const auto plans = parsePowerPlans(QString::fromLocal8Bit(output));
        done(ok && !plans.isEmpty() ? plans.first().guid : QString());
    });
}

void Controller::setActivePlan(const QString& guid, const std::function<void(bool)>& done) {
    // An explicit user selection supersedes any outstanding automatic mapping.
    ++m_planGeneration;
    m_planQueuedRequest = false;
    applyActivePlan(guid, done);
}

void Controller::applyActivePlan(const QString& guid, const std::function<void(bool)>& done) {
    if (m_passive || guid.isEmpty()) {
        done(false);
        return;
    }
    runPowerCfg(this, m_powerCfgProgram, {QStringLiteral("/setactive"), guid}, [done](bool ok, const QByteArray&) { done(ok); });
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
    // Continue firmware verification only after resolving this tick's scene /
    // manual target. A superseded Performance request must not write another
    // candidate before a new mode takes ownership.
    if (targets.mode == ThermalMode::Performance && targets.mode == m_lastModeWritten)
        m_hal.thermal->pollPendingModeWrite();
    // Custom is the firmware's manual fan mode: enter it successfully before
    // any fan write, otherwise BIOS/G-Mode can override every curve target.
    if (targets.mode && targets.mode != m_lastModeWritten) {
        m_hal.thermal->cancelPendingModeWrite();
        if (m_hal.thermal->setMode(*targets.mode)) {
            m_lastFanPercent.clear();
            m_lastFanWriteMs = 0;
            m_lastModeWritten = targets.mode;
            m_pendingModeWrite = false;
            m_pendingWriteAttempts = 0;
            m_lastModeWriteMs = m_snapshot.tsMs;
            m_failedMode.reset();
            handlePlanSync(*targets.mode);
            emit thermalModeChanged(*targets.mode, false);
        } else {
            m_failedMode = targets.mode;
            if (++m_pendingWriteAttempts >= 5)
                m_pendingModeWrite = false;
            return; // do not apply dependent fan/power targets in the wrong mode
        }
    }
    // BIOS modes own fan curves, including scene overrides of a Custom baseline.
    if (targets.mode && *targets.mode != ThermalMode::Custom)
        targets.fanPercent.clear();
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
            if (!m_lastFanPercent.contains(it.key()) || qAbs(it.value() - last) >= kFanDeadZonePercent) {
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
