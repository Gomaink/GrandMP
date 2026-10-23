#define GRANDMP_REMOTE_PED_MOVE_VARIANT 1
#include "../client/windows/tools/native_reassignment_guard.hpp"
#include "../client/windows/tools/remote_ped_move.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace n = grandmp::windows::continuous_remote_native;
namespace rm = grandmp::windows::remote_ped_move;
namespace ps = grandmp::windows::ped_smoke;
using C = n::Coordinator;
using G = n::ReassignmentGuard;
using Point = grandmp::core::MovementPoint;
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }

// Synthetic identities only. This backend deliberately has no destructor,
// cancellation, network, logging, GTA calls or actual allocation operations.
struct Backend {
    n::Entity entity{{2,42},0x1000,0x101};
    std::thread::id thread = std::this_thread::get_id();
    std::uint32_t intelligence = 0x2000;
    std::array<std::uint32_t,4> slots{};
    bool context = true, code = true, owned = true, owner = true, window = true;
    bool allocateFails = false, constructFails = false, allocationOkay = true, taskOkay = true;
    bool collision = false, terrain = true, transferFails = false, changeDuringProbe = false;
    unsigned reads = 0, calls = 0, allocations = 0, constructions = 0, transfers = 0, spheres = 0;
    unsigned transferEntries = 0, taskEpoch = 1, validatedTaskEpoch = 0;
    bool auditRefuseChangedTransfer = false;
    std::uint32_t partial = 0;
    std::function<void()> afterGeometry, afterAllocate, afterConstruct, afterTaskIdentity, afterTransfer;
    std::function<void(std::uint32_t)> afterRead;
    std::vector<std::string> order;
    bool nativeContext() const { return context && thread == std::this_thread::get_id(); }
    bool matches(const n::Entity& expected) const { return entity == expected; }
    void touch() { check(nativeContext(),"no native reads/calls outside authenticated phase"); ++calls; }
    bool engineReady() { touch(); return code; }
    bool owns(std::uint32_t ped) { touch(); return owned && ped == entity.ped; }
    n::ReadResult readMemory(std::uint32_t address, void* out, std::size_t size) {
        touch(); ++reads; check(size == 4,"DWORD only");
        std::uint32_t value = 0;
        if (address == entity.ped + n::layout::intelligence) value = intelligence;
        else if (address == intelligence || address == intelligence + n::layout::manager + n::layout::managerOwner)
            value = owner ? entity.ped : 0;
        else if (address >= intelligence + n::layout::manager &&
            address <= intelligence + n::layout::manager + 3 * n::layout::pointerBytes && (address - intelligence - n::layout::manager) % 4 == 0)
            value = slots[(address - intelligence - n::layout::manager) / 4];
        else throw std::runtime_error("historic manager read, task dereference or slot 4 access");
        const auto bytes = address == partial ? size - 1 : size;
        std::memcpy(out,&value,bytes);
        if (changeDuringProbe && address == intelligence + n::layout::manager + 3 * n::layout::pointerBytes) {
            intelligence += 0x1000; changeDuringProbe = false;
        }
        if (address == intelligence + n::layout::manager + n::layout::managerOwner) order.push_back("owner_probe");
        if (afterRead) afterRead(address);
        return {true,bytes};
    }
    bool transferWindowProven() const { return window; } // Proof for synchronous fake ONLY.
    float ground(float, float, float, bool& found) { touch(); found = terrain; return 29; }
    rm::SphereProbeResult sphere(ps::Position) {
        touch(); ++spheres;
        return {collision, collision ? rm::CollisionIdentity::unclassified_hit : rm::CollisionIdentity::no_hit};
    }
    bool geometry(const C::Command& command) {
        touch(); order.push_back("geometry");
        ps::Position destination{};
        const ps::Position from{command.origin.x,command.origin.y,command.origin.z};
        const ps::Position candidate{command.destination.x,command.destination.y,command.destination.z};
        const auto failure = rm::findDestination(*this,from,candidate,destination);
        if (afterGeometry) afterGeometry();
        return failure == nullptr && destination.x == candidate.x && destination.y == candidate.y && destination.z == candidate.z;
    }
    std::uint32_t allocate(const C::Command&) {
        touch(); ++allocations; order.push_back("allocate");
        if (afterAllocate) afterAllocate();
        return allocateFails ? 0u : 0x7000u;
    }
    bool allocationValid(std::uint32_t value) { touch(); return allocationOkay && value == 0x7000; }
    std::uint32_t construct(std::uint32_t value, const C::Command&) {
        touch(); ++constructions; order.push_back("construct");
        if (afterConstruct) afterConstruct();
        return constructFails ? 0u : value;
    }
    bool taskValid(std::uint32_t task) {
        touch(); order.push_back("task_identity");
        validatedTaskEpoch = taskEpoch;
        if (afterTaskIdentity) afterTaskIdentity();
        return taskOkay && task == 0x7000;
    }
    bool transfer(std::uint32_t manager, std::uint32_t task) {
        touch();
        ++transferEntries;
        // Audit-only veto: GTA SetTask returns void and has no equivalent veto.
        // Count entry separately so a guard reaching this boundary is visible.
        if (auditRefuseChangedTransfer && (manager != intelligence + n::layout::manager ||
            slots != std::array<std::uint32_t,4>{} || taskEpoch != validatedTaskEpoch)) return false;
        check(manager == intelligence + n::layout::manager,"fresh authenticated manager only");
        check(slots == std::array<std::uint32_t,4>{},"never overwrite a protected slot");
        check(task == 0x7000,"only authenticated new task");
        order.push_back("transfer"); ++transfers; slots[3] = task;
        if (afterTransfer) afterTransfer();
        return !transferFails;
    }
};

