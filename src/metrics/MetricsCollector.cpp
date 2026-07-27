#include "metrics/MetricsCollector.h"
namespace rtsp {
MetricsCollector::MetricsCollector(QObject* parent) : QObject(parent) { timer_.setInterval(1000); connect(&timer_, &QTimer::timeout, this, &MetricsCollector::tick); timer_.start(); }
StreamStatistics MetricsCollector::snapshot() const { return stats_; }
void MetricsCollector::packetIn(bool video, qsizetype n) { ++stats_.incomingPackets; (video ? videoBytes_ : audioBytes_) += static_cast<quint64>(n); }
void MetricsCollector::packetOut(qsizetype n) { ++stats_.outgoingPackets; outputBytes_ += static_cast<quint64>(n); }
void MetricsCollector::setBuffer(quint64 b, qint64 d) { stats_.bufferBytes = b; stats_.bufferDurationUs = d; }
void MetricsCollector::addDropped(quint64 count) { stats_.droppedPackets += count; }
void MetricsCollector::incrementDecodeErrors() { ++stats_.decodeErrors; }
void MetricsCollector::incrementEncodeErrors() { ++stats_.encodeErrors; }
void MetricsCollector::incrementObsReconnects() { ++stats_.obsReconnects; }
void MetricsCollector::incrementDestinationReconnects() { ++stats_.destinationReconnects; }
void MetricsCollector::tick() { stats_.incomingVideoKbps = videoBytes_ * 8.0 / 1000.0; stats_.incomingAudioKbps = audioBytes_ * 8.0 / 1000.0; stats_.outgoingKbps = outputBytes_ * 8.0 / 1000.0; videoBytes_ = audioBytes_ = outputBytes_ = 0; emit updated(stats_); }
}
