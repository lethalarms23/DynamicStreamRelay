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
    if (!packets_.empty() && p.dtsUs < packets_.back().dtsUs) return false;
    p.sequence = nextSequence_++;
    bytes_ += accountedBytes(p);
    packets_.push_back(std::move(p));
    enforceLimits();
    return true;
}

void PacketBuffer::enforceLimits() {
    while (!packets_.empty()) {
        const auto span = packets_.back().dtsUs - packets_.front().dtsUs;
        const auto durationLimit = std::min(maxDurationUs_, retainedDurationUs_);
        if (bytes_ <= maxBytes_ && span <= durationLimit) break;
        bytes_ -= accountedBytes(packets_.front());
        packets_.pop_front(); ++overflows_;
    }
    // A decodable buffer must begin no earlier than its first remaining video keyframe.
    auto key = std::find_if(packets_.begin(), packets_.end(), [](const auto& p) { return p.type == StreamType::Video && p.keyframe; });
    while (key != packets_.end() && packets_.begin() != key) {
        bytes_ -= accountedBytes(packets_.front());
        packets_.pop_front();
        key = std::find_if(packets_.begin(), packets_.end(), [](const auto& p) { return p.type == StreamType::Video && p.keyframe; });
    }
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
        if (p.dtsUs > time) break;
        if (p.type == StreamType::Video && p.keyframe) found = p;
    }
    return found;
}

std::optional<CursorSelection> PacketBuffer::selectAt(std::int64_t desired) const {
    std::scoped_lock lock(mutex_); const BufferedPacket* video = nullptr;
    for (const auto& p : packets_) {
        if (p.dtsUs > desired) break;
        if (p.type == StreamType::Video && p.keyframe) video = &p;
    }
    if (!video) return std::nullopt;
    const BufferedPacket* audio = nullptr;
    for (const auto& p : packets_) {
        if (p.type == StreamType::Audio && p.ptsUs >= video->ptsUs) { audio = &p; break; }
    }
    return CursorSelection{video->sequence, audio ? audio->sequence : video->sequence, video->ptsUs};
}

void PacketBuffer::discardBefore(std::uint64_t seq) {
    std::scoped_lock lock(mutex_);
    while (!packets_.empty() && packets_.front().sequence < seq) {
        bytes_ -= accountedBytes(packets_.front()); packets_.pop_front();
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
        bytes_ = 0;
        return;
    }
    enforceLimits();
}
std::int64_t PacketBuffer::durationUs() const { std::scoped_lock lock(mutex_); return packets_.size() < 2 ? 0 : packets_.back().dtsUs - packets_.front().dtsUs; }
std::int64_t PacketBuffer::inputHeadUs() const { std::scoped_lock lock(mutex_); return packets_.empty() ? 0 : packets_.back().dtsUs; }
std::size_t PacketBuffer::memoryBytes() const { std::scoped_lock lock(mutex_); return bytes_; }
std::size_t PacketBuffer::size() const { std::scoped_lock lock(mutex_); return packets_.size(); }
std::uint64_t PacketBuffer::overflowCount() const { std::scoped_lock lock(mutex_); return overflows_; }
std::uint64_t PacketBuffer::firstSequence() const { std::scoped_lock lock(mutex_); return packets_.empty() ? nextSequence_ : packets_.front().sequence; }
}
