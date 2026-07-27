#include "app/RelaySession.h"

namespace rtsp {
RelaySession::RelaySession(AppConfig profile, Logger& logger, QObject* parent)
    : QObject(parent), profile_(std::move(profile)), logger_(logger),
      buffer_(static_cast<qint64>(profile_.maximumDelaySeconds) * 1000000,
              static_cast<std::size_t>(profile_.maximumBufferMiB) * 1024 * 1024),
      delay_(static_cast<qint64>(profile_.maximumDelaySeconds) * 1000000),
      ingest_(buffer_), publisher_(buffer_) {
    publisher_.setRequestedDelaySeconds(profile_.requestedDelaySeconds);
    connect(&ingest_, &IngestReader::connected, this, [this](bool connected) {
        if (connected && sourcePreviouslyConnected_) metrics_.incrementObsReconnects();
        if (connected) sourcePreviouslyConnected_ = true;
        sourceConnected_ = connected;
        publisher_.setSourceConnected(connected);
        if (publisher_.active())
            setState(connected ? ApplicationState::Buffering : ApplicationState::SendingFiller);
        else if (gatewayRunning_)
            setState(ApplicationState::WaitingForSource);
    });
    connect(&ingest_, &IngestReader::packetReceived, &metrics_, &MetricsCollector::packetIn);
    connect(&ingest_, &IngestReader::error, this, [this](const QString& message) {
        logger_.log(Severity::Error, profile_.profileName + "/Ingest", message);
    });
    connect(&publisher_, &OutboundPublisher::connected, this, [this](bool connected) {
        destinationConnected_ = connected;
        if (connected) {
            if (!uptime_.isValid()) uptime_.start();
            setState(sourceConnected_ ? ApplicationState::Buffering : ApplicationState::SendingFiller);
        } else if (publisher_.active()) {
            setState(ApplicationState::ReconnectingDestination);
        }
    });
    connect(&publisher_, &OutboundPublisher::packetWritten, &metrics_, &MetricsCollector::packetOut);
    connect(&publisher_, &OutboundPublisher::encoderSelected, this, [this](const QString& encoder) {
        activeEncoder_ = encoder;
        logger_.log(Severity::Info, profile_.profileName + "/Encoder", "Selected video encoder: " + encoder);
        emit encoderSelected(profile_.profileId, encoder);
    });
    connect(&publisher_, &OutboundPublisher::decoderError, this, [this](const QString& message) {
        metrics_.incrementDecodeErrors();
        logger_.log(Severity::Error, profile_.profileName + "/Decoder", message);
    });
    connect(&publisher_, &OutboundPublisher::encoderError, this, [this](const QString&) {
        metrics_.incrementEncodeErrors();
    });
    connect(&publisher_, &OutboundPublisher::health, this, [this](const QString& detail) {
        logger_.log(Severity::Info, profile_.profileName + "/Publisher", detail);
    });
    connect(&publisher_, &OutboundPublisher::error, this, [this](const QString& message) {
        logger_.log(Severity::Error, profile_.profileName + "/Publisher", message);
    });
    connect(&publisher_, &OutboundPublisher::reconnectAttempt, this, [this](quint64 attempt) {
        metrics_.incrementDestinationReconnects();
        emit reconnectAttempt(profile_.profileId, attempt);
        logger_.log(Severity::Warning, profile_.profileName + "/Publisher",
                    QString("Reconnect attempt %1").arg(attempt));
    });
    connect(&publisher_, &OutboundPublisher::sourceForwardingChanged, this, [this](bool forwarding, const QString& detail) {
        logger_.log(forwarding ? Severity::Info : Severity::Warning, profile_.profileName + "/Decoder", detail);
        if (destinationConnected_) setState(forwarding ? ApplicationState::Relaying : ApplicationState::SendingFiller);
    });
    connect(&publisher_, &OutboundPublisher::relayPosition, this, [this](qint64 head, qint64 source) {
        delay_.setCursor(source, head);
    });
    connect(&publisher_, &OutboundPublisher::delayApplied, this, [this](qint64 effective) {
        const auto head = buffer_.inputHeadUs();
        delay_.setCursor(std::max<qint64>(0, head - effective), head);
        logger_.log(Severity::Info, profile_.profileName,
                    QString("Delay completed at %1 s").arg(effective / 1000000.0, 0, 'f', 2));
        if (destinationConnected_) setState(sourceConnected_ ? ApplicationState::Relaying : ApplicationState::SendingFiller);
    });
    connect(&metrics_, &MetricsCollector::updated, this, [this](const StreamStatistics& statistics) {
        emit metricsChanged(profile_.profileId, statistics);
    });
    updateTimer_.setInterval(250);
    connect(&updateTimer_, &QTimer::timeout, this, &RelaySession::updateSnapshot);
    updateTimer_.start();
    updateBufferStoragePolicy();
}

RelaySession::~RelaySession() { stopAll(); }

QString RelaySession::internalIngestUrl() const {
    return QString("rtmp://127.0.0.1:%1/%2/%3")
        .arg(profile_.rtmpPort).arg(profile_.applicationName, profile_.localStreamKey);
}

void RelaySession::updateProfile(const AppConfig& profile) {
    const bool pathChanged = profile_.rtmpPort != profile.rtmpPort
        || profile_.applicationName != profile.applicationName
        || profile_.localStreamKey != profile.localStreamKey;
    profile_ = profile;
    delay_.setMaximumDelay(static_cast<qint64>(profile_.maximumDelaySeconds) * 1000000);
    publisher_.setDelayOverlayText(profile_.delayOverlayText);
    publisher_.setRequestedDelaySeconds(profile_.requestedDelaySeconds);
    updateBufferStoragePolicy();
    if (pathChanged && gatewayRunning_) startIngest();
    updateSnapshot();
}

void RelaySession::setGatewayRunning(bool running) {
    gatewayRunning_ = running;
    if (running) startIngest();
    else stopIngest();
}

void RelaySession::startIngest() {
    if (!gatewayRunning_) return;
    ingest_.start(internalIngestUrl());
    if (!publisher_.active()) setState(ApplicationState::WaitingForSource);
}

void RelaySession::stopIngest() {
    ingest_.stop();
    sourceConnected_ = false;
    publisher_.setSourceConnected(false);
    if (publisher_.active()) setState(ApplicationState::SendingFiller);
    else setState(ApplicationState::Stopped);
}

void RelaySession::startRelay(const QString& destination) {
    activeEncoder_.clear();
    setState(ApplicationState::ConnectingDestination);
    publisher_.setRequestedDelaySeconds(profile_.requestedDelaySeconds);
    publisher_.start(destination, profile_);
    updateBufferStoragePolicy();
}

void RelaySession::requestRelayStop() {
    publisher_.requestStop();
}

void RelaySession::finishRelayStop() {
    publisher_.waitForStop();
    destinationConnected_ = false;
    activeEncoder_.clear();
    uptime_.invalidate();
    updateBufferStoragePolicy();
    setState(gatewayRunning_ ? ApplicationState::WaitingForSource : ApplicationState::Stopped);
}

void RelaySession::stopRelay() {
    requestRelayStop();
    finishRelayStop();
}

void RelaySession::stopAll() {
    requestRelayStop();
    finishStopAll();
}

void RelaySession::finishStopAll() {
    publisher_.waitForStop();
    ingest_.stop();
    destinationConnected_ = sourceConnected_ = gatewayRunning_ = false;
    activeEncoder_.clear();
    state_ = ApplicationState::Stopped;
}

void RelaySession::applyDelay(int seconds) {
    seconds = std::clamp(seconds, 0, profile_.maximumDelaySeconds);
    publisher_.setRequestedDelaySeconds(seconds);
    const auto decision = delay_.request(static_cast<qint64>(seconds) * 1000000,
        buffer_.inputHeadUs(), std::max<qint64>(0, buffer_.inputHeadUs() - delay_.effectiveDelayUs()),
        buffer_.durationUs());
    profile_.requestedDelaySeconds = seconds;
    if (decision.action == DelayAction::InsertFiller) setState(ApplicationState::IncreasingDelay);
    else if (decision.action == DelayAction::JumpForward) setState(ApplicationState::DecreasingDelay);
    updateBufferStoragePolicy();
}

bool RelaySession::cancelDelayIncrease() {
    const bool cancelled = delay_.cancelIncrease();
    if (cancelled) setState(destinationConnected_ ? ApplicationState::Relaying : ApplicationState::WaitingForSource);
    return cancelled;
}

void RelaySession::setState(ApplicationState state) {
    if (state_ == state) return;
    state_ = state;
    logger_.log(Severity::Info, profile_.profileName, "State: " + toString(state));
    updateSnapshot();
}

void RelaySession::updateBufferStoragePolicy() {
    const auto recoverySeconds = std::max(6, profile_.keyframeIntervalSeconds * 3);
    const auto operationalSeconds = std::max(profile_.maximumDelaySeconds,
        profile_.requestedDelaySeconds + recoverySeconds);
    const auto operationalUs = static_cast<qint64>(operationalSeconds) * 1000000;
    buffer_.setLimits(operationalUs, static_cast<std::size_t>(profile_.maximumBufferMiB) * 1024 * 1024);
    if (publisher_.active()) buffer_.setStoragePolicy(true, operationalUs);
    else if (profile_.requestedDelaySeconds <= 0) buffer_.setStoragePolicy(false, 0);
    else buffer_.setStoragePolicy(true,
        static_cast<qint64>(profile_.requestedDelaySeconds + recoverySeconds) * 1000000);
}

AppSnapshot RelaySession::snapshot() const {
    return {profile_.profileId, state_, gatewayRunning_, sourceConnected_, destinationConnected_,
        delay_.requestedDelayUs(), delay_.effectiveDelayUs(), buffer_.durationUs(),
        delay_.remainingFillerUs(), static_cast<quint64>(buffer_.memoryBytes()),
        uptime_.isValid() ? uptime_.elapsed() / 1000 : 0, activeEncoder_};
}

void RelaySession::updateSnapshot() {
    const qint64 now = uptime_.isValid() ? uptime_.elapsed() : 0;
    if (state_ == ApplicationState::IncreasingDelay && delay_.remainingFillerUs() > 0) {
        const qint64 elapsedMs = lastUpdateMs_ > 0 ? std::max<qint64>(0, now - lastUpdateMs_) : updateTimer_.interval();
        delay_.advanceFiller(elapsedMs * 1000);
    }
    lastUpdateMs_ = now;
    const auto overflowCount = buffer_.overflowCount();
    if (overflowCount > lastOverflowCount_) metrics_.addDropped(overflowCount - lastOverflowCount_);
    lastOverflowCount_ = overflowCount;
    metrics_.setBuffer(buffer_.memoryBytes(), buffer_.durationUs());
    emit snapshotChanged(snapshot());
}
}
