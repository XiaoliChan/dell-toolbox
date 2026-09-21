#include <QTemporaryDir>
#include <QtTest>

#include "core/Logger.h"

using namespace dtb;

class TestLogger : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        dir = std::make_unique<QTemporaryDir>();
        QVERIFY(dir->isValid());
    }

    void writesToFile() {
        const QString path = dir->filePath("plain.log");
        Logger::init(path, Logger::debug);
        dtbLog(info) << "hello" << 42;
        Logger::shutdown();
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString content = QString::fromUtf8(f.readAll());
        QVERIFY(content.contains("hello 42"));
        QVERIFY(content.contains("[info]"));
    }

    void filtersBelowMinimumLevel() {
        const QString path = dir->filePath("filtered.log");
        Logger::init(path, Logger::warn);
        dtbLog(debug) << "dropped";
        dtbLog(error) << "kept";
        Logger::shutdown();
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString content = QString::fromUtf8(f.readAll());
        QVERIFY(!content.contains("dropped"));
        QVERIFY(content.contains("kept"));
    }

    void truncatesWhenTooBig() {
        // One file, always: overflow resets in place, never a .1 backup.
        const QString path = dir->filePath("rotating.log");
        Logger::init(path, Logger::debug, 4096);
        for (int i = 0; i < 200; ++i)
            dtbLog(info) << QString(40, 'x') << i;
        Logger::shutdown();
        QFile current(path);
        QFile rotated(path + ".1");
        QVERIFY(current.exists());
        QVERIFY(!rotated.exists()); // no backup files, ever
        QVERIFY(current.size() < 4096); // reset in place
    }

private:
    std::unique_ptr<QTemporaryDir> dir;
};

QTEST_GUILESS_MAIN(TestLogger)
#include "test_logger.moc"
