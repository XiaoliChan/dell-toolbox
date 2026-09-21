#include "hal/win/WinChargeHAL.h"

#include <QCoreApplication>
#include <QDir>
#include <QLoggingCategory>
#include <QProcess>
#include <QRegularExpression>

namespace dtb::win {
namespace {
Q_LOGGING_CATEGORY(lcCharge, "dtb.charge")

bool isPasswordBlocked(int exitCode) {
    return exitCode == 65 || exitCode == 58 || exitCode == 66 || exitCode == 67;
}

QString acpiModeToKey(unsigned char m) {
    switch (m) {
    case DellAcpiChannel::ChargeStandard:
        return QStringLiteral("standard");
    case DellAcpiChannel::ChargeExpress:
        return QStringLiteral("express");
    case DellAcpiChannel::ChargePredominatelyAc:
        return QStringLiteral("primacuse");
    case DellAcpiChannel::ChargeAuto:
        return QStringLiteral("adaptive");
    case DellAcpiChannel::ChargeCustom:
        return QStringLiteral("custom");
    case DellAcpiChannel::ChargeLongLife:
        return QStringLiteral("longlife");
    case DellAcpiChannel::ChargeAdvanced:
        return QStringLiteral("advanced");
    default:
        return {};
    }
}

unsigned char keyToAcpiMode(const QString& key) {
    if (key == QLatin1String("standard"))
        return DellAcpiChannel::ChargeStandard;
    if (key == QLatin1String("express"))
        return DellAcpiChannel::ChargeExpress;
    if (key == QLatin1String("primacuse"))
        return DellAcpiChannel::ChargePredominatelyAc;
    if (key == QLatin1String("adaptive"))
        return DellAcpiChannel::ChargeAuto;
    if (key == QLatin1String("custom"))
        return DellAcpiChannel::ChargeCustom;
    return 0;
}
} // namespace

WinChargeHAL::WinChargeHAL() {
    m_acpi.initialize();
    if (m_acpi.valid()) {
        m_source = QStringLiteral("dell-acpi");
        return;
    }
    const QStringList roots = {
        QDir::cleanPath(qEnvironmentVariable("ProgramFiles(x86)") + "/Dell/Command Configure/X86_64"),
        QDir::cleanPath(qEnvironmentVariable("ProgramFiles") + "/Dell/Command Configure/X86_64"),
    };
    for (const QString& root : roots) {
        const QString candidate = root + "/cctk.exe";
        if (QFile::exists(candidate)) {
            m_cctkPath = candidate;
            m_source = QStringLiteral("cctk");
            return;
        }
    }
    m_reason = QStringLiteral(
        "No Dell ACPI-WMI channel and no Dell Command Configure (cctk) found; "
        "charging modes unavailable on this machine");
}

QList<IChargeThresholdHAL::Mode> WinChargeHAL::modes() {
    QList<Mode> all = {{"Adaptive", "adaptive"},
                       {"Standard", "standard"},
                       {"Express", "express"},
                       {"Primarily AC Use", "primacuse"},
                       {"Custom", "custom"}};
    if (m_source == QLatin1String("dell-acpi")) {
        // Keep only modes the BIOS reports as supported.
        DellAcpiChannel::ChargeState state;
        if (m_acpi.getChargeState(state) && state.error == 0 && state.capability != 0) {
            QList<Mode> filtered;
            for (const Mode& entry : all) {
                const unsigned char byte = keyToAcpiMode(entry.cctkValue);
                unsigned int bit = 0;
                switch (byte) {
                case DellAcpiChannel::ChargeStandard:
                    bit = DellAcpiChannel::CapStandard;
                    break;
                case DellAcpiChannel::ChargeExpress:
                    bit = DellAcpiChannel::CapExpress;
                    break;
                case DellAcpiChannel::ChargePredominatelyAc:
                    bit = DellAcpiChannel::CapPredominatelyAc;
                    break;
                case DellAcpiChannel::ChargeAuto:
                    bit = DellAcpiChannel::CapAuto;
                    break;
                case DellAcpiChannel::ChargeCustom:
                    bit = DellAcpiChannel::CapCustom;
                    break;
                default:
                    break;
                }
                if (bit == 0 || (state.capability & bit))
                    filtered.append(entry);
            }
            if (!filtered.isEmpty())
                return filtered;
        }
    }
    return all;
}

int WinChargeHAL::runCctk(const QStringList& args, QString* stdoutText) {
    QProcess process;
    process.start(m_cctkPath, args);
    if (!process.waitForFinished(15000))
        return -1;
    if (stdoutText)
        *stdoutText = QString::fromUtf8(process.readAllStandardOutput());
    return process.exitCode();
}

QString WinChargeHAL::currentMode() {
    if (m_source == QLatin1String("dell-acpi")) {
        DellAcpiChannel::ChargeState state;
        if (!m_acpi.getChargeState(state) || state.error != 0) {
            qCWarning(lcCharge) << "charge state read failed: ok=" << m_acpi.valid()
                                << "error=" << state.error << "mode=" << state.mode;
            return {};
        }
        const QString key = acpiModeToKey(static_cast<unsigned char>(state.mode));
        if (key.isEmpty()) {
            // Firmware reports a mode we don't map; the raw byte identifies it.
            qCInfo(lcCharge) << "unmapped current charging mode byte:" << state.mode;
        }
        return key;
    }
    if (m_source == QLatin1String("cctk")) {
        QString out;
        const int code = runCctk({"--PrimaryBattChargeCfg"}, &out);
        if (code != 0 || isPasswordBlocked(code))
            return {};
        static const QRegularExpression re("PrimaryBattChargeCfg\\s*=\\s*([A-Za-z]+)");
        const auto match = re.match(out);
        return match.hasMatch() ? match.captured(1).toLower() : QString();
    }
    return {};
}

bool WinChargeHAL::setMode(const QString& value) {
    if (m_source == QLatin1String("dell-acpi")) {
        // "custom:start-stop" carries the custom thresholds.
        unsigned char start = 0, stop = 0;
        QString key = value;
        if (value.startsWith(QLatin1String("custom:"))) {
            key = QStringLiteral("custom");
            const QRegularExpression rangeRe("^custom:(\\d+)-(\\d+)$");
            const auto m = rangeRe.match(value);
            if (m.hasMatch()) {
                start = static_cast<unsigned char>(m.captured(1).toInt());
                stop = static_cast<unsigned char>(m.captured(2).toInt());
            }
        }
        const unsigned char mode = keyToAcpiMode(key);
        if (mode == 0)
            return false;
        return m_acpi.setChargeMode(mode, start, stop);
    }
    if (m_source == QLatin1String("cctk")) {
        const int code = runCctk({"--PrimaryBattChargeCfg=" + value}, nullptr);
        if (isPasswordBlocked(code)) {
            m_reason = QStringLiteral("BIOS setup password blocks cctk changes");
            m_cctkUsable = false;
            return false;
        }
        return code == 0;
    }
    return false;
}

QString WinChargeHAL::sourceName() const {
    if (m_source == QLatin1String("dell-acpi"))
        return QStringLiteral("dell-acpi (%1)").arg(m_acpi.transport());
    return m_source;
}

bool WinChargeHAL::available() {
    return m_source == QLatin1String("dell-acpi") || (m_source == QLatin1String("cctk") && m_cctkUsable);
}

} // namespace dtb::win
