#pragma once

#include <QDateTime>
#include <QObject>
#include <QSet>

#include <functional>
#include <QTimer>

#include <functional>

#include "core/BaselinePolicy.h"
#include "core/ConfigStore.h"
#include "core/DynamicPolicy.h"
#include "core/FailsafePolicy.h"
#include "core/ManualOverride.h"
#include "core/PolicyChain.h"
#include "core/SceneDetector.h"
#include "core/ScenePolicy.h"
#include "hal/HalInterfaces.h"

namespace dtb {

// The 1 Hz closed loop: read HAL -> snapshot -> scene/failsafe timers ->
// policy chain -> manual override on top -> apply with write throttling.
// tickOnce() is the synchronous single step used by tests; start() drives the
// same path from a timer.
class Controller : public QObject {
    Q_OBJECT
public:
    Controller(HalSet hal, ConfigStore* cfg, QObject* parent = nullptr);
    ~Controller() override;

    // Injectable clock so tests can exercise the write throttling deterministically.
    void setClock(std::function<qint64()> fn) { m_clock = std::move(fn); }

    void start(int periodMs = 1000);
    // Coexistence with official apps: while passive the loop keeps reading and
    // feeding the UI but performs NO writes — AWCC (or another controller)
    // keeps ownership of the hardware.
    void setPassive(bool on);
    bool passive() const { return m_passive; }
    void stop();
    void tickOnce();

    SystemSnapshot lastSnapshot() const { return m_snapshot; }
    QString activePolicy() const { return m_activeName; }
    // Last known charging mode ("" until the first successful poll); the
    // initial value is seeded silently - chargingModeChanged only reports
    // genuine changes afterwards.
    QString chargingMode() const { return m_chargingMode; }
    ManualOverride* manual() { return &m_manual; }
    // Easy fan boost: per-fan minimum percent. The chain raises curve targets
    // up to this floor when applying; 0 releases the fan back to the curve.
    void setFanBoost(FanId fan, int percent) {
        m_fanBoost[fan] = qBound(0, percent, 100);
        m_fanForceRewrite.insert(fan); // bypass the dead zone once: a drop to 0
        // (release to curve) must reach the hardware even at the same value
    }
    int fanBoost(FanId fan) const { return m_fanBoost.value(fan, 0); }
    // Name of the manual power profile currently applied (empty = none).
    void setManualProfileName(const QString& name) { m_manualProfileName = name; }
    QString manualProfileName() const { return m_manualProfileName; }
    void handlePlanSync(ThermalMode mode);
    void beginPlanChain();
    void finishPlanChain();
    void queryActivePlan(const std::function<void(const QString&)>& done);
    void setActivePlan(const QString& guid, const std::function<void()>& done);
    // Thermal-profile -> power plan sync: G-Mode / Ultra Performance switch
    // Windows to the High Performance plan, every other profile to Balanced.
    // Applies only when enabled in Settings.
    void setPowerPlanSyncEnabled(bool on) { m_planSyncEnabled = on; }
    bool powerPlanSyncEnabled() const { return m_planSyncEnabled; }
    // Overrides monitor mode: write even when AWCC processes are detected.
    void setForceControl(bool on) { m_forceControl = on; }
    // Full state reload after Settings "Reset to defaults": every running
    // object re-reads the (now default) config - mode, curves, boost floors,
    // scene, failsafe - otherwise stale in-memory values survive the reset.
    void reloadFromConfig();
    // Drop the cached last-written mode so the next tick re-applies it (mode UI).
    // Marks a user write as pending: external-profile adoption must not fight it.
    void forceNextModeWrite() {
        m_lastModeWritten.reset();
        m_pendingModeWrite = true;
        m_pendingWriteAttempts = 0;
    }
    // Set when the last mode write was rejected by the hardware (e.g. G-Mode
    // on machines without it); cleared on the next successful write. UI reads
    // this after every snapshotUpdated to show a banner.
    std::optional<ThermalMode> failedMode() const { return m_failedMode; }
    SceneDetector* scene() { return &m_scene; }
    BaselinePolicy* baseline() { return &m_baseline; }
    FailsafePolicy* failsafe() { return &m_failsafe; }
    DynamicPolicy* dynamicPolicy() { return &m_dynamic; }

signals:
    void snapshotUpdated(const dtb::SystemSnapshot& snapshot);
    void activePolicyChanged(const QString& name);
    // Emitted whenever the applied thermal profile changes — our own write
    // succeeding, or an external change (AWCC/DPM) being adopted. UI and tray
    // both sync from this single signal.
    void thermalModeChanged(dtb::ThermalMode mode, bool external);
    // Emitted after reloadFromConfig(); pages refresh their controls from it.
    void configReloaded();
    // Emitted when the firmware-reported charging mode changes (polled every
    // 3rd tick; empty string = read failed, signal suppressed then; the first
    // successful read only seeds chargingMode(), no signal).
    void chargingModeChanged(const QString& mode);

private:
    SystemSnapshot readSnapshot();
    void adoptExternalProfile();
    void apply(const ControlTargets& t);

    HalSet m_hal; // not owned; the creator keeps the HAL objects alive
    ConfigStore* m_cfg; // not owned
    QTimer m_timer;
    bool m_passive = false;

    SceneDetector m_scene;
    ScenePolicy m_scenePolicy;
    DynamicPolicy m_dynamic;
    FailsafePolicy m_failsafe;
    BaselinePolicy m_baseline;
    ManualOverride m_manual;
    PolicyChain m_chain;

    SystemSnapshot m_snapshot;
    std::function<qint64()> m_clock = [] { return QDateTime::currentMSecsSinceEpoch(); };
    QString m_activeName;

    // write throttling state
    std::optional<ThermalMode> m_lastModeWritten;
    std::optional<ThermalMode> m_failedMode;
    bool m_pendingModeWrite = false; // user picked a mode; its write has not succeeded yet
    int m_pendingWriteAttempts = 0; // failed retries of the pending write
    qint64 m_lastModeWriteMs = 0; // last successful mode write (adoption grace window)
    QHash<FanId, int> m_lastFanPercent;
    QHash<FanId, int> m_fanBoost; // Easy mode: per-fan minimum speed
    QString m_chargingMode; // last known charging mode ("" until first read)
    QString m_manualProfileName; // active manual power profile (display)
    bool m_planSyncEnabled = true;
    bool m_forceControl = false;
    bool m_planBusy = false; // a powercfg chain is in flight
    ThermalMode m_planWanted = ThermalMode::Balanced; // latest request
    bool m_planAppliedHigh = false; // what the last completed chain applied
    bool m_planCreateTried = false; // one duplicatescheme retry per request
    int m_chargePollTick = 0; // charge read every 3rd tick (WMI cost)
    QSet<FanId> m_fanForceRewrite; // next write bypasses the dead zone
    qint64 m_lastFanWriteMs = 0;
    qint64 m_lastGpuWriteMs = 0;
    static constexpr int kFanDeadZonePercent = 2;
    static constexpr qint64 kMinWriteIntervalMs = 1000;
};

} // namespace dtb
