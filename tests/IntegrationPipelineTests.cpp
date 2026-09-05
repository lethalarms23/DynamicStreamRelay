#include "ingest/IngestReader.h"
#include "app/RelaySession.h"
#include "app/CapacityEstimator.h"
#include "logging/Logger.h"
#include "media/OutboundPublisher.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QThread>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>
#include <gtest/gtest.h>

using namespace rtsp;

namespace {
bool waitUntil(const std::function<bool()>& condition, int timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(50);
    }
    return condition();
}

TEST(IntegrationPipeline, TwoProfilesRelayConcurrentlyOnSharedPorts) {
    const QString mediaMtx = QStringLiteral(RTSP_SOURCE_DIR) + "/resources/mediamtx/linux/mediamtx";
    if (!QFileInfo::exists(mediaMtx)) GTEST_SKIP() << "Bundled Linux MediaMTX is absent";
    QTemporaryDir temporary; ASSERT_TRUE(temporary.isValid());
    const auto configPath=temporary.path()+"/mediamtx.yml";QFile config(configPath);
    ASSERT_TRUE(config.open(QIODevice::WriteOnly|QIODevice::Text));
    config.write("logLevel: warn\napi: no\nmetrics: no\npprof: no\n"
        "rtmp: yes\nrtmpAddress: 127.0.0.1:19420\nrtsp: no\nhls: no\nwebrtc: no\n"
        "srt: yes\nsrtAddress: 127.0.0.1:19421\nmoq: no\n"
        "paths:\n  live/source-b:\n    srtPublishPassphrase: shared-port-test\n  all_others:\n");
    config.close();
    QProcess server;server.start(mediaMtx,{configPath});ASSERT_TRUE(server.waitForStarted(3000));QThread::msleep(500);
    Logger logger;
    AppConfig a;a.profileId="a";a.profileName="Camera A";a.rtmpPort=19420;a.localStreamKey="source-a";
    a.width=320;a.height=180;a.fps=30;a.videoBitrateKbps=800;a.videoEncoder="libx264";a.maximumBufferMiB=64;a.maximumDelaySeconds=30;
    AppConfig b=a;b.profileId="b";b.profileName="Camera B";b.localStreamKey="source-b";
    RelaySession first(a,logger),second(b,logger);
    first.setGatewayRunning(true);second.setGatewayRunning(true);
    first.startRelay("rtmp://127.0.0.1:19420/live/output-a");
    second.startRelay("rtmp://127.0.0.1:19420/live/output-b");
    ASSERT_TRUE(waitUntil([&]{return first.snapshot().destinationConnected&&second.snapshot().destinationConnected;},8000));
    auto startSource=[](const QString& pattern,const QString& destination){
        auto process=std::make_unique<QProcess>();process->setProcessChannelMode(QProcess::MergedChannels);
        process->start("ffmpeg",{"-hide_banner","-loglevel","error","-re","-f","lavfi","-i",
            pattern,"-f","lavfi","-i","sine=frequency=700:sample_rate=48000","-t","7",
            "-c:v","libx264","-preset","ultrafast","-g","30","-pix_fmt","yuv420p","-c:a","aac","-f","flv",destination});
        return process;
    };
    auto sourceA=startSource("testsrc2=size=320x180:rate=30","rtmp://127.0.0.1:19420/live/source-a");
    auto sourceB=std::make_unique<QProcess>();sourceB->setProcessChannelMode(QProcess::MergedChannels);
    sourceB->start("ffmpeg",{"-hide_banner","-loglevel","error","-re","-f","lavfi","-i",
        "smptebars=size=320x180:rate=30","-f","lavfi","-i","sine=frequency=900:sample_rate=48000","-t","7",
        "-c:v","libx264","-preset","ultrafast","-g","30","-pix_fmt","yuv420p","-c:a","aac","-f","mpegts",
        "srt://127.0.0.1:19421?streamid=publish:live/source-b&pkt_size=1316&passphrase=shared-port-test&pbkeylen=32"});
    ASSERT_TRUE(sourceA->waitForStarted(3000));ASSERT_TRUE(sourceB->waitForStarted(3000));
    ASSERT_TRUE(waitUntil([&]{return first.snapshot().sourceConnected&&second.snapshot().sourceConnected;},8000))
        << sourceA->readAll().constData() << sourceB->readAll().constData();
    first.applyDelay(2);
    EXPECT_EQ(first.snapshot().requestedDelayUs,2000000);
    EXPECT_EQ(second.snapshot().requestedDelayUs,0);
    ASSERT_TRUE(waitUntil([&]{return first.statistics().outgoingPackets>100&&second.statistics().outgoingPackets>100;},8000));
    EXPECT_GT(first.statistics().incomingPackets,30);
    EXPECT_GT(second.statistics().incomingPackets,30);
    sourceA->waitForFinished(12000);sourceB->waitForFinished(12000);
    EXPECT_TRUE(waitUntil([&]{return !first.snapshot().sourceConnected&&!second.snapshot().sourceConnected;},6000));
    EXPECT_TRUE(first.snapshot().destinationConnected);
    EXPECT_TRUE(second.snapshot().destinationConnected);
    first.stopAll();second.stopAll();server.terminate();server.waitForFinished(3000);
}

