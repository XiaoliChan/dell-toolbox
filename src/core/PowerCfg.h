#pragma once

#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVector>

namespace dtb {

// One "GUID: <guid> (<name>)" line of `powercfg /list` and
// `powercfg /getactivescheme` output.
struct PowerPlanEntry {
    QString guid;
    QString name;
};

// powercfg can take seconds: callers run it through QProcess asynchronously
// and feed readAllStandardOutput() here (never block the UI thread). Shared by
// the Controller's profile->plan sync and the PerformancePage plan picker.
inline QVector<PowerPlanEntry> parsePowerPlans(const QString& powerCfgOutput) {
    static const QRegularExpression kPlanRe(QStringLiteral("GUID: ([0-9a-fA-F-]+)\\s+\\(([^)]+)\\)"));
    QVector<PowerPlanEntry> plans;
    const QStringList lines = powerCfgOutput.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        const auto m = kPlanRe.match(line.trimmed());
        if (m.hasMatch())
            plans.append({m.captured(1), m.captured(2)});
    }
    return plans;
}

} // namespace dtb
