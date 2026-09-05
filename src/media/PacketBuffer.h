#pragma once

#include <QByteArray>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <set>

namespace rtsp {
enum class StreamType { Video, Audio };
struct BufferedPacket {
    StreamType type{StreamType::Video};
    std::int64_t dtsUs{0};
    std::int64_t ptsUs{0};
    std::int64_t durationUs{0};
    bool keyframe{false};
    std::uint64_t codecGeneration{0};
    QByteArray data;
    std::int64_t arrivalMonotonicUs{0};
    std::uint64_t sourceSessionId{0};
    std::uint64_t sequence{0};
    int codecId{0};
    QByteArray codecExtraData;
    int width{0}, height{0};
    int sampleRate{0}, channels{0};
};

struct CursorSelection {
    std::uint64_t videoSequence{0};
    std::uint64_t audioSequence{0};
    std::int64_t selectedMediaTimeUs{0};
};

class PacketBuffer {
public:
    PacketBuffer(std::int64_t maxDurationUs, std::size_t maxBytes);
    bool append(BufferedPacket packet);
    std::optional<BufferedPacket> packet(std::uint64_t sequence) const;
    std::optional<BufferedPacket> packetAtOrAfter(std::uint64_t sequence) const;
    std::optional<BufferedPacket> nearestKeyframeAtOrBefore(std::int64_t timeUs) const;
    std::optional<CursorSelection> selectAt(std::int64_t desiredTimeUs) const;
    void discardBefore(std::uint64_t sequence);
    void setLimits(std::int64_t maxDurationUs, std::size_t maxBytes);
    void setStoragePolicy(bool enabled, std::int64_t retainedDurationUs);
    std::int64_t durationUs() const;
    std::int64_t inputHeadUs() const;
    std::size_t memoryBytes() const;
    std::size_t size() const;
    std::uint64_t overflowCount() const;
    std::uint64_t firstSequence() const;
private:
    static std::size_t accountedBytes(const BufferedPacket& packet);
    void enforceLimits();
    mutable std::mutex mutex_;
    std::deque<BufferedPacket> packets_;
    std::multiset<std::int64_t> mediaTimes_;
    std::int64_t maxDurationUs_;
    std::int64_t retainedDurationUs_;
    std::size_t maxBytes_;
    bool storageEnabled_{true};
    bool needsKeyframeAlignment_{false};
    std::size_t bytes_{0};
    std::uint64_t nextSequence_{1};
    std::uint64_t overflows_{0};
};
}