TEST(IntegrationPipeline, CapacityBenchmarkMeasuresRequestedSoftwareWorkload) {
    CapacityEstimator estimator;
    CapacityResult result;bool completed=false;
    QObject::connect(&estimator,&CapacityEstimator::completed,&estimator,[&](const CapacityResult& value){
        result=value;completed=true;
    });
    CapacitySettings settings;settings.encoder="libx264";settings.width=320;settings.height=180;
    settings.fps=30;settings.maximumCandidates=2;settings.benchmarkSeconds=1;settings.safetyPercent=60;
    estimator.start(settings);
    ASSERT_TRUE(waitUntil([&]{return completed;},15000));
    EXPECT_EQ(result.actualEncoder,"libx264");
    EXPECT_GE(result.safeStreams,1);
    EXPECT_GE(result.encoderOpenLimit,1);
    EXPECT_GT(result.measuredFps,30.0);
}

TEST(IntegrationPipeline, SourceAudioRemainsContinuousAtZeroDelay) {
    const QString mediaMtx = QStringLiteral(RTSP_SOURCE_DIR) + "/resources/mediamtx/linux/mediamtx";
    if (!QFileInfo::exists(mediaMtx)) GTEST_SKIP() << "Bundled Linux MediaMTX is absent";
    QTemporaryDir temporary; ASSERT_TRUE(temporary.isValid());
    const auto configPath=temporary.path()+"/mediamtx.yml";QFile config(configPath);
    ASSERT_TRUE(config.open(QIODevice::WriteOnly|QIODevice::Text));
    config.write("logLevel: warn\napi: no\nmetrics: no\npprof: no\n"
        "rtmp: yes\nrtmpAddress: 127.0.0.1:19430\nrtsp: no\nhls: no\nwebrtc: no\n"
        "srt: no\nmoq: no\npaths:\n  all_others:\n");
    config.close();
    QProcess server;server.start(mediaMtx,{configPath});ASSERT_TRUE(server.waitForStarted(3000));QThread::msleep(500);

    PacketBuffer buffer(30000000,128*1024*1024);IngestReader ingest(buffer);OutboundPublisher publisher(buffer);
    AppConfig profile;profile.width=320;profile.height=180;profile.fps=30;profile.videoBitrateKbps=800;
    profile.videoEncoder="libx264";profile.maximumBufferMiB=128;profile.maximumDelaySeconds=30;
    std::atomic_bool outputConnected{false};
    std::atomic_int ingestConnectedAtMs{-1},forwardingAtMs{-1};QElapsedTimer testClock;testClock.start();
    std::mutex publisherDiagnosticsMutex;QString publisherDiagnostics;
    QObject::connect(&publisher,&OutboundPublisher::connected,&publisher,
        [&outputConnected](bool connected){outputConnected=connected;},Qt::DirectConnection);
    QObject::connect(&publisher,&OutboundPublisher::error,&publisher,[&](const QString& message){
        std::scoped_lock lock(publisherDiagnosticsMutex);publisherDiagnostics+=message+'\n';
    },Qt::DirectConnection);
    QObject::connect(&ingest,&IngestReader::connected,&publisher,
        [&publisher,&ingestConnectedAtMs,&testClock](bool connected){
            publisher.setSourceConnected(connected);if(connected&&ingestConnectedAtMs<0)ingestConnectedAtMs=testClock.elapsed();
        },Qt::DirectConnection);
    QObject::connect(&publisher,&OutboundPublisher::sourceForwardingChanged,&publisher,
        [&forwardingAtMs,&testClock](bool forwarding,const QString&){if(forwarding&&forwardingAtMs<0)forwardingAtMs=testClock.elapsed();},Qt::DirectConnection);
    publisher.start("rtmp://127.0.0.1:19430/live/output",profile);
    ASSERT_TRUE(waitUntil([&]{return outputConnected.load();},8000));
    // MediaMTX exposes a newly published RTMP path asynchronously; match the
    // proven reader-attachment grace period used by the main pipeline test.
    QThread::sleep(3);

    const auto pcmPath=temporary.path()+"/audio.f32le";
    QProcess sink;sink.setProcessChannelMode(QProcess::MergedChannels);
    sink.start("ffmpeg",{"-y","-hide_banner","-loglevel","error","-i","rtmp://127.0.0.1:19430/live/output",
        "-t","14","-map","0:a:0","-ac","1","-ar","48000","-f","f32le",pcmPath});
    ASSERT_TRUE(sink.waitForStarted(3000));
    QProcess source;source.setProcessChannelMode(QProcess::MergedChannels);
    source.start("ffmpeg",{"-hide_banner","-loglevel","error","-re","-f","lavfi","-i","testsrc2=size=320x180:rate=30",
        "-f","lavfi","-i","sine=frequency=1000:sample_rate=48000","-t","12","-c:v","libx264","-preset","ultrafast",
        "-g","30","-pix_fmt","yuv420p","-c:a","aac","-f","flv","rtmp://127.0.0.1:19430/live/source"});
    ASSERT_TRUE(source.waitForStarted(3000));
    // Ensure the source publisher is registered before the ingest reader's
    // first RTMP open attempt; otherwise its intentional 3 s timeout tests
    // connection retry timing rather than audio continuity.
    QThread::sleep(2);
    ingest.start("rtmp://127.0.0.1:19430/live/source");
    ASSERT_TRUE(source.waitForFinished(18000)) << source.readAll().constData();
    ASSERT_TRUE(sink.waitForFinished(18000)) << sink.readAll().constData();
    const auto sinkDiagnostics=sink.readAll();
    QString diagnostics;{std::scoped_lock lock(publisherDiagnosticsMutex);diagnostics=publisherDiagnostics;}
    ASSERT_EQ(sink.exitCode(),0) << sinkDiagnostics.constData() << "\nPublisher:\n" << diagnostics.toStdString()
        << "\nMediaMTX:\n" << server.readAll().constData();
    ingest.stop();publisher.stop();server.terminate();server.waitForFinished(3000);

    QFile pcm(pcmPath);ASSERT_TRUE(pcm.open(QIODevice::ReadOnly)) << sinkDiagnostics.constData();const auto samples=pcm.readAll();
    constexpr int windowSamples=4800;const int windowBytes=windowSamples*static_cast<int>(sizeof(float));
    const int windows=samples.size()/windowBytes;std::vector<bool> active(windows,false);
    for(int window=0;window<windows;++window){
        double energy=0;
        for(int sample=0;sample<windowSamples;++sample){
            float value=0;std::memcpy(&value,samples.constData()+window*windowBytes+sample*sizeof(float),sizeof(value));
            energy+=static_cast<double>(value)*value;
        }
        active[window]=std::sqrt(energy/windowSamples)>0.01;
    }
    const auto first=std::find(active.cbegin(),active.cend(),true);
    const auto last=std::find(active.crbegin(),active.crend(),true);
    ASSERT_NE(first,active.cend()) << "Relayed audio remained silent";
    const int firstIndex=static_cast<int>(std::distance(active.cbegin(),first));
    const int lastIndex=windows-1-static_cast<int>(std::distance(active.crbegin(),last));
    const int span=lastIndex-firstIndex+1;
    const int activeWindows=static_cast<int>(std::count(active.cbegin()+firstIndex,active.cbegin()+lastIndex+1,true));
    QString activePattern;for(const bool window:active)activePattern+=window?'#':'.';
    EXPECT_GE(span,60) << "Too little source audio reached the destination; windows=" << activePattern.toStdString()
        << ", ingest connected at " << ingestConnectedAtMs.load() << " ms, video forwarding at " << forwardingAtMs.load() << " ms";
    EXPECT_GE(activeWindows*100,span*85)
        << "Audio contained repeated dropouts: " << activeWindows << " active 100 ms windows across a " << span
        << " window span; windows=" << activePattern.toStdString();
}
}

