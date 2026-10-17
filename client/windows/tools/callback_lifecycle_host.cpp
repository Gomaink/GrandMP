#include "grandmp/windows/callback_lifecycle.hpp"

#include <atomic>
#include <charconv>
#include <cstdio>
#include <string_view>

using grandmp::windows::CallbackLifecycleController;
using grandmp::windows::CallbackLifecycleState;
using grandmp::windows::CallbackRegistration;
using grandmp::windows::DispatchResult;
using grandmp::windows::LifecycleResult;

int main(int argc, char** argv) {
    if (argc != 3 || std::string_view(argv[1]) != "--ticks") {
        std::fprintf(stderr, "usage: grandmp_callback_lifecycle_host --ticks <count>\n");
        return 2;
    }
    unsigned ticks = 0;
    const std::string_view text(argv[2]);
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), ticks);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || ticks == 0 || ticks > 1000000) {
        std::fprintf(stderr, "tick count must be in range 1..1000000\n");
        return 2;
    }

    std::atomic<unsigned> callbacks{0};
    auto controller = CallbackLifecycleController::create();
    if (controller->requestInitialization() != LifecycleResult::accepted) return 3;

    // This is the simulated host's deferred safe point, reached only after its
    // process and update loop are initialized. It is not an ASI callback.
    CallbackRegistration registration;
    if (controller->runDeferredInitialization([&] {
            registration = controller->registerCallback([&] { callbacks.fetch_add(1, std::memory_order_relaxed); });
            return registration.result == LifecycleResult::accepted;
        }, [] {}) != LifecycleResult::completed)
        return 4;

    for (unsigned tick = 0; tick < ticks; ++tick) {
        if (controller->dispatch() != DispatchResult::invoked) return 5;
    }

    if (controller->requestShutdown() != LifecycleResult::accepted) return 6;
    // The host has stopped producing update events before confirming quiescence.
    if (controller->confirmSourceQuiescent() != LifecycleResult::completed) return 7;
    if (controller->waitForShutdown() != LifecycleResult::completed || !controller->unloadSafe()) return 8;

    std::printf("ticks=%u callbacks=%u state=%s unload_safe=%d\n", ticks,
                callbacks.load(std::memory_order_relaxed),
                controller->state() == CallbackLifecycleState::stopped ? "stopped" : "invalid",
                controller->unloadSafe() ? 1 : 0);
    return callbacks.load(std::memory_order_relaxed) == ticks ? 0 : 9;
}
