#pragma once

#include <QObject>
#include <QList>
#include <QString>

namespace rtsp {
struct AppConfig {
    QString profileId;
    QString profileName{"Default"};
    QString profileColor{"#3977d5"};
    QString inputProtocol{"rtmp"};
    QString listenAddress{"127.0.0.1"};
    QString advertisedHost;
    quint16 rtmpPort{1935};
    quint16 srtPort{8890};
    int srtLatencyMs{2000};
    bool srtEncryption{true};
    QString applicationName{"live"};
    QString localStreamKey;
    QString destinationUrl;
    QString servicePreset{"Custom"};
    int requestedDelaySeconds{0};
    int maximumDelaySeconds{300};
    int maximumBufferMiB{384};
    int width{1920}; int height{1080}; int fps{60};
    int videoBitrateKbps{6000}; int audioBitrateKbps{160};
    int audioSampleRate{48000}; int keyframeIntervalSeconds{2};
    QString videoEncoder{"auto"};
    QString fillerMode{"hold"};
    QString standbyImagePath;
    QString delayOverlayText;
    bool autoStartIngest{true}; bool autoStartRelay{false};
};

class ConfigurationManager final : public QObject {
    Q_OBJECT
public:
    explicit ConfigurationManager(QObject* parent = nullptr);
    AppConfig load() const;
    void save(const AppConfig& config) const;
    QList<AppConfig> loadProfiles() const;
    void saveProfiles(const QList<AppConfig>& profiles) const;
    QString selectedProfileId() const;
    void setSelectedProfileId(const QString& id) const;
    static QString generateStreamKey();
    static QString generateProfileId();
};
}
