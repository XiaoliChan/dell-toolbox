#pragma once

#include <QList>
#include <QString>

#include "hal/HalInterfaces.h"
#include "hal/win/DellAcpiChannel.h"

namespace dtb::win {

// Charging-mode backend. Primary path: Dell ACPI-WMI (the channel Dell Power
// Manager itself uses — no external dependencies). Fallback: cctk.exe from
// Dell Command Configure. which() reports the active path for the UI.
class WinChargeHAL : public IChargeThresholdHAL {
public:
    WinChargeHAL();

    QList<Mode> modes() override;
    QString currentMode() override;
    bool setMode(const QString& value) override;
    bool available() override;
    QString sourceName() const override;
    QString unavailableReason() const { return m_reason; }

private:
    int runCctk(const QStringList& args, QString* stdoutText);

    DellAcpiChannel m_acpi;
    QString m_cctkPath;
    QString m_source; // "dell-acpi" | "cctk" | ""
    QString m_reason;
    bool m_cctkUsable = true; // cctk found but maybe password-blocked at runtime
};

} // namespace dtb::win
