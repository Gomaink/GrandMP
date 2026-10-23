#pragma once
#include "continuous_remote_native_observer.hpp"
#include <thread>

namespace grandmp::windows::continuous_remote_native {

// Slice 2D.1: CLOSED adapter, not included by any ASI. Backend allocation,
// construction, geometry and transfer operations currently exist ONLY in tests.
// In particular, transferWindowProven() has NO admitted Hoodlum implementation.
class ReassignmentGuard final {
public:
    enum class Failure { none, context, reentry, session, callback, authentication,
        manager_changed, slots, command, geometry, transfer_contract, allocation,
        construction, task_identity, transfer };
    explicit ReassignmentGuard(Entity entity) : entity_(entity), thread_(std::this_thread::get_id()) {}
    ReassignmentGuard(const ReassignmentGuard&) = delete;
    ReassignmentGuard& operator=(const ReassignmentGuard&) = delete;
    Failure failure() const noexcept { return failure_; } // Value only; native consumer.

    template<class Backend>
    std::optional<Coordinator::Command> next(Coordinator& coordinator, Backend& backend,
        core::MovementPoint actual, std::uint64_t callback, std::uint64_t now) {
        if (std::this_thread::get_id() != thread_) return std::nullopt; // No shared-state write/read or engine call.
        if (!enter(coordinator, backend)) return std::nullopt;
        Scope scope{busy_};
        if (!callback || callback <= lastCallback_ || ticket_) {
            reject(coordinator, Failure::callback); return std::nullopt;
        }
        lastCallback_ = callback;
        if (!session(coordinator, backend)) return std::nullopt;
        const auto observation = probe(coordinator, backend);
        if (failure_ != Failure::none) return std::nullopt;
        auto command = coordinator.next(actual, observation.slots, true, true, now);
        if (command) { command_ = *command; ticket_ = true; commandAt_ = now; }
        return command;
    }

    template<class Backend>
    bool assign(Coordinator& coordinator, Backend& backend, std::uint64_t callback, std::uint64_t now) {
        if (std::this_thread::get_id() != thread_) return false;
        if (!enter(coordinator, backend)) return false;
        Scope scope{busy_};
        const bool authorized = ticket_;
        ticket_ = false; // Consume BEFORE allocation, construction or reentrant calls.
        if (!authorized || callback != lastCallback_ || now < commandAt_ ||
            now - commandAt_ > Coordinator::timeoutMs) return reject(coordinator, Failure::callback);
        if (!checkpoint(coordinator, backend)) return false;
        // This is an audited backend contract, not a build/environment switch.
        // No native backend can claim it from this fake's successful tests.
        if (!backend.transferWindowProven()) return reject(coordinator, Failure::transfer_contract);
        if (!backend.geometry(command_)) return reject(coordinator, Failure::geometry);
        if (!checkpoint(coordinator, backend)) return false;
        const auto allocation = backend.allocate(command_);
        if (!allocation) return reject(coordinator, Failure::allocation);
        if (!checkpoint(coordinator, backend)) return false;
        if (!backend.allocationValid(allocation)) return reject(coordinator, Failure::task_identity);
        if (!checkpoint(coordinator, backend)) return false;
        const auto task = backend.construct(allocation, command_);
        if (!task || task != allocation) return reject(coordinator, Failure::construction);
        if (!checkpoint(coordinator, backend)) return false;
        if (!backend.transferWindowProven()) return reject(coordinator, Failure::transfer_contract);
        // Re-probe geometry after construction too. This exists only in the
        // closed backend; it does not add a query to the Alpha 0.7 callback.
        if (!backend.geometry(command_)) return reject(coordinator, Failure::geometry);
        if (!checkpoint(coordinator, backend)) return false;
        if (!backend.taskValid(task)) return reject(coordinator, Failure::task_identity);
        if (!backend.transferWindowProven()) return reject(coordinator, Failure::transfer_contract);
        // Last full probe AFTER all operations that can allocate/construct or
        // query geometry. It derives the CURRENT manager, never reads through
        // manager_ (which is only an opaque identity for comparison).
        if (!checkpoint(coordinator, backend)) return false;
        // No intervening backend operation. A snapshot is still NOT a lock:
        // transferWindowProven must cover this interval and transfer effects.
        const bool accepted = backend.transfer(manager_, task);
        // Never inspect/delete task after ownership was transferred, even if
        // transfer or acknowledgement fails. No destructor/abort API exists here.
        if (!accepted) return reject(coordinator, Failure::transfer);
        if (!coordinator.acknowledge(command_.attempt, true, now)) return reject(coordinator, Failure::command);
        return true;
    }
private:
    struct Scope { bool& busy; ~Scope() { busy = false; } };
    bool reject(Coordinator& coordinator, Failure reason) {
        if (failure_ == Failure::none) failure_ = reason;
        ticket_ = false;
        coordinator.cancel(entity_.session); // Value-only; never cancel an engine task.
        return false;
    }
    template<class Backend>
    bool enter(Coordinator& coordinator, Backend& backend) {
        if (failure_ != Failure::none) return false;
        if (busy_) return reject(coordinator, Failure::reentry);
        if (!backend.nativeContext()) return reject(coordinator, Failure::context);
        busy_ = true;
        return true;
    }
    template<class Backend>
    bool session(Coordinator& coordinator, Backend& backend) {
        if (failure_ != Failure::none) return false;
        if (!backend.nativeContext()) return reject(coordinator, Failure::context);
        if (coordinator.session() != entity_.session || coordinator.poisoned() ||
            coordinator.state() == Coordinator::State::inactive || coordinator.state() == Coordinator::State::cancelled ||
            coordinator.state() == Coordinator::State::failed) return reject(coordinator, Failure::session);
        return true;
    }
    template<class Backend>
    Observation probe(Coordinator& coordinator, Backend& backend) {
        std::uint32_t intelligence = 0, manager = 0;
        const auto result = detail::observeSlots(backend, entity_, [&](std::uint32_t i, std::uint32_t m) {
            intelligence = i; manager = m;
        });
        if (failure_ != Failure::none) return {};
        if (result.slots == Coordinator::Slots::unknown) reject(coordinator, Failure::authentication);
        else if (manager_ && (intelligence != intelligence_ || manager != manager_)) reject(coordinator, Failure::manager_changed);
        else { intelligence_ = intelligence; manager_ = manager; }
        return result;
    }
    template<class Backend>
    bool checkpoint(Coordinator& coordinator, Backend& backend) {
        if (!session(coordinator, backend)) return false;
        if (coordinator.state() != Coordinator::State::issuing ||
            coordinator.command().attempt != command_.attempt || coordinator.command().session != command_.session)
            return reject(coordinator, Failure::command);
        const auto observation = probe(coordinator, backend);
        if (failure_ != Failure::none) return false;
        if (observation.slots != Coordinator::Slots::empty) return reject(coordinator, Failure::slots);
        return session(coordinator, backend); // No memory reads; detects callback-driven cancellation/reentry.
    }
    const Entity entity_;
    const std::thread::id thread_;
    Coordinator::Command command_{};
    std::uint64_t lastCallback_ = 0, commandAt_ = 0;
    std::uint32_t intelligence_ = 0, manager_ = 0; // Never serialized or dereferenced.
    Failure failure_ = Failure::none;
    bool busy_ = false, ticket_ = false;
};
} // namespace grandmp::windows::continuous_remote_native
