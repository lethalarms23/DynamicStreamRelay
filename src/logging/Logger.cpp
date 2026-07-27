#include "logging/Logger.h"
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>

namespace rtsp {
Logger::Logger(QObject* parent) : QObject(parent) {
    QDir().mkpath(logDirectory()); rotate();
    file_.setFileName(logDirectory() + "/application.log"); file_.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}
QString Logger::logDirectory() const { return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/logs"; }
void Logger::rotate() {
    QFile current(logDirectory() + "/application.log");
    if (current.size() < 5 * 1024 * 1024) return;
    QFile::remove(logDirectory() + "/application.3.log");
    for (int i = 2; i >= 1; --i) QFile::rename(logDirectory() + QString("/application.%1.log").arg(i), logDirectory() + QString("/application.%1.log").arg(i + 1));
    QFile::rename(current.fileName(), logDirectory() + "/application.1.log");
}
void Logger::addSecret(const QString& s) { QMutexLocker lock(&mutex_); redactor_.addSecret(s); }
void Logger::log(Severity severity, const QString& component, const QString& raw) {
    QMutexLocker lock(&mutex_); const auto message = redactor_.redact(raw); const auto stamp = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    static const char* names[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    if (file_.isOpen()) { QTextStream out(&file_); out << stamp << ' ' << names[static_cast<int>(severity)] << ' ' << component << ' ' << message << '\n'; file_.flush(); }
    emit entry(stamp, static_cast<int>(severity), component, message);
}
}

