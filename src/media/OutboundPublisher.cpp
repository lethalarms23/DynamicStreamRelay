#include "media/OutboundPublisher.h"
#include "media/FFmpegRAII.h"
#include "media/Decoder.h"
#include <algorithm>
#include <chrono>
#include <thread>
#include <QImage>
#include <QPainter>
#include <QFont>
extern "C" {
#include <libavutil/opt.h>
#include <libavutil/time.h>
}

namespace rtsp {
namespace {
int ioInterrupt(void* opaque) { return static_cast<std::atomic_bool*>(opaque)->load() ? 1 : 0; }
bool sendFrames(AVCodecContext* codec, AVFrame* frame, AVFormatContext* output, AVStream* stream,
                const std::function<void(qsizetype, bool)>& written, QString& problem) {
    int rc = avcodec_send_frame(codec, frame);
    if (rc < 0) { problem = ffmpegError(rc); return false; }
    PacketPtr packet(av_packet_alloc());
    while ((rc = avcodec_receive_packet(codec, packet.get())) >= 0) {
        av_packet_rescale_ts(packet.get(), codec->time_base, stream->time_base); packet->stream_index = stream->index;
        const auto bytes = packet->size; const bool keyframe = (packet->flags & AV_PKT_FLAG_KEY) != 0;
        rc = av_interleaved_write_frame(output, packet.get()); av_packet_unref(packet.get());
        if (rc < 0) { problem = ffmpegError(rc); return false; } written(bytes, keyframe);
    }
    return rc == AVERROR(EAGAIN) || rc == AVERROR_EOF;
}
bool copyFramePixels(AVFrame* destination, const AVFrame* source) {
    return destination && source && av_frame_make_writable(destination) >= 0 && av_frame_copy(destination, source) >= 0;
}
// FFmpeg 7.1 (libavcodec 61) deprecated AVCodec's raw capability arrays
// (sample_fmts, pix_fmts, ...) in favor of avcodec_get_supported_config();
// FFmpeg 8.0 (libavcodec 62) removes the old fields entirely. Support both.
AVSampleFormat firstSupportedSampleFormat(const AVCodec* codec) {
#if LIBAVCODEC_VERSION_MAJOR >= 61
    const AVSampleFormat* formats = nullptr;
    const int rc = avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0,
        reinterpret_cast<const void**>(&formats), nullptr);
    return (rc >= 0 && formats) ? formats[0] : AV_SAMPLE_FMT_FLTP;
#else
    return codec->sample_fmts ? codec->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
#endif
}
bool loadStandbyImage(const QString& path, AVFrame* destination) {
    QImage source(path);
    if (source.isNull() || !destination) return false;
    QImage canvas(destination->width, destination->height, QImage::Format_RGBA8888);
    canvas.fill(Qt::black);
    const QImage scaled = source.convertToFormat(QImage::Format_RGBA8888).scaled(canvas.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPainter painter(&canvas); painter.drawImage((canvas.width() - scaled.width()) / 2, (canvas.height() - scaled.height()) / 2, scaled); painter.end();
    SwsPtr scaler(sws_getContext(canvas.width(), canvas.height(), AV_PIX_FMT_RGBA,
        destination->width, destination->height, static_cast<AVPixelFormat>(destination->format), SWS_BICUBIC, nullptr, nullptr, nullptr));
    if (!scaler || av_frame_make_writable(destination) < 0) return false;
    const uint8_t* input[]{canvas.constBits()}; const int strides[]{static_cast<int>(canvas.bytesPerLine())};
    return sws_scale(scaler.get(), input, strides, 0, canvas.height(), destination->data, destination->linesize) == destination->height;
}
bool renderTextOverlay(const AVFrame* source, AVFrame* destination, const QString& text) {
    if (!source || !destination || text.trimmed().isEmpty()) return false;
    QImage canvas(source->width, source->height, QImage::Format_RGBA8888);
    SwsPtr toRgba(sws_getContext(source->width, source->height, static_cast<AVPixelFormat>(source->format),
        canvas.width(), canvas.height(), AV_PIX_FMT_RGBA, SWS_BICUBIC, nullptr, nullptr, nullptr));
    if (!toRgba) return false;
    const uint8_t* sourceData[]{source->data[0], source->data[1], source->data[2], source->data[3]};
    uint8_t* rgbaData[]{canvas.bits()}; const int rgbaStride[]{static_cast<int>(canvas.bytesPerLine())};
    if (sws_scale(toRgba.get(), sourceData, source->linesize, 0, source->height, rgbaData, rgbaStride) != source->height) return false;
    QPainter painter(&canvas); painter.setRenderHint(QPainter::Antialiasing); painter.setRenderHint(QPainter::TextAntialiasing);
    QFont font = painter.font(); font.setBold(true); font.setPixelSize(std::max(24, canvas.height() / 22)); painter.setFont(font);
    const int margin = std::max(24, canvas.width() / 24); const int padding = std::max(14, canvas.height() / 54);
    QRect textRect(margin, 0, canvas.width() - margin * 2, canvas.height() / 3);
    const QRect measured = painter.boundingRect(textRect, Qt::AlignCenter | Qt::TextWordWrap, text);
    QRect background = measured.adjusted(-padding * 2, -padding, padding * 2, padding);
    background.moveCenter(QPoint(canvas.width() / 2, canvas.height() * 3 / 4));
    painter.setPen(Qt::NoPen); painter.setBrush(QColor(0, 0, 0, 180)); painter.drawRoundedRect(background, padding, padding);
    painter.setPen(Qt::white); painter.drawText(background.adjusted(padding, padding / 2, -padding, -padding / 2), Qt::AlignCenter | Qt::TextWordWrap, text); painter.end();
    SwsPtr toYuv(sws_getContext(canvas.width(), canvas.height(), AV_PIX_FMT_RGBA,
        destination->width, destination->height, static_cast<AVPixelFormat>(destination->format), SWS_BICUBIC, nullptr, nullptr, nullptr));
    if (!toYuv || av_frame_make_writable(destination) < 0) return false;
    const uint8_t* input[]{canvas.constBits()}; const int inputStride[]{static_cast<int>(canvas.bytesPerLine())};
    return sws_scale(toYuv.get(), input, inputStride, 0, canvas.height(), destination->data, destination->linesize) == destination->height;
}
}

OutboundPublisher::OutboundPublisher(PacketBuffer& buffer, QObject* parent) : QObject(parent), buffer_(buffer) {}
OutboundPublisher::~OutboundPublisher() { stop(); }
void OutboundPublisher::start(QString destination, AppConfig profile) {
    stop(); interrupt_ = false; gracefulStop_ = false; videoFrame_ = audioSample_ = 0;
    { std::lock_guard lock(workerExitMutex_); workerExited_ = false; }
    { std::lock_guard lock(overlayTextMutex_); delayOverlayText_ = profile.delayOverlayText; }
    worker_ = std::jthread([this, destination = std::move(destination), profile](std::stop_token s){ run(s, destination, profile); });
}
void OutboundPublisher::setDelayOverlayText(QString text) { std::lock_guard lock(overlayTextMutex_); delayOverlayText_ = std::move(text); }
void OutboundPublisher::requestStop() {
    if (!worker_.joinable()) return;
    gracefulStop_ = true; worker_.request_stop();
}
void OutboundPublisher::waitForStop() {
    if (!worker_.joinable()) return;
    { std::unique_lock lock(workerExitMutex_); if (!workerExitCv_.wait_for(lock, std::chrono::seconds(3), [this]{ return workerExited_; })) interrupt_ = true; }
    worker_.join();
}
void OutboundPublisher::stop() {
    requestStop();
    waitForStop();
}
void OutboundPublisher::run(std::stop_token token, QString destination, AppConfig profile) {
    static const int backoff[]{1, 2, 5, 10, 20, 30}; quint64 attempt = 0;
    while (!token.stop_requested()) {
        const bool stable = publishSession(token, destination, profile); emit connected(false);
        if (token.stop_requested()) break;
        if (stable) attempt = 0; else ++attempt; emit reconnectAttempt(attempt);
        const auto seconds = backoff[std::min<std::size_t>(attempt ? attempt - 1 : 0, std::size(backoff) - 1)];
        for (int i = 0; i < seconds * 10 && !token.stop_requested(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    { std::lock_guard lock(workerExitMutex_); workerExited_ = true; } workerExitCv_.notify_all();
}

bool OutboundPublisher::publishSession(std::stop_token token, const QString& destination, const AppConfig& p) {
    AVFormatContext* raw = nullptr; const auto target = destination.toUtf8();
    int rc = avformat_alloc_output_context2(&raw, nullptr, "flv", target.constData());
    if (rc < 0 || !raw) { emit error("FLV output setup failed: " + ffmpegError(rc)); return false; }
    OutputFormatPtr output(raw); output->interrupt_callback = {ioInterrupt, &interrupt_};
    const AVCodec* ac = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!ac) {
        const QString message = "AAC encoder is unavailable in this FFmpeg build.";
        emit encoderError(message); emit error(message); return false;
    }

    CodecPtr video;
    QStringList videoFailures;
    QStringList encoderOrder;
    if (p.videoEncoder == "auto") encoderOrder = {"h264_nvenc", "h264_qsv", "h264_amf", "libx264"};
    else encoderOrder = {p.videoEncoder};
    for (const auto& encoderNameString : encoderOrder) {
        const auto encoderNameBytes = encoderNameString.toLatin1();
        const char* encoderName = encoderNameBytes.constData();
        const AVCodec* candidate = avcodec_find_encoder_by_name(encoderName);
        if (!candidate) continue;
        CodecPtr attempt(avcodec_alloc_context3(candidate));
        if (!attempt) continue;
        attempt->width = p.width; attempt->height = p.height; attempt->pix_fmt = AV_PIX_FMT_YUV420P;
        attempt->time_base = {1, p.fps}; attempt->framerate = {p.fps, 1};
        attempt->bit_rate = p.videoBitrateKbps * 1000LL; attempt->rc_max_rate = attempt->bit_rate;
        attempt->rc_buffer_size = attempt->bit_rate * 2; attempt->gop_size = p.keyframeIntervalSeconds * p.fps;
        attempt->max_b_frames = 0;
        if (output->oformat->flags & AVFMT_GLOBALHEADER) attempt->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        AVDictionary* options = nullptr;
        if (QString::fromLatin1(encoderName) == "libx264") {
            av_dict_set(&options, "preset", "veryfast", 0);
            av_dict_set(&options, "tune", "zerolatency", 0);
            av_dict_set(&options, "x264-params", "nal-hrd=cbr:force-cfr=1", 0);
            av_dict_set(&options, "profile", "high", 0);
        } else if (QString::fromLatin1(encoderName) == "h264_nvenc") {
            av_dict_set(&options, "preset", "p4", 0);
            av_dict_set(&options, "tune", "ll", 0);
            av_dict_set(&options, "rc", "cbr", 0);
            av_dict_set(&options, "cbr", "1", 0);
            av_dict_set(&options, "cbr_padding", "1", 0);
            av_dict_set(&options, "zerolatency", "1", 0);
            av_dict_set(&options, "delay", "0", 0);
            av_dict_set(&options, "forced-idr", "1", 0);
            av_dict_set(&options, "strict_gop", "1", 0);
            av_dict_set(&options, "profile", "high", 0);
        }
        const int openResult = avcodec_open2(attempt.get(), candidate, &options);
        av_dict_free(&options);
        if (openResult >= 0) {
            video = std::move(attempt);
            emit encoderSelected(QString::fromUtf8(candidate->name));
            break;
        }
        videoFailures.push_back(QString("%1: %2").arg(encoderName, ffmpegError(openResult)));
    }
    if (!video) {
        const QString message = "No usable H.264 encoder. " + videoFailures.join("; ");
        emit encoderError(message); emit error(message);
        return false;
    }

    CodecPtr audio(avcodec_alloc_context3(ac));
    if (!audio) {
        const QString message = "Audio encoder allocation failed.";
        emit encoderError(message); emit error(message); return false;
    }
    audio->sample_rate = p.audioSampleRate; av_channel_layout_default(&audio->ch_layout, 2);
    audio->sample_fmt = firstSupportedSampleFormat(ac); audio->time_base = {1, p.audioSampleRate}; audio->bit_rate = p.audioBitrateKbps * 1000LL;
    if (output->oformat->flags & AVFMT_GLOBALHEADER) audio->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    rc = avcodec_open2(audio.get(), ac, nullptr);
    if (rc < 0) {
        const QString message = "AAC encoder open failed: " + ffmpegError(rc);
        emit encoderError(message); emit error(message); return false;
    }
    AVStream* vs = avformat_new_stream(output.get(), nullptr); AVStream* as = avformat_new_stream(output.get(), nullptr);
    if (!vs || !as) { emit error("Could not create output streams."); return false; }
    vs->time_base = video->time_base; as->time_base = audio->time_base;
    avcodec_parameters_from_context(vs->codecpar, video.get()); avcodec_parameters_from_context(as->codecpar, audio.get());
    rc = avio_open2(&output->pb, target.constData(), AVIO_FLAG_WRITE, &output->interrupt_callback, nullptr);
    if (rc < 0) { emit error("Destination connection failed: " + ffmpegError(rc)); return false; }
    struct IoCloser { AVFormatContext* f; ~IoCloser(){ if (f && f->pb) avio_closep(&f->pb); } } closer{output.get()};
    rc = avformat_write_header(output.get(), nullptr); if (rc < 0) { emit error("Destination rejected stream header: " + ffmpegError(rc)); return false; }
    emit connected(true); const auto connectedAt = std::chrono::steady_clock::now();
    FramePtr vf(av_frame_alloc()), af(av_frame_alloc());
    vf->format = video->pix_fmt; vf->width = video->width; vf->height = video->height; av_frame_get_buffer(vf.get(), 32);
    FramePtr held(av_frame_alloc()), standby(av_frame_alloc()), delayOverlay(av_frame_alloc());
    for (AVFrame* frame : {held.get(), standby.get(), delayOverlay.get()}) {
        frame->format = video->pix_fmt; frame->width = video->width; frame->height = video->height; av_frame_get_buffer(frame, 32);
    }
    bool heldValid = false, delayOverlayValid = false; QString renderedOverlayText;
    const bool standbyValid = !p.standbyImagePath.isEmpty() && loadStandbyImage(p.standbyImagePath, standby.get());
    if (!p.standbyImagePath.isEmpty()) emit health(standbyValid ? "Custom standby image loaded." : "Custom standby image could not be loaded; black fallback will be used.");
    af->format = audio->sample_fmt; af->sample_rate = audio->sample_rate; av_channel_layout_copy(&af->ch_layout, &audio->ch_layout);
    af->nb_samples = audio->frame_size > 0 ? audio->frame_size : 1024; av_frame_get_buffer(af.get(), 0);
    Decoder decoder(buffer_); bool sourceReady = false, reportedForwarding = false;
    QString lastReportedDecoderError;
    std::int64_t sourceTimeUs = 0, sourceBaseUs = 0, sourceFrameTick = 0, sourceAudioSamples = 0;
    std::int64_t observedDelayUs = requestedDelayUs_.load(), fillerRemainingUs = 0;
    bool delayCompletionPending = false;
    auto next = std::chrono::steady_clock::now(); QString problem;
    auto healthAt = connectedAt; quint64 healthVideoPackets = 0, healthAudioPackets = 0, healthBytes = 0, healthKeyframes = 0;
    while (!token.stop_requested()) {
        av_frame_make_writable(vf.get());
        vf->pict_type = AV_PICTURE_TYPE_NONE;
        for (int y = 0; y < vf->height; ++y) std::fill_n(vf->data[0] + y * vf->linesize[0], vf->width, 16);
        for (int y = 0; y < vf->height / 2; ++y) { std::fill_n(vf->data[1] + y * vf->linesize[1], vf->width / 2, 128); std::fill_n(vf->data[2] + y * vf->linesize[2], vf->width / 2, 128); }
        const auto frameDurationUs = 1000000LL / p.fps;
        const auto requested = requestedDelayUs_.load();
        const auto head = buffer_.inputHeadUs();
        if (!sourceConnected_.load()) sourceReady = false;
        const bool initialDelayAvailable = requested <= 0 || buffer_.durationUs() >= requested;
        if (sourceConnected_.load() && !sourceReady && initialDelayAvailable) {
            if (auto selection = buffer_.selectAt(std::max<std::int64_t>(0, head - requested))) {
                decoder.seek(*selection); sourceBaseUs = sourceTimeUs = selection->selectedMediaTimeUs; sourceFrameTick = sourceAudioSamples = 0; sourceReady = true;
                observedDelayUs = requested; fillerRemainingUs = 0;
            }
        }
        if (sourceReady && requested != observedDelayUs) {
            const auto effective = std::max<std::int64_t>(0, head - sourceTimeUs);
            bool requestAccepted = false, completedBySeek = false;
            std::int64_t seekEffectiveUs = effective;
            if (requested > effective) { fillerRemainingUs = requested - effective; requestAccepted = true; }
            else if (auto selection = buffer_.selectAt(std::max<std::int64_t>(0, head - requested))) {
                decoder.seek(*selection); sourceBaseUs = sourceTimeUs = selection->selectedMediaTimeUs; sourceFrameTick = sourceAudioSamples = 0;
                requestAccepted = completedBySeek = true;
                seekEffectiveUs = std::max<std::int64_t>(0, head - selection->selectedMediaTimeUs);
            }
            // A keyframe can be momentarily absent while packets are arriving.
            // Do not consume the request in that case: retry on the next frame.
            if (requestAccepted) {
                observedDelayUs = requested;
                if (completedBySeek) { delayCompletionPending = false; emit delayApplied(seekEffectiveUs); }
                else delayCompletionPending = true;
            }
        }
        if (sourceReady && fillerRemainingUs <= 0 && requested == observedDelayUs && head > 0) {
            const auto effective = std::max<std::int64_t>(0, head - sourceTimeUs);
            const auto recoveryToleranceUs = std::max<std::int64_t>(
                3000000, static_cast<std::int64_t>(p.keyframeIntervalSeconds) * 2000000);
            const auto firstAvailable = buffer_.firstSequence();
            const bool cursorEvicted = decoder.nextSequence() > 0 && decoder.nextSequence() < firstAvailable;
            const bool excessiveDrift = effective > requested + recoveryToleranceUs;
            if (cursorEvicted || excessiveDrift) {
                if (auto selection = buffer_.selectAt(std::max<std::int64_t>(0, head - requested))) {
                    decoder.seek(*selection);
                    sourceBaseUs = sourceTimeUs = selection->selectedMediaTimeUs;
                    sourceFrameTick = sourceAudioSamples = 0;
                    const auto recoveredDelay = std::max<std::int64_t>(0, head - sourceTimeUs);
                    emit health(QString("Source cursor recovered at a keyframe (%1 s effective delay; reason: %2).")
                        .arg(recoveredDelay / 1000000.0, 0, 'f', 2)
                        .arg(cursorEvicted ? "buffer underrun" : "delay drift"));
                    emit delayApplied(recoveredDelay);
                }
            }
        }
        bool usingSource = false;
        if (sourceReady && fillerRemainingUs <= 0) {
            usingSource = decoder.videoFrameAt(sourceTimeUs, vf.get());
            const auto decoderErrorMessage = decoder.lastError();
            if (!decoderErrorMessage.isEmpty() && decoderErrorMessage != lastReportedDecoderError) {
                lastReportedDecoderError = decoderErrorMessage;
                emit decoderError(decoderErrorMessage);
            }
            if (usingSource) {
                ++sourceFrameTick;
                sourceTimeUs = sourceBaseUs + av_rescale_q_rnd(sourceFrameTick, AVRational{1, p.fps}, AVRational{1, 1000000}, AV_ROUND_NEAR_INF);
            }
        } else if (fillerRemainingUs > 0) fillerRemainingUs = std::max<std::int64_t>(0, fillerRemainingUs - frameDurationUs);
        if (usingSource != reportedForwarding) {
            // Make source/filler transitions independently decodable immediately.
            // This is especially important for platform transcoders that join or
            // refresh their decode pipeline in the middle of the persistent stream.
            vf->pict_type = AV_PICTURE_TYPE_I;
            reportedForwarding = usingSource;
            emit sourceForwardingChanged(usingSource, usingSource ? "Decoded OBS content is being forwarded." :
                (sourceConnected_.load() ? "Waiting for a decodable source frame; sending filler." : "OBS is disconnected; sending filler."));
        }
        if (usingSource) {
            heldValid = copyFramePixels(held.get(), vf.get());
            // All packets below the decoder's next cursor are already represented
            // by bounded decoded queues and can no longer be selected again.
            buffer_.discardBefore(decoder.nextSequence());
            if ((videoFrame_ % std::max(1, p.fps)) == 0) emit relayPosition(head, sourceTimeUs);
            if (delayCompletionPending && fillerRemainingUs <= 0) {
                emit delayApplied(std::max<std::int64_t>(0, head - sourceTimeUs));
                delayCompletionPending = false;
            }
        } else if (p.fillerMode == "hold" && heldValid) {
            copyFramePixels(vf.get(), held.get());
        } else if ((p.fillerMode == "image" || p.fillerMode == "hold") && standbyValid) {
            copyFramePixels(vf.get(), standby.get());
        }
        QString overlayText; { std::lock_guard lock(overlayTextMutex_); overlayText = delayOverlayText_; }
        if (overlayText != renderedOverlayText) { delayOverlayValid = false; renderedOverlayText = overlayText; }
        if (fillerRemainingUs > 0 && !overlayText.trimmed().isEmpty()) {
            if (!delayOverlayValid) delayOverlayValid = renderTextOverlay(vf.get(), delayOverlay.get(), overlayText);
            if (delayOverlayValid) copyFramePixels(vf.get(), delayOverlay.get());
        } else {
            delayOverlayValid = false;
        }
        vf->pts = videoFrame_++;
        if (!sendFrames(video.get(), vf.get(), output.get(), vs, [this, &healthVideoPackets, &healthBytes, &healthKeyframes](qsizetype n, bool key){
            ++healthVideoPackets; healthBytes += static_cast<quint64>(n); if (key) ++healthKeyframes; emit packetWritten(n);
        }, problem)) { emit error("Video publish failed: " + problem); break; }
        const auto requiredAudio = av_rescale_q(videoFrame_, video->time_base, audio->time_base);
        while (audioSample_ + af->nb_samples <= requiredAudio) {
            av_frame_make_writable(af.get());
            bool decodedAudio = false;
            if (usingSource) {
                const auto audioSourceTimeUs = sourceBaseUs + av_rescale_q(
                    sourceAudioSamples, AVRational{1, audio->sample_rate}, AVRational{1, 1000000});
                decodedAudio = decoder.audioFrameAt(audioSourceTimeUs, af.get());
                sourceAudioSamples += af->nb_samples;
            }
            if (!decodedAudio)
                av_samples_set_silence(af->data, 0, af->nb_samples, audio->ch_layout.nb_channels, audio->sample_fmt);
            af->pts = audioSample_; audioSample_ += af->nb_samples;
            if (!sendFrames(audio.get(), af.get(), output.get(), as, [this, &healthAudioPackets, &healthBytes](qsizetype n, bool){
                ++healthAudioPackets; healthBytes += static_cast<quint64>(n); emit packetWritten(n);
            }, problem)) { emit error("Audio publish failed: " + problem); return false; }
        }
        const auto healthNow = std::chrono::steady_clock::now();
        if (healthNow - healthAt >= std::chrono::seconds(10)) {
            const auto elapsedMs = std::max<std::int64_t>(1, std::chrono::duration_cast<std::chrono::milliseconds>(healthNow - healthAt).count());
            const double kbps = healthBytes * 8.0 / static_cast<double>(elapsedMs);
            emit health(QString("Output healthy: %1 Kbit/s, %2 video packets, %3 audio packets, %4 keyframes in %5 ms (%6).")
                .arg(kbps, 0, 'f', 0).arg(healthVideoPackets).arg(healthAudioPackets).arg(healthKeyframes).arg(elapsedMs)
                .arg(usingSource ? "OBS content" : "filler"));
            healthAt = healthNow; healthVideoPackets = healthAudioPackets = healthBytes = healthKeyframes = 0;
        }
        next += std::chrono::microseconds(1000000 / p.fps);
        const auto now = std::chrono::steady_clock::now();
        // Never burst old timestamps after a slow decode/seek iteration. Resume
        // pacing from wall clock and keep the destination connection continuous.
        if (next < now) next = now;
        std::this_thread::sleep_until(next);
    }
    if (token.stop_requested() && gracefulStop_ && !interrupt_) {
        const int trailerResult = av_write_trailer(output.get());
        if (trailerResult < 0) emit error("Graceful destination close failed: " + ffmpegError(trailerResult));
        else emit health("Destination stream finalized cleanly.");
    }
    return std::chrono::steady_clock::now() - connectedAt > std::chrono::seconds(30);
}
}
