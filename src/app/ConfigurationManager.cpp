#include "app/ConfigurationManager.h"
#include <QRandomGenerator>
#include <QSettings>
#include <QUuid>

namespace rtsp {
ConfigurationManager::ConfigurationManager(QObject* parent) : QObject(parent) {}

QString ConfigurationManager::generateStreamKey() {
    QByteArray bytes(24, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(bytes.data()), 6);
    return QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}
QString ConfigurationManager::generateProfileId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

AppConfig ConfigurationManager::load() const {
    QSettings s;
    AppConfig c;
    c.profileId = s.value("profile/id").toString();
    if (c.profileId.isEmpty()) c.profileId = generateProfileId();
    c.profileName = s.value("profile/name", c.profileName).toString();
    c.profileColor = s.value("profile/color", c.profileColor).toString();
    c.inputProtocol = s.value("ingest/protocol", c.inputProtocol).toString().toLower();
    if (c.inputProtocol != "rtmp" && c.inputProtocol != "srt") c.inputProtocol = "rtmp";
    c.listenAddress = s.value("ingest/address", c.listenAddress).toString();
    c.advertisedHost = s.value("ingest/advertisedHost", c.advertisedHost).toString();
    c.rtmpPort = static_cast<quint16>(s.value("ingest/port", c.rtmpPort).toUInt());
    c.srtPort = static_cast<quint16>(s.value("ingest/srtPort", c.srtPort).toUInt());
    c.srtLatencyMs = s.value("ingest/srtLatencyMs", c.srtLatencyMs).toInt();
    c.srtEncryption = s.value("ingest/srtEncryption", c.srtEncryption).toBool();
    c.applicationName = s.value("ingest/application", c.applicationName).toString();
    c.localStreamKey = s.value("ingest/key").toString();
    if (c.localStreamKey.isEmpty()) c.localStreamKey = generateStreamKey();
    c.destinationUrl = s.value("destination/url").toString();
    c.servicePreset = s.value("destination/servicePreset", c.servicePreset).toString();
    c.requestedDelaySeconds = s.value("delay/requested", c.requestedDelaySeconds).toInt();
    c.maximumDelaySeconds = s.value("buffer/maxDuration", c.maximumDelaySeconds).toInt();
    c.maximumBufferMiB = s.value("buffer/maxMemoryMiB", c.maximumBufferMiB).toInt();
    c.width = s.value("output/width", c.width).toInt(); c.height = s.value("output/height", c.height).toInt();
    c.fps = s.value("output/fps", c.fps).toInt(); c.videoBitrateKbps = s.value("output/videoKbps", c.videoBitrateKbps).toInt();
    c.audioBitrateKbps = s.value("output/audioKbps", c.audioBitrateKbps).toInt();
    c.videoEncoder = s.value("output/videoEncoder", c.videoEncoder).toString();
    c.fillerMode = s.value("output/fillerMode", c.fillerMode).toString();
    c.standbyImagePath = s.value("output/standbyImagePath", c.standbyImagePath).toString();
    c.delayOverlayText = s.value("output/delayOverlayText", c.delayOverlayText).toString();
    c.autoStartIngest = s.value("startup/ingest", c.autoStartIngest).toBool();
    c.autoStartRelay = s.value("startup/relay", c.autoStartRelay).toBool();
    return c;
}

void ConfigurationManager::save(const AppConfig& c) const {
    QSettings s;
    s.setValue("profile/id", c.profileId); s.setValue("profile/name", c.profileName);
    s.setValue("profile/color", c.profileColor);
    s.setValue("ingest/protocol", c.inputProtocol);
    s.setValue("ingest/address", c.listenAddress); s.setValue("ingest/port", c.rtmpPort);
    s.setValue("ingest/advertisedHost", c.advertisedHost);
    s.setValue("ingest/srtPort", c.srtPort); s.setValue("ingest/srtLatencyMs", c.srtLatencyMs);
    s.setValue("ingest/srtEncryption", c.srtEncryption);
    s.setValue("ingest/application", c.applicationName); s.setValue("ingest/key", c.localStreamKey);
    s.setValue("destination/url", c.destinationUrl); s.setValue("destination/servicePreset", c.servicePreset); // destination key is deliberately excluded
    s.setValue("delay/requested", c.requestedDelaySeconds);
    s.setValue("buffer/maxDuration", c.maximumDelaySeconds); s.setValue("buffer/maxMemoryMiB", c.maximumBufferMiB);
    s.setValue("output/width", c.width); s.setValue("output/height", c.height); s.setValue("output/fps", c.fps);
    s.setValue("output/videoKbps", c.videoBitrateKbps); s.setValue("output/audioKbps", c.audioBitrateKbps);
    s.setValue("output/videoEncoder", c.videoEncoder);
    s.setValue("output/fillerMode", c.fillerMode); s.setValue("output/standbyImagePath", c.standbyImagePath);
    s.setValue("output/delayOverlayText", c.delayOverlayText);
    s.setValue("startup/ingest", c.autoStartIngest); s.setValue("startup/relay", c.autoStartRelay);
}

QList<AppConfig> ConfigurationManager::loadProfiles() const {
    QSettings s;
    QList<AppConfig> profiles;
    const int count = s.beginReadArray("profiles");
    for (int i = 0; i < count; ++i) {
        s.setArrayIndex(i);
        AppConfig c;
        c.profileId = s.value("id").toString();
        if (c.profileId.isEmpty()) continue;
        c.profileName = s.value("name", QString("Stream %1").arg(i + 1)).toString();
        c.profileColor = s.value("color", c.profileColor).toString();
        c.inputProtocol = s.value("inputProtocol", c.inputProtocol).toString();
        c.advertisedHost = s.value("advertisedHost").toString();
        c.localStreamKey = s.value("localStreamKey").toString();
        if (c.localStreamKey.isEmpty()) c.localStreamKey = generateStreamKey();
        c.srtLatencyMs = s.value("srtLatencyMs", c.srtLatencyMs).toInt();
        c.srtEncryption = s.value("srtEncryption", c.srtEncryption).toBool();
        c.destinationUrl = s.value("destinationUrl").toString();
        c.servicePreset = s.value("servicePreset", c.servicePreset).toString();
        c.requestedDelaySeconds = s.value("requestedDelaySeconds", c.requestedDelaySeconds).toInt();
        c.maximumDelaySeconds = s.value("maximumDelaySeconds", c.maximumDelaySeconds).toInt();
        c.maximumBufferMiB = s.value("maximumBufferMiB", c.maximumBufferMiB).toInt();
        c.width = s.value("width", c.width).toInt(); c.height = s.value("height", c.height).toInt();
        c.fps = s.value("fps", c.fps).toInt(); c.videoBitrateKbps = s.value("videoBitrateKbps", c.videoBitrateKbps).toInt();
        c.audioBitrateKbps = s.value("audioBitrateKbps", c.audioBitrateKbps).toInt();
        c.audioSampleRate = s.value("audioSampleRate", c.audioSampleRate).toInt();
        c.keyframeIntervalSeconds = s.value("keyframeIntervalSeconds", c.keyframeIntervalSeconds).toInt();
        c.videoEncoder = s.value("videoEncoder", c.videoEncoder).toString();
        c.fillerMode = s.value("fillerMode", c.fillerMode).toString();
        c.standbyImagePath = s.value("standbyImagePath", c.standbyImagePath).toString();
        c.delayOverlayText = s.value("delayOverlayText", c.delayOverlayText).toString();
        c.autoStartRelay = s.value("autoStartRelay", c.autoStartRelay).toBool();
        profiles.push_back(c);
    }
    s.endArray();
    if (profiles.isEmpty()) {
        auto legacy = load();
        if (legacy.profileId.isEmpty()) legacy.profileId = generateProfileId();
        if (legacy.profileName.isEmpty()) legacy.profileName = "Default";
        profiles.push_back(legacy);
        saveProfiles(profiles);
        setSelectedProfileId(legacy.profileId);
    }
    const auto gateway = load();
    for (auto& c : profiles) {
        c.listenAddress = gateway.listenAddress;
        if (c.advertisedHost.isEmpty() && profiles.size() == 1) c.advertisedHost = gateway.advertisedHost;
        c.rtmpPort = gateway.rtmpPort; c.srtPort = gateway.srtPort;
        c.applicationName = gateway.applicationName; c.autoStartIngest = gateway.autoStartIngest;
    }
    return profiles;
}

void ConfigurationManager::saveProfiles(const QList<AppConfig>& profiles) const {
    QSettings s;
    s.beginWriteArray("profiles", profiles.size());
    for (int i = 0; i < profiles.size(); ++i) {
        s.setArrayIndex(i); const auto& c = profiles[i];
        s.setValue("id", c.profileId); s.setValue("name", c.profileName); s.setValue("color", c.profileColor);
        s.setValue("inputProtocol", c.inputProtocol); s.setValue("advertisedHost", c.advertisedHost);
        s.setValue("localStreamKey", c.localStreamKey);
        s.setValue("srtLatencyMs", c.srtLatencyMs); s.setValue("srtEncryption", c.srtEncryption);
        s.setValue("destinationUrl", c.destinationUrl); s.setValue("servicePreset", c.servicePreset);
        s.setValue("requestedDelaySeconds", c.requestedDelaySeconds);
        s.setValue("maximumDelaySeconds", c.maximumDelaySeconds); s.setValue("maximumBufferMiB", c.maximumBufferMiB);
        s.setValue("width", c.width); s.setValue("height", c.height); s.setValue("fps", c.fps);
        s.setValue("videoBitrateKbps", c.videoBitrateKbps); s.setValue("audioBitrateKbps", c.audioBitrateKbps);
        s.setValue("audioSampleRate", c.audioSampleRate); s.setValue("keyframeIntervalSeconds", c.keyframeIntervalSeconds);
        s.setValue("videoEncoder", c.videoEncoder); s.setValue("fillerMode", c.fillerMode);
        s.setValue("standbyImagePath", c.standbyImagePath); s.setValue("delayOverlayText", c.delayOverlayText);
        s.setValue("autoStartRelay", c.autoStartRelay);
    }
    s.endArray();
    if (!profiles.isEmpty()) save(profiles.front());
}

QString ConfigurationManager::selectedProfileId() const {
    return QSettings().value("profiles/selectedId").toString();
}
void ConfigurationManager::setSelectedProfileId(const QString& id) const {
    QSettings().setValue("profiles/selectedId", id);
}
}
