#pragma once
#include <QString>

namespace rtsp {
struct UrlValidation { bool valid{false}; QString error; };
UrlValidation validateRtmpUrl(const QString& url);
QString joinDestination(const QString& serverUrl, const QString& streamKey);
}

