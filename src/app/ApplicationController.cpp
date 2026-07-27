#include "app/ApplicationController.h"
#include "media/UrlUtils.h"
#include <QAbstractSocket>
#include <QNetworkInterface>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <memory>
extern "C" {
#include <libavcodec/avcodec.h>
}

namespace rtsp {
ApplicationController::ApplicationController(QObject* parent)
    : QObject(parent), profiles_(settings_.loadProfiles()) {
    qRegisterMetaType<AppSnapshot>();
    qRegisterMetaType<AppConfig>();
    qRegisterMetaType<StreamStatistics>();
    selectedProfileId_ = settings_.selectedProfileId();
    if (!profileById(selectedProfileId_) && !profiles_.isEmpty())
        selectedProfileId_ = profiles_.front().profileId;
    updateSelectedConfig();
    snapshotsTimer_.setInterval(500);
    connect(&snapshotsTimer_, &QTimer::timeout, this, [this] {
        if (!snapshotsDirty_) return;
        snapshotsDirty_ = false;
        emit sessionSnapshotsChanged(sessionSnapshots());
    });
    snapshotsTimer_.start();
    pendingDestinationLoads_ = profiles_.size();
    pendingSrtLoads_ = profiles_.size();
    for (const auto& profile : profiles_) {
        logger_.addSecret(profile.localStreamKey);
        createSession(profile);
        createCredentialStores(profile);
    }
    QSettings migrationSettings;
    if (profiles_.size()==1&&!migrationSettings.value("profiles/legacyCredentialsMigrated",false).toBool()) {
        const auto id=profiles_.front().profileId;
        auto* legacyDestination=new CredentialStore("destination-stream-key",this);
        auto* legacySrt=new CredentialStore("srt-passphrase",this);
        auto completed=std::make_shared<int>(0);
        auto finishMigration=[completed,legacyDestination,legacySrt] {
            if(++*completed==2){
                QSettings().setValue("profiles/legacyCredentialsMigrated",true);
                legacyDestination->deleteLater();legacySrt->deleteLater();
            }
        };
        connect(legacyDestination,&CredentialStore::loaded,this,[this,id,finishMigration](const QString& value,bool remembered,bool secure){
            if(!value.isEmpty()){
                destinationKeys_[id]=value;destinationRemembered_[id]=remembered;destinationSecure_[id]=secure;
                destinationStores_[id]->save(value);logger_.addSecret(value);
                if(id==selectedProfileId_)emit destinationCredentialLoaded(value,true,secure);
            }
            finishMigration();
        });
        connect(legacyDestination,&CredentialStore::failed,this,[finishMigration](const QString&,bool){finishMigration();});
        connect(legacySrt,&CredentialStore::loaded,this,[this,id,finishMigration](const QString& value,bool,bool secure){
            if(value.size()>=10){
                srtPassphrases_[id]=value;srtSecure_[id]=secure;srtStores_[id]->save(value);logger_.addSecret(value);
                if(id==selectedProfileId_)emit srtPassphraseChanged(value,secure);
                if(credentialsInitialized_)rebuildGateway();
            }
            finishMigration();
        });
        connect(legacySrt,&CredentialStore::failed,this,[finishMigration](const QString&,bool){finishMigration();});
        legacyDestination->load();legacySrt->load();
    }
    connect(&mediamtx_, &MediaMTXManager::runningChanged, this, [this](bool running) {
        for (auto* session : sessions_) session->setGatewayRunning(running);
        scheduleSnapshotsUpdate();
    });
    connect(&mediamtx_, &MediaMTXManager::logLine, this,
        [this](const QString& line) { logger_.log(Severity::Info, "MediaMTX", line); });
    connect(&mediamtx_, &MediaMTXManager::error, this,
        [this](const QString& message) { logger_.log(Severity::Error, "MediaMTX", message); });
    emit profilesChanged(profiles_, selectedProfileId_);
    if (pendingDestinationLoads_ == 0 && pendingSrtLoads_ == 0) credentialsReady();
}

ApplicationController::~ApplicationController() {
    stopAll();
    saveProfiles();
}

AppConfig* ApplicationController::profileById(const QString& id) {
    for (auto& profile : profiles_) if (profile.profileId == id) return &profile;
    return nullptr;
}
const AppConfig* ApplicationController::profileById(const QString& id) const {
    for (const auto& profile : profiles_) if (profile.profileId == id) return &profile;
    return nullptr;
}
RelaySession* ApplicationController::sessionById(const QString& id) const {
    return sessions_.value(id, nullptr);
}

void ApplicationController::createSession(const AppConfig& profile) {
    auto* session = new RelaySession(profile, logger_, this);
    sessions_.insert(profile.profileId, session);
    if (mediamtx_.running()) session->setGatewayRunning(true);
    connect(session, &RelaySession::snapshotChanged, this, [this](const AppSnapshot& snapshot) {
        if (snapshot.profileId == selectedProfileId_) emit snapshotChanged(snapshot);
        scheduleSnapshotsUpdate();
    });
    connect(session, &RelaySession::metricsChanged, this,
        [this](const QString& id, const StreamStatistics& statistics) {
            if (id == selectedProfileId_) emit metricsChanged(statistics);
        });
}

void ApplicationController::createCredentialStores(const AppConfig& profile) {
    auto* destination = new CredentialStore("profile-" + profile.profileId + "-destination", this);
    auto* srt = new CredentialStore("profile-" + profile.profileId + "-srt", this);
    destinationStores_.insert(profile.profileId, destination);
    srtStores_.insert(profile.profileId, srt);
    connect(destination, &CredentialStore::loaded, this,
        [this, id=profile.profileId](const QString& value, bool remembered, bool secure) {
            destinationKeys_[id] = value; destinationRemembered_[id] = remembered; destinationSecure_[id] = secure;
            if (id == selectedProfileId_) emit destinationCredentialLoaded(value, remembered, secure);
            credentialLoadFinished(false);
        });
    connect(destination, &CredentialStore::failed, this,
        [this, id=profile.profileId](const QString& message, bool fallback) {
            if (id == selectedProfileId_) emit credentialStorageFailed(message, fallback);
            credentialLoadFinished(false);
        });
    connect(srt, &CredentialStore::loaded, this,
        [this, id=profile.profileId, srt](const QString& value, bool, bool secure) {
            QString passphrase = value;
            if (passphrase.size() < 10) {
                passphrase = ConfigurationManager::generateStreamKey();
                if (srt->secureBackendCompiled()) srt->save(passphrase);
            }
            srtPassphrases_[id] = passphrase; srtSecure_[id] = secure || srt->secureBackendCompiled();
            logger_.addSecret(passphrase);
            if (id == selectedProfileId_) emit srtPassphraseChanged(passphrase, srtSecure_[id]);
            credentialLoadFinished(true);
        });
    connect(srt, &CredentialStore::failed, this,
        [this, id=profile.profileId](const QString& message, bool fallback) {
            srtPassphrases_[id] = ConfigurationManager::generateStreamKey();
            logger_.addSecret(srtPassphrases_[id]);
            if (id == selectedProfileId_) emit srtCredentialStorageFailed(message, fallback);
            credentialLoadFinished(true);
        });
    destination->load();
    srt->load();
}

void ApplicationController::credentialLoadFinished(bool srtCredential) {
    auto& pending = srtCredential ? pendingSrtLoads_ : pendingDestinationLoads_;
    if (pending > 0) --pending;
    if (pendingDestinationLoads_ == 0 && pendingSrtLoads_ == 0) {
        if (!credentialsInitialized_) credentialsReady();
        else if (srtCredential) rebuildGateway();
    } else if (credentialsInitialized_ && srtCredential) {
        rebuildGateway();
    }
}

void ApplicationController::credentialsReady() {
    if (credentialsInitialized_) return;
    credentialsInitialized_ = true;
    if (config_.autoStartIngest) QTimer::singleShot(0, this, &ApplicationController::startIngest);
    QTimer::singleShot(0, this, [this] {
        for (const auto& profile : profiles_) if (profile.autoStartRelay) startProfile(profile.profileId);
    });
}

void ApplicationController::scheduleSnapshotsUpdate() {
    snapshotsDirty_ = true;
}

void ApplicationController::saveProfiles() {
    settings_.saveProfiles(profiles_);
    settings_.setSelectedProfileId(selectedProfileId_);
}

void ApplicationController::updateSelectedConfig() {
    if (const auto* profile = profileById(selectedProfileId_)) config_ = *profile;
}

QList<AppSnapshot> ApplicationController::sessionSnapshots() const {
    QList<AppSnapshot> result;
    for (const auto& profile : profiles_)
        if (auto* session = sessionById(profile.profileId)) result.push_back(session->snapshot());
    return result;
}

QStringList ApplicationController::availableVideoEncoders() const {
    QStringList result{"auto"};
    for (const char* name : {"h264_nvenc", "h264_qsv", "h264_amf", "libx264"})
        if (avcodec_find_encoder_by_name(name)) result.push_back(QString::fromLatin1(name));
    return result;
}

QString ApplicationController::advertisedIngestHost() const {
    if (!config_.advertisedHost.trimmed().isEmpty()) return config_.advertisedHost.trimmed();
    QString displayAddress = config_.listenAddress;
    if (displayAddress == "0.0.0.0") {
        for (const auto& interface : QNetworkInterface::allInterfaces()) {
            if (!(interface.flags() & QNetworkInterface::IsUp)
                || !(interface.flags() & QNetworkInterface::IsRunning)
                || (interface.flags() & QNetworkInterface::IsLoopBack)) continue;
            for (const auto& entry : interface.addressEntries()) {
                if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol && !entry.ip().isLoopback()) {
                    displayAddress = entry.ip().toString(); break;
                }
            }
            if (displayAddress != "0.0.0.0") break;
        }
    }
    return displayAddress;
}

