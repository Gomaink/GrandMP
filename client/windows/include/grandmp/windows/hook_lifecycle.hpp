#pragma once

#include "grandmp/windows/executable_detection.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>

namespace grandmp::windows {

using CallBytes = std::array<std::uint8_t, 5>;

// The current slice supplies only a controlled-memory test implementation.
// A game backend must establish thread quiescence; a five-byte code write is
// not made atomic by this interface.
enum class PatchAttempt { applied, mismatch, failed_unchanged, partial_or_unknown };

class ICallSite {
public:
    virtual ~ICallSite() = default;
    [[nodiscard]] virtual PatchAttempt exchangeIfEqual(const CallBytes& expected,
                                                       const CallBytes& replacement) noexcept = 0;
};

class CallbackGate final : public std::enable_shared_from_this<CallbackGate> {
public:
    class Lease final {
    public:
        Lease() noexcept = default;
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease(Lease&& other) noexcept;
        Lease& operator=(Lease&& other) noexcept;
        ~Lease();

        [[nodiscard]] explicit operator bool() const noexcept { return owner_ != nullptr; }

    private:
        friend class CallbackGate;
        explicit Lease(std::shared_ptr<CallbackGate> owner) noexcept : owner_(std::move(owner)) {}
        std::shared_ptr<CallbackGate> owner_;
    };

    [[nodiscard]] Lease tryEnter() noexcept;
    void disableDiagnostics() noexcept;
    [[nodiscard]] bool diagnosticsEnabled() const noexcept;
    [[nodiscard]] bool callbackInFlight() const noexcept;

private:
    friend class HookManager;
    void enableDiagnostics() noexcept;
    mutable std::mutex mutex_;
    bool enabled_ = false;
    bool entered_ = false;
};

enum class HookState { idle, installed, logically_disabled, conflict, uncertain };
enum class HookResult {
    installed, restored, disabled, unsupported_game, residence_unverified,
    unexpected_bytes, duplicate, write_failed, partial_write, busy,
    quiescence_unverified, foreign_patch, invalid_state
};

enum class ResidenceEvidence { unverified, externally_established };
enum class QuiescenceEvidence { unverified, externally_established };

// Policy and ownership model only. No ICallSite for GTA is implemented.
class HookManager final {
public:
    // Destroying a registration that may still be referenced is a contract
    // violation. This is a synthetic model, not a GTA patch backend.
    ~HookManager() noexcept;

    [[nodiscard]] HookResult install(const ExecutableInspection& inspection,
                                     std::span<const std::byte, 32> sha256,
                                     ResidenceEvidence residence,
                                     QuiescenceEvidence quiescence,
                                     std::shared_ptr<ICallSite> site,
                                     std::shared_ptr<CallbackGate> callback,
                                     const CallBytes& original, const CallBytes& replacement) noexcept;
    [[nodiscard]] HookResult disableDiagnostics() noexcept;
    [[nodiscard]] HookResult restore(QuiescenceEvidence quiescence) noexcept;
    [[nodiscard]] HookState state() const noexcept;
    [[nodiscard]] bool ownsPatch() const noexcept;
    [[nodiscard]] bool mustRemainResident() const noexcept;

private:
    mutable std::mutex mutex_;
    HookState state_ = HookState::idle;
    std::shared_ptr<ICallSite> site_;
    std::shared_ptr<CallbackGate> callback_;
    CallBytes original_{};
    CallBytes replacement_{};
};

}  // namespace grandmp::windows
