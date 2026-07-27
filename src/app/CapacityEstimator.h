#pragma once

#include <QObject>
#include <QString>
#include <atomic>
#include <functional>
#include <thread>

namespace rtsp {
struct CapacitySettings {
    QString encoder{"auto"};
    int width{1920}, height{1080}, fps{60};
    int videoBitrateKbps{6000};
    int bufferMiB{384};
    int maximumCandidates{8};
    int benchmarkSeconds{2};
    int safetyPercent{65};
};
struct CapacityResult {
    QString requestedEncoder;
    QString actualEncoder;
    int safeStreams{0};
    int encoderOpenLimit{0};
    int throughputLimit{0};
    int memoryLimit{0};
    double measuredFps{0};
    quint64 availableMemoryMiB{0};
    QString detail;
};
class CapacityEstimator final : public QObject {
    Q_OBJECT
public:
    explicit CapacityEstimator(QObject* parent = nullptr);
    ~CapacityEstimator() override;
    bool busy() const { return busy_; }
    void start(CapacitySettings settings);
    void cancel();
signals:
    void progress(QString message);
    void completed(rtsp::CapacityResult result);
private:
    static CapacityResult benchmark(const CapacitySettings& settings, std::stop_token token,
                                    const std::function<void(QString)>& progress);
    std::jthread worker_;
    std::atomic_bool busy_{false};
};
}
Q_DECLARE_METATYPE(rtsp::CapacitySettings)
Q_DECLARE_METATYPE(rtsp::CapacityResult)