TEST(IntegrationPipeline, GeneratedObsContentReplacesBlackFiller) {
    const QString mediaMtx = QStringLiteral(RTSP_SOURCE_DIR) + "/resources/mediamtx/linux/mediamtx";
    if (!QFileInfo::exists(mediaMtx)) GTEST_SKIP() << "Bundled Linux MediaMTX is absent";
    QTemporaryDir temporary; ASSERT_TRUE(temporary.isValid());
    const auto configPath = temporary.path() + "/mediamtx.yml"; QFile config(configPath);
    ASSERT_TRUE(config.open(QIODevice::WriteOnly | QIODevice::Text));
    config.write("logLevel: warn\nrtmp: yes\nrtmpAddress: 127.0.0.1:19400\nrtsp: no\nhls: no\nwebrtc: no\nsrt: no\napi: no\nmetrics: no\npprof: no\npaths:\n  all_others:\n"); config.close();
    QProcess server; server.start(mediaMtx, {configPath}); ASSERT_TRUE(server.waitForStarted(3000)); QThread::msleep(500);

    PacketBuffer buffer(30000000, 128 * 1024 * 1024);
    IngestReader ingest(buffer); OutboundPublisher publisher(buffer); AppConfig profile;
    std::atomic_int decreaseAcknowledgements{0};
    QObject::connect(&publisher, &OutboundPublisher::delayApplied, &publisher,
        [&decreaseAcknowledgements](qint64 effective){ if (effective <= 2000000) ++decreaseAcknowledgements; }, Qt::DirectConnection);
    profile.width=320; profile.height=180; profile.fps=30; profile.videoBitrateKbps=1000;
    profile.delayOverlayText="Increasing stream delay — please wait";
    publisher.setRequestedDelaySeconds(3); publisher.setSourceConnected(true);
    publisher.start("rtmp://127.0.0.1:19400/live/output", profile);
    ingest.start("rtmp://127.0.0.1:19400/live/source");
    // Hardware probing and software fallback can take a moment; wait until the
    // RTMP output publisher has had time to register before attaching the reader.
    QThread::sleep(3);

    QProcess sink; sink.setProcessChannelMode(QProcess::MergedChannels);
    sink.start("ffmpeg", {"-hide_banner","-loglevel","info","-i","rtmp://127.0.0.1:19400/live/output",
        "-t","14","-vf","signalstats,metadata=print","-an","-f","null","-"});
    ASSERT_TRUE(sink.waitForStarted(3000));
    QProcess source; source.setProcessChannelMode(QProcess::MergedChannels);
    source.start("ffmpeg", {"-hide_banner","-loglevel","error","-re","-f","lavfi","-i","testsrc2=size=320x180:rate=30",
        "-f","lavfi","-i","sine=frequency=1000:sample_rate=48000","-t","12","-c:v","libx264","-preset","ultrafast",
        "-g","30","-pix_fmt","yuv420p","-c:a","aac","-f","flv","rtmp://127.0.0.1:19400/live/source"});
    ASSERT_TRUE(source.waitForStarted(3000));
    QThread::sleep(5); publisher.setRequestedDelaySeconds(5); // add two seconds of filler
    QThread::sleep(3); publisher.setRequestedDelaySeconds(0); // bounded keyframe jump
    ASSERT_TRUE(source.waitForFinished(18000)) << source.readAll().constData();
    sink.waitForFinished(12000);
    ingest.stop(); publisher.stop(); server.terminate(); server.waitForFinished(3000);

    const QString analysis = QString::fromUtf8(sink.readAll());
    QRegularExpression averageExpression("lavfi\\.signalstats\\.YAVG=([0-9.]+)");
    auto matches = averageExpression.globalMatch(analysis); double maximumAverage = 0, finalAverage = 0; int samples = 0, firstSourceFrame = -1;
    while (matches.hasNext()) { const auto match=matches.next(); const double value=match.captured(1).toDouble(); finalAverage=value; maximumAverage=std::max(maximumAverage,value); if(firstSourceFrame<0&&value>30.0)firstSourceFrame=samples; ++samples; }
    QRegularExpression differenceExpression("lavfi\\.signalstats\\.YDIF=([0-9.]+)");
    auto differences = differenceExpression.globalMatch(analysis); int movingFrames = 0;
    while (differences.hasNext()) if (differences.next().captured(1).toDouble() > 0.1) ++movingFrames;
    EXPECT_GT(samples, 300) << "Output cadence stalled across a delay change\n" << analysis.toStdString();
    EXPECT_GT(maximumAverage, 30.0) << "Output remained black filler; max YAVG=" << maximumAverage;
    EXPECT_GT(firstSourceFrame, 60) << "Initial delay did not emit a meaningful filler interval";
    EXPECT_LT(firstSourceFrame, 210) << "Source did not replace initial filler near the requested delay";
    EXPECT_GT(movingFrames, 60) << "Decoded source motion was excessively duplicated; moving frames=" << movingFrames;
    EXPECT_GT(finalAverage, 30.0) << "The relay did not hold the last source frame after source content ended";
    EXPECT_GE(decreaseAcknowledgements.load(), 1) << "Decrease request was not acknowledged";
}

