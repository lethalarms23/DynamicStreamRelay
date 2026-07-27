#include "media/FFmpegRAII.h"
extern "C" {
#include <libavutil/error.h>
}
namespace rtsp {
QString ffmpegError(int code) { char buffer[AV_ERROR_MAX_STRING_SIZE]{}; av_strerror(code, buffer, sizeof(buffer)); return QString::fromUtf8(buffer); }
}
