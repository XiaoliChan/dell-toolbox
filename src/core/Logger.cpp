#include "core/Logger.h"

#include <QDateTime>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>

namespace dtb {

namespace {
QMutex g_mutex;
QString g_path;
Logger::Level g_minLevel = Logger::info;
qint64 g_rotateBytes = 1024 * 1024;
QFile g_file;

void rotateIfNeeded() {
    if (g_file.isOpen() && g_file.size() < g_rotateBytes)
        return;
    // Reset in place - never spawn .1/.2 files. The previous session's log
    // is dropped at startup (init removes the file); the size cap only
    // guards against unbounded growth within one session.
    g_file.close();
    if (!g_path.isEmpty())
        QFile::remove(g_path);
}
} // namespace

void Logger::init(const QString& filePath, Level minLevel, qint64 rotateBytes) {
    const QMutexLocker lock(&g_mutex);
    g_path = filePath;
    g_minLevel = minLevel;
    g_rotateBytes = rotateBytes;
    g_file.close();
    if (QFile::exists(filePath))
        QFile::remove(filePath); // fresh log each run: one file, no backups
    g_file.setFileName(filePath);
}

void Logger::shutdown() {
    const QMutexLocker lock(&g_mutex);
    g_file.close();
    g_path.clear();
}

QString Logger::levelName(Level l) {
    switch (l) {
    case debug:
        return QStringLiteral("debug");
    case info:
        return QStringLiteral("info");
    case warn:
        return QStringLiteral("warn");
    default:
        return QStringLiteral("error");
    }
}

void Logger::write(Level level, const char* file, int line, const QString& message) {
    const QMutexLocker lock(&g_mutex);
    if (g_path.isEmpty() || level < g_minLevel)
        return;
    rotateIfNeeded();
    if (!g_file.isOpen()) {
        g_file.setFileName(g_path);
        if (!g_file.open(QIODevice::Append | QIODevice::WriteOnly | QIODevice::Text))
            return;
    }
    const QString ts = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    g_file.write(QString("%1 [%2] %3:%4 %5\n")
                     .arg(ts, levelName(level), QString::fromLatin1(file), QString::number(line), message)
                     .toUtf8());
    g_file.flush();
}

LogLine::LogLine(Logger::Level level, const char* file, int line)
    : m_level(level), m_file(file), m_line(line), m_buffer(), m_debug(&m_buffer) {}

LogLine::~LogLine() { Logger::write(m_level, m_file, m_line, m_buffer.trimmed()); }

} // namespace dtb