TEST(IntegrationPipeline, EncryptedSrtVideoOnlySurvivesReaderRestart) {
    const QString mediaMtx = QStringLiteral(RTSP_SOURCE_DIR) + "/resources/mediamtx/linux/mediamtx";
    if (!QFileInfo::exists(mediaMtx)) GTEST_SKIP() << "Bundled Linux MediaMTX is absent";
    QTemporaryDir temporary; ASSERT_TRUE(temporary.isValid());
    const auto configPath = temporary.path() + "/mediamtx.yml"; QFile config(configPath);
    ASSERT_TRUE(config.open(QIODevice::WriteOnly | QIODevice::Text));
    config.write("logLevel: warn\napi: no\nmetrics: no\npprof: no\n"
        "rtmp: yes\nrtmpAddress: 127.0.0.1:19410\nrtsp: no\nhls: no\nwebrtc: no\n"
        "srt: yes\nsrtAddress: 127.0.0.1:19411\nmoq: no\n"
        "paths:\n  live/source-key:\n    source: publisher\n"
        "    srtPublishPassphrase: integration-passphrase\n");
    config.close();
    QProcess server; server.setProcessChannelMode(QProcess::MergedChannels);
    server.start(mediaMtx, {configPath}); ASSERT_TRUE(server.waitForStarted(3000));
    QThread::msleep(500);

    PacketBuffer buffer(30000000, 128 * 1024 * 1024);
    IngestReader ingest(buffer);
    std::mutex diagnosticMutex;
    QString ingestDiagnostics;
    std::atomic_int receivedPackets{0};
    QObject::connect(&ingest, &IngestReader::error, &ingest, [&](const QString& message) {
        std::scoped_lock lock(diagnosticMutex);
        ingestDiagnostics += "error: " + message + '\n';
    }, Qt::DirectConnection);
    QObject::connect(&ingest, &IngestReader::connected, &ingest, [&](bool connected) {
        std::scoped_lock lock(diagnosticMutex);
        ingestDiagnostics += QString("connected: %1\n").arg(connected);
    }, Qt::DirectConnection);
    QObject::connect(&ingest, &IngestReader::codecInfo, &ingest, [&](const QString& info) {
        std::scoped_lock lock(diagnosticMutex);
        ingestDiagnostics += "codec: " + info + '\n';
    }, Qt::DirectConnection);
    QObject::connect(&ingest, &IngestReader::packetReceived, &ingest, [&](bool, int) {
        ++receivedPackets;
    }, Qt::DirectConnection);
    ingest.start("rtmp://127.0.0.1:19410/live/source-key");
    const QString destination = "srt://127.0.0.1:19411?streamid=publish:live/source-key"
        "&pkt_size=1316&latency=200000&passphrase=integration-passphrase&pbkeylen=32";
    auto publishVideoOnly = [&destination] {
        auto process = std::make_unique<QProcess>();
        process->setProcessChannelMode(QProcess::MergedChannels);
        process->start("ffmpeg", {"-hide_banner","-loglevel","error","-re",
            "-f","lavfi","-i","testsrc2=size=320x180:rate=30","-t","6",
            "-c:v","libx264","-preset","ultrafast","-g","30","-pix_fmt","yuv420p",
            "-an","-f","mpegts",destination});
        return process;
    };

    auto first = publishVideoOnly(); ASSERT_TRUE(first->waitForStarted(3000));
    EXPECT_TRUE(first->waitForFinished(15000));
    const auto firstPublisherLog = first->readAll();
    ASSERT_TRUE(waitUntil([&buffer]{ return buffer.inputHeadUs() > 1000000; }, 5000))
        << "No video packets arrived through encrypted SRT\nFFmpeg:\n"
        << firstPublisherLog.constData() << "\nMediaMTX:\n"
        << server.readAll().constData() << "\nIngest:\n"
        << ingestDiagnostics.toStdString() << "received packets: " << receivedPackets.load()
        << ", buffered packets: " << buffer.size() << ", input head: " << buffer.inputHeadUs();
    const auto firstHead = buffer.inputHeadUs();
    const auto firstSize = buffer.size();

    ingest.stop();
    QThread::msleep(500);
    ingest.start("rtmp://127.0.0.1:19410/live/source-key");
    auto second = publishVideoOnly(); ASSERT_TRUE(second->waitForStarted(3000));
    EXPECT_TRUE(second->waitForFinished(15000)) << second->readAll().constData();
    EXPECT_TRUE(waitUntil([&buffer, firstHead]{ return buffer.inputHeadUs() > firstHead + 1000000; }, 5000))
        << "The normalized timeline did not advance after a full reader restart";
    EXPECT_GT(buffer.size(), firstSize);
    EXPECT_TRUE(buffer.nearestKeyframeAtOrBefore(buffer.inputHeadUs()).has_value());

    ingest.stop(); server.terminate(); server.waitForFinished(3000);
}
