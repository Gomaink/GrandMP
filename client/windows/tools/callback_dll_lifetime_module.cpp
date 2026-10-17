#include "callback_dll_lifetime_api.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "grandmp/windows/callback_lifecycle.hpp"

#include <atomic>
#include <memory>
#include <new>
#include <utility>

using namespace grandmp::windows;
using namespace grandmp::windows::lifetime_lab;

namespace {

struct ModuleState {
    std::shared_ptr<CallbackLifecycleController> lifecycle;
    HostNotify notify = nullptr;
    void* notifyContext = nullptr;
    std::uint32_t mode = mode_normal;
    std::atomic<std::uint64_t> callbacksStarted{0};
    std::atomic<std::uint64_t> callbacksFinished{0};
    std::atomic<std::uint64_t> reentrantDispatches{0};
    std::atomic<std::uint64_t> cleanupCalls{0};
};

// Zero-initialized POD pointer. It is assigned only through Initialize after
// LoadLibrary returns and cleared only after the host has joined all callers.
std::atomic<ModuleState*> moduleState{nullptr};
std::atomic_flag initializationClaim = ATOMIC_FLAG_INIT;

int __cdecl initialize(std::uint32_t mode, HostNotify notify, void* context) {
    if (mode > mode_reentrant || initializationClaim.test_and_set(std::memory_order_acquire)) return 0;
    std::unique_ptr<ModuleState> owned;
    try {
        owned = std::make_unique<ModuleState>();
        owned->lifecycle = CallbackLifecycleController::create();
        owned->notify = notify;
        owned->notifyContext = context;
        owned->mode = mode;
        if (owned->lifecycle->requestInitialization() != LifecycleResult::accepted) {
            initializationClaim.clear(std::memory_order_release);
            return 0;
        }
    } catch (...) {
        initializationClaim.clear(std::memory_order_release);
        return 0;
    }

    ModuleState* state = owned.release();
    moduleState.store(state, std::memory_order_release);
    try {
        const auto result = state->lifecycle->runDeferredInitialization([state] {
            const auto registration = state->lifecycle->registerCallback([state] {
                state->callbacksStarted.fetch_add(1, std::memory_order_relaxed);
                if (state->mode == mode_reentrant &&
                    state->lifecycle->dispatch() == DispatchResult::reentrant) {
                    state->reentrantDispatches.fetch_add(1, std::memory_order_relaxed);
                }
                if (state->notify) state->notify(notify_callback_enter, state->notifyContext);
                state->callbacksFinished.fetch_add(1, std::memory_order_relaxed);
                if (state->notify) state->notify(notify_callback_leave, state->notifyContext);
            });
            if (registration.result != LifecycleResult::accepted) return false;
            return state->mode != mode_fail_initialization && state->mode != mode_fail_rollback;
        }, [state] {
            state->cleanupCalls.fetch_add(1, std::memory_order_relaxed);
            if (state->mode == mode_fail_rollback || state->mode == mode_fail_shutdown_cleanup) throw 1;
        });
        return result == LifecycleResult::completed ? 1 : 0;
    } catch (...) {
        // The host still owns this initialized module state and must request
        // shutdown; it cannot interpret this failure as unload permission.
        return 0;
    }
}

int __cdecl tick() {
    ModuleState* state = moduleState.load(std::memory_order_acquire);
    if (!state || !state->lifecycle) return static_cast<int>(DispatchResult::failed);
    return static_cast<int>(state->lifecycle->dispatch());
}

int __cdecl requestShutdown() {
    ModuleState* state = moduleState.load(std::memory_order_acquire);
    if (!state || !state->lifecycle) return 0;
    const auto result = state->lifecycle->requestShutdown();
    return result == LifecycleResult::accepted || result == LifecycleResult::already_requested;
}

int __cdecl confirmSourceQuiescent() {
    ModuleState* state = moduleState.load(std::memory_order_acquire);
    if (!state || !state->lifecycle) return 0;
    return state->lifecycle->confirmSourceQuiescent() == LifecycleResult::completed;
}

int __cdecl waitForShutdown() {
    ModuleState* state = moduleState.load(std::memory_order_acquire);
    if (!state || !state->lifecycle) return 0;
    return state->lifecycle->waitForShutdown() == LifecycleResult::completed;
}

int __cdecl canUnload() {
    ModuleState* state = moduleState.load(std::memory_order_acquire);
    return state && state->lifecycle && state->lifecycle->unloadSafe();
}

int __cdecl getMetrics(Metrics* output) {
    ModuleState* state = moduleState.load(std::memory_order_acquire);
    if (!state || !output) return 0;
    output->callbacksStarted = state->callbacksStarted.load(std::memory_order_relaxed);
    output->callbacksFinished = state->callbacksFinished.load(std::memory_order_relaxed);
    output->reentrantDispatches = state->reentrantDispatches.load(std::memory_order_relaxed);
    output->cleanupCalls = state->cleanupCalls.load(std::memory_order_relaxed);
    return 1;
}

int __cdecl destroy() {
    if (!canUnload()) return 0;
    ModuleState* retired = moduleState.exchange(nullptr, std::memory_order_acq_rel);
    if (!retired) return 0;
    delete retired;
    return 1;
}

const ApiV1 api = {
    api_version, initialize, tick, requestShutdown, confirmSourceQuiescent,
    waitForShutdown, canUnload, getMetrics, destroy
};

}  // namespace

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) {
    return TRUE;
}

extern "C" const ApiV1* __cdecl GmpLabGetApi(std::uint32_t requestedVersion) {
    return requestedVersion == api_version ? &api : nullptr;
}
