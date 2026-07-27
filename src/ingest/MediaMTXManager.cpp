#include "ingest/MediaMTXManager.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

namespace rtsp {
MediaMTXManager::MediaMTXManager(QObject* parent) : QObject(parent) {
    process_.setProcessChannelMode(QProcess::MergedChannels);
    connect(&process_, &QProcess::readyReadStandardOutput, this, &MediaMTXManager::consumeOutput);
    connect(&process_, &QProcess::started, this, [this]{ emit runningChanged(true); });
    connect(&process_, &QProcess::finished, this, [this](int, QProcess::ExitStatus){ publisher_ = false; emit publisherChanged(false); emit runningChanged(false); });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError){ emit error(process_.errorString()); });
}
QString MediaMTXManager::findExecutable() const {
#ifdef Q_OS_WIN
    const QString name = "mediamtx.exe", platform = "windows";
#else
    const QString name = "mediamtx", platform = "linux";
#endif
    const QStringList candidates{QCoreApplication::applicationDirPath() + '/' + name,
        QCoreApplication::applicationDirPath() + "/../share/rtmp-timeshift-proxy/mediamtx/" + platform + '/' + name,
        QCoreApplication::applicationDirPath() + "/../resources/mediamtx/" + platform + '/' + name,
        QStandardPaths::findExecutable(name)};
    for (const auto& path : candidates) if (!path.isEmpty() && QFileInfo::exists(path)) return QFileInfo(path).canonicalFilePath();
    return {};
}
QString MediaMTXManager::createConfig(const AppConfig& c, const QList<IngestPath>& paths) {
    const auto dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/rtmp-timeshift-proxy";
    QDir().mkpath(dir); const auto path = dir + "/mediamtx.yml"; QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return {};
    QTextStream out(&f);
    out << "logLevel: info\napi: yes\napiAddress: 127.0.0.1:9997\nmetrics: no\npprof: no\n"
        << "rtmp: yes\nrtmpAddress: " << c.listenAddress << ':' << c.rtmpPort << "\n"
        << "rtsp: no\nhls: no\nwebrtc: no\n"
        << "srt: yes\nsrtAddress: " << c.listenAddress << ':' << c.srtPort << "\n"
        << "moq: no\npaths:\n";
    for (const auto& ingest : paths) {
        out << "  " << ingest.path << ":\n    source: publisher\n";
        if (!ingest.srtPassphrase.isEmpty())
            out << "    srtPublishPassphrase: " << ingest.srtPassphrase << "\n";
    }
    out.flush();
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return path;
}
void MediaMTXManager::start(const AppConfig& c, const QList<IngestPath>& paths) {
    const auto exe = findExecutable();
    if (exe.isEmpty()) { emit error("MediaMTX executable is missing. Install it beside the application or in the bundled resources directory."); return; }
    configPath_ = createConfig(c, paths); if (configPath_.isEmpty()) { emit error("Could not create the MediaMTX runtime configuration."); return; }
    // MediaMTX watches its configuration file and reloads path changes without
    // interrupting existing publishers and readers.
    if (running()) return;
    process_.setProgram(exe); process_.setArguments({configPath_}); process_.start();
}
void MediaMTXManager::stop() {
    if (process_.state() == QProcess::NotRunning) return; process_.terminate();
    if (!process_.waitForFinished(3000)) { process_.kill(); process_.waitForFinished(1000); }
}
bool MediaMTXManager::running() const { return process_.state() == QProcess::Running; }
void MediaMTXManager::consumeOutput() {
    const auto lines = QString::fromUtf8(process_.readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);
    for (const auto& line : lines) {
        emit logLine(line);
        const bool published = line.contains("is publishing", Qt::CaseInsensitive) || line.contains("publisher connected", Qt::CaseInsensitive);
        const bool gone = line.contains("publisher disconnected", Qt::CaseInsensitive) || line.contains("closed: EOF", Qt::CaseInsensitive);
        if (published && !publisher_) { publisher_ = true; emit publisherChanged(true); }
        if (gone && publisher_) { publisher_ = false; emit publisherChanged(false); }
    }
}
}
