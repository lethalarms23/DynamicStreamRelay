#pragma once
#include <QObject>
#include <QTimer>

namespace rtsp {
struct StreamStatistics {
    quint64 incomingPackets{0}, outgoingPackets{0}, droppedPackets{0};
    quint64 decodeErrors{0}, encodeErrors{0}, obsReconnects{0}, destinationReconnects{0};
    quint64 bufferBytes{0}; qint64 bufferDurationUs{0};
    double incomingVideoKbps{0}, incomingAudioKbps{0}, outgoingKbps{0};
};
class MetricsCollector final : public QObject {
    Q_OBJECT
public:
    explicit MetricsCollector(QObject* parent = nullptr);
    StreamStatistics snapshot() const;
    void packetIn(bool video, qsizetype bytes); void packetOut(qsizetype bytes);
    void setBuffer(quint64 bytes, qint64 durationUs);
    void addDropped(quint64 count = 1);
    void incrementDecodeErrors();
    void incrementEncodeErrors();
    void incrementObsReconnects();
    void incrementDestinationReconnects();
signals: void updated(rtsp::StreamStatistics stats);
private slots: void tick();
private:
    StreamStatistics stats_; quint64 videoBytes_{0}, audioBytes_{0}, outputBytes_{0}; QTimer timer_;
};
}
Q_DECLARE_METATYPE(rtsp::StreamStatistics)