struct Fixture {
    Backend engine;
    C core;
    G guard{engine.entity};
    Point actual{1000,2000,30};
    std::uint64_t callback = 0, now = 0, revision = 0;
    Fixture() {
        check(core.begin(engine.entity.session,{42,1,100,200,30},actual,now),"begin authenticated fake session");
    }
    void offer(float x) {
        ++revision;
        check(core.offer(engine.entity.session,{42,static_cast<std::uint32_t>(revision+1),x,200,30},revision) == C::SampleResult::accepted,"ordered position");
    }
    auto next() { now += 100; return guard.next(core,engine,actual,++callback,now); }
    bool assign() { return guard.assign(core,engine,callback,now); }
    void first() { offer(102); check(next().has_value() && assign(),"first guarded task"); }
    void arrive(bool release = true) {
        actual.x = core.command().destination.x - 1;
        core.observe(core.command().attempt,C::Progress::moving,now += 100);
        actual = core.command().destination;
        core.observe(core.command().attempt,C::Progress::arrived,now += 400);
        if (release) engine.slots = {};
        check(core.state() == C::State::awaiting_release,"actual progress/arrival feedback required");
    }
    void second() { first(); arrive(); offer(104); check(next().has_value(),"second intention"); }
    void refused(unsigned transfers = 1) {
        const auto allocations = engine.allocations;
        check(!assign(),"refuse transfer");
        check(engine.transfers == transfers,"no extra SetTask");
        check(!next() && !assign(),"failure is terminal, no retry");
        check(engine.allocations == allocations,"no retry allocation");
    }
};

