#include "media/Decoder.h"
#include <algorithm>
#include <chrono>
extern "C" {
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
}

namespace rtsp {
Decoder::Decoder(PacketBuffer& buffer) : buffer_(buffer) {}
Decoder::~Decoder() { reset(); }
void Decoder::reset() {
    video_.reset(); audio_.reset(); lastVideoFrame_.reset(); videoFrames_.clear(); audioFrames_.clear();
    session_ = 0; codecGeneration_ = 0;
    if (scaler_) { sws_freeContext(scaler_); scaler_ = nullptr; }
    if (resampler_) swr_free(&resampler_);
}
bool Decoder::seek(const CursorSelection& selection) {
    reset(); cursor_ = std::min(selection.videoSequence, selection.audioSequence);
    seekTimeUs_ = selection.selectedMediaTimeUs; session_ = 0; lastError_.clear(); return true;
}
bool Decoder::ensureCodec(const BufferedPacket& p) {
    CodecPtr& context = p.type == StreamType::Video ? video_ : audio_;
    if (context) return true;
    const auto* codec = avcodec_find_decoder(static_cast<AVCodecID>(p.codecId));
    if (!codec) { lastError_ = "Required source decoder is unavailable."; return false; }
    context.reset(avcodec_alloc_context3(codec)); if (!context) { lastError_ = "Decoder allocation failed."; return false; }
    context->pkt_timebase = {1, 1000000};
    if (p.type == StreamType::Video) { context->width = p.width; context->height = p.height; }
    else { context->sample_rate = p.sampleRate; av_channel_layout_default(&context->ch_layout, std::max(1, p.channels)); }
    if (!p.codecExtraData.isEmpty()) {
        context->extradata_size = p.codecExtraData.size();
        context->extradata = static_cast<std::uint8_t*>(av_mallocz(context->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE));
        if (!context->extradata) { lastError_ = "Decoder extradata allocation failed."; return false; }
        std::memcpy(context->extradata, p.codecExtraData.constData(), context->extradata_size);
    }
    const int rc = avcodec_open2(context.get(), codec, nullptr);
    if (rc < 0) { lastError_ = QString("%1 decoder open failed: %2").arg(codec->name, ffmpegError(rc)); context.reset(); return false; }
    return true;
}
bool Decoder::decode(const BufferedPacket& p) {
    if ((session_ && p.sourceSessionId != session_)
        || (codecGeneration_ && p.codecGeneration != codecGeneration_)) { reset(); }
    session_ = p.sourceSessionId; codecGeneration_ = p.codecGeneration;
    if (!ensureCodec(p)) return false;
    AVCodecContext* context = p.type == StreamType::Video ? video_.get() : audio_.get();
    PacketPtr packet(av_packet_alloc()); if (!packet || av_new_packet(packet.get(), p.data.size()) < 0) return false;
    std::memcpy(packet->data, p.data.constData(), p.data.size()); packet->pts = p.ptsUs; packet->dts = p.dtsUs;
    packet->duration = p.durationUs; if (p.keyframe) packet->flags |= AV_PKT_FLAG_KEY;
    int rc = avcodec_send_packet(context, packet.get());
    if (rc < 0 && rc != AVERROR(EAGAIN)) { lastError_ = "Source packet decode failed: " + ffmpegError(rc); return false; }
    for (;;) {
        FramePtr frame(av_frame_alloc()); rc = avcodec_receive_frame(context, frame.get());
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) break;
        if (rc < 0) { lastError_ = "Source frame decode failed: " + ffmpegError(rc); return false; }
        const auto timestamp = frame->best_effort_timestamp == AV_NOPTS_VALUE ? frame->pts : frame->best_effort_timestamp;
        frame->pts = timestamp;
        if (p.type == StreamType::Video) videoFrames_.push_back(std::move(frame));
        else if (timestamp >= seekTimeUs_) audioFrames_.push_back(std::move(frame));
    }
    return true;
}
bool Decoder::pump(std::int64_t through, bool needAudio) {
    // Decoding runs on the publishing worker. Bound each refill so a keyframe
    // jump cannot starve real-time filler/packet output for hundreds of ms.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(6);
    for (int count = 0; count < 32; ++count) {
        const bool haveVideo = !videoFrames_.empty() && videoFrames_.back()->pts >= through;
        const bool haveAudio = !audioFrames_.empty();
        if (haveVideo && (!needAudio || haveAudio)) return true;
        auto packet = buffer_.packetAtOrAfter(cursor_); if (!packet) return haveVideo || (needAudio && haveAudio);
        cursor_ = packet->sequence + 1;
        if (!decode(*packet)) return false;
        if (std::chrono::steady_clock::now() >= deadline) break;
    }
    return !videoFrames_.empty();
}
bool Decoder::videoFrameAt(std::int64_t time, AVFrame* out) {
    pump(time, false);
    // Select only frames whose source timestamp is due. Retain the last due
    // frame for output-rate conversion (e.g. 30 -> 60 FPS) instead of showing
    // a future frame early and then repeating it on the following tick.
    while (!videoFrames_.empty() && videoFrames_.front()->pts <= time) {
        lastVideoFrame_ = std::move(videoFrames_.front()); videoFrames_.pop_front();
    }
    if (!lastVideoFrame_) return false;
    auto& source = lastVideoFrame_;
    scaler_ = sws_getCachedContext(scaler_, source->width, source->height, static_cast<AVPixelFormat>(source->format),
        out->width, out->height, static_cast<AVPixelFormat>(out->format), SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!scaler_) { lastError_ = "Video scaler setup failed."; return false; }
    if (av_frame_make_writable(out) < 0) return false;
    sws_scale(scaler_, source->data, source->linesize, 0, source->height, out->data, out->linesize);
    return true;
}
bool Decoder::audioFrameAt(std::int64_t time, AVFrame* out) {
    // Keep audio attached to the same source clock used by video. If decoding
    // or network delivery stalls, discard frames whose media interval has
    // already passed instead of playing stale audio after outputting silence.
    for (int refill = 0; refill < 3; ++refill) {
        if (audioFrames_.empty()) pump(time, true);
        while (!audioFrames_.empty()) {
            const auto& candidate = audioFrames_.front();
            const auto sampleRate = std::max(1, candidate->sample_rate);
            const auto durationUs = av_rescale_q(candidate->nb_samples, AVRational{1, sampleRate}, AVRational{1, 1000000});
            if (candidate->pts + durationUs > time) break;
            audioFrames_.pop_front();
        }
        if (!audioFrames_.empty()) break;
    }
    if (audioFrames_.empty()) return false;
    // A future audio frame must not be emitted early. Silence fills the gap and
    // the frame remains queued for the output interval it actually belongs to.
    if (audioFrames_.front()->pts > time + 2000) return false;
    auto source = std::move(audioFrames_.front()); audioFrames_.pop_front();
    if (!resampler_) {
        AVChannelLayout destinationLayout; av_channel_layout_copy(&destinationLayout, &out->ch_layout);
        const int rc = swr_alloc_set_opts2(&resampler_, &destinationLayout, static_cast<AVSampleFormat>(out->format), out->sample_rate,
            &source->ch_layout, static_cast<AVSampleFormat>(source->format), source->sample_rate, 0, nullptr);
        av_channel_layout_uninit(&destinationLayout);
        if (rc < 0 || swr_init(resampler_) < 0) { lastError_ = "Audio resampler setup failed."; return false; }
    }
    if (av_frame_make_writable(out) < 0) return false;
    const int converted = swr_convert(resampler_, out->data, out->nb_samples,
        const_cast<const std::uint8_t**>(source->extended_data), source->nb_samples);
    if (converted < 0) { lastError_ = "Audio resampling failed: " + ffmpegError(converted); return false; }
    if (converted < out->nb_samples)
        av_samples_set_silence(out->data, converted, out->nb_samples - converted, out->ch_layout.nb_channels, static_cast<AVSampleFormat>(out->format));
    return true;
}
}
