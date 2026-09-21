#pragma once

#include <QList>
#include <QPair>
#include <QSettings>
#include <QString>

#include "core/DynamicPolicy.h"
#include "core/FailsafePolicy.h"
#include "core/SceneDetector.h"
#include "core/Snapshot.h"

namespace dtb {

// Persistence for every user setting, one read/write pair per key group.
// All keys live under the `dtb/` prefix. Any corrupt or missing value falls
// back to the built-in defaults.
class ConfigStore {
public:
    ConfigStore();                 // platform default location
    explicit ConfigStore(const QString& iniPath); // explicit file (tests, portable mode)

    ThermalMode loadMode();
    void saveMode(ThermalMode m);

    QList<QPair<int, int>> loadCurve(FanId fan);
    void saveCurve(FanId fan, const QList<QPair<int, int>>& pts);
    int loadFanBoost(FanId fan); // 0 = follow the mode curve (auto)
    void saveFanBoost(FanId fan, int percent);

    SceneParams loadSceneParams();
    void saveSceneParams(const SceneParams& p);

    FailsafeParams loadFailsafeParams();
    void saveFailsafeParams(const FailsafeParams& p);

    int loadManualExpireMin();
    void saveManualExpireMin(int minutes);

    // Named manual power-limit profiles ("Profile 1", pl1/pl2 watts).
    struct ManualProfile {
        QString name;
        int pl1W = 45;
        int pl2W = 54;
    };
    QList<ManualProfile> loadManualProfiles();
    void saveManualProfiles(const QList<ManualProfile>& profiles);

    DynamicProfile loadDynamicProfile();
    void saveDynamicProfile(const DynamicProfile& p);

    // Coexist with official apps: auto-pause our writers while AWCC runs.
    bool loadAwccCoexist();
    void saveAwccCoexist(bool on);

    bool loadAutostart();
    void saveAutostart(bool on);

    int loadLogLevel();
    void saveLogLevel(int level); // 0 debug, 1 info, 2 warn, 3 error

    void resetToDefaults();

    static QList<QPair<int, int>> parsePointList(const QString& text);
    static QString formatPointList(const QList<QPair<int, int>>& pts);

private:
    QList<QPair<int, int>> loadTable(const char* key, const QList<QPair<int, int>>& fallback);

    QSettings m_s;
};

} // namespace dtb
