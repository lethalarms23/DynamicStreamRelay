#include "ingest/IngestReader.h"
#include "ingest/TimestampNormalizer.h"
#include "media/FFmpegRAII.h"
#include <QDateTime>
extern "C" {
#include <libavutil/time.h>
}

namespace rtsp {
static int interruptCallback(void* opaque) { return static_cast<std::atomic_bool*>(opaque)->load() ? 1 : 0; }
IngestReader::IngestReader(PacketBuffer& buffer, QObject* parent) : QObject(parent), buffer_(buffer) {}
IngestReader::~IngestReader() { stop(); }
void IngestReader::start(QString url) { stop(); interrupt_ = false; worker_ = std::jthread([this, url = std::move(url)](std::stop_token s){ run(s, url); }); }
void IngestReader::stop() { interrupt_ = true; if (worker_.joinable()) { worker_.request_stop(); worker_.join(); } }
void IngestReader::run(std::stop_token stop, QString url) {
    while (!stop.stop_requested()) {
        AVFormatContext* raw = avformat_alloc_context();
        if (!raw) { emit error("Unable to allocate FFmpeg input context."); return; }
        raw->interrupt_callback = {interruptCallback, &interrupt_};
        AVDictionary* options = nullptr; av_dict_set(&options, "rw_timeout", "3000000", 0);
        const int opened = avformat_open_input(&raw, url.toUtf8().constData(), nullptr, &options); av_dict_free(&options);
        if (opened < 0) { avformat_free_context(raw); if (!stop.stop_requested()) std::this_thread::sleep_for(std::chrono::seconds(1)); continue; }
        InputFormatPtr format(raw);
        if (avformat_find_stream_info(format.get(), nullptr) < 0) { emit error("Could not inspect the OBS stream."); continue; }
        int video = -1, audio = -1;
        for (unsigned i = 0; i < format->nb_streams; ++i) {
            const auto id = format->streams[i]->codecpar->codec_id;
            if (id == AV_CODEC_ID_H264) video = static_cast<int>(i);
            if (id == AV_CODEC_ID_AAC) audio = static_cast<int>(i);
        }
        if (video < 0) { emit error("Unsupported source: H.264 video is required."); return; }
        const auto session = ++nextSessionId_;
        const auto codecGeneration = ++nextCodecGeneration_;
        // Both clocks start at the same origin so a reconnect doesn't introduce
        // a fixed A/V offset, even though each then advances independently.
        const auto sharedBase = std::max(videoNormalizer_.lastOutput(), audioNormalizer_.lastOutput()) + 1;
        videoNormalizer_.beginSession(session, sharedBase);
        audioNormalizer_.beginSession(session, sharedBase);
        emit connected(true);
        emit codecInfo(QString("H.264 %1x%2%3").arg(format->streams[video]->codecpar->width)
            .arg(format->streams[video]->codecpar->height).arg(audio >= 0 ? " + AAC" : " + generated silence"));
        PacketPtr packet(av_packet_alloc());
        while (!stop.stop_requested() && av_read_frame(format.get(), packet.get()) >= 0) {
            if (packet->stream_index != video && packet->stream_index != audio) { av_packet_unref(packet.get()); continue; }
            const auto* stream = format->streams[packet->stream_index];
            auto toUs = [stream](std::int64_t t) { return t == AV_NOPTS_VALUE ? std::int64_t{0} : av_rescale_q(t, stream->time_base, AVRational{1, 1000000}); };
            const bool isVideo = packet->stream_index == video;
            auto& normalizer = isVideo ? videoNormalizer_ : audioNormalizer_;
            const auto ndts = normalizer.normalize(toUs(packet->dts));
            BufferedPacket copy;
            copy.type = isVideo ? StreamType::Video : StreamType::Audio;
            copy.dtsUs = ndts; copy.ptsUs = ndts + std::max<std::int64_t>(0, toUs(packet->pts) - toUs(packet->dts));
            copy.durationUs = toUs(packet->duration); copy.keyframe = (packet->flags & AV_PKT_FLAG_KEY) != 0;
            copy.data = QByteArray(reinterpret_cast<const char*>(packet->data), packet->size);
            copy.arrivalMonotonicUs = av_gettime_relative(); copy.sourceSessionId = session;
            copy.codecGeneration = codecGeneration;
            const auto* parameters = stream->codecpar;
            copy.codecId = static_cast<int>(parameters->codec_id);
            if (parameters->extradata && parameters->extradata_size > 0)
                copy.codecExtraData = QByteArray(reinterpret_cast<const char*>(parameters->extradata), parameters->extradata_size);
            copy.width = parameters->width; copy.height = parameters->height;
            copy.sampleRate = parameters->sample_rate; copy.channels = parameters->ch_layout.nb_channels;
            if (buffer_.append(std::move(copy))) emit packetReceived(packet->stream_index == video, packet->size);
            av_packet_unref(packet.get());
        }
        emit connected(false);
        if (!stop.stop_requested()) std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
}
