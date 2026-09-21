#pragma once

#include <QCoreApplication>
#include <QString>

namespace dtb::ui {

// Human-readable names for firmware charging-mode keys (adaptive/standard/...
// as returned by IChargeThresholdHAL::currentMode).
inline QString chargingModeName(const QString& key) {
    if (key == QLatin1String("adaptive"))
        return QCoreApplication::translate("Names", "Adaptive");
    if (key == QLatin1String("standard"))
        return QCoreApplication::translate("Names", "Standard");
    if (key == QLatin1String("express"))
        return QCoreApplication::translate("Names", "Express");
    if (key == QLatin1String("primacuse"))
        return QCoreApplication::translate("Names", "Primarily AC");
    if (key == QLatin1String("custom"))
        return QCoreApplication::translate("Names", "Custom");
    if (key == QLatin1String("longlife"))
        return QCoreApplication::translate("Names", "Long Life");
    if (key == QLatin1String("advanced"))
        return QCoreApplication::translate("Names", "Advanced");
    return key;
}

} // namespace dtb::ui
