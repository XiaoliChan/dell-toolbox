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

QString xmlForExe() {
    QFile templateFile(QStringLiteral(":/res/autostart-task.xml"));
    QString xml = templateFile.open(QIODevice::ReadOnly)
                      ? QString::fromUtf8(templateFile.readAll())
                      : QString();
    xml.replace(QStringLiteral("<!--EXE_FILE_PATH-->"),
                QCoreApplication::applicationFilePath().replace(QLatin1Char('\\'), QLatin1Char('/')));
    return xml;
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
    QTemporaryDir dir;
    const QString xmlPath = dir.filePath(QStringLiteral("task.xml"));
    QFile out(xmlPath);
    if (!out.open(QIODevice::WriteOnly)) {
        dtbLog(warn) << "autostart: cannot write task xml";
        return false;
    }
    out.write(xmlForExe().toUtf8());
    out.close();
    QProcess create;
    create.start(QStringLiteral("schtasks"),
                 {QStringLiteral("/create"), QStringLiteral("/tn"), kTaskName, QStringLiteral("/xml"), xmlPath,
                  QStringLiteral("/f")});
    create.waitForFinished(10000);
    if (create.exitCode() == 0)
        return true;
    dtbLog(warn) << "autostart: XML create failed:" << createError(create);
    return createViaCli(exePathBackslashes());
}

#else

bool Autostart::enabled() { return false; }

bool Autostart::setEnabled(bool) { return false; } // not applicable outside Windows

#endif

} // namespace dtb
