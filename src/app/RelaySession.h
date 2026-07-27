#pragma once

#include "app/ApplicationState.h"
#include "app/ConfigurationManager.h"
#include "ingest/IngestReader.h"
#include "logging/Logger.h"
#include "media/DelayController.h"
#include "media/OutboundPublisher.h"
#include "media/PacketBuffer.h"
#include "metrics/MetricsCollector.h"
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

namespace rtsp {
struct AppSnapshot {
    QString profileId;
    ApplicationState state{ApplicationState::Stopped};
    bool ingestRunning{false}, sourceConnected{false}, destinationConnected{false};
    qint64 requestedDelayUs{0}, effectiveDelayUs{0}, bufferDurationUs{0}, remainingFillerUs{0};
    quint64 bufferBytes{0};
    qint64 uptimeSeconds{0};
    QString activeEncoder;
};

class RelaySession final : public QObject {
    Q_OBJECT
public:
    RelaySession(AppConfig profile, Logger& logger, QObject* parent = nullptr);
    ~RelaySession() override;
    const AppConfig& profile() const { return profile_; }
    AppSnapshot snapshot() const;
    StreamStatistics statistics() const { return metrics_.snapshot(); }
    bool active() const { return publisher_.active(); }
    void updateProfile(const AppConfig& profile);
    void setGatewayRunning(bool running);
    void startIngest();
    void stopIngest();
    void startRelay(const QString& destination);
    void requestRelayStop();
    void finishRelayStop();
    void finishStopAll();
    void stopRelay();
    void stopAll();
    void applyDelay(int seconds);
    bool cancelDelayIncrease();
signals:
    void snapshotChanged(rtsp::AppSnapshot snapshot);
    void metricsChanged(QString profileId, rtsp::StreamStatistics statistics);
    void encoderSelected(QString profileId, QString encoder);
    void reconnectAttempt(QString profileId, quint64 attempt);
private:
    void setState(ApplicationState state);
    void updateSnapshot();
    void updateBufferStoragePolicy();
    QString internalIngestUrl() const;
    AppConfig profile_;
    Logger& logger_;
    PacketBuffer buffer_;
    DelayController delay_;
    IngestReader ingest_;
    OutboundPublisher publisher_;
    MetricsCollector metrics_;
    ApplicationState state_{ApplicationState::Stopped};
    bool gatewayRunning_{false}, sourceConnected_{false}, destinationConnected_{false};
    bool sourcePreviouslyConnected_{false};
    QString activeEncoder_;
    quint64 lastOverflowCount_{0};
    QElapsedTimer uptime_;
    QTimer updateTimer_;
    qint64 lastUpdateMs_{0};
};
}
Q_DECLARE_METATYPE(rtsp::AppSnapshot)
