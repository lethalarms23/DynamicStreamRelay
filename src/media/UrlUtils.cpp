#include "media/UrlUtils.h"
#include <QUrl>

namespace rtsp {
UrlValidation validateRtmpUrl(const QString& text) {
    const QUrl url(text, QUrl::StrictMode);
    if (!url.isValid()) return {false, "URL syntax is invalid."};
    if (url.scheme().compare("rtmp", Qt::CaseInsensitive) && url.scheme().compare("rtmps", Qt::CaseInsensitive))
        return {false, "Only rtmp:// and rtmps:// URLs are supported."};
    if (url.host().isEmpty()) return {false, "The destination host is missing."};
    if (!url.userInfo().isEmpty()) return {false, "Credentials must not be embedded in the server URL."};
    return {true, {}};
}
QString joinDestination(const QString& server, const QString& key) {
    QString base = server.trimmed(); while (base.endsWith('/')) base.chop(1);
    QString cleanKey = key.trimmed(); while (cleanKey.startsWith('/')) cleanKey.remove(0, 1);
    return cleanKey.isEmpty() ? base : base + '/' + QString::fromUtf8(QUrl::toPercentEncoding(cleanKey, "/:?&="));
}
}