QString ApplicationController::ingestServerUrl(const AppConfig& profile) const {
    QUrl url; url.setScheme(profile.inputProtocol); url.setHost(advertisedIngestHost());
    url.setPort(profile.inputProtocol == "srt" ? profile.srtPort : profile.rtmpPort);
    if (profile.inputProtocol == "rtmp") url.setPath('/' + profile.applicationName);
    return url.toString(QUrl::FullyEncoded);
}
QString ApplicationController::ingestFullUrl(const AppConfig& profile) const {
    if (profile.inputProtocol == "rtmp") return ingestServerUrl(profile) + '/' + profile.localStreamKey;
    QUrl url(ingestServerUrl(profile)); QUrlQuery query;
    query.addQueryItem("streamid", "publish:" + profile.applicationName + '/' + profile.localStreamKey);
    query.addQueryItem("pkt_size", "1316");
    query.addQueryItem("latency", QString::number(static_cast<qint64>(profile.srtLatencyMs) * 1000));
    const auto passphrase = srtPassphrases_.value(profile.profileId);
    if (profile.srtEncryption && !passphrase.isEmpty()) {
        query.addQueryItem("passphrase", passphrase); query.addQueryItem("pbkeylen", "32");
    }
    url.setQuery(query); return url.toString(QUrl::FullyEncoded);
}
QString ApplicationController::ingestServerUrl() const { return ingestServerUrl(config_); }
QString ApplicationController::ingestFullUrl() const { return ingestFullUrl(config_); }
QString ApplicationController::internalIngestUrl() const {
    return QString("rtmp://127.0.0.1:%1/%2/%3").arg(config_.rtmpPort)
        .arg(config_.applicationName, config_.localStreamKey);
}
QString ApplicationController::srtPassphrase() const { return srtPassphrases_.value(selectedProfileId_); }

