#include "app/CredentialStore.h"
#include <QSettings>
#include <utility>
#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#elif defined(RTSP_HAS_KEYCHAIN)
#include <qt6keychain/keychain.h>
#endif

namespace rtsp {
namespace {
constexpr auto service = "RTMPTimeShiftProxy";
#ifdef Q_OS_WIN
QString windowsCredentialError(DWORD code) { return QString("Windows Credential Manager error %1.").arg(code); }
#endif
}
CredentialStore::CredentialStore(QString entryName, QObject* parent)
    : QObject(parent), entryName_(std::move(entryName)) {}
QString CredentialStore::fallbackSettingsKey() const { return "credentials/insecure/" + entryName_; }
QString CredentialStore::loadInsecureFallback() const {
    QSettings settings;
    auto value = settings.value(fallbackSettingsKey()).toString();
    if (value.isEmpty() && entryName_ == QStringLiteral("destination-stream-key")) {
        value = settings.value(QStringLiteral("credentials/insecureDestinationKey")).toString();
        if (!value.isEmpty()) {
            settings.setValue(fallbackSettingsKey(), value);
            settings.remove(QStringLiteral("credentials/insecureDestinationKey"));
        }
    }
    return value;
}
bool CredentialStore::secureBackendCompiled() const {
#if defined(Q_OS_WIN) || defined(RTSP_HAS_KEYCHAIN)
    return true;
#else
    return false;
#endif
}
void CredentialStore::load() {
#ifdef Q_OS_WIN
    const auto target = QString("RTMPTimeShiftProxy/%1").arg(entryName_).toStdWString();
    PCREDENTIALW credential = nullptr;
    if (CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
        const auto value = QString::fromUtf8(reinterpret_cast<const char*>(credential->CredentialBlob),
            static_cast<qsizetype>(credential->CredentialBlobSize));
        CredFree(credential); emit loaded(value, true, true); return;
    }
    const DWORD error = GetLastError();
    const auto fallback = loadInsecureFallback();
    if (error == ERROR_NOT_FOUND) {
        emit loaded(fallback, !fallback.isEmpty(), fallback.isEmpty());
        return;
    }
    if (!fallback.isEmpty()) emit loaded(fallback, true, false);
    else emit failed("Secure credential storage is unavailable: " + windowsCredentialError(error), true);
#elif defined(RTSP_HAS_KEYCHAIN)
    auto* job = new QKeychain::ReadPasswordJob(service, this); job->setKey(entryName_);
    connect(job, &QKeychain::Job::finished, this, [this](QKeychain::Job* base){
        auto* read = static_cast<QKeychain::ReadPasswordJob*>(base);
        if (read->error() == QKeychain::NoError) { emit loaded(read->textData(), true, true); return; }
        const auto fallback = loadInsecureFallback();
        if (read->error() == QKeychain::EntryNotFound) {
            emit loaded(fallback, !fallback.isEmpty(), fallback.isEmpty());
            return;
        }
        if (!fallback.isEmpty()) emit loaded(fallback, true, false);
        else emit failed("Secure credential storage is unavailable: " + read->errorString(), true);
    }); job->start();
#else
    const auto value = loadInsecureFallback();
    emit loaded(value, !value.isEmpty(), false);
#endif
}
void CredentialStore::save(const QString& value, bool insecure) {
    if (insecure) { QSettings settings; settings.setValue(fallbackSettingsKey(), value); emit saved(false); return; }
#ifdef Q_OS_WIN
    const auto target = QString("RTMPTimeShiftProxy/%1").arg(entryName_).toStdWString();
    const QByteArray bytes = value.toUtf8(); CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC; credential.TargetName = const_cast<wchar_t*>(target.c_str());
    credential.CredentialBlobSize = static_cast<DWORD>(bytes.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(bytes.constData()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<wchar_t*>(L"RTMP TimeShift Proxy");
    if (CredWriteW(&credential, 0)) emit saved(true);
    else { const DWORD error = GetLastError(); emit failed("Could not save the credential securely: " + windowsCredentialError(error), true); }
#elif defined(RTSP_HAS_KEYCHAIN)
    auto* job = new QKeychain::WritePasswordJob(service, this); job->setKey(entryName_); job->setTextData(value);
    connect(job, &QKeychain::Job::finished, this, [this](QKeychain::Job* finished){
        if (finished->error() == QKeychain::NoError) emit saved(true);
        else emit failed("Could not save the credential securely: " + finished->errorString(), true);
    }); job->start();
#else
    emit failed("This build has no secure keychain backend.", true);
#endif
}
void CredentialStore::remove(bool insecure) {
    QSettings settings; settings.remove(fallbackSettingsKey());
    if (entryName_ == QStringLiteral("destination-stream-key"))
        settings.remove(QStringLiteral("credentials/insecureDestinationKey"));
    if (insecure) { emit saved(false); return; }
#ifdef Q_OS_WIN
    const auto target = QString("RTMPTimeShiftProxy/%1").arg(entryName_).toStdWString();
    if (CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0)) emit saved(true);
    else { const DWORD error = GetLastError(); if (error == ERROR_NOT_FOUND) emit saved(true);
        else emit failed("Could not remove the saved credential: " + windowsCredentialError(error), false); }
#elif defined(RTSP_HAS_KEYCHAIN)
    auto* job = new QKeychain::DeletePasswordJob(service, this); job->setKey(entryName_);
    connect(job, &QKeychain::Job::finished, this, [this](QKeychain::Job* finished){
        if (finished->error() == QKeychain::NoError || finished->error() == QKeychain::EntryNotFound) emit saved(true);
        else emit failed("Could not remove the saved credential: " + finished->errorString(), false);
    }); job->start();
#else
    emit saved(false);
#endif
}
}
