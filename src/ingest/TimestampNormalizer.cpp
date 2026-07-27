#include "ingest/TimestampNormalizer.h"
#include <algorithm>

namespace rtsp {
void TimestampNormalizer::beginSession(std::uint64_t id) {
    sessionId_ = id; firstInput_.reset(); sessionBase_ = lastOutput_ < 0 ? 0 : lastOutput_ + 1;
}
std::int64_t TimestampNormalizer::normalize(std::int64_t input) {
    if (!firstInput_) firstInput_ = input;
    auto candidate = sessionBase_ + std::max<std::int64_t>(0, input - *firstInput_);
    lastOutput_ = std::max(candidate, lastOutput_ + 1);
    return lastOutput_;
}
}
