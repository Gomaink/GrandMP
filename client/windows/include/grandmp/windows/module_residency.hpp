#pragma once

#include "grandmp/windows/executable_detection.hpp"

#include <mutex>

namespace grandmp::windows {

// The backend isolates the irreversible Windows operation from policy tests.
class IModulePinApi {
public:
    virtual ~IModulePinApi() = default;
    [[nodiscard]] virtual void* acquireContaining(const void* address) noexcept = 0;
    [[nodiscard]] virtual void* pinContaining(const void* address) noexcept = 0;
    virtual void releaseAcquired(void* module) noexcept = 0;
};

class Win32ModulePinApi final : public IModulePinApi {
public:
    [[nodiscard]] void* acquireContaining(const void* address) noexcept override;
    [[nodiscard]] void* pinContaining(const void* address) noexcept override;
    void releaseAcquired(void* module) noexcept override;
};

enum class LoaderIdentity { unknown, smoke_ual_win32_770 };
enum class ResidenceState { unattempted, pinned, rejected, failed };
enum class ResidenceResult {
    pinned, already_pinned, unsupported_game, unsupported_loader, invalid_identity,
    acquire_failed, module_mismatch, pin_failed, pin_mismatch, previous_failure
};

class ModuleResidency final {
public:
    [[nodiscard]] ResidenceResult pinHoodlum(GameVersion version, LoaderIdentity loader,
                                               void* expectedModule,
                                               const void* ownAddress, IModulePinApi& api) noexcept;
    [[nodiscard]] ResidenceState state() const noexcept;
    [[nodiscard]] bool permitsFutureHookPreparation() const noexcept;

private:
    mutable std::mutex mutex_;
    ResidenceState state_ = ResidenceState::unattempted;
};

}  // namespace grandmp::windows
