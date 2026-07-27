#pragma once
#include <cstdint>

namespace rtsp {
enum class DelayAction { None, InsertFiller, JumpForward, WaitForBuffer };
struct DelayDecision {
    DelayAction action{DelayAction::None};
    std::int64_t fillerDurationUs{0};
    std::int64_t desiredSourceTimeUs{0};
};

class DelayController {
public:
    explicit DelayController(std::int64_t maximumDelayUs);
    DelayDecision request(std::int64_t requestedDelayUs, std::int64_t inputHeadUs,
                          std::int64_t readCursorUs, std::int64_t availableDurationUs);
    void advanceFiller(std::int64_t durationUs);
    void completeJump(std::int64_t selectedSourceTimeUs, std::int64_t inputHeadUs);
    void setCursor(std::int64_t sourceTimeUs, std::int64_t inputHeadUs);
    std::int64_t requestedDelayUs() const { return requested_; }
    std::int64_t effectiveDelayUs() const { return effective_; }
    std::int64_t remainingFillerUs() const { return remainingFiller_; }
    void setMaximumDelay(std::int64_t maximumDelayUs);
    bool cancelIncrease();
private:
    std::int64_t maximum_; std::int64_t requested_{0}; std::int64_t effective_{0};
    std::int64_t remainingFiller_{0}; bool increaseStarted_{false};
};
}
