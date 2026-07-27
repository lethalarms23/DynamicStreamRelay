#pragma once
#include <QString>
#include <QStringList>

namespace rtsp {
class SecretRedactor {
public:
    void addSecret(const QString& secret);
    QString redact(QString text) const;
private:
    QStringList secrets_;
};
}

