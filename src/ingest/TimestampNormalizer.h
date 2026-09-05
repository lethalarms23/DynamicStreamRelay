#pragma once
#include <cstdint>
#include <optional>

namespace rtsp {
// Tracks one media stream's own monotonic timeline. Video and audio must each
// get their own instance: interleaving both streams' packets through a single
// shared clock forces one stream's timestamps to clamp forward past where the
// other stream's packets landed, corrupting playback timing for both.
class TimestampNormalizer {
public:
    // sharedBase lets multiple normalizers (e.g. video + audio) start a new
    // session at the same origin, so a reconnect does not introduce a fixed
    // A/V offset even though each stream's own clock advances independently
    // afterward.
    void beginSession(std::uint64_t sessionId, std::int64_t sharedBase);
    std::int64_t normalize(std::int64_t timestampUs);
    std::int64_t lastOutput() const { return lastOutput_; }
    std::uint64_t sessionId() const { return sessionId_; }
private:
    std::uint64_t sessionId_{0};
    std::optional<std::int64_t> firstInput_;
    std::int64_t sessionBase_{0};
    std::int64_t lastOutput_{-1};
};
}