void run(const std::string& name) {
    Fixture f;
    // Characterization, NOT safety proofs: deliberately violate the purported
    // transferWindowProven contract while it still returns true. No native call.
    if (name == "audit_manager_aba") {
        f.first(); f.arrive();
        const auto original = f.engine.intelligence;
        f.engine.intelligence += 0x1000; // Different incarnation between probes.
        f.engine.intelligence = original; // Same address again, no observable token.
        f.offer(104);
        check(f.next().has_value() && f.assign(),"address equality cannot detect unobserved ABA");
        check(f.engine.transfers == 2 && f.guard.failure() == G::Failure::none,"true boolean cannot prove continuity");
        return;
    }
    if (name == "audit_final_slot" || name == "audit_final_manager" || name == "audit_task_reuse") {
        f.second();
        bool armed = false;
        unsigned ownerReads = 0;
        f.engine.auditRefuseChangedTransfer = true;
        f.engine.afterTaskIdentity = [&] { armed = true; };
        f.engine.afterRead = [&](std::uint32_t address) {
            if (!armed || address != f.engine.intelligence + n::layout::manager + n::layout::managerOwner || ++ownerReads != 2) return;
            // Happens AFTER copying the last owner read. Slots/current manager
            // were read earlier, and task identity before the whole final probe.
            if (name == "audit_final_slot") f.engine.slots[0] = 1;
            else if (name == "audit_final_manager") f.engine.intelligence += 0x1000;
            else f.engine.taskEpoch += 128; // Same address/tag period, different model incarnation.
        };
        check(!f.assign(),"audit backend refuses changed state at boundary");
        check(ownerReads == 2 && f.engine.transferEntries == 2 && f.engine.transfers == 1,
            "guard entered transfer despite invalidated final snapshot");
        check(f.guard.failure() == G::Failure::transfer,"only backend veto, not guard probe, refused");
        f.engine.afterRead = {};
        f.refused();
        return;
    }
    if (name == "audit_elapsed_argument") {
        f.second();
        std::uint64_t actualNow = f.now;
        f.engine.afterConstruct = [&] { actualNow += C::timeoutMs + 1; };
        check(f.assign(),"passed-by-value now is not refreshed inside transaction");
        check(actualNow - f.now > C::timeoutMs && f.core.state() == C::State::assigned,
            "same callback token alone does not bound elapsed engine-call time");
        check(f.engine.transfers == 2,"characterization only, not permission for native binding");
        return;
    }
    if (name == "audit_transfer_ownership") {
        f.second();
        f.engine.afterTransfer = [&] { f.engine.owned = false; };
        check(f.assign() && !f.engine.owned,"a true backend return alone does not authenticate post-transfer ownership");
        check(f.engine.transfers == 2 && f.core.state() == C::State::assigned,"postcondition is a backend obligation");
        check(!f.next(),"subsequent probe refuses lost ownership");
        f.refused(2);
        return;
    }
    if (name == "audit_transfer_reentry") {
        f.second();
        f.engine.afterTransfer = [&] {
            check(!f.guard.next(f.core,f.engine,f.actual,f.callback,f.now),"reentry rejected while transferring");
        };
        check(!f.assign() && f.engine.transfers == 2 && f.guard.failure() == G::Failure::reentry,
            "terminal reentry does not undo transfer already performed");
        f.refused(2);
        return;
    }
    if (name == "three_walks") {
        for (unsigned i = 1; i <= 3; ++i) {
            f.offer(100.f + 2.f * i); check(f.next().has_value(),"next sequential leg");
            check(f.assign(),"guarded transfer"); f.arrive();
            std::printf("walk=%u transfers=%u arrival_observed=1 slots_released=1\n",i,f.engine.transfers);
        }
        check(f.engine.transfers == 3 && f.engine.allocations == 3,"three tasks, three observed arrivals");
        check(!f.next() && f.engine.transfers == 3,"no task spam");
        const auto& order = f.engine.order;
        for (std::size_t i = 1; i < order.size(); ++i)
            if (order[i] == "transfer") check(order[i-1] == "owner_probe","final full probe immediately precedes transfer");
        return;
    }
    if (name == "arrival_busy" || name == "early_release") {
        f.first(); f.offer(104);
        if (name == "early_release") { f.engine.slots = {}; check(!f.next(),"empty before arrival cannot authorize next task"); }
        f.arrive(false);
        if (name == "arrival_busy") check(!f.next(),"arrival with occupied slots still waits");
        f.engine.slots = {}; check(f.next().has_value() && f.assign(),"arrival AND fresh release permit next leg"); return;
    }
    if (name == "manager_same" || name == "manager_replace" || name == "manager_during_probe" || name == "unknown" || name == "manager_active") {
        if (name == "manager_active") {
            f.first(); f.engine.intelligence += 0x1000;
            check(!f.next(),"manager change during active walk is terminal"); f.refused(); return;
        }
        f.first(); f.arrive(); f.offer(104);
        if (name == "manager_replace") f.engine.intelligence += 0x1000;
        if (name == "manager_during_probe") f.engine.changeDuringProbe = true;
        if (name == "unknown") f.engine.partial = f.engine.intelligence + n::layout::manager + 8;
        auto command = f.next();
        if (name == "manager_same") check(command.has_value() && f.assign(),"stable manager permits sequential fake transfer");
        else { check(!command,"manager discontinuity/unknown blocks intention"); f.refused(); }
        return;
    }
    if (name == "thread") {
        f.second(); const auto reads = f.engine.reads, calls = f.engine.calls;
        std::thread other([&] { check(!f.guard.assign(f.core,f.engine,f.callback,f.now),"wrong thread denied"); }); other.join();
        check(f.engine.reads == reads && f.engine.calls == calls,"foreign thread performs no engine operations");
        check(f.assign(),"foreign call never mutates native-local guard"); return;
    }
    f.second();
    if (name == "slots_before") f.engine.slots[0] = 1;
    else if (name == "slots_after_allocate") f.engine.afterAllocate = [&] { f.engine.slots[1] = 1; };
    else if (name == "slots_after_construct") f.engine.afterConstruct = [&] { f.engine.slots[2] = 1; };
    else if (name == "slots_final") f.engine.afterTaskIdentity = [&] { f.engine.slots[3] = 1; };
    else if (name == "manager_after_allocate") f.engine.afterAllocate = [&] { f.engine.intelligence += 0x1000; };
    else if (name == "manager_after_construct") f.engine.afterConstruct = [&] { f.engine.intelligence += 0x1000; };
    else if (name == "generation") ++f.engine.entity.session.generation;
    else if (name == "reference") ++f.engine.entity.reference;
    else if (name == "ped") f.engine.entity.ped += 0x100;
    else if (name == "ownership") f.engine.owned = false;
    else if (name == "context") f.engine.context = false;
    else if (name == "owner") f.engine.owner = false;
    else if (name == "code") f.engine.code = false;
    else if (name == "disconnect") f.core.cancel(f.engine.entity.session);
    else if (name == "reconnect") {
        f.core.cancel(f.engine.entity.session);
        check(!f.core.begin({3,43},{43,1,100,200,30},f.actual,f.now),"reconnect needs authenticated cleanup first");
    }
    else if (name == "late_callback") ++f.callback;
    else if (name == "expired") f.now += C::timeoutMs + 1;
    else if (name == "contract") f.engine.window = false;
    else if (name == "collision") f.engine.collision = true;
    else if (name == "terrain") f.engine.terrain = false;
    else if (name == "collision_after_construct") f.engine.afterConstruct = [&] { f.engine.collision = true; };
    else if (name == "terrain_after_construct") f.engine.afterConstruct = [&] { f.engine.terrain = false; };
    else if (name == "contract_after_construct") f.engine.afterConstruct = [&] { f.engine.window = false; };
    else if (name == "contract_final") f.engine.afterTaskIdentity = [&] { f.engine.window = false; };
    else if (name == "allocation") f.engine.allocateFails = true;
    else if (name == "allocation_identity") f.engine.allocationOkay = false;
    else if (name == "construction") f.engine.constructFails = true;
    else if (name == "task_identity") f.engine.taskOkay = false;
    else if (name == "ownership_after_construct") f.engine.afterConstruct = [&] { f.engine.owned = false; };
    else if (name == "generation_after_construct") f.engine.afterConstruct = [&] { ++f.engine.entity.session.generation; };
    else if (name == "generation_final") f.engine.afterTaskIdentity = [&] { ++f.engine.entity.session.generation; };
    else if (name == "manager_final") f.engine.afterTaskIdentity = [&] { f.engine.intelligence += 0x1000; };
    else if (name == "context_after_construct") f.engine.afterConstruct = [&] { f.engine.context = false; };
    else if (name == "cancel_after_construct") f.engine.afterConstruct = [&] { f.core.cancel(f.engine.entity.session); };
    else if (name == "reentry") f.engine.afterAllocate = [&] { check(!f.guard.next(f.core,f.engine,f.actual,f.callback,f.now),"nested entry refused"); };
    else if (name == "transfer_failure") {
        f.engine.transferFails = true;
        check(!f.assign() && f.engine.transfers == 2,"failed transferred task remains engine owned");
        const auto allocations = f.engine.allocations;
        check(!f.next() && !f.assign() && f.engine.allocations == allocations,"no retry/delete after transfer uncertainty"); return;
    }
    else if (name == "replay") {
        check(f.assign(),"accepted once"); const auto allocations = f.engine.allocations;
        check(!f.assign() && f.engine.transfers == 2 && f.engine.allocations == allocations,"ticket cannot be replayed"); return;
    }
    else throw std::runtime_error("unknown case");
    const auto allocations = f.engine.allocations;
    check(!f.assign() && f.engine.transfers == 1,"guard blocks second SetTask");
    const auto after = f.engine.allocations;
    check(!f.next() && !f.assign() && f.engine.allocations == after,"no retry or speculative destruction");
    if (name == "collision" || name == "terrain" || name == "contract") check(after == allocations,"refuse before allocation");
}
int main(int argc, char** argv) {
    try { check(argc == 2,"one case required"); run(argv[1]); return 0; }
    catch (const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
