#include "grandmp/windows/callback_lifecycle.hpp"

#include <algorithm>
#include <utility>

namespace grandmp::windows {
namespace {
thread_local const CallbackLifecycleController* activeController = nullptr;
}

std::shared_ptr<CallbackLifecycleController> CallbackLifecycleController::create() {
    return std::shared_ptr<CallbackLifecycleController>(new CallbackLifecycleController());
}

LifecycleResult CallbackLifecycleController::requestInitialization() noexcept {
    std::lock_guard lock(mutex_);
    if (state_ == CallbackLifecycleState::initialization_queued ||
        state_ == CallbackLifecycleState::initializing || state_ == CallbackLifecycleState::ready)
        return LifecycleResult::already_requested;
    if (state_ != CallbackLifecycleState::cold) return LifecycleResult::invalid_transition;
    state_ = CallbackLifecycleState::initialization_queued;
    changed_.notify_all();
    return LifecycleResult::accepted;
}

LifecycleResult CallbackLifecycleController::runDeferredInitialization(const Work& initialize,
                                                                        const Cleanup& rollback) noexcept {
    {
        std::lock_guard lock(mutex_);
        if (state_ != CallbackLifecycleState::initialization_queued || initializationRunning_)
            return LifecycleResult::invalid_transition;
        initializationRunning_ = true;
        state_ = CallbackLifecycleState::initializing;
    }

    bool initialized = false;
    try { initialized = initialize && initialize(); } catch (...) { initialized = false; }

    bool cancelled = false;
    bool cleanupAssignmentFailed = false;
    if (initialized) {
        try {
            std::lock_guard lock(mutex_);
            cancelled = shutdownRequested_;
            if (!cancelled) {
                cleanup_ = rollback;
                cleanupPending_ = static_cast<bool>(cleanup_);
            }
        } catch (...) {
            cleanupAssignmentFailed = true;
        }
    }

    if (initialized && !cancelled && !cleanupAssignmentFailed) {
        std::lock_guard lock(mutex_);
        if (!shutdownRequested_) {
            initializationRunning_ = false;
            state_ = CallbackLifecycleState::ready;
            changed_.notify_all();
            return LifecycleResult::completed;
        }
        cancelled = true;
    }

    bool rollbackSucceeded = true;
    try { if (rollback) rollback(); } catch (...) { rollbackSucceeded = false; }
    Cleanup retiredCleanup;
    {
        std::lock_guard lock(mutex_);
        initializationRunning_ = false;
        cleanupPending_ = false;
        retiredCleanup = std::move(cleanup_);
        if (!rollbackSucceeded) cleanupFailed_ = true;
        state_ = shutdownRequested_ ? CallbackLifecycleState::stopping : CallbackLifecycleState::failed;
        changed_.notify_all();
    }
    retiredCleanup = {};
    return LifecycleResult::rejected;
}

CallbackRegistration CallbackLifecycleController::registerCallback(Callback callback) noexcept {
    if (!callback) return {LifecycleResult::rejected, 0};
    try {
        auto slot = std::make_shared<CallbackSlot>();
        slot->callback = std::move(callback);
        std::lock_guard lock(mutex_);
        if (state_ != CallbackLifecycleState::initializing && state_ != CallbackLifecycleState::ready)
            return {LifecycleResult::invalid_transition, 0};
        if (nextCallbackId_ == 0) return {LifecycleResult::rejected, 0};
        slot->id = nextCallbackId_++;
        callbacks_.push_back(std::move(slot));
        return {LifecycleResult::accepted, callbacks_.back()->id};
    } catch (...) {
        return {LifecycleResult::rejected, 0};
    }
}

LifecycleResult CallbackLifecycleController::unregisterCallback(CallbackId id) noexcept {
    if (id == 0) return LifecycleResult::rejected;
    std::unique_lock lock(mutex_);
    const auto found = std::find_if(callbacks_.begin(), callbacks_.end(),
        [id](const auto& slot) { return slot->id == id; });
    if (found == callbacks_.end()) return LifecycleResult::invalid_transition;
    auto slot = *found;
    slot->enabled = false;
    callbacks_.erase(found);
    if (activeController == this) return LifecycleResult::would_deadlock;
    changed_.wait(lock, [&] { return slot->inFlight == 0; });
    return LifecycleResult::completed;
}

DispatchResult CallbackLifecycleController::dispatch() noexcept {
    if (activeController == this) return DispatchResult::reentrant;
    auto keepAlive = weak_from_this().lock();
    if (!keepAlive) return DispatchResult::failed;

    std::unique_lock serial(dispatchMutex_);
    std::vector<std::shared_ptr<CallbackSlot>> snapshot;
    {
        std::lock_guard lock(mutex_);
        if (state_ != CallbackLifecycleState::ready)
            return state_ == CallbackLifecycleState::stopping || state_ == CallbackLifecycleState::stopped
                ? DispatchResult::disabled : DispatchResult::not_ready;
        try {
            snapshot = callbacks_;
            if (snapshot.empty()) return DispatchResult::no_callbacks;
        } catch (...) { return DispatchResult::failed; }
        ++inFlight_;
    }

    const auto* previous = activeController;
    activeController = this;
    bool succeeded = true;
    bool invoked = false;
    for (const auto& slot : snapshot) {
        {
            std::lock_guard lock(mutex_);
            if (state_ != CallbackLifecycleState::ready) break;
            if (!slot->enabled) continue;
            ++slot->inFlight;
            invoked = true;
        }
        try { slot->callback(); } catch (...) { succeeded = false; }
        {
            std::lock_guard lock(mutex_);
            --slot->inFlight;
            changed_.notify_all();
        }
    }
    activeController = previous;
    finishDispatch();
    if (!succeeded) return DispatchResult::failed;
    return invoked ? DispatchResult::invoked : DispatchResult::no_callbacks;
}

void CallbackLifecycleController::finishDispatch() noexcept {
    std::lock_guard lock(mutex_);
    --inFlight_;
    changed_.notify_all();
}

LifecycleResult CallbackLifecycleController::requestShutdown() noexcept {
    std::lock_guard lock(mutex_);
    if (state_ == CallbackLifecycleState::stopped || state_ == CallbackLifecycleState::stopping)
        return LifecycleResult::already_requested;
    if (state_ == CallbackLifecycleState::cold || state_ == CallbackLifecycleState::initialization_queued ||
        state_ == CallbackLifecycleState::initializing || state_ == CallbackLifecycleState::ready ||
        state_ == CallbackLifecycleState::failed) {
        shutdownRequested_ = true;
        state_ = CallbackLifecycleState::stopping;
        for (auto& slot : callbacks_) slot->enabled = false;
        changed_.notify_all();
        return LifecycleResult::accepted;
    }
    return LifecycleResult::invalid_transition;
}

LifecycleResult CallbackLifecycleController::waitForCallbacks() noexcept {
    if (activeController == this) return LifecycleResult::would_deadlock;
    std::unique_lock lock(mutex_);
    if (state_ != CallbackLifecycleState::stopping) return LifecycleResult::invalid_transition;
    changed_.wait(lock, [this] { return inFlight_ == 0 && !initializationRunning_; });
    return LifecycleResult::completed;
}

LifecycleResult CallbackLifecycleController::waitForShutdown() noexcept {
    if (activeController == this) return LifecycleResult::would_deadlock;
    Cleanup cleanup;
    {
        std::unique_lock lock(mutex_);
        if (state_ == CallbackLifecycleState::stopped) return LifecycleResult::completed;
        if (state_ != CallbackLifecycleState::stopping || !sourceQuiescent_)
            return LifecycleResult::invalid_transition;
        changed_.wait(lock, [this] { return inFlight_ == 0 && !initializationRunning_ && !cleanupRunning_; });
        if (cleanupFailed_) return LifecycleResult::rejected;
        cleanupRunning_ = true;
        if (cleanupPending_) {
            cleanup = std::move(cleanup_);
            cleanupPending_ = false;
        }
    }

    bool cleaned = true;
    try { if (cleanup) cleanup(); } catch (...) { cleaned = false; }

    std::vector<std::shared_ptr<CallbackSlot>> retired;
    Cleanup retiredCleanup;
    {
        std::lock_guard lock(mutex_);
        cleanupRunning_ = false;
        if (!cleaned) {
            cleanupFailed_ = true;
            changed_.notify_all();
            return LifecycleResult::rejected;
        }
        retiredCleanup = std::move(cleanup_);
        retired.swap(callbacks_);
        state_ = CallbackLifecycleState::stopped;
        changed_.notify_all();
    }
    retired.clear();
    retiredCleanup = {};
    return LifecycleResult::completed;
}

LifecycleResult CallbackLifecycleController::confirmSourceQuiescent() noexcept {
    std::lock_guard lock(mutex_);
    if (state_ != CallbackLifecycleState::stopping && state_ != CallbackLifecycleState::stopped)
        return LifecycleResult::invalid_transition;
    if (inFlight_ != 0 || initializationRunning_ || cleanupRunning_)
        return LifecycleResult::invalid_transition;
    sourceQuiescent_ = true;
    changed_.notify_all();
    return LifecycleResult::completed;
}

CallbackLifecycleState CallbackLifecycleController::state() const noexcept {
    std::lock_guard lock(mutex_);
    return state_;
}

std::size_t CallbackLifecycleController::callbacksInFlight() const noexcept {
    std::lock_guard lock(mutex_);
    return inFlight_;
}

bool CallbackLifecycleController::unloadSafe() const noexcept {
    std::lock_guard lock(mutex_);
    return state_ == CallbackLifecycleState::stopped && inFlight_ == 0 && sourceQuiescent_ &&
           callbacks_.empty() && !cleanupPending_ && !cleanupFailed_;
}

}  // namespace grandmp::windows
