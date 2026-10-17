#pragma once

#include <atomic>

namespace grandmp::windows {

enum class AsiBootstrapState : unsigned char {
    cold,
    entry_seen,
    initializing,
    resident,
    failed,
    logically_disabled,
};

enum class AsiBootstrapResult : unsigned char {
    accepted,
    already_requested,
    not_requested,
    in_progress,
    completed,
    residence_failed,
    already_resident,
    disabled,
};

// This controller records bootstrap intent only. The caller must invoke
// beginDeferred() from a separately established safe point. It owns no DLL
// reference and cannot prove loader-lock or game-thread properties.
class AsiBootstrapController final {
public:
    [[nodiscard]] AsiBootstrapResult requestEntry() noexcept;
    [[nodiscard]] AsiBootstrapResult beginDeferred() noexcept;
    [[nodiscard]] AsiBootstrapResult publishResidence(bool acquired) noexcept;
    [[nodiscard]] AsiBootstrapResult disableLogically() noexcept;
    [[nodiscard]] AsiBootstrapState state() const noexcept;

private:
    std::atomic<AsiBootstrapState> state_{AsiBootstrapState::cold};
};

}  // namespace grandmp::windows
