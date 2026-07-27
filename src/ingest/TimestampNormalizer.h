#pragma once
#include <cstdint>
#include <optional>

namespace rtsp {
class TimestampNormalizer {
public:
    void beginSession(std::uint64_t sessionId);
    std::int64_t normalize(std::int64_t timestampUs);
    std::uint64_t sessionId() const { return sessionId_; }
private:
    std::uint64_t sessionId_{0};
    std::optional<std::int64_t> firstInput_;
    std::int64_t sessionBase_{0};
    std::int64_t lastOutput_{-1};
};
}

