#include "logging/SecretRedactor.h"
#include <QRegularExpression>

namespace rtsp {
void SecretRedactor::addSecret(const QString& secret) { if (secret.size() >= 4 && !secrets_.contains(secret)) secrets_.push_back(secret); }
QString SecretRedactor::redact(QString text) const {
    for (const auto& secret : secrets_) text.replace(secret, "[REDACTED]", Qt::CaseSensitive);
    static const QRegularExpression urlCredentials(R"((rtmps?://)[^/@\s]+@)", QRegularExpression::CaseInsensitiveOption);
    text.replace(urlCredentials, "\\1[REDACTED]@");
    static const QRegularExpression namedKey(R"((stream[_ -]?key\s*[=:]\s*)[^\s,;]+)", QRegularExpression::CaseInsensitiveOption);
    text.replace(namedKey, "\\1[REDACTED]");
    static const QRegularExpression srtPassphrase(R"((passphrase\s*[=:]\s*)[^&\s,;]+)", QRegularExpression::CaseInsensitiveOption);
    text.replace(srtPassphrase, "\\1[REDACTED]");
    return text;
}
}