void ApplicationController::rebuildGateway() {
    if (!credentialsInitialized_) return;
    QList<IngestPath> paths;
    for (const auto& profile : profiles_) {
        const auto passphrase = srtPassphrases_.value(profile.profileId);
        // Never publish an encrypted profile as an unencrypted path while its
        // passphrase is still being loaded from the platform keychain.
        if (profile.srtEncryption && passphrase.isEmpty()) continue;
        paths.push_back({profile.applicationName + '/' + profile.localStreamKey,
            profile.srtEncryption ? passphrase : QString{}});
    }
    mediamtx_.start(config_, paths);
}
void ApplicationController::startIngest() { rebuildGateway(); }
void ApplicationController::restartIngest() { rebuildGateway(); }

void ApplicationController::startRelay(QString url, QString key, bool remember, bool insecure) {
    const auto validation = validateRtmpUrl(url);
    if (!validation.valid || key.trimmed().isEmpty()) {
        logger_.log(Severity::Error, config_.profileName,
            validation.valid ? "A destination stream key is required." : validation.error);
        return;
    }
    if (auto* session = sessionById(selectedProfileId_); session && session->active()) {
        logger_.log(Severity::Warning, config_.profileName, "This relay is already active.");
        return;
    }
    if (destinationUsedByAnotherProfile(selectedProfileId_, url, key)) {
        logger_.log(Severity::Error, config_.profileName,
            "Another active profile is already publishing to this destination.");
        return;
    }
    if (auto* profile = profileById(selectedProfileId_)) {
        profile->destinationUrl = url; config_ = *profile; destinationKeys_[selectedProfileId_] = key;
        logger_.addSecret(key);
        if (remember) destinationStores_[selectedProfileId_]->save(key, insecure);
        else destinationStores_[selectedProfileId_]->remove(insecure);
        saveProfiles();
    }
    if (auto* session = sessionById(selectedProfileId_))
        session->startRelay(joinDestination(url, key));
}
void ApplicationController::stopRelay() { stopProfile(selectedProfileId_); }
void ApplicationController::startProfile(QString id) {
    const auto* profile = profileById(id); auto* session = sessionById(id);
    const auto key = destinationKeys_.value(id);
    if (!profile || !session || profile->destinationUrl.isEmpty() || key.isEmpty()) {
        logger_.log(Severity::Error, profile ? profile->profileName : "Profile",
                    "Destination URL and saved stream key are required.");
        return;
    }
    if (session->active()) {
        logger_.log(Severity::Warning, profile->profileName, "This relay is already active.");
        return;
    }
    if (destinationUsedByAnotherProfile(id, profile->destinationUrl, key)) {
        logger_.log(Severity::Error, profile->profileName,
            "Another active profile is already publishing to this destination.");
        return;
    }
    session->startRelay(joinDestination(profile->destinationUrl, key));
}
void ApplicationController::stopProfile(QString id) { if (auto* session = sessionById(id)) session->stopRelay(); }
void ApplicationController::startAllProfiles() { for (const auto& p : profiles_) startProfile(p.profileId); }
void ApplicationController::stopAllRelays() {
    for (auto* session : sessions_) session->requestRelayStop();
    for (auto* session : sessions_) session->finishRelayStop();
}

