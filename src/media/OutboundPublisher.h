#pragma once
#include "app/ConfigurationManager.h"
#include "media/PacketBuffer.h"
#include <QObject>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>

namespace rtsp {
class OutboundPublisher final : public QObject {
    Q_OBJECT
public:
    explicit OutboundPublisher(PacketBuffer& buffer, QObject* parent = nullptr); ~OutboundPublisher() override;
    void start(QString destination, AppConfig profile);
    void requestStop();
    void waitForStop();
    void stop();
    bool active() const { return worker_.joinable(); }
    void setSourceConnected(bool connected) { sourceConnected_ = connected; }
    void setRequestedDelaySeconds(int seconds) { requestedDelayUs_ = static_cast<std::int64_t>(seconds) * 1000000; }
    void setDelayOverlayText(QString text);
signals:
    void connected(bool value); void packetWritten(qsizetype bytes); void reconnectAttempt(quint64 count);
    void encoderSelected(QString name);
    void decoderError(QString message);
    void encoderError(QString message);
    void health(QString detail);
    void sourceForwardingChanged(bool forwarding, QString detail);
    void relayPosition(qint64 inputHeadUs, qint64 sourceTimeUs);
    void delayApplied(qint64 effectiveDelayUs);
    void error(QString message);
private:
    void run(std::stop_token token, QString destination, AppConfig profile);
    bool publishSession(std::stop_token token, const QString& destination, const AppConfig& profile);
    PacketBuffer& buffer_; std::jthread worker_; std::atomic_bool interrupt_{false}, sourceConnected_{false};
    std::atomic<std::int64_t> requestedDelayUs_{0}; std::int64_t videoFrame_{0}, audioSample_{0};
    std::mutex overlayTextMutex_; QString delayOverlayText_;
    std::mutex workerExitMutex_; std::condition_variable workerExitCv_; bool workerExited_{true};
    std::atomic_bool gracefulStop_{false};
};
}
