#include "core/Autostart.h"

#include "core/AppInfo.h"
#include "core/Logger.h"

#ifdef Q_OS_WIN
#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#endif

namespace dtb {

#ifdef Q_OS_WIN

namespace {
constexpr const char* kTaskName = "DellToolbox";

QString exePathBackslashes() {
    return QCoreApplication::applicationFilePath().replace(QLatin1Char('/'), QLatin1Char('\\'));
}

QString createError(const QProcess& proc) {
    const QString err = proc.readAllStandardError().trimmed();
    return err.isEmpty() ? QStringLiteral("exit code %1").arg(proc.exitCode()) : err;
}

// Task XML in UTF-16 LE with BOM - the encoding Windows' own task exports
// use. schtasks' MSXML parser rejects our previous UTF-8 declaration
// ("unable to switch the encoding", pointing at column 40 of line 1).
QString xmlForExe() {
    QFile templateFile(QStringLiteral(":/res/autostart-task.xml"));
    QString xml = templateFile.open(QIODevice::ReadOnly)
                      ? QString::fromUtf8(templateFile.readAll())
                      : QString();
    xml.replace(QStringLiteral("<!--EXE_FILE_PATH-->"), exePathBackslashes());
    xml.replace(QStringLiteral("encoding=\"UTF-8\""), QStringLiteral("encoding=\"UTF-16\""));
    return xml;
}

bool createViaXml(const QString& xml) {
    QTemporaryDir dir;
    const QString xmlPath = dir.filePath(QStringLiteral("task.xml"));
    QFile out(xmlPath);
    if (!out.open(QIODevice::WriteOnly))
        return false;
    // UTF-16 LE bytes + BOM; the declaration inside says UTF-16 to match.
    const QString bom = QString(QChar(0xFEFF));
    out.write(QString(bom + xml).toUtf16());
    out.close();
    QProcess create;
    create.start(QStringLiteral("schtasks"),
                 {QStringLiteral("/create"), QStringLiteral("/tn"), kTaskName, QStringLiteral("/xml"), xmlPath,
                  QStringLiteral("/f")});
    create.waitForFinished(10000);
    if (create.exitCode() == 0)
        return true;
    dtbLog(warn) << "autostart: XML create failed:" << createError(create);
    return false;
}

bool createViaCli(const QString& exe) {
    // Fallback: schtasks' native CLI creation - more forgiving than the XML
    // import (no parser involved).
    QProcess create;
    create.start(QStringLiteral("schtasks"),
                 {QStringLiteral("/create"), QStringLiteral("/tn"), kTaskName,
                  QStringLiteral("/tr"), QStringLiteral("\"%1\" --minimized").arg(exe),
                  QStringLiteral("/sc"), QStringLiteral("onlogon"), QStringLiteral("/rl"),
                  QStringLiteral("highest"), QStringLiteral("/f")});
    create.waitForFinished(10000);
    if (create.exitCode() != 0) {
        dtbLog(warn) << "autostart: CLI create failed:" << createError(create);
        return false;
    }
    return true;
}
} // namespace

bool Autostart::enabled() {
    QProcess query;
    query.start(QStringLiteral("schtasks"), {QStringLiteral("/query"), QStringLiteral("/tn"), kTaskName});
    query.waitForFinished(5000);
    return query.exitCode() == 0;
}

bool Autostart::setEnabled(bool on) {
    if (!on) {
        QProcess remove;
        remove.start(QStringLiteral("schtasks"),
                     {QStringLiteral("/delete"), QStringLiteral("/tn"), kTaskName, QStringLiteral("/f")});
        remove.waitForFinished(5000);
        return true;
    }
    if (createViaXml(xmlForExe()))
        return true;
    return createViaCli(exePathBackslashes());
}

#else

bool Autostart::enabled() { return false; }

bool Autostart::setEnabled(bool) { return false; } // not applicable outside Windows

#endif

} // namespace dtb
