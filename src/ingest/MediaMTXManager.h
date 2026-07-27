#pragma once
#include "app/ConfigurationManager.h"
#include <QObject>
#include <QProcess>

namespace rtsp {
struct IngestPath {
    QString path;
    QString srtPassphrase;
};
class MediaMTXManager final : public QObject {
    Q_OBJECT
public:
    explicit MediaMTXManager(QObject* parent = nullptr);
    void start(const AppConfig& gateway, const QList<IngestPath>& paths); void stop(); bool running() const;
signals:
    void runningChanged(bool running); void publisherChanged(bool connected);
    void logLine(QString line); void error(QString message);
private slots:
    void consumeOutput();
private:
    QString findExecutable() const; QString createConfig(const AppConfig& gateway, const QList<IngestPath>& paths);
    QProcess process_; QString configPath_; bool publisher_{false};
};
}
