#pragma once
#include "media/FFmpegRAII.h"
#include "media/PacketBuffer.h"
#include <deque>

namespace rtsp {
class Decoder final {
public:
    explicit Decoder(PacketBuffer& buffer);
    ~Decoder();
    bool seek(const CursorSelection& selection);
    bool videoFrameAt(std::int64_t sourceTimeUs, AVFrame* destination);
    bool audioFrameAt(std::int64_t sourceTimeUs, AVFrame* destination);
    std::uint64_t nextSequence() const { return cursor_; }
    QString lastError() const { return lastError_; }
private:
    bool pump(std::int64_t throughTimeUs, bool needAudio);
    bool ensureCodec(const BufferedPacket& packet);
    bool decode(const BufferedPacket& packet);
    void reset();
    PacketBuffer& buffer_; std::uint64_t cursor_{0}, session_{0}, codecGeneration_{0}; std::int64_t seekTimeUs_{0};
    CodecPtr video_, audio_; FramePtr lastVideoFrame_; std::deque<FramePtr> videoFrames_, audioFrames_;
    SwsContext* scaler_{nullptr}; SwrContext* resampler_{nullptr}; QString lastError_;
};
}
