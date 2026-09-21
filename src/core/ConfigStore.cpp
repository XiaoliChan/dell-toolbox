#include "core/ConfigStore.h"

#include "core/AppInfo.h"
#include "core/FanCurve.h"

namespace dtb {

ConfigStore::ConfigStore() : m_s(app::kId, app::kId) {}

ConfigStore::ConfigStore(const QString& iniPath) : m_s(iniPath, QSettings::IniFormat) {}

QList<QPair<int, int>> ConfigStore::parsePointList(const QString& text) {
    QList<QPair<int, int>> pts;
    const auto tokens = text.split(',', Qt::SkipEmptyParts);
    for (const QString& tok : tokens) {
        const int colon = tok.indexOf(':');
        if (colon <= 0)
            return {};
        bool okT = false, okP = false;
        const int t = tok.left(colon).toInt(&okT);
        const int p = tok.mid(colon + 1).toInt(&okP);
        if (!okT || !okP)
            return {};
        pts.append({t, p});
    }
    return pts;
}

QString ConfigStore::formatPointList(const QList<QPair<int, int>>& pts) {
    QStringList out;
    for (const auto& tp : pts)
        out.append(QString::number(tp.first) + ':' + QString::number(tp.second));
    return out.join(',');
}

ThermalMode ConfigStore::loadMode() {
    const int v = m_s.value("dtb/mode", 0).toInt();
    switch (v) {
    case 0:
        return ThermalMode::Balanced;
    case 1:
        return ThermalMode::GMode;
    case 2:
        return ThermalMode::Custom;
    case 3:
        return ThermalMode::Quiet;
    case 4:
        return ThermalMode::Cool;
    case 5:
        return ThermalMode::Performance;
    default:
        return ThermalMode::Balanced;
    }
}

void ConfigStore::saveMode(ThermalMode m) {
    int v = 0;
    switch (m) {
    case ThermalMode::GMode:
        v = 1;
        break;
    case ThermalMode::Custom:
        v = 2;
        break;
    case ThermalMode::Quiet:
        v = 3;
        break;
    case ThermalMode::Cool:
        v = 4;
        break;
    case ThermalMode::Performance:
        v = 5;
        break;
    default:
        v = 0;
        break;
    }
    m_s.setValue("dtb/mode", v);
}

int ConfigStore::loadFanBoost(FanId fan) {
    return m_s.value(QStringLiteral("dtb/fanboost/%1").arg(fan), 0).toInt();
}

void ConfigStore::saveFanBoost(FanId fan, int percent) {
    m_s.setValue(QStringLiteral("dtb/fanboost/%1").arg(fan), percent);
}

QList<QPair<int, int>> ConfigStore::loadCurve(FanId fan) {
    const QString key = "dtb/fan/" + QString::number(fan) + "/curve";
    const QString raw = m_s.value(key).toString();
    if (!raw.isEmpty()) {
        FanCurve c;
        c.setPoints(parsePointList(raw));
        if (c.valid())
            return c.points();
    }
    return FanCurve::defaultCurve().points();
}

void ConfigStore::saveCurve(FanId fan, const QList<QPair<int, int>>& pts) {
    FanCurve c;
    c.setPoints(pts);
    if (!c.valid())
        return; // never persist an invalid curve
    m_s.setValue("dtb/fan/" + QString::number(fan) + "/curve", formatPointList(pts));
}

SceneParams ConfigStore::loadSceneParams() {
    SceneParams p;
    p.enabled = m_s.value("dtb/scene/enabled", p.enabled).toBool();
    p.gpuEnterThreshold = m_s.value("dtb/scene/gpuEnter", p.gpuEnterThreshold).toInt();
    p.gpuExitThreshold = m_s.value("dtb/scene/gpuExit", p.gpuExitThreshold).toInt();
    p.enterDebounceS = m_s.value("dtb/scene/enterDebounce", p.enterDebounceS).toInt();
    p.exitDebounceS = m_s.value("dtb/scene/exitDebounce", p.exitDebounceS).toInt();
    p.gameProcesses = m_s.value("dtb/scene/processes").toString().split(',', Qt::SkipEmptyParts);
    return p;
}

void ConfigStore::saveSceneParams(const SceneParams& p) {
    m_s.setValue("dtb/scene/enabled", p.enabled);
    m_s.setValue("dtb/scene/gpuEnter", p.gpuEnterThreshold);
    m_s.setValue("dtb/scene/gpuExit", p.gpuExitThreshold);
    m_s.setValue("dtb/scene/enterDebounce", p.enterDebounceS);
    m_s.setValue("dtb/scene/exitDebounce", p.exitDebounceS);
    m_s.setValue("dtb/scene/processes", p.gameProcesses.join(','));
}

QList<ConfigStore::ManualProfile> ConfigStore::loadManualProfiles() {
    QList<ManualProfile> out;
    const QStringList raw = m_s.value("dtb/manual/profiles").toString().split(';', Qt::SkipEmptyParts);
    for (const QString& entry : raw) {
        const QStringList parts = entry.split('|');
        if (parts.size() != 3)
            continue;
        ManualProfile p;
        p.name = parts[0];
        p.pl1W = parts[1].toInt();
        p.pl2W = parts[2].toInt();
        if (!p.name.isEmpty() && p.pl1W > 0 && p.pl2W > 0)
            out.append(p);
    }
    return out;
}

void ConfigStore::saveManualProfiles(const QList<ManualProfile>& profiles) {
    QStringList raw;
    for (const ManualProfile& p : profiles)
        raw.append(QStringLiteral("%1|%2|%3").arg(p.name).arg(p.pl1W).arg(p.pl2W));
    m_s.setValue("dtb/manual/profiles", raw.join(';'));
}

FailsafeParams ConfigStore::loadFailsafeParams() {
    FailsafeParams p;
    p.cpuTripC = m_s.value("dtb/failsafe/cpuTripC", p.cpuTripC).toInt();
    p.gpuTripC = m_s.value("dtb/failsafe/gpuTripC", p.gpuTripC).toInt();
    p.triggerDelayS = m_s.value("dtb/failsafe/delayS", p.triggerDelayS).toInt();
    p.releaseDelayS = m_s.value("dtb/failsafe/resetS", p.releaseDelayS).toInt();
    return p;
}

void ConfigStore::saveFailsafeParams(const FailsafeParams& p) {
    m_s.setValue("dtb/failsafe/cpuTripC", p.cpuTripC);
    m_s.setValue("dtb/failsafe/gpuTripC", p.gpuTripC);
    m_s.setValue("dtb/failsafe/delayS", p.triggerDelayS);
    m_s.setValue("dtb/failsafe/resetS", p.releaseDelayS);
}

int ConfigStore::loadManualExpireMin() {
    return m_s.value("dtb/manual/expireMin", 30).toInt();
}

void ConfigStore::saveManualExpireMin(int minutes) { m_s.setValue("dtb/manual/expireMin", minutes); }

QList<QPair<int, int>> ConfigStore::loadTable(const char* key,
                                              const QList<QPair<int, int>>& fallback) {
    const QString raw = m_s.value(key).toString();
    if (!raw.isEmpty()) {
        const auto pts = parsePointList(raw);
        if (pts.size() >= 2)
            return pts;
    }
    return fallback;
}

DynamicProfile ConfigStore::loadDynamicProfile() {
    DynamicProfile def;
    DynamicProfile p;
    p.cpuPl1Table = loadTable("dtb/dynamic/cpuPl1Table", def.cpuPl1Table);
    p.gpuPptTable = loadTable("dtb/dynamic/gpuPptTable", def.gpuPptTable);
    return p;
}

void ConfigStore::saveDynamicProfile(const DynamicProfile& p) {
    m_s.setValue("dtb/dynamic/cpuPl1Table", formatPointList(p.cpuPl1Table));
    m_s.setValue("dtb/dynamic/gpuPptTable", formatPointList(p.gpuPptTable));
}

bool ConfigStore::loadStartMinimized() { return m_s.value("dtb/startMinimized", false).toBool(); }

void ConfigStore::saveStartMinimized(bool on) { m_s.setValue("dtb/startMinimized", on); }

bool ConfigStore::loadAwccCoexist() { return m_s.value("dtb/awccCoexist", true).toBool(); }

void ConfigStore::saveAwccCoexist(bool on) { m_s.setValue("dtb/awccCoexist", on); }

bool ConfigStore::loadAutostart() { return m_s.value("dtb/autostart", false).toBool(); }

void ConfigStore::saveAutostart(bool on) { m_s.setValue("dtb/autostart", on); }

int ConfigStore::loadLogLevel() { return m_s.value("dtb/logLevel", 1).toInt(); }

void ConfigStore::saveLogLevel(int level) { m_s.setValue("dtb/logLevel", level); }

void ConfigStore::resetToDefaults() {
    m_s.clear();
    m_s.sync();
}

} // namespace dtb
