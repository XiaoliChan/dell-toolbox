#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QThread>
#include <cstdio>

// Small cross-platform subprocess fixture, never calls the real powercfg.
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QString dir = QCoreApplication::applicationDirPath();
    const QStringList args = app.arguments().mid(1);
    QFile trace(dir + QStringLiteral("/trace"));
    if (!trace.open(QIODevice::WriteOnly | QIODevice::Append))
        return 1;
    trace.write(args.join(' ').toUtf8() + '\n');
    trace.close();
    if (args.value(0) == QStringLiteral("/list")) {
        QElapsedTimer timeout;
        timeout.start();
        while (QFile::exists(dir + QStringLiteral("/gate"))
               && !QFile::exists(dir + QStringLiteral("/release"))) {
            if (timeout.elapsed() > 10000)
                return 2;
            QThread::msleep(10);
        }
        std::puts("381b4222-f694-41f0-9685-ff5bb260df2e (Balanced)");
        std::puts("8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c (High performance)");
    } else if (args.value(0) == QStringLiteral("/getactivescheme")) {
        std::puts("00000000-0000-0000-0000-000000000001 (Other)");
    }
    return 0;
}