void ApplicationController::selectProfile(QString id) {
    if (!profileById(id) || id == selectedProfileId_) return;
    selectedProfileId_ = id; updateSelectedConfig(); settings_.setSelectedProfileId(id);
    emit configChanged(config_);
    emit profilesChanged(profiles_, selectedProfileId_);
    emit destinationCredentialLoaded(destinationKeys_.value(id), destinationRemembered_.value(id), destinationSecure_.value(id));
    emit srtPassphraseChanged(srtPassphrases_.value(id), srtSecure_.value(id));
    if (auto* session = sessionById(id)) {
        emit snapshotChanged(session->snapshot());
        emit metricsChanged(session->statistics());
    }
}

QString ApplicationController::createProfile(QString name) {
    AppConfig profile = config_;
    profile.profileId = ConfigurationManager::generateProfileId();
    profile.profileName = name.trimmed().isEmpty() ? QString("Stream %1").arg(profiles_.size() + 1) : name.trimmed();
    profile.localStreamKey = ConfigurationManager::generateStreamKey();
    profile.destinationUrl.clear(); profile.autoStartRelay = false;
    profiles_.push_back(profile); logger_.addSecret(profile.localStreamKey);
    ++pendingDestinationLoads_; ++pendingSrtLoads_;
    createSession(profile); createCredentialStores(profile); saveProfiles(); rebuildGateway();
    emit profilesChanged(profiles_, selectedProfileId_);
    return profile.profileId;
}
QString ApplicationController::duplicateProfile(QString id) {
    const auto* source = profileById(id); if (!source) return {};
    AppConfig copy = *source; copy.profileId = ConfigurationManager::generateProfileId();
    copy.profileName += " Copy"; copy.localStreamKey = ConfigurationManager::generateStreamKey();
    copy.autoStartRelay = false;
    profiles_.push_back(copy); ++pendingDestinationLoads_; ++pendingSrtLoads_;
    createSession(copy); createCredentialStores(copy);
    saveProfiles(); rebuildGateway(); emit profilesChanged(profiles_, selectedProfileId_);
    return copy.profileId;
}
bool ApplicationController::removeProfile(QString id) {
    if (profiles_.size() <= 1) return false;
    auto* session = sessionById(id); if (session && session->active()) return false;
    for (qsizetype i = 0; i < profiles_.size(); ++i) if (profiles_[i].profileId == id) { profiles_.removeAt(i); break; }
    if (session) { sessions_.remove(id); session->deleteLater(); }
    const auto removeStore = [](CredentialStore* store) {
        if (!store) return;
        QObject::connect(store, &CredentialStore::saved, store, [store](bool) { store->deleteLater(); });
        QObject::connect(store, &CredentialStore::failed, store,
            [store](const QString&, bool) { store->deleteLater(); });
        store->remove();
    };
    removeStore(destinationStores_.value(id));
    removeStore(srtStores_.value(id));
    destinationStores_.remove(id); srtStores_.remove(id);
    destinationKeys_.remove(id); srtPassphrases_.remove(id);
    if (selectedProfileId_ == id) { selectedProfileId_ = profiles_.front().profileId; updateSelectedConfig(); emit configChanged(config_); }
    saveProfiles(); rebuildGateway(); emit profilesChanged(profiles_, selectedProfileId_); return true;
}
void ApplicationController::renameProfile(QString id, QString name) {
    if (auto* profile = profileById(id); profile && !name.trimmed().isEmpty()) {
        profile->profileName = name.trimmed(); if (id == selectedProfileId_) config_ = *profile;
        if (auto* session = sessionById(id)) session->updateProfile(*profile);
        saveProfiles(); emit profilesChanged(profiles_, selectedProfileId_); emit configChanged(config_);
    }
}

