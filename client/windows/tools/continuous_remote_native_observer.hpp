#pragma once
#include "grandmp/core/continuous_remote_movement.hpp"
#include "ped_smoke_addresses.hpp"
#include "ped_smoke_task_layout.hpp"
#include <cstddef>
#include <limits>

namespace grandmp::windows::continuous_remote_native {
using Coordinator = core::ContinuousRemoteMovement;
using Session = core::MovementSession;
namespace layout = ped_smoke::task_layout;

// Consumer-local identity. Never publish this record or a backend to the worker.
struct Entity {
    Session session{};
    std::uint32_t ped = 0;
    std::int32_t reference = -1;
    bool operator==(const Entity&) const = default;
};
enum class Reason { none, context, identity, engine, ownership, intelligence, manager_owner,
    incomplete_read, changed, pool, removal_not_completed };
struct Observation {
    Coordinator::Slots slots = Coordinator::Slots::unknown;
    Reason reason = Reason::none;
    unsigned occupiedMask = 0; // Values only, no task addresses escape observation.
};
struct ReadResult { bool success; std::size_t transferred; };

// Backend contract (not a physical binding in Slice 2B): nativeContext verifies
// actual consumer thread, phase/caller and reentrancy WITHOUT native reads;
// matches validates the admitted session/registry; engineReady verifies admitted
// executable/live code, gameplay and streaming; owns authenticates reference,
// pool tag and registered link. readMemory must report the exact byte count.
template<class Backend>
Reason authenticate(Backend& backend, const Entity& entity) {
    if (!backend.nativeContext()) return Reason::context;
    if (!entity.session.player || !entity.session.generation || !entity.ped || entity.reference < 0 ||
        !backend.matches(entity)) return Reason::identity;
    if (!backend.engineReady()) return Reason::engine;
    if (!backend.owns(entity.ped)) return Reason::ownership;
    return Reason::none;
}
template<class Backend, class T>
bool read(Backend& backend, std::uint64_t address, T& value) {
    if (!address || address + sizeof(T) - 1 > std::numeric_limits<std::uint32_t>::max()) return false;
    const auto result = backend.readMemory(static_cast<std::uint32_t>(address), &value, sizeof(T));
    return result.success && result.transferred == sizeof(T);
}
namespace detail {
// The visitor is synchronous and native-thread local. Observation remains a
// value-only record; no manager identity is added to diagnostic events/queues.
template<class Backend, class Visitor>
Observation observeSlots(Backend& backend, const Entity& entity, Visitor&& visitor) {
    if (const auto reason = authenticate(backend, entity); reason != Reason::none)
        return {Coordinator::Slots::unknown, reason};
    std::uint32_t intelligence = 0, owner = 0;
    if (!read(backend, std::uint64_t{entity.ped} + layout::intelligence, intelligence) || !intelligence ||
        !read(backend, intelligence, owner) || owner != entity.ped)
        return {Coordinator::Slots::unknown, Reason::intelligence};
    const auto manager = std::uint64_t{intelligence} + layout::manager;
    if (!read(backend, manager + layout::managerOwner, owner) || owner != entity.ped)
        return {Coordinator::Slots::unknown, Reason::manager_owner};
    unsigned mask = 0;
    for (unsigned index = 0; index <= layout::primarySlot; ++index) {
        std::uint32_t opaque = 0;
        if (!read(backend, manager + index * layout::pointerBytes, opaque))
            return {Coordinator::Slots::unknown, Reason::incomplete_read};
        if (opaque) mask |= 1u << index; // Occupancy only. NEVER dereference a task.
    }
    if (authenticate(backend, entity) != Reason::none)
        return {Coordinator::Slots::unknown, Reason::changed};
    std::uint32_t currentIntelligence = 0;
    if (!read(backend, std::uint64_t{entity.ped} + layout::intelligence, currentIntelligence) || currentIntelligence != intelligence ||
        !read(backend, intelligence, owner) || owner != entity.ped ||
        !read(backend, manager + layout::managerOwner, owner) || owner != entity.ped)
        return {Coordinator::Slots::unknown, Reason::changed};
    visitor(intelligence, static_cast<std::uint32_t>(manager));
    return {mask ? Coordinator::Slots::busy : Coordinator::Slots::empty, Reason::none, mask};
}
} // namespace detail

template<class Backend>
Observation observeSlots(Backend& backend, const Entity& entity) {
    return detail::observeSlots(backend, entity, [](std::uint32_t, std::uint32_t) {});
}

// Called only by the consumer. A snapshot is not a reservation: assignMove must
// repeat authentication/slot checks, as the existing NativeEngine already does.
template<class Backend>
std::optional<Coordinator::Command> next(Coordinator& coordinator, Backend& backend,
    const Entity& entity, core::MovementPoint actual, std::uint64_t now) {
    if (!backend.nativeContext()) return std::nullopt; // No backend reads off-phase.
    if (coordinator.session() != entity.session || coordinator.poisoned() ||
        coordinator.state() == Coordinator::State::inactive || coordinator.state() == Coordinator::State::cancelled ||
        coordinator.state() == Coordinator::State::failed) return std::nullopt;
    if (coordinator.state() == Coordinator::State::ready && !coordinator.pendingCount()) return std::nullopt;
    const auto observation = observeSlots(backend, entity);
    if (observation.reason == Reason::identity || observation.reason == Reason::ownership ||
        observation.reason == Reason::engine || observation.reason == Reason::changed) {
        coordinator.invalidateIdentity(); return std::nullopt;
    }
    // active() deliberately ignores slot availability in the core. Unknown must
    // still fail closed during an active episode, without manipulating its task.
    if (observation.slots == Coordinator::Slots::unknown &&
        (coordinator.state() == Coordinator::State::issuing || coordinator.state() == Coordinator::State::assigned ||
         coordinator.state() == Coordinator::State::moving)) {
        coordinator.observe(coordinator.command().attempt, Coordinator::Progress::failed, now);
        return std::nullopt;
    }
    return coordinator.next(actual, observation.slots, true, true, now);
}

// Completion evidence wraps the EXISTING synchronous entity-removal operation.
// It never calls a task destructor/abort/SetTask. Registry fields cleared before
// RemovePed are insufficient: require return AND the old pool slot free.
class RemovalEvidence final {
public:
    RemovalEvidence() = default;
    RemovalEvidence(const RemovalEvidence&) = delete;
    RemovalEvidence& operator=(const RemovalEvidence&) = delete;
    bool admissionAllowed(const Coordinator& coordinator) const noexcept {
        return !attempted_ && !coordinator.poisoned() && coordinator.state() == Coordinator::State::inactive;
    }
    template<class Backend, class Remove>
    bool remove(Backend& backend, const Entity& entity, Remove&& operation) {
        if (attempted_ || authenticate(backend, entity) != Reason::none) return false;
        Pool before{};
        if (!pool(backend, entity, before) || before.flag != (entity.reference & 0xFF) || (before.flag & 0x80u)) return false;
        attempted_ = true; entity_ = entity; pool_ = before;
        const bool removed = operation(); // Only the existing lifecycle owns deletion.
        returned_ = removed; // Reached only after the synchronous operation returned.
        completed_ = removed && postcondition(backend);
        return removed; // Preserve lifecycle outcome even when evidence is incomplete.
    }
    template<class Backend>
    bool confirmRemoved(Coordinator& coordinator, Backend& backend, const Entity& entity) {
        if (coordinator.poisoned() || !completed_ || !returned_ || entity != entity_ || coordinator.session() != entity.session ||
            (coordinator.state() != Coordinator::State::cancelled && coordinator.state() != Coordinator::State::failed) ||
            !postcondition(backend) || !coordinator.confirmRemoved(entity.session)) return false;
        completed_ = returned_ = attempted_ = false;
        entity_ = {}; pool_ = {};
        return true;
    }
    bool completed() const noexcept { return completed_; }
private:
    struct Pool { std::uint32_t address = 0, objects = 0, flags = 0; std::int32_t size = 0; unsigned char flag = 0; };
    template<class Backend>
    static bool pool(Backend& backend, const Entity& entity, Pool& value) {
        const auto index = static_cast<unsigned>(entity.reference) >> 8;
        return read(backend, ped_smoke::pedPool, value.address) && value.address &&
            read(backend, value.address, value.objects) && value.objects &&
            read(backend, std::uint64_t{value.address} + 4, value.flags) && value.flags &&
            read(backend, std::uint64_t{value.address} + 8, value.size) && value.size > 0 &&
            index < static_cast<unsigned>(value.size) && read(backend, std::uint64_t{value.flags} + index, value.flag);
    }
    template<class Backend>
    bool postcondition(Backend& backend) const {
        // No owns(), manager or PED-field reads after destruction. Only stable
        // registry/session data and bounded reads of the original pool metadata.
        if (!backend.nativeContext() || !backend.sessionMatches(entity_.session) ||
            !backend.engineReady() || !backend.registryReleased()) return false;
        std::uint32_t currentPool = 0;
        if (!read(backend, ped_smoke::pedPool, currentPool) || currentPool != pool_.address) return false;
        Pool after{};
        return pool(backend, entity_, after) && after.address == pool_.address && after.objects == pool_.objects &&
            after.flags == pool_.flags && after.size == pool_.size && (after.flag & 0x80u);
    }
    Entity entity_{};
    Pool pool_{};
    bool attempted_ = false, returned_ = false, completed_ = false;
};
} // namespace grandmp::windows::continuous_remote_native
