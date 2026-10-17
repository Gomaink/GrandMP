#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "grandmp/windows/module_residency.hpp"

namespace grandmp::windows {

void* Win32ModulePinApi::acquireContaining(const void* address) noexcept {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                            reinterpret_cast<LPCWSTR>(address), &module)) return nullptr;
    return module;
}

void* Win32ModulePinApi::pinContaining(const void* address) noexcept {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(address), &module)) return nullptr;
    return module;
}

void Win32ModulePinApi::releaseAcquired(void* module) noexcept {
    if (module) FreeLibrary(static_cast<HMODULE>(module));
}

ResidenceResult ModuleResidency::pinHoodlum(GameVersion version, LoaderIdentity loader,
                                             void* expectedModule,
                                             const void* ownAddress, IModulePinApi& api) noexcept {
    std::lock_guard lock(mutex_);
    if (state_ == ResidenceState::pinned) return ResidenceResult::already_pinned;
    if (state_ != ResidenceState::unattempted) return ResidenceResult::previous_failure;
    if (version != GameVersion::gta_sa_10us_hoodlum) {
        state_ = ResidenceState::rejected;
        return ResidenceResult::unsupported_game;
    }
    if (loader != LoaderIdentity::smoke_ual_win32_770) {
        state_ = ResidenceState::rejected;
        return ResidenceResult::unsupported_loader;
    }
    if (!expectedModule || !ownAddress) {
        state_ = ResidenceState::failed;
        return ResidenceResult::invalid_identity;
    }
    void* acquired = api.acquireContaining(ownAddress);
    if (!acquired) {
        state_ = ResidenceState::failed;
        return ResidenceResult::acquire_failed;
    }
    if (acquired != expectedModule) {
        api.releaseAcquired(acquired);
        state_ = ResidenceState::failed;
        return ResidenceResult::module_mismatch;
    }
    void* pinned = api.pinContaining(ownAddress);
    api.releaseAcquired(acquired);
    if (!pinned) {
        state_ = ResidenceState::failed;
        return ResidenceResult::pin_failed;
    }
    if (pinned != expectedModule) {
        state_ = ResidenceState::failed;
        return ResidenceResult::pin_mismatch;
    }
    state_ = ResidenceState::pinned;
    return ResidenceResult::pinned;
}

ResidenceState ModuleResidency::state() const noexcept {
    std::lock_guard lock(mutex_);
    return state_;
}

bool ModuleResidency::permitsFutureHookPreparation() const noexcept {
    return state() == ResidenceState::pinned;
}

}  // namespace grandmp::windows
