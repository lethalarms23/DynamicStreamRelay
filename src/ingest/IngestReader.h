#pragma once
#include "ingest/TimestampNormalizer.h"
#include "media/PacketBuffer.h"
#include <QObject>
#include <atomic>
#include <thread>

namespace rtsp {
class IngestReader final : public QObject {
    Q_OBJECT
public:
    explicit IngestReader(PacketBuffer& buffer, QObject* parent = nullptr);
    ~IngestReader() override;
    void start(QString url); void stop();
signals:
    void connected(bool value); void packetReceived(bool video, qsizetype bytes);
    void codecInfo(QString description); void error(QString message);
private:
    void run(std::stop_token stop, QString url);
    PacketBuffer& buffer_; std::jthread worker_; std::atomic_bool interrupt_{false};
    TimestampNormalizer normalizer_;
    std::uint64_t nextSessionId_{0};
    std::uint64_t nextCodecGeneration_{0};
};
}