void ApplicationController::loadDestinationCredential() {
    emit destinationCredentialLoaded(destinationKeys_.value(selectedProfileId_),
        destinationRemembered_.value(selectedProfileId_), destinationSecure_.value(selectedProfileId_));
}
void ApplicationController::saveDestinationCredential(QString key, bool insecure) {
    destinationKeys_[selectedProfileId_] = key; logger_.addSecret(key);
    destinationStores_[selectedProfileId_]->save(key, insecure);
}
void ApplicationController::configureDestination(QString url, QString key, bool remember) {
    if (auto* profile=profileById(selectedProfileId_)) {
        profile->destinationUrl=url.trimmed();
        destinationKeys_[selectedProfileId_]=key;
        destinationRemembered_[selectedProfileId_]=remember;
        logger_.addSecret(key);
        if(remember&&!key.isEmpty())destinationStores_[selectedProfileId_]->save(key);
        else if(!remember)destinationStores_[selectedProfileId_]->remove();
        updateSelectedConfig();saveProfiles();
        emit configChanged(config_);emit profilesChanged(profiles_,selectedProfileId_);
    }
}
void ApplicationController::setProfileAutoStart(bool enabled) {
    if(auto* profile=profileById(selectedProfileId_)){
        profile->autoStartRelay=enabled;updateSelectedConfig();saveProfiles();emit configChanged(config_);
    }
}

