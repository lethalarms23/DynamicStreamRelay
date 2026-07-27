#include "media/DelayController.h"
#include <algorithm>

namespace rtsp {
DelayController::DelayController(std::int64_t maximum) : maximum_(maximum) {}
void DelayController::setMaximumDelay(std::int64_t maximum) {
    maximum_ = std::max<std::int64_t>(0, maximum);
    requested_ = std::min(requested_, maximum_);
    effective_ = std::min(effective_, maximum_);
    remainingFiller_ = std::min(remainingFiller_, std::max<std::int64_t>(0, maximum_ - effective_));
}
DelayDecision DelayController::request(std::int64_t wanted, std::int64_t head, std::int64_t cursor, std::int64_t available) {
    wanted = std::clamp<std::int64_t>(wanted, 0, maximum_); requested_ = wanted;
    effective_ = std::max<std::int64_t>(0, head - cursor);
    if (wanted > effective_) {
        remainingFiller_ = wanted - effective_; increaseStarted_ = false;
        return {DelayAction::InsertFiller, remainingFiller_, cursor};
    }
    if (wanted < effective_) return {DelayAction::JumpForward, 0, std::max<std::int64_t>(0, head - wanted)};
    if (available < wanted) return {DelayAction::WaitForBuffer, 0, cursor};
    return {};
}
void DelayController::advanceFiller(std::int64_t amount) {
    if (amount > 0) increaseStarted_ = true;
    auto consumed = std::min(amount, remainingFiller_); remainingFiller_ -= consumed; effective_ += consumed;
}
void DelayController::completeJump(std::int64_t selected, std::int64_t head) { effective_ = std::max<std::int64_t>(0, head - selected); remainingFiller_ = 0; }
void DelayController::setCursor(std::int64_t cursor, std::int64_t head) { effective_ = std::max<std::int64_t>(0, head - cursor); }
bool DelayController::cancelIncrease() { if (increaseStarted_) return false; remainingFiller_ = 0; requested_ = effective_; return true; }
}
