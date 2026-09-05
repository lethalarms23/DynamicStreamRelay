#pragma once

#include "app/ConfigurationManager.h"
#include "app/CredentialStore.h"
#include "app/RelaySession.h"
#include "ingest/MediaMTXManager.h"
#include "logging/Logger.h"
#include <QHash>
#include <QObject>
#include <QTimer>

namespace rtsp {
class ApplicationController final : public QObject {
    Q_OBJECT
public:
    explicit ApplicationController(QObject* parent = nullptr);
    ~ApplicationController() override;
    const AppConfig& config() const { return config_; }
    const QList<AppConfig>& profiles() const { return profiles_; }
    QString selectedProfileId() const { return selectedProfileId_; }
    QList<AppSnapshot> sessionSnapshots() const;
    QStringList availableVideoEncoders() const;
    QString ingestServerUrl() const;
    QString ingestFullUrl() const;
    QString internalIngestUrl() const;
    QString srtPassphrase() const;
    Logger* logger() { return &logger_; }
public slots:
    void startIngest();
    void restartIngest();
    void stopAll();
    void startRelay(QString serverUrl, QString streamKey, bool rememberKey = false, bool insecureStorage = false);
    void stopRelay();
    void startProfile(QString profileId);
    void stopProfile(QString profileId);
    void startAllProfiles();
    void stopAllRelays();
    void selectProfile(QString profileId);
    QString createProfile(QString name = {});
    QString duplicateProfile(QString profileId);
    bool removeProfile(QString profileId);
    void renameProfile(QString profileId, QString name);
    void loadDestinationCredential();
    void saveDestinationCredential(QString streamKey, bool insecureStorage);
    void configureDestination(QString serverUrl, QString streamKey, bool rememberKey);
    void setProfileAutoStart(bool enabled);
    void setVideoEncoder(QString encoder);
    void setOutputProfile(int width, int height, int fps, int videoBitrateKbps,
                          int audioBitrateKbps, int keyframeIntervalSeconds);
    void setFillerMode(QString mode);
    void setStandbyImage(QString path);
    void setDelayOverlayText(QString text);
    void setServicePreset(QString preset);
    void setInputProtocol(QString protocol);
    void setAdvertisedHost(QString host);
    void setSrtSettings(int port, int latencyMs);
    void setSrtEncryption(bool enabled);
    void regenerateSrtPassphrase();
    void saveSrtPassphraseCredential(bool insecureStorage);
    void setAllowLan(bool enabled);
    void setBufferLimits(int maximumDurationSeconds, int maximumMemoryMiB);
    void applyDelay(int seconds);
    bool cancelDelayIncrease();
    void regenerateLocalKey();
    // Profile-scoped variants: unlike applyDelay()/cancelDelayIncrease() above,
    // these act on an explicit profile instead of selectedProfileId_, so a
    // remote controller (the web panel) can act on any profile without
    // hijacking which profile is selected in the desktop UI.
    void applyDelayForProfile(QString profileId, int seconds);
    bool cancelDelayIncreaseForProfile(QString profileId);
signals:
    void snapshotChanged(rtsp::AppSnapshot snapshot);
    void sessionSnapshotsChanged(QList<rtsp::AppSnapshot> snapshots);
    void profilesChanged(QList<rtsp::AppConfig> profiles, QString selectedProfileId);
    void configChanged(rtsp::AppConfig config);
    void metricsChanged(rtsp::StreamStatistics stats);
    void confirmationRequired(QString message);
    void destinationCredentialLoaded(QString key, bool remembered, bool secure);
    void credentialStorageFailed(QString message, bool canUseInsecureFallback);
    void srtPassphraseChanged(QString passphrase, bool securelyStored);
    void srtCredentialStorageFailed(QString message, bool canUseInsecureFallback);
private:
    void createSession(const AppConfig& profile);
    void createCredentialStores(const AppConfig& profile);
    void credentialLoadFinished(bool srtCredential);
    void credentialsReady();
    void rebuildGateway();
    void scheduleSnapshotsUpdate();
    bool destinationUsedByAnotherProfile(const QString& profileId, const QString& serverUrl,
                                         const QString& streamKey) const;
    void saveProfiles();
    void updateSelectedConfig();
    AppConfig* profileById(const QString& id);
    const AppConfig* profileById(const QString& id) const;
    RelaySession* sessionById(const QString& id) const;
    QString advertisedIngestHost() const;
    QString ingestServerUrl(const AppConfig& profile) const;
    QString ingestFullUrl(const AppConfig& profile) const;
    ConfigurationManager settings_;
    QList<AppConfig> profiles_;
    QString selectedProfileId_;
    AppConfig config_;
    QHash<QString, RelaySession*> sessions_;
    QHash<QString, CredentialStore*> destinationStores_, srtStores_;
    QHash<QString, QString> destinationKeys_, srtPassphrases_;
    QHash<QString, bool> destinationRemembered_, destinationSecure_, srtSecure_;
    int pendingDestinationLoads_{0};
    int pendingSrtLoads_{0};
    bool credentialsInitialized_{false};
    bool snapshotsDirty_{false};
    QTimer snapshotsTimer_;
    MediaMTXManager mediamtx_;
    Logger logger_;
};
}
Q_DECLARE_METATYPE(rtsp::AppConfig)
