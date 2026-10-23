#pragma once
#include "native_diagnostic_deferred.hpp"

namespace grandmp::windows::native_diagnostic_integration {
namespace deferred = native_diagnostic_deferred;
#if defined(GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD) && GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD == 1
inline constexpr bool buildEnabled = true;
#else
inline constexpr bool buildEnabled = false;
#endif

// Preparation-thread policy only. The clock/pause seam permits deterministic
// tests of the real 120 s arm window without waiting or touching engine memory.
struct PreparationResult {
    bool requested, stopped, runtimeDisabled, expired;
    std::uint64_t elapsedMs;
    bool ready() const noexcept { return requested && !stopped && !runtimeDisabled && !expired; }
    const char* reason() const noexcept {
        if (stopped) return "session_stopped";
        if (runtimeDisabled) return "runtime_disabled";
        if (expired) return "deadline_expired";
        return requested ? "request_observed" : "request_absent";
    }
};
template<class Producer, class Source, class Disabled, class Clock, class Pause>
PreparationResult awaitPreparation(Producer& producer, Source& source, Disabled& disabled,
    Clock clock, Pause pause, std::uint64_t armMs) {
    const auto start = clock();
    const auto deadline = start + armMs;
    while (!producer.requested() && !source.stopped() && !disabled.load() && clock() < deadline) pause();
    // Recheck after waking, including a signal racing expiry. No retry/extension.
    const bool requested = producer.requested(), stopped = source.stopped(), runtimeDisabled = disabled.load();
    const auto now = clock();
    return {requested,stopped,runtimeDisabled,now >= deadline,now - start};
}
inline bool formatPreparation(char* line, std::size_t capacity, const PreparationResult& result) noexcept {
    if (!line || !capacity) return false;
    const int count = std::snprintf(line,capacity,
        "preparation=%s requested=%u stopped=%u runtime_disabled=%u deadline_expired=%u elapsed_ms=%llu consumer_start_attempted=0 win32_source=not_called",
        result.reason(),unsigned(result.requested),unsigned(result.stopped),unsigned(result.runtimeDisabled),
        unsigned(result.expired),static_cast<unsigned long long>(result.elapsedMs));
    return count > 0 && static_cast<std::size_t>(count) < capacity;
}

// Producer endpoint ONLY. Source is application memory retained by the resident
// owner, never a GTA address. No task API, reset, allocation, formatting or wait.
template<bool Enabled = false>
class Producer final {
public:
    explicit Producer(deferred::Session<Enabled>& source) : source_(source) {}
    bool requested() const noexcept { return requested_.load(std::memory_order_acquire); }
    template<class View, class Controller>
    void beforeAttempt(View& view, const Controller& controller, std::uint64_t callback, std::uint64_t now) {
        if constexpr (Enabled) {
            if (!view.nativeContext() || consumed_ || source_.stopped() || source_.closed()) return;
            consumed_ = true;
            source_.beforeAttempt(view,controller,1,callback,now);
            requested_.store(true,std::memory_order_release); // Preparation worker starts the sole consumer.
        }
    }
    template<class View, class Controller>
    void afterTick(View& view, const Controller& controller, std::uint64_t callback, std::uint64_t now) {
        if constexpr (Enabled) {
            if (!view.nativeContext() || !consumed_) return;
            source_.afterTick(view,controller,1,callback,now);
        }
    }
private:
    deferred::Session<Enabled>& source_;
    std::atomic<bool> requested_{false};
    bool consumed_ = false; // Only authenticated native thread; preparation reads requested(), not this field.
};
} // namespace grandmp::windows::native_diagnostic_integration
