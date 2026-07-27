#pragma once
#include "logging/SecretRedactor.h"
#include <QFile>
#include <QMutex>
#include <QObject>
#include <QTextStream>

namespace rtsp {
enum class Severity { Debug, Info, Warning, Error };
class Logger final : public QObject {
    Q_OBJECT
public:
    explicit Logger(QObject* parent = nullptr);
    void addSecret(const QString& secret);
    void log(Severity severity, const QString& component, const QString& message);
    QString logDirectory() const;
signals:
    void entry(QString timestamp, int severity, QString component, QString message);
private:
    void rotate();
    mutable QMutex mutex_; QFile file_; SecretRedactor redactor_;
};
}

