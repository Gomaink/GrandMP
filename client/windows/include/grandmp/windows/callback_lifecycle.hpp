#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace grandmp::windows {

enum class CallbackLifecycleState {
    cold,
    initialization_queued,
    initializing,
    ready,
    stopping,
    stopped,
    failed
};

enum class LifecycleResult {
    accepted,
    already_requested,
    completed,
    rejected,
    would_deadlock,
    invalid_transition
};

enum class DispatchResult { invoked, no_callbacks, not_ready, disabled, reentrant, failed };

using CallbackId = std::uint64_t;
struct CallbackRegistration {
    LifecycleResult result = LifecycleResult::invalid_transition;
    CallbackId id = 0;
};

// Platform adapter lifecycle only. It does not patch the GTA or establish that
// any caller is outside loader lock or on a safe game thread.
class CallbackLifecycleController final
    : public std::enable_shared_from_this<CallbackLifecycleController> {
public:
    using Work = std::function<bool()>;
    using Cleanup = std::function<void()>;
    using Callback = std::function<void()>;

    [[nodiscard]] static std::shared_ptr<CallbackLifecycleController> create();
    CallbackLifecycleController(const CallbackLifecycleController&) = delete;
    CallbackLifecycleController& operator=(const CallbackLifecycleController&) = delete;
    ~CallbackLifecycleController() = default;

    // Queue only. The host/scheduler must call runDeferredInitialization from
    // its explicitly selected safe point; this method creates no thread.
    [[nodiscard]] LifecycleResult requestInitialization() noexcept;
    [[nodiscard]] LifecycleResult runDeferredInitialization(const Work& initialize,
                                                            const Cleanup& rollback) noexcept;

    // Registration is staged during initialization or can be changed while
    // ready. A dispatch snapshots registrations; newly added callbacks begin
    // on the next tick. Unregistration disables admission and drains its active
    // callback body. It is not permission to unload code: the source must stop,
    // join every dispatch call through return to its host, then confirm quiescence.
    [[nodiscard]] CallbackRegistration registerCallback(Callback callback) noexcept;
    [[nodiscard]] LifecycleResult unregisterCallback(CallbackId id) noexcept;

    [[nodiscard]] DispatchResult dispatch() noexcept;

    // Close admission immediately. Waiting is a separate operation so a
    // callback can request shutdown without waiting for itself.
    [[nodiscard]] LifecycleResult requestShutdown() noexcept;
    [[nodiscard]] LifecycleResult waitForCallbacks() noexcept;
    [[nodiscard]] LifecycleResult waitForShutdown() noexcept;

    // The source must first detach this callback and join all dispatcher calls,
    // including their return to host code. This is stronger than inFlight==0.
    [[nodiscard]] LifecycleResult confirmSourceQuiescent() noexcept;

    [[nodiscard]] CallbackLifecycleState state() const noexcept;
    [[nodiscard]] std::size_t callbacksInFlight() const noexcept;
    [[nodiscard]] bool unloadSafe() const noexcept;

private:
    struct CallbackSlot {
        CallbackId id = 0;
        Callback callback;
        std::size_t inFlight = 0;
        bool enabled = true;
    };

    CallbackLifecycleController() = default;
    void finishDispatch() noexcept;

    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::mutex dispatchMutex_;
    Cleanup cleanup_;
    std::vector<std::shared_ptr<CallbackSlot>> callbacks_;
    CallbackId nextCallbackId_ = 1;
    CallbackLifecycleState state_ = CallbackLifecycleState::cold;
    std::size_t inFlight_ = 0;
    bool shutdownRequested_ = false;
    bool sourceQuiescent_ = false;
    bool initializationRunning_ = false;
    bool cleanupRunning_ = false;
    bool cleanupPending_ = false;
    bool cleanupFailed_ = false;
};

}  // namespace grandmp::windows
