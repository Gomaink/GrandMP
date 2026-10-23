#pragma once
#include "native_diagnostic_binding.hpp"

namespace grandmp::windows::native_diagnostic_deferred {
namespace binding = native_diagnostic_binding;

// One owner retains this object for the entire producer lifetime. The Windows
// worker retains shared ownership too; an external timeout cannot free it while
// a phase is in flight. No engine/backend/context pointer lives in this object.
template<bool Enabled = false>
class Session final {
public:
    template<class View, class Controller>
    void beforeAttempt(View& view, const Controller& controller, std::uint64_t attempt,
        std::uint64_t callback, std::uint64_t now, unsigned limit = binding::diag::Observer::defaultLimit) {
        if constexpr (Enabled) {
            if (!view.nativeContext() || stopped() || closed()) return;
            binding_.beforeAttempt(view,controller,attempt,callback,limit);
            beat(now); finishIfTerminal();
        }
    }
    template<class View, class Controller>
    void afterTick(View& view, const Controller& controller, std::uint64_t attempt,
        std::uint64_t callback, std::uint64_t now) {
        if constexpr (Enabled) {
            if (!view.nativeContext() || stopped() || closed()) return;
            binding_.afterTick(view,controller,attempt,callback);
            beat(now); finishIfTerminal();
        }
    }
    template<class View>
    void producerStopped(View& view) noexcept {
        if constexpr (Enabled) { if (view.nativeContext()) closed_.store(true,std::memory_order_release); }
    }
    // Consumer interface: values/atomics only, never producer-only Binding state.
    bool pop(binding::Record& record) noexcept { return binding_.pop(record); }
    bool overflowed() const noexcept { return binding_.overflowed(); }
    bool closed() const noexcept { return closed_.load(std::memory_order_acquire); }
    bool stopped() const noexcept { return stop_.load(std::memory_order_acquire); }
    void requestStop() noexcept { stop_.store(true,std::memory_order_release); }
    bool claimConsumer() noexcept {
        bool expected = false;
        return consumerClaimed_.compare_exchange_strong(expected,true,std::memory_order_acq_rel);
    }
    bool heartbeat(std::uint64_t& now) const noexcept {
        if (!haveHeartbeat_.load(std::memory_order_acquire)) return false;
        now = heartbeat_.load(std::memory_order_acquire); return true;
    }
private:
    void beat(std::uint64_t now) noexcept {
        heartbeat_.store(now,std::memory_order_release);
        haveHeartbeat_.store(true,std::memory_order_release);
    }
    void finishIfTerminal() noexcept {
        if (binding_.terminal()) closed_.store(true,std::memory_order_release);
    }
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    binding::Binding<Enabled> binding_;
    std::atomic<std::uint64_t> heartbeat_{0};
    std::atomic<bool> haveHeartbeat_{false}, closed_{false}, stop_{false}, consumerClaimed_{false};
};

enum class Outcome { observing, terminal_observed, overflow, producer_ended_without_terminal,
    callbacks_absent, lifetime_limit, clock_regressed, cancelled, sink_failed, consumer_conflict, invalid_configuration };
inline const char* name(Outcome value) noexcept {
    switch (value) {
    case Outcome::observing: return "observing";
    case Outcome::terminal_observed: return "terminal_observed";
    case Outcome::overflow: return "overflow";
    case Outcome::producer_ended_without_terminal: return "producer_ended_without_terminal";
    case Outcome::callbacks_absent: return "callbacks_absent";
    case Outcome::lifetime_limit: return "lifetime_limit";
    case Outcome::clock_regressed: return "clock_regressed";
    case Outcome::cancelled: return "cancelled";
    case Outcome::sink_failed: return "sink_failed";
    case Outcome::consumer_conflict: return "consumer_conflict";
    default: return "invalid_configuration";
    }
}
struct Limits { std::uint64_t absenceMs = 5000, lifetimeMs = 120000; };

// Sole consumer, outside the native phase. poll is bounded even with a producer
// publishing concurrently. A terminal Event is evidence, not permission to walk.
class Consumer final {
public:
    Consumer(std::uint64_t now, Limits limits = {}) : start_(now), previous_(now), limits_(limits) {}
    Consumer(const Consumer&) = delete;
    Consumer& operator=(const Consumer&) = delete;
    Outcome outcome() const noexcept { return outcome_; }
    unsigned records() const noexcept { return records_; }
    bool loggingFailed() const noexcept { return loggingFailed_; }
    template<class Source, class Sink>
    Outcome poll(Source& source, std::uint64_t now, Sink& sink) noexcept {
        if (outcome_ != Outcome::observing) return outcome_;
        auto finish = [&](Outcome value) {
            source.requestStop(); outcome_ = value;
            char line[128]{};
            const int count = std::snprintf(line,sizeof(line),"diagnostic_history=%s reason=%s records=%u",
                value == Outcome::terminal_observed ? "complete" : "inconclusive",name(value),records_);
            if (count <= 0 || static_cast<std::size_t>(count) >= sizeof(line) || !sink(line)) {
                loggingFailed_ = true;
                // Preserve an inconclusive primary cause, especially overflow.
                if (value == Outcome::terminal_observed) outcome_ = Outcome::sink_failed;
            }
            return outcome_;
        };
        if (!claimed_) {
            if (!source.claimConsumer()) return finish(Outcome::consumer_conflict);
            claimed_ = true;
        }
        if (!limits_.absenceMs || limits_.lifetimeMs < limits_.absenceMs) return finish(Outcome::invalid_configuration);
        // Acquire closure BEFORE draining: no publication may follow that store.
        const bool closed = source.closed();
        binding::Record record;
        for (unsigned n = 0; n < binding::EventOutbox::capacity && source.pop(record); ++n) {
            char line[binding::messageCapacity]{};
            if (!binding::format(line,sizeof(line),record) || !sink(line)) {
                loggingFailed_ = true;
                return finish(source.overflowed() ? Outcome::overflow : Outcome::sink_failed);
            }
            ++records_; terminal_ = terminal_ || record.event.terminal;
        }
        // Overflow wins even if the terminal record happened to survive.
        if (source.overflowed()) return finish(Outcome::overflow);
        if (closed) return finish(terminal_ ? Outcome::terminal_observed : Outcome::producer_ended_without_terminal);
        if (source.stopped()) return finish(Outcome::cancelled);
        if (now < previous_ || now < start_) return finish(Outcome::clock_regressed);
        previous_ = now;
        std::uint64_t beat = start_;
        source.heartbeat(beat);
        // Producer may have published a NEWER tick after the caller sampled now.
        // Treat that as fresh, not unsigned underflow or a clock failure.
        if (now >= beat && now - beat >= limits_.absenceMs) return finish(Outcome::callbacks_absent);
        if (now - start_ >= limits_.lifetimeMs) return finish(Outcome::lifetime_limit);
        return outcome_;
    }
private:
    std::uint64_t start_, previous_;
    Limits limits_;
    Outcome outcome_ = Outcome::observing;
    unsigned records_ = 0;
    bool terminal_ = false, loggingFailed_ = false, claimed_ = false;
};
} // namespace grandmp::windows::native_diagnostic_deferred
