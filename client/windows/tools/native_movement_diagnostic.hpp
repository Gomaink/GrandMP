#pragma once
#include "native_diagnostic_binding.hpp"
#include <cmath>
#include <cstdio>
#include <thread>
#include <type_traits>

namespace grandmp::windows::native_movement_diagnostic {
namespace native = continuous_remote_native;
namespace ps = ped_smoke;

// Hoodlum file evidence: docs/goto-animation-shift-velocity-limitation-trace.md.
// These are reads ONLY, at the existing pre-CWorld phase, never around UpdatePosition.
namespace layout {
inline constexpr unsigned matrix = 0x14, position = 0x30, speed = 0x44;
inline constexpr unsigned flags = 0x46C, fallenFlags = 0x474, localShift = 0x4D8;
inline constexpr unsigned states = 0x530, health = 0x540, worldShift = 0x550;
inline constexpr unsigned support = 0x568, type = 0x598;
}
enum class Reason { complete, stopped, identity, lifecycle, movement_failed, authentication,
    incomplete_read, matrix_changed, invalid_values, callback_limit, clock_regressed, capacity };
inline const char* name(Reason reason) noexcept {
    switch (reason) {
    case Reason::complete: return "sample_window_complete_not_updateposition_proof";
    case Reason::stopped: return "stop_requested";
    case Reason::identity: return "identity_changed";
    case Reason::lifecycle: return "lifecycle_ended";
    case Reason::movement_failed: return "movement_failed";
    case Reason::authentication: return "authentication_failed";
    case Reason::incomplete_read: return "incomplete_read";
    case Reason::matrix_changed: return "matrix_changed";
    case Reason::invalid_values: return "invalid_values";
    case Reason::callback_limit: return "observation_limit";
    case Reason::clock_regressed: return "clock_regressed";
    default: return "buffer_full";
    }
}
struct Pair { float x = 0, y = 0; };
struct Record {
    std::uint64_t callback = 0, remoteId = 0, generation = 0;
    ps::MoveState walk = ps::MoveState::idle;
    std::int32_t pedState = 0, moveState = 0;
    bool fallen = false, standing = false, supportPresent = false;
    Pair speed{}, local{}, converted{};
    ps::Position position{};
};
static_assert(std::is_trivially_copyable_v<Record> && sizeof(Record) <= 112);
inline const char* name(ps::MoveState walk) noexcept {
    switch (walk) {
    case ps::MoveState::idle: return "before_assignment";
    case ps::MoveState::assigned: return "assigned";
    case ps::MoveState::moving: return "started";
    case ps::MoveState::arrived: return "arrived";
    default: return "failed";
    }
}
inline constexpr unsigned messageCapacity = 480; // Existing FileSink adds its prefix in 512 bytes.
// Deferred caller ONLY. All numbers below are copied values, never entity addresses.
inline bool format(char* line, std::size_t size, const Record& r) noexcept {
    if (!line || !size) return false;
    const int n = std::snprintf(line,size,
        "movement_sample phase=pre_world cb=%llu remote=%llu generation=%llu walk=%s fallen=%u standing=%u support_present=%u ped_state=%d move_state=%d speed=(%.6g,%.6g) local=(%.6g,%.6g) converted=(%.6g,%.6g) pos=(%.6g,%.6g,%.6g)",
        static_cast<unsigned long long>(r.callback),static_cast<unsigned long long>(r.remoteId),
        static_cast<unsigned long long>(r.generation),name(r.walk),unsigned(r.fallen),unsigned(r.standing),
        unsigned(r.supportPresent),r.pedState,r.moveState,r.speed.x,r.speed.y,r.local.x,r.local.y,
        r.converted.x,r.converted.y,r.position.x,r.position.y,r.position.z);
    if (n < 0 || static_cast<std::size_t>(n) >= size) { line[0] = '\0'; return false; }
    return true;
}

template<bool> class Capture;
class Consumer;
// Single publication of a bounded batch. Consumer NEVER touches an in-flight array.
// Owner must retain Batch/Capture until producer quiescence (the diagnostic root
// remains resident until process exit); expiry/stop is NOT permission to destroy it.
class Batch final {
public:
    static constexpr unsigned capacity = 32;
    void requestStop() noexcept { stop_.store(true,std::memory_order_release); }
    bool sealed() const noexcept { return sealed_.load(std::memory_order_acquire); }
private:
    template<bool> friend class Capture;
    friend class Consumer;
    std::array<Record,capacity> records_{};
    std::thread::id producer_{}; // Thread identity only. Published once by started_.
    std::atomic<bool> producerClaimed_{false}, started_{false}, sealed_{false}, stop_{false}, claimed_{false};
    std::atomic<std::uint64_t> heartbeat_{0};
    unsigned count_ = 0;
    Reason reason_ = Reason::stopped;
};
static_assert(sizeof(Batch) <= 4096); // Includes all 32 records; no dynamic storage.

// Default disabled. Only the explicitly gated task diagnostic variant and
// closed tests instantiate true; never any production/Alpha 0.7 target.
template<bool Enabled = false>
class Capture final {
public:
    explicit Capture(Batch& batch) : batch_(batch) {}
    Capture(const Capture&) = delete;
    Capture& operator=(const Capture&) = delete;
    template<class View, class Controller>
    void beforeAttempt(View& view, const Controller& controller, std::uint64_t callback, std::uint64_t now) {
        if constexpr (Enabled) {
            if (!view.nativeContext() || used_) return;
            used_ = true; start_ = last_ = callback; startMs_ = lastMs_ = now;
            if (batch_.producerClaimed_.exchange(true,std::memory_order_acq_rel)) { done_=true; return; }
            batch_.producer_ = std::this_thread::get_id();
            batch_.heartbeat_.store(now,std::memory_order_relaxed);
            batch_.started_.store(true,std::memory_order_release);
            entity_ = view.identity(); // Native-thread private; NEVER copied into Batch/Record.
            if (batch_.stop_.load(std::memory_order_acquire)) { finish(Reason::stopped); return; }
            if (!matches(controller) || controller.moveState() != ps::MoveState::idle) { finish(Reason::identity); return; }
            sample(view,controller,callback);
        }
    }
    template<class View, class Controller>
    void afterTick(View& view, const Controller& controller, std::uint64_t callback, std::uint64_t now) {
        if constexpr (Enabled) {
            if (!view.nativeContext() || !used_ || done_) return;
            if (batch_.stop_.load(std::memory_order_acquire)) { finish(Reason::stopped); return; }
            if (callback < last_ || now < lastMs_) { finish(Reason::clock_regressed); return; }
            last_ = callback; lastMs_ = now;
            batch_.heartbeat_.store(now,std::memory_order_release);
            if (callback-start_ >= callbackLimit || now-startMs_ >= lifetimeMs) { finish(Reason::callback_limit); return; }
            if (!matches(controller) || view.identity() != entity_) { finish(Reason::identity); return; }
            if (controller.state() != ps::State::ped_created) { finish(Reason::lifecycle); return; }
            const auto walk = controller.moveState();
            if (walk == ps::MoveState::failed) { finish(Reason::movement_failed); return; }
            const bool due = !batch_.count_ || walk != batch_.records_[batch_.count_-1].walk || callback-lastSample_ >= stride;
            if (due) sample(view,controller,callback);
            if (!done_ && walk == ps::MoveState::arrived) finish(Reason::complete);
        }
    }
    static constexpr unsigned stride = 32, callbackLimit = 512;
    static constexpr std::uint64_t lifetimeMs = 120000;
private:
    template<class Controller> bool matches(const Controller& c) const noexcept {
        return c.ped() == entity_.ped && c.key().generation == entity_.session.generation && c.key().player == entity_.session.player;
    }
    void finish(Reason reason) noexcept {
        done_ = true; batch_.reason_ = reason;
        batch_.sealed_.store(true,std::memory_order_release); // Last producer write to batch data.
    }
    template<class View, class Controller>
    void sample(View& view, const Controller& controller, std::uint64_t callback) {
        if (batch_.count_ == Batch::capacity) { finish(Reason::capacity); return; }
        if (controller.state() != ps::State::ped_created) { finish(Reason::lifecycle); return; }
        if (native::authenticate(view,entity_) != native::Reason::none) { finish(Reason::authentication); return; }
        Record r{}; r.callback=callback; r.remoteId=entity_.session.player; r.generation=entity_.session.generation;
        r.walk=controller.moveState();
        std::uint32_t matrix=0, flags=0, fallen=0, support=0, current=0;
        struct States { std::int32_t ped, move; } states{};
        static_assert(sizeof(States) == 8 && sizeof(Pair) == 8);
        std::int32_t type=0; float health=0;
        const std::uint64_t ped=entity_.ped;
        if (!native::read(view,ped+layout::matrix,matrix) || !matrix ||
            !native::read(view,ped+layout::flags,flags) || !native::read(view,ped+layout::fallenFlags,fallen) ||
            !native::read(view,ped+layout::states,states) || !native::read(view,ped+layout::support,support) ||
            !native::read(view,ped+layout::speed,r.speed) || !native::read(view,ped+layout::localShift,r.local) ||
            !native::read(view,ped+layout::worldShift,r.converted) || !native::read(view,std::uint64_t{matrix}+layout::position,r.position) ||
            !native::read(view,ped+layout::type,type) || !native::read(view,ped+layout::health,health)) {
            finish(Reason::incomplete_read); return; // Never publish a partial/default sample.
        }
        if (!native::read(view,ped+layout::matrix,current) || current != matrix) { finish(Reason::matrix_changed); return; }
        if (native::authenticate(view,entity_) != native::Reason::none || !matches(controller)) { finish(Reason::authentication); return; }
        const auto finite=[](Pair p) { return std::isfinite(p.x) && std::isfinite(p.y); };
        if (type != 4 || !std::isfinite(health) || health <= 0 || (flags & 0x100U) ||
            !finite(r.speed) || !finite(r.local) || !finite(r.converted) || !ps::finite(r.position)) { finish(Reason::invalid_values); return; }
        r.fallen=(fallen & 0x100000U)!=0; r.standing=(flags & 1U)!=0; r.supportPresent=support!=0;
        r.pedState=states.ped; r.moveState=states.move;
        batch_.records_[batch_.count_++]=r; lastSample_=callback;
    }
    Batch& batch_;
    native::Entity entity_{}; // Producer-local opaque identity; no historical pointer is dereferenced.
    std::uint64_t start_=0,last_=0,lastSample_=0,startMs_=0,lastMs_=0;
    bool used_=false,done_=false;
};

// Bounded deferred drain; no backend/view/engine is available here. A future
// worker polls this after the attempt signal, using the existing residence
// policy/FileSink. Neither the batch nor the consumer holds any engine view.
class Consumer final {
public:
    enum class Outcome { waiting, complete, inconclusive, not_deferred };
    explicit Consumer(std::uint64_t now) : start_(now), previous_(now) {}
    Consumer(const Consumer&) = delete;
    Consumer& operator=(const Consumer&) = delete;
    template<class Sink> Outcome poll(Batch& batch, std::uint64_t now, Sink& sink) {
        if (finished_) return outcome_;
        const bool started=batch.started_.load(std::memory_order_acquire);
        if (started && batch.producer_ == std::this_thread::get_id()) return Outcome::not_deferred;
        auto end=[&](const char* reason, bool complete=false) {
            batch.requestStop(); finished_=true; outcome_=complete ? Outcome::complete : Outcome::inconclusive;
            char line[192]{};
            const int n=std::snprintf(line,sizeof(line),"movement_history=%s reason=%s records=%u",complete ? "complete" : "inconclusive",reason,records_);
            if (n<0 || static_cast<std::size_t>(n)>=sizeof(line) || !sink(line)) outcome_=Outcome::inconclusive;
            return outcome_;
        };
        if (!claimed_) {
            if (batch.claimed_.exchange(true,std::memory_order_acq_rel)) return end("consumer_conflict");
            claimed_=true;
        }
        if (batch.sealed()) {
            for (unsigned i=0;i<batch.count_;++i) {
                char line[messageCapacity]{};
                if (!format(line,sizeof(line),batch.records_[i]) || !sink(line)) return end("format_or_sink_failed");
                ++records_;
            }
            return end(name(batch.reason_),batch.reason_==Reason::complete);
        }
        if (batch.stop_.load(std::memory_order_acquire)) return end("stop_requested_unsealed_batch_not_read");
        if (now<previous_ || now<start_) return end("clock_regressed");
        previous_=now;
        const auto beat=started ? batch.heartbeat_.load(std::memory_order_acquire) : start_;
        if (now>=beat && now-beat>=5000) return end("callbacks_absent_unsealed_batch_not_read");
        if (now-start_>=Capture<>::lifetimeMs) return end("lifetime_limit_unsealed_batch_not_read");
        return Outcome::waiting;
    }
private:
    std::uint64_t start_,previous_;
    unsigned records_=0;
    bool claimed_=false,finished_=false;
    Outcome outcome_=Outcome::waiting;
};
} // namespace grandmp::windows::native_movement_diagnostic
