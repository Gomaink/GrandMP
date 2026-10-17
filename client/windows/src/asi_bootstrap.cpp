#include "grandmp/windows/asi_bootstrap.hpp"

namespace grandmp::windows {

AsiBootstrapResult AsiBootstrapController::requestEntry() noexcept {
    auto expected = AsiBootstrapState::cold;
    if (state_.compare_exchange_strong(expected, AsiBootstrapState::entry_seen,
                                       std::memory_order_acq_rel)) {
        return AsiBootstrapResult::accepted;
    }
    return expected == AsiBootstrapState::entry_seen ||
                   expected == AsiBootstrapState::initializing ||
                   expected == AsiBootstrapState::resident ||
                   expected == AsiBootstrapState::logically_disabled
        ? AsiBootstrapResult::already_requested
        : AsiBootstrapResult::in_progress;
}

AsiBootstrapResult AsiBootstrapController::beginDeferred() noexcept {
    auto expected = AsiBootstrapState::entry_seen;
    if (state_.compare_exchange_strong(expected, AsiBootstrapState::initializing,
                                       std::memory_order_acq_rel)) {
        return AsiBootstrapResult::accepted;
    }
    if (expected == AsiBootstrapState::initializing) return AsiBootstrapResult::in_progress;
    if (expected == AsiBootstrapState::resident) return AsiBootstrapResult::already_resident;
    if (expected == AsiBootstrapState::logically_disabled) return AsiBootstrapResult::disabled;
    return AsiBootstrapResult::not_requested;
}

AsiBootstrapResult AsiBootstrapController::publishResidence(const bool acquired) noexcept {
    auto expected = AsiBootstrapState::initializing;
    const auto target = acquired ? AsiBootstrapState::resident : AsiBootstrapState::failed;
    if (!state_.compare_exchange_strong(expected, target, std::memory_order_acq_rel)) {
        return AsiBootstrapResult::in_progress;
    }
    return acquired ? AsiBootstrapResult::completed : AsiBootstrapResult::residence_failed;
}

AsiBootstrapResult AsiBootstrapController::disableLogically() noexcept {
    auto expected = AsiBootstrapState::resident;
    if (state_.compare_exchange_strong(expected, AsiBootstrapState::logically_disabled,
                                       std::memory_order_acq_rel)) {
        return AsiBootstrapResult::disabled;
    }
    return expected == AsiBootstrapState::logically_disabled
        ? AsiBootstrapResult::disabled : AsiBootstrapResult::not_requested;
}

AsiBootstrapState AsiBootstrapController::state() const noexcept {
    return state_.load(std::memory_order_acquire);
}

}  // namespace grandmp::windows
