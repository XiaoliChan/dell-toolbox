#pragma once

#include <QDebug>
#include <QString>

namespace dtb {

// Minimal rotating file logger. init() once from main; every dtbLog() line
// goes through the global mutex, so the control loop and the UI threads can
// both log safely.
class Logger {
public:
    enum Level { debug = 0, info = 1, warn = 2, error = 3 };

    static void init(const QString& filePath, Level minLevel = info, qint64 rotateBytes = 1024 * 1024);
    static void write(Level level, const char* file, int line, const QString& message);
    static QString levelName(Level l);
    static void shutdown();
};

// Usage: dtbLog(info) << "fan " << id << " -> " << pct;
class LogLine {
public:
    LogLine(Logger::Level level, const char* file, int line);
    ~LogLine();
    LogLine(const LogLine&) = delete;
    LogLine& operator=(const LogLine&) = delete;

    template <typename T>
    LogLine& operator<<(const T& value) {
        m_debug << value;
        return *this;
    }

private:
    Logger::Level m_level;
    const char* m_file;
    int m_line;
    QString m_buffer;
    QDebug m_debug;
};

} // namespace dtb

#define dtbLog(level) dtb::LogLine(dtb::Logger::Level::level, __FILE__, __LINE__)
