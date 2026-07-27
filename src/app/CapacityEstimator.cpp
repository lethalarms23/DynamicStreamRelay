#include "app/CapacityEstimator.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <vector>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
}
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <sys/sysinfo.h>
#endif

namespace rtsp {
namespace {
struct EncoderInstance {
    AVCodecContext* context{};
    AVFrame* frame{};
    AVPacket* packet{};
    ~EncoderInstance() {
        av_packet_free(&packet);
        av_frame_free(&frame);
        avcodec_free_context(&context);
    }
};

quint64 availableMemoryMiB() {
#ifdef Q_OS_WIN
    MEMORYSTATUSEX status{}; status.dwLength=sizeof(status);
    return GlobalMemoryStatusEx(&status) ? status.ullAvailPhys / 1048576ULL : 0;
#else
    struct sysinfo info{};
    return sysinfo(&info)==0 ? static_cast<quint64>(info.freeram) * info.mem_unit / 1048576ULL : 0;
#endif
}

std::unique_ptr<EncoderInstance> openEncoder(const QString& name, const CapacitySettings& s) {
    const auto bytes=name.toLatin1(); const AVCodec* codec=avcodec_find_encoder_by_name(bytes.constData());
    if(!codec)return {};
    auto instance=std::make_unique<EncoderInstance>();
    instance->context=avcodec_alloc_context3(codec);
    if(!instance->context)return {};
    auto* c=instance->context;
    c->width=s.width;c->height=s.height;c->pix_fmt=AV_PIX_FMT_YUV420P;
    c->time_base={1,s.fps};c->framerate={s.fps,1};c->bit_rate=s.videoBitrateKbps*1000LL;
    c->rc_max_rate=c->bit_rate;c->rc_buffer_size=c->bit_rate*2;c->gop_size=s.fps*2;c->max_b_frames=0;
    AVDictionary* options=nullptr;
    if(name=="libx264"){av_dict_set(&options,"preset","veryfast",0);av_dict_set(&options,"tune","zerolatency",0);}
    else if(name=="h264_nvenc"){av_dict_set(&options,"preset","p4",0);av_dict_set(&options,"tune","ll",0);av_dict_set(&options,"rc","cbr",0);}
    const int opened=avcodec_open2(c,codec,&options);av_dict_free(&options);
    if(opened<0)return {};
    instance->frame=av_frame_alloc();instance->packet=av_packet_alloc();
    if(!instance->frame||!instance->packet)return {};
    instance->frame->format=c->pix_fmt;instance->frame->width=c->width;instance->frame->height=c->height;
    if(av_frame_get_buffer(instance->frame,32)<0)return {};
    return instance;
}

bool encodeFrame(EncoderInstance& instance, qint64 pts) {
    if(av_frame_make_writable(instance.frame)<0)return false;
    // Moving deterministic pattern is less optimistic than an unchanging black frame.
    for(int y=0;y<instance.frame->height;++y)
        for(int x=0;x<instance.frame->width;++x)
            instance.frame->data[0][y*instance.frame->linesize[0]+x]=static_cast<uint8_t>((x+y+pts*3)&255);
    for(int plane=1;plane<3;++plane)
        for(int y=0;y<instance.frame->height/2;++y)
            std::fill_n(instance.frame->data[plane]+y*instance.frame->linesize[plane],instance.frame->width/2,
                        static_cast<uint8_t>(plane==1?96+(pts&31):160-(pts&31)));
    instance.frame->pts=pts;
    if(avcodec_send_frame(instance.context,instance.frame)<0)return false;
    while(true){
        const int rc=avcodec_receive_packet(instance.context,instance.packet);
        if(rc==AVERROR(EAGAIN)||rc==AVERROR_EOF)break;
        if(rc<0)return false;
        av_packet_unref(instance.packet);
    }
    return true;
}
}

CapacityEstimator::CapacityEstimator(QObject* parent):QObject(parent){
    qRegisterMetaType<CapacityResult>();
}
CapacityEstimator::~CapacityEstimator(){cancel();}
void CapacityEstimator::cancel(){if(worker_.joinable()){worker_.request_stop();worker_.join();}busy_=false;}
void CapacityEstimator::start(CapacitySettings settings){
    if(busy_.exchange(true))return;
    worker_=std::jthread([this,settings](std::stop_token token){
        auto result=benchmark(settings,token,[this](QString message){emit progress(std::move(message));});
        busy_=false;
        if(!token.stop_requested())emit completed(std::move(result));
    });
}
CapacityResult CapacityEstimator::benchmark(const CapacitySettings& settings,std::stop_token token,
                                             const std::function<void(QString)>& progress){
    CapacityResult result;result.requestedEncoder=settings.encoder;result.availableMemoryMiB=availableMemoryMiB();
    const QStringList order=settings.encoder=="auto"
        ?QStringList{"h264_nvenc","h264_qsv","h264_amf","libx264"}:QStringList{settings.encoder};
    std::unique_ptr<EncoderInstance> primary;
    for(const auto& name:order){
        progress("Probing "+name+"…");primary=openEncoder(name,settings);
        if(primary){result.actualEncoder=name;break;}
    }
    if(!primary){result.detail="No requested encoder could be opened.";return result;}
    progress("Measuring "+result.actualEncoder+" throughput…");
    const int targetFrames=std::max(settings.fps*settings.benchmarkSeconds*settings.maximumCandidates,settings.fps*2);
    const auto started=std::chrono::steady_clock::now();int encoded=0;
    for(;encoded<targetFrames&&!token.stop_requested();++encoded)
        if(!encodeFrame(*primary,encoded))break;
    const double elapsed=std::max(0.001,std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count());
    result.measuredFps=encoded/elapsed;
    result.throughputLimit=std::clamp(static_cast<int>(std::floor(
        result.measuredFps/settings.fps*settings.safetyPercent/100.0)),0,settings.maximumCandidates);
    primary.reset();
    progress("Checking concurrent encoder contexts…");
    std::vector<std::unique_ptr<EncoderInstance>> opened;
    for(int i=0;i<settings.maximumCandidates&&!token.stop_requested();++i){
        auto candidate=openEncoder(result.actualEncoder,settings);
        if(!candidate)break;
        opened.push_back(std::move(candidate));
    }
    result.encoderOpenLimit=static_cast<int>(opened.size());
    const double framePoolMiB=settings.width*settings.height*1.5*18.0/1048576.0;
    const int perStreamMiB=std::max(256,settings.bufferMiB+static_cast<int>(std::ceil(framePoolMiB))+192);
    result.memoryLimit=result.availableMemoryMiB>0
        ?std::clamp(static_cast<int>((result.availableMemoryMiB*0.75)/perStreamMiB),0,settings.maximumCandidates)
        :settings.maximumCandidates;
    result.safeStreams=std::min({result.encoderOpenLimit,result.throughputLimit,result.memoryLimit});
    result.detail=QString("%1 sustained approximately %2 fps at %3×%4. Limits — encoder contexts: %5, measured throughput: %6, available RAM: %7.")
        .arg(result.actualEncoder).arg(result.measuredFps,0,'f',1).arg(settings.width).arg(settings.height)
        .arg(result.encoderOpenLimit).arg(result.throughputLimit).arg(result.memoryLimit);
    return result;
}
}