void ApplicationController::setVideoEncoder(QString value) {
    if (auto* p=profileById(selectedProfileId_); p && !sessionById(selectedProfileId_)->active() && availableVideoEncoders().contains(value)) { p->videoEncoder=value; updateSelectedConfig(); sessionById(p->profileId)->updateProfile(*p); saveProfiles(); emit configChanged(config_); }
}
void ApplicationController::setOutputProfile(int width,int height,int fps,int videoKbps,int audioKbps,int keyframeSeconds) {
    if(auto* p=profileById(selectedProfileId_);p&&!sessionById(p->profileId)->active()){
        p->width=std::clamp(width,320,3840);p->height=std::clamp(height,180,2160);p->fps=std::clamp(fps,15,120);
        p->videoBitrateKbps=std::clamp(videoKbps,500,50000);p->audioBitrateKbps=std::clamp(audioKbps,64,512);
        p->keyframeIntervalSeconds=std::clamp(keyframeSeconds,1,10);
        updateSelectedConfig();sessionById(p->profileId)->updateProfile(*p);saveProfiles();emit configChanged(config_);
    }
}
void ApplicationController::setFillerMode(QString value) {
    if (auto* p=profileById(selectedProfileId_); p && !sessionById(selectedProfileId_)->active() && QStringList{"hold","image","black"}.contains(value)) { p->fillerMode=value; updateSelectedConfig(); sessionById(p->profileId)->updateProfile(*p); saveProfiles(); emit configChanged(config_); }
}
void ApplicationController::setStandbyImage(QString value) {
    if (auto* p=profileById(selectedProfileId_); p && !sessionById(selectedProfileId_)->active()) { p->standbyImagePath=value; updateSelectedConfig(); sessionById(p->profileId)->updateProfile(*p); saveProfiles(); emit configChanged(config_); }
}
void ApplicationController::setDelayOverlayText(QString value) {
    if (auto* p=profileById(selectedProfileId_)) { p->delayOverlayText=value; updateSelectedConfig(); sessionById(p->profileId)->updateProfile(*p); saveProfiles(); emit configChanged(config_); }
}
void ApplicationController::setServicePreset(QString value) {
    if (auto* p=profileById(selectedProfileId_)) { p->servicePreset=value; updateSelectedConfig(); saveProfiles(); emit configChanged(config_); }
}
void ApplicationController::setInputProtocol(QString value) {
    value=value.toLower(); if(value!="rtmp"&&value!="srt")return;
    if(auto* p=profileById(selectedProfileId_)){p->inputProtocol=value;updateSelectedConfig();sessionById(p->profileId)->updateProfile(*p);saveProfiles();emit configChanged(config_);}
}
void ApplicationController::setAdvertisedHost(QString value) {
    value=value.trimmed();
    if(auto* p=profileById(selectedProfileId_)){p->advertisedHost=value;updateSelectedConfig();saveProfiles();emit configChanged(config_);}
}
void ApplicationController::setSrtSettings(int port,int latency) {
    for(auto& p:profiles_)p.srtPort=static_cast<quint16>(std::clamp(port,1,65535));
    if(auto* p=profileById(selectedProfileId_))p->srtLatencyMs=std::clamp(latency,120,20000);
    for(auto& p:profiles_)sessions_[p.profileId]->updateProfile(p);
    updateSelectedConfig();saveProfiles();emit configChanged(config_);rebuildGateway();
}
void ApplicationController::setSrtEncryption(bool enabled) {
    if(auto* p=profileById(selectedProfileId_)){p->srtEncryption=enabled;updateSelectedConfig();saveProfiles();emit configChanged(config_);rebuildGateway();}
}
void ApplicationController::regenerateSrtPassphrase() {
    const auto value=ConfigurationManager::generateStreamKey();srtPassphrases_[selectedProfileId_]=value;logger_.addSecret(value);
    srtStores_[selectedProfileId_]->save(value);emit srtPassphraseChanged(value,true);rebuildGateway();
}
void ApplicationController::saveSrtPassphraseCredential(bool insecure) { srtStores_[selectedProfileId_]->save(srtPassphrases_.value(selectedProfileId_),insecure); }
void ApplicationController::setAllowLan(bool enabled) {
    const auto value=enabled?QString("0.0.0.0"):QString("127.0.0.1");for(auto& p:profiles_)p.listenAddress=value;
    for(auto& p:profiles_)sessions_[p.profileId]->updateProfile(p);updateSelectedConfig();saveProfiles();emit configChanged(config_);rebuildGateway();
}
void ApplicationController::setBufferLimits(int seconds,int memory) {
    if(auto* p=profileById(selectedProfileId_);p&&!sessionById(p->profileId)->active()){p->maximumDelaySeconds=std::clamp(seconds,1,3600);p->maximumBufferMiB=std::clamp(memory,64,4096);p->requestedDelaySeconds=std::min(p->requestedDelaySeconds,p->maximumDelaySeconds);updateSelectedConfig();sessionById(p->profileId)->updateProfile(*p);saveProfiles();emit configChanged(config_);}
}
void ApplicationController::applyDelay(int seconds) {
    if(auto* p=profileById(selectedProfileId_)){seconds=std::clamp(seconds,0,p->maximumDelaySeconds);p->requestedDelaySeconds=seconds;config_=*p;sessionById(p->profileId)->applyDelay(seconds);saveProfiles();emit configChanged(config_);}
}
bool ApplicationController::cancelDelayIncrease(){auto*s=sessionById(selectedProfileId_);return s&&s->cancelDelayIncrease();}
void ApplicationController::regenerateLocalKey(){
    if(auto*p=profileById(selectedProfileId_)){p->localStreamKey=ConfigurationManager::generateStreamKey();logger_.addSecret(p->localStreamKey);updateSelectedConfig();sessionById(p->profileId)->updateProfile(*p);saveProfiles();emit configChanged(config_);rebuildGateway();}
}

bool ApplicationController::destinationUsedByAnotherProfile(const QString& profileId,
    const QString& serverUrl, const QString& streamKey) const {
    const auto destination = joinDestination(serverUrl, streamKey);
    for (const auto& profile : profiles_) {
        if (profile.profileId == profileId) continue;
        const auto* session = sessionById(profile.profileId);
        const auto key = destinationKeys_.value(profile.profileId);
        if (session && session->active() && !key.isEmpty()
            && joinDestination(profile.destinationUrl, key) == destination)
            return true;
    }
    return false;
}

void ApplicationController::stopAll(){
    for(auto*session:sessions_)session->requestRelayStop();
    for(auto*session:sessions_)session->finishStopAll();
    mediamtx_.stop();
}
}
