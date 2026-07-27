#pragma once
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#include <memory>
#include <QString>

namespace rtsp {
struct PacketDeleter { void operator()(AVPacket* p) const { av_packet_free(&p); } };
struct FrameDeleter { void operator()(AVFrame* p) const { av_frame_free(&p); } };
struct CodecDeleter { void operator()(AVCodecContext* p) const { avcodec_free_context(&p); } };
struct InputFormatDeleter { void operator()(AVFormatContext* p) const { avformat_close_input(&p); } };
struct OutputFormatDeleter { void operator()(AVFormatContext* p) const { if (p) avformat_free_context(p); } };
struct SwsDeleter { void operator()(SwsContext* p) const { sws_freeContext(p); } };
struct SwrDeleter { void operator()(SwrContext* p) const { swr_free(&p); } };
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
using CodecPtr = std::unique_ptr<AVCodecContext, CodecDeleter>;
using InputFormatPtr = std::unique_ptr<AVFormatContext, InputFormatDeleter>;
using OutputFormatPtr = std::unique_ptr<AVFormatContext, OutputFormatDeleter>;
using SwsPtr = std::unique_ptr<SwsContext, SwsDeleter>;
QString ffmpegError(int code);
}
