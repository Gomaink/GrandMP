#include "grandmp/windows/hook_lifecycle.hpp"

#include <exception>
#include <mutex>
#include <utility>

namespace grandmp::windows {

HookManager::~HookManager() noexcept {
    if (site_ != nullptr) std::terminate();
}

CallbackGate::Lease::Lease(Lease&& other) noexcept : owner_(std::move(other.owner_)) {}

CallbackGate::Lease& CallbackGate::Lease::operator=(Lease&& other) noexcept {
    if (this != &other) {
        if (owner_) {
            std::lock_guard lock(owner_->mutex_);
            owner_->entered_ = false;
        }
        owner_ = std::move(other.owner_);
    }
    return *this;
}

CallbackGate::Lease::~Lease() {
    if (owner_) {
        std::lock_guard lock(owner_->mutex_);
        owner_->entered_ = false;
    }
}

CallbackGate::Lease CallbackGate::tryEnter() noexcept {
    auto owner = weak_from_this().lock();
    if (!owner) return {};
    std::lock_guard lock(mutex_);
    if (!enabled_ || entered_) return {};
    entered_ = true;
    return Lease(std::move(owner));
}

void CallbackGate::enableDiagnostics() noexcept { std::lock_guard lock(mutex_); enabled_ = true; }
void CallbackGate::disableDiagnostics() noexcept { std::lock_guard lock(mutex_); enabled_ = false; }
bool CallbackGate::diagnosticsEnabled() const noexcept { std::lock_guard lock(mutex_); return enabled_; }
bool CallbackGate::callbackInFlight() const noexcept { std::lock_guard lock(mutex_); return entered_; }

HookResult HookManager::install(const ExecutableInspection& inspection,
                                std::span<const std::byte, 32> sha256,
                                ResidenceEvidence residence,
                                QuiescenceEvidence quiescence,
                                std::shared_ptr<ICallSite> site,
                                std::shared_ptr<CallbackGate> callback,
                                const CallBytes& original, const CallBytes& replacement) noexcept {
    std::lock_guard lock(mutex_);
    if (state_ != HookState::idle) return HookResult::duplicate;
    if (recognizeGameVersion(inspection, sha256) != GameVersion::gta_sa_10us_hoodlum)
        return HookResult::unsupported_game;
    if (residence != ResidenceEvidence::externally_established) return HookResult::residence_unverified;
    if (quiescence != QuiescenceEvidence::externally_established) return HookResult::quiescence_unverified;
    if (!site || !callback || original == replacement) return HookResult::invalid_state;

    // Keep both objects alive even when the backend reports an uncertain write.
    site_ = std::move(site);
    callback_ = std::move(callback);
    original_ = original;
    replacement_ = replacement;
    switch (site_->exchangeIfEqual(original_, replacement_)) {
    case PatchAttempt::applied:
        state_ = HookState::installed;
        callback_->enableDiagnostics();
        return HookResult::installed;
    case PatchAttempt::mismatch:
        site_.reset();
        callback_.reset();
        return HookResult::unexpected_bytes;
    case PatchAttempt::failed_unchanged:
        site_.reset();
        callback_.reset();
        return HookResult::write_failed;
    case PatchAttempt::partial_or_unknown:
        state_ = HookState::uncertain;
        return HookResult::partial_write;
    }
    state_ = HookState::uncertain;
    return HookResult::partial_write;
}

HookResult HookManager::disableDiagnostics() noexcept {
    std::lock_guard lock(mutex_);
    if (state_ != HookState::installed && state_ != HookState::logically_disabled)
        return HookResult::invalid_state;
    callback_->disableDiagnostics();
    state_ = HookState::logically_disabled;
    return HookResult::disabled;
}

HookResult HookManager::restore(QuiescenceEvidence quiescence) noexcept {
    std::lock_guard lock(mutex_);
    if (state_ != HookState::logically_disabled) return HookResult::invalid_state;
    if (quiescence != QuiescenceEvidence::externally_established) return HookResult::quiescence_unverified;
    if (callback_->callbackInFlight()) return HookResult::busy;
    switch (site_->exchangeIfEqual(replacement_, original_)) {
    case PatchAttempt::applied:
        site_.reset();
        callback_.reset();
        state_ = HookState::idle;
        return HookResult::restored;
    case PatchAttempt::mismatch:
        state_ = HookState::conflict;
        return HookResult::foreign_patch;
    case PatchAttempt::failed_unchanged:
        return HookResult::write_failed;
    case PatchAttempt::partial_or_unknown:
        state_ = HookState::uncertain;
        return HookResult::partial_write;
    }
    state_ = HookState::uncertain;
    return HookResult::partial_write;
}

bool HookManager::ownsPatch() const noexcept {
    std::lock_guard lock(mutex_);
    return state_ == HookState::installed || state_ == HookState::logically_disabled;
}

bool HookManager::mustRemainResident() const noexcept {
    std::lock_guard lock(mutex_);
    return site_ != nullptr;
}

HookState HookManager::state() const noexcept {
    std::lock_guard lock(mutex_);
    return state_;
}

}  // namespace grandmp::windows
