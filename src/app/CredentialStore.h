#pragma once
#include <QObject>
#include <QString>

namespace rtsp {
class CredentialStore final : public QObject {
    Q_OBJECT
public:
    explicit CredentialStore(QString entryName = QStringLiteral("destination-stream-key"), QObject* parent = nullptr);
    bool secureBackendCompiled() const;
    void load();
    void save(const QString& value, bool insecureFallback = false);
    void remove(bool insecureFallback = false);
signals:
    void loaded(QString value, bool remembered, bool secure);
    void saved(bool secure);
    void failed(QString message, bool canUseInsecureFallback);
private:
    QString fallbackSettingsKey() const;
    QString loadInsecureFallback() const;
    QString entryName_;
};
}
