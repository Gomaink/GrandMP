#pragma once

#include "grandmp/windows/executable_detection.hpp"

namespace grandmp::windows::remote_player_binding {

// Offline evidence is not a live-process admission, slot lease or dispatch proof.
// No address/function from this report is callable through this API.
struct HoodlumContract {
    bool exactExecutable = false;
    bool playerConstructorWindows = false;
    bool slotFactoryWindows = false;
    bool slotDestructorWindows = false;
    bool processControlVtable = false;
    // These obligations remain unproven, even if every code window matches.
    static constexpr bool additionalSlotAvailable = false;
    static constexpr bool completePlayerLifecycle = false;
    static constexpr bool originalDispatchAbi = false;
    static constexpr bool completePadFocusCameraContext = false;
    static constexpr bool executionApproved = false;
};

[[nodiscard]] HoodlumContract inspectHoodlumPlayerContract(std::span<const std::byte> file) noexcept;

} // namespace grandmp::windows::remote_player_binding
