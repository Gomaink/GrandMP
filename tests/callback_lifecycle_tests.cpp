#include "grandmp/windows/callback_lifecycle.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using namespace grandmp::windows;

namespace {

bool ready(const std::shared_ptr<CallbackLifecycleController>& c,
           CallbackLifecycleController::Callback callback,
           CallbackId* id = nullptr,
           CallbackLifecycleController::Cleanup cleanup = [] {}) {
    if (c->requestInitialization() != LifecycleResult::accepted) return false;
    CallbackRegistration registration;
    const auto result = c->runDeferredInitialization([&] {
        registration = c->registerCallback(std::move(callback));
        return registration.result == LifecycleResult::accepted;
    }, cleanup);
    if (id) *id = registration.id;
    return result == LifecycleResult::completed && c->state() == CallbackLifecycleState::ready;
}

bool finish(const std::shared_ptr<CallbackLifecycleController>& c) {
    if (c->requestShutdown() != LifecycleResult::accepted) return false;
    if (c->confirmSourceQuiescent() != LifecycleResult::completed) return false;
    return c->waitForShutdown() == LifecycleResult::completed && c->unloadSafe();
}

bool initializationAndShutdown() {
    auto c = CallbackLifecycleController::create();
    std::atomic<unsigned> calls{0};
    if (c->dispatch() != DispatchResult::not_ready) return false;
    std::atomic<unsigned> accepted{0};
    std::atomic<bool> start{false};
    std::atomic<bool> invalid{false};
    std::vector<std::thread> requesters;
    for (unsigned i = 0; i < 8; ++i) requesters.emplace_back([&] {
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        const auto result = c->requestInitialization();
        if (result == LifecycleResult::accepted) ++accepted;
        else if (result != LifecycleResult::already_requested) invalid = true;
    });
    start.store(true, std::memory_order_release);
    for (auto& thread : requesters) thread.join();
    if (invalid || accepted != 1) return false;
    CallbackRegistration registration;
    if (c->runDeferredInitialization([&] {
            registration = c->registerCallback([&] { ++calls; });
            return registration.result == LifecycleResult::accepted;
        }, [] {}) != LifecycleResult::completed) return false;
    if (c->requestInitialization() != LifecycleResult::already_requested) return false;
    if (c->dispatch() != DispatchResult::invoked || calls != 1) return false;
    return finish(c);
}

bool partialInitializationRollback() {
    auto c = CallbackLifecycleController::create();
    std::atomic<unsigned> rollback{0};
    if (c->requestInitialization() != LifecycleResult::accepted) return false;
    const auto result = c->runDeferredInitialization([] { return false; }, [&] { ++rollback; });
    if (result != LifecycleResult::rejected || rollback != 1 || c->state() != CallbackLifecycleState::failed)
        return false;
    if (c->dispatch() != DispatchResult::not_ready || c->requestShutdown() != LifecycleResult::accepted)
        return false;
    if (c->confirmSourceQuiescent() != LifecycleResult::completed ||
        c->waitForShutdown() != LifecycleResult::completed || !c->unloadSafe()) return false;

    auto rollbackFailure = CallbackLifecycleController::create();
    if (rollbackFailure->requestInitialization() != LifecycleResult::accepted) return false;
    if (rollbackFailure->runDeferredInitialization([] { return false; }, [] { throw 1; }) !=
        LifecycleResult::rejected) return false;
    if (rollbackFailure->requestShutdown() != LifecycleResult::accepted ||
        rollbackFailure->confirmSourceQuiescent() != LifecycleResult::completed ||
        rollbackFailure->waitForShutdown() != LifecycleResult::rejected || rollbackFailure->unloadSafe())
        return false;
    return true;
}

bool reentrancyAndCallbackShutdown() {
    auto c = CallbackLifecycleController::create();
    DispatchResult nested = DispatchResult::failed;
    LifecycleResult requested = LifecycleResult::invalid_transition;
    LifecycleResult waited = LifecycleResult::completed;
    auto* raw = c.get();
    if (!ready(c, [&] {
        nested = raw->dispatch();
        requested = raw->requestShutdown();
        waited = raw->waitForShutdown();
    })) return false;
    if (c->dispatch() != DispatchResult::invoked || nested != DispatchResult::reentrant ||
        requested != LifecycleResult::accepted || waited != LifecycleResult::would_deadlock ||
        c->unloadSafe()) return false;
    return c->confirmSourceQuiescent() == LifecycleResult::completed &&
           c->waitForShutdown() == LifecycleResult::completed && c->unloadSafe();
}

bool concurrentDispatchAndRegistration() {
    constexpr unsigned workers = 6;
    constexpr unsigned rounds = 250;
    auto c = CallbackLifecycleController::create();
    std::atomic<unsigned> baseCalls{0};
    if (!ready(c, [&] { ++baseCalls; })) return false;
    std::atomic<bool> start{false};
    std::atomic<bool> failed{false};
    std::vector<std::thread> dispatchers;
    for (unsigned i = 0; i < workers; ++i) dispatchers.emplace_back([&] {
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        for (unsigned n = 0; n < rounds; ++n)
            if (c->dispatch() != DispatchResult::invoked) failed.store(true, std::memory_order_relaxed);
    });
    std::vector<std::thread> registrars;
    std::atomic<unsigned> registeredCalls{0};
    for (unsigned i = 0; i < workers; ++i) registrars.emplace_back([&] {
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        for (unsigned n = 0; n < rounds; ++n) {
            const auto r = c->registerCallback([&] { ++registeredCalls; });
            if (r.result == LifecycleResult::accepted) {
                if (c->unregisterCallback(r.id) != LifecycleResult::completed)
                    failed.store(true, std::memory_order_relaxed);
            } else failed.store(true, std::memory_order_relaxed);
        }
    });
    start.store(true, std::memory_order_release);
    for (auto& t : dispatchers) t.join();
    for (auto& t : registrars) t.join();
    if (failed || baseCalls != workers * rounds) return false;
    const unsigned before = registeredCalls.load();
    const auto persistent = c->registerCallback([&] { ++registeredCalls; });
    if (persistent.result != LifecycleResult::accepted || c->dispatch() != DispatchResult::invoked ||
        registeredCalls != before + 1 || c->unregisterCallback(persistent.id) != LifecycleResult::completed ||
        c->dispatch() != DispatchResult::invoked || registeredCalls != before + 1) return false;
    return finish(c);
}

bool shutdownWaitsForCallback() {
    auto c = CallbackLifecycleController::create();
    std::mutex mutex;
    std::condition_variable cv;
    bool entered = false;
    bool release = false;
    if (!ready(c, [&] {
        std::unique_lock lock(mutex);
        entered = true;
        cv.notify_all();
        cv.wait(lock, [&] { return release; });
    })) return false;
    std::thread dispatcher([&] { (void)c->dispatch(); });
    bool didEnter = false;
    {
        std::unique_lock lock(mutex);
        didEnter = cv.wait_for(lock, 2s, [&] { return entered; });
    }
    if (!didEnter) {
        { std::lock_guard lock(mutex); release = true; }
        cv.notify_all();
        dispatcher.join();
        return false;
    }
    const bool requested = c->requestShutdown() == LifecycleResult::accepted;
    auto waiter = std::async(std::launch::async, [&] { return c->waitForCallbacks(); });
    const bool blocked = waiter.wait_for(50ms) == std::future_status::timeout && !c->unloadSafe();
    { std::lock_guard lock(mutex); release = true; }
    cv.notify_all();
    dispatcher.join();
    if (!requested || !blocked || waiter.get() != LifecycleResult::completed) return false;
    return c->confirmSourceQuiescent() == LifecycleResult::completed &&
           c->waitForShutdown() == LifecycleResult::completed && c->unloadSafe();
}

bool shutdownDuringInitialization() {
    auto c = CallbackLifecycleController::create();
    std::mutex mutex;
    std::condition_variable cv;
    bool entered = false;
    bool release = false;
    std::atomic<unsigned> rollback{0};
    if (c->requestInitialization() != LifecycleResult::accepted) return false;
    std::thread initializer([&] {
        (void)c->runDeferredInitialization([&] {
            std::unique_lock lock(mutex);
            entered = true;
            cv.notify_all();
            cv.wait(lock, [&] { return release; });
            return true;
        }, [&] { ++rollback; });
    });
    bool didEnter = false;
    {
        std::unique_lock lock(mutex);
        didEnter = cv.wait_for(lock, 2s, [&] { return entered; });
    }
    if (!didEnter) {
        { std::lock_guard lock(mutex); release = true; }
        cv.notify_all();
        initializer.join();
        return false;
    }
    const bool requested = c->requestShutdown() == LifecycleResult::accepted;
    auto waiter = std::async(std::launch::async, [&] { return c->waitForCallbacks(); });
    const bool blocked = waiter.wait_for(50ms) == std::future_status::timeout;
    { std::lock_guard lock(mutex); release = true; }
    cv.notify_all();
    initializer.join();
    if (!requested || !blocked || waiter.get() != LifecycleResult::completed || rollback != 1) return false;
    return c->confirmSourceQuiescent() == LifecycleResult::completed &&
           c->waitForShutdown() == LifecycleResult::completed && c->unloadSafe();
}

bool sourceQuiescenceAndCleanupFailure() {
    auto c = CallbackLifecycleController::create();
    if (!ready(c, [] {}, nullptr, [] { throw 1; })) return false;
    if (c->requestShutdown() != LifecycleResult::accepted) return false;
    if (c->waitForShutdown() != LifecycleResult::invalid_transition || c->unloadSafe()) return false;
    if (c->confirmSourceQuiescent() != LifecycleResult::completed) return false;
    return c->waitForShutdown() == LifecycleResult::rejected && !c->unloadSafe();
}

bool unregisterDrainsAndRetiresCallable() {
    auto c = CallbackLifecycleController::create();
    std::mutex mutex;
    std::condition_variable cv;
    bool entered = false;
    bool release = false;
    auto token = std::make_shared<int>(7);
    std::weak_ptr<int> weak = token;
    CallbackId id = 0;
    if (!ready(c, [&, token] {
        std::unique_lock lock(mutex);
        entered = true;
        cv.notify_all();
        cv.wait(lock, [&] { return release; });
        if (*token != 7) std::abort();
    }, &id)) return false;
    token.reset();
    std::thread dispatcher([&] { (void)c->dispatch(); });
    bool didEnter = false;
    {
        std::unique_lock lock(mutex);
        didEnter = cv.wait_for(lock, 2s, [&] { return entered; });
    }
    if (!didEnter) {
        { std::lock_guard lock(mutex); release = true; }
        cv.notify_all();
        dispatcher.join();
        return false;
    }
    auto unregister = std::async(std::launch::async, [&] { return c->unregisterCallback(id); });
    const bool waiting = unregister.wait_for(50ms) == std::future_status::timeout && !weak.expired();
    { std::lock_guard lock(mutex); release = true; }
    cv.notify_all();
    dispatcher.join();
    if (!waiting || unregister.get() != LifecycleResult::completed || !weak.expired()) return false;
    return finish(c);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string_view name(argv[1]);
    bool pass = false;
    if (name == "initialization") pass = initializationAndShutdown();
    else if (name == "partial") pass = partialInitializationRollback();
    else if (name == "reentrant") pass = reentrancyAndCallbackShutdown();
    else if (name == "concurrency") pass = concurrentDispatchAndRegistration();
    else if (name == "shutdown") pass = shutdownWaitsForCallback();
    else if (name == "shutdown_race") pass = shutdownDuringInitialization();
    else if (name == "cleanup_failure") pass = sourceQuiescenceAndCleanupFailure();
    else if (name == "unregister") pass = unregisterDrainsAndRetiresCallable();
    else return 2;
    if (!pass) std::fprintf(stderr, "callback lifecycle test failed: %.*s\n", static_cast<int>(name.size()), name.data());
    return pass ? 0 : 1;
}
