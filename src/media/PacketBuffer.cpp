#include "media/PacketBuffer.h"
#include <algorithm>

namespace rtsp {
PacketBuffer::PacketBuffer(std::int64_t maxDurationUs, std::size_t maxBytes)
    : maxDurationUs_(maxDurationUs), retainedDurationUs_(maxDurationUs), maxBytes_(maxBytes) {}

std::size_t PacketBuffer::accountedBytes(const BufferedPacket& p) {
    return sizeof(BufferedPacket)
        + static_cast<std::size_t>(p.data.capacity())
        + static_cast<std::size_t>(p.codecExtraData.capacity());
}

bool PacketBuffer::append(BufferedPacket p) {
    std::scoped_lock lock(mutex_);
    if (!storageEnabled_) return true;
    p.sequence = nextSequence_++;
    bytes_ += accountedBytes(p);
    mediaTimes_.insert(p.dtsUs);
    packets_.push_back(std::move(p));
    enforceLimits();
    return true;
}

void PacketBuffer::enforceLimits() {
    bool evictedForLimit = false;
    while (!packets_.empty()) {
        const auto span = mediaTimes_.empty() ? 0 : *mediaTimes_.rbegin() - *mediaTimes_.begin();
        const auto durationLimit = std::min(maxDurationUs_, retainedDurationUs_);
        if (bytes_ <= maxBytes_ && span <= durationLimit) break;
        bytes_ -= accountedBytes(packets_.front());
        if (const auto timestamp = mediaTimes_.find(packets_.front().dtsUs); timestamp != mediaTimes_.end())
            mediaTimes_.erase(timestamp);
        packets_.pop_front(); ++overflows_; evictedForLimit = true;
    }
    // Align to a keyframe only when enforcing a duration or memory limit has
    // actually evicted media. During normal playback discardBefore() can leave
    // the cursor inside a GOP; deleting that GOP when the next keyframe arrives
    // would remove undecoded audio and produce periodic silence bursts.
    if (evictedForLimit) needsKeyframeAlignment_ = true;
    if (!needsKeyframeAlignment_) return;
    auto key = std::find_if(packets_.begin(), packets_.end(), [](const auto& p) { return p.type == StreamType::Video && p.keyframe; });
    if (key == packets_.end()) return;
    while (key != packets_.end() && packets_.begin() != key) {
        bytes_ -= accountedBytes(packets_.front());
        if (const auto timestamp = mediaTimes_.find(packets_.front().dtsUs); timestamp != mediaTimes_.end())
            mediaTimes_.erase(timestamp);
        packets_.pop_front();
        key = std::find_if(packets_.begin(), packets_.end(), [](const auto& p) { return p.type == StreamType::Video && p.keyframe; });
    }
    needsKeyframeAlignment_ = false;
}

std::optional<BufferedPacket> PacketBuffer::packet(std::uint64_t seq) const {
    std::scoped_lock lock(mutex_);
    auto it = std::find_if(packets_.begin(), packets_.end(), [seq](const auto& p){ return p.sequence == seq; });
    return it == packets_.end() ? std::nullopt : std::optional<BufferedPacket>(*it);
}
std::optional<BufferedPacket> PacketBuffer::packetAtOrAfter(std::uint64_t seq) const {
    std::scoped_lock lock(mutex_);
    auto it = std::find_if(packets_.begin(), packets_.end(), [seq](const auto& p){ return p.sequence >= seq; });
    return it == packets_.end() ? std::nullopt : std::optional<BufferedPacket>(*it);
}

std::optional<BufferedPacket> PacketBuffer::nearestKeyframeAtOrBefore(std::int64_t time) const {
    std::scoped_lock lock(mutex_); std::optional<BufferedPacket> found;
    for (const auto& p : packets_) {
        if (p.dtsUs <= time && p.type == StreamType::Video && p.keyframe
            && (!found || p.dtsUs > found->dtsUs)) found = p;
    }
    return found;
}

std::optional<CursorSelection> PacketBuffer::selectAt(std::int64_t desired) const {
    std::scoped_lock lock(mutex_); const BufferedPacket* video = nullptr;
    for (const auto& p : packets_) {
        if (p.dtsUs <= desired && p.type == StreamType::Video && p.keyframe
            && (!video || p.dtsUs > video->dtsUs)) video = &p;
    }
    if (!video) return std::nullopt;
    const BufferedPacket* audio = nullptr;
    for (const auto& p : packets_) {
        if (p.type == StreamType::Audio && p.ptsUs >= video->ptsUs
            && (!audio || p.ptsUs < audio->ptsUs)) audio = &p;
    }
    return CursorSelection{video->sequence, audio ? audio->sequence : video->sequence, video->ptsUs};
}

void PacketBuffer::discardBefore(std::uint64_t seq) {
    std::scoped_lock lock(mutex_);
    while (!packets_.empty() && packets_.front().sequence < seq) {
        bytes_ -= accountedBytes(packets_.front());
        if (const auto timestamp = mediaTimes_.find(packets_.front().dtsUs); timestamp != mediaTimes_.end())
            mediaTimes_.erase(timestamp);
        packets_.pop_front();
    }
}
void PacketBuffer::setLimits(std::int64_t maxDurationUs, std::size_t maxBytes) {
    std::scoped_lock lock(mutex_);
    maxDurationUs_ = std::max<std::int64_t>(1000000, maxDurationUs);
    maxBytes_ = std::max<std::size_t>(64 * 1024 * 1024, maxBytes);
    enforceLimits();
}
void PacketBuffer::setStoragePolicy(bool enabled, std::int64_t retainedDurationUs) {
    std::scoped_lock lock(mutex_);
    storageEnabled_ = enabled;
    retainedDurationUs_ = std::max<std::int64_t>(0, retainedDurationUs);
    if (!storageEnabled_) {
        packets_.clear();
        mediaTimes_.clear();
        bytes_ = 0;
        needsKeyframeAlignment_ = false;
        return;
    }
    enforceLimits();
}
std::int64_t PacketBuffer::durationUs() const { std::scoped_lock lock(mutex_); return mediaTimes_.size() < 2 ? 0 : *mediaTimes_.rbegin() - *mediaTimes_.begin(); }
std::int64_t PacketBuffer::inputHeadUs() const { std::scoped_lock lock(mutex_); return mediaTimes_.empty() ? 0 : *mediaTimes_.rbegin(); }
std::size_t PacketBuffer::memoryBytes() const { std::scoped_lock lock(mutex_); return bytes_; }
std::size_t PacketBuffer::size() const { std::scoped_lock lock(mutex_); return packets_.size(); }
std::uint64_t PacketBuffer::overflowCount() const { std::scoped_lock lock(mutex_); return overflows_; }
std::uint64_t PacketBuffer::firstSequence() const { std::scoped_lock lock(mutex_); return packets_.empty() ? nextSequence_ : packets_.front().sequence; }
}
