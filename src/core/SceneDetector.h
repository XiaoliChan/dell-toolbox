#pragma once

#include <QString>
#include <QStringList>

#include "core/Snapshot.h"

namespace dtb {

struct SceneParams {
    bool enabled = true;
    int gpuEnterThreshold = 60; // percent, rising edge
    int gpuExitThreshold = 55;  // percent, sustain level below this counts as leaving
    int enterDebounceS = 10;
    int exitDebounceS = 30;
    QStringList gameProcesses; // process file names, compared case-insensitively
};

// Detects "a game is running" from GPU load and/or the foreground process,
// with dual-threshold hysteresis and debounce on both edges. Call onTick once
// per control tick (1 Hz in production, so counters are in ticks ~= seconds).
class SceneDetector {
public:
    explicit SceneDetector(SceneParams p);

    void onTick(const SystemSnapshot& s);
    bool inScene() const { return m_inScene; }
    QString trigger() const { return m_trigger; } // "gpu" or "process:<name>"
    const SceneParams& params() const { return m_params; }
    void setParams(SceneParams p);

    static QString normalizeProcessName(const QString& pathOrName);

private:
    QString matchProcess(const QString& normalized) const;

    SceneParams m_params;
    bool m_inScene = false;
    int m_streak = 0; // consecutive ticks satisfying (or violating) the edge condition
    QString m_trigger;
};

} // namespace dtb
