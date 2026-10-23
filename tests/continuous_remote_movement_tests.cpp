// Closed reference adapter: existing lifecycle, movement observer and FU6 probes
// with a fake backend. No Windows/GTA entry points, native addresses or sockets.
#define GRANDMP_REMOTE_PED_MOVE_VARIANT 1
#include "grandmp/core/continuous_remote_movement.hpp"
#include "../client/windows/tools/remote_ped_move.hpp"
#include "../client/windows/tools/continuous_remote_native_observer.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <array>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace core = grandmp::core;
namespace rp = grandmp::windows::remote_ped;
namespace rm = grandmp::windows::remote_ped_move;
namespace ps = grandmp::windows::ped_smoke;
namespace native = grandmp::windows::continuous_remote_native;
using Coordinator = core::ContinuousRemoteMovement;
using Key = core::MovementSession;
using Point = core::MovementPoint;
void check(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
Point point(ps::Position p) { return {p.x,p.y,p.z}; }
ps::Position position(Point p) { return {p.x,p.y,p.z}; }
core::RemotePose pose(unsigned sequence = 1, float x = 100, std::uint64_t id = 42) {
    return {id,sequence,x,200,30};
}

struct FakeEngine {
    const std::thread::id owner = std::this_thread::get_id();
    ps::Position spawn{1000,2000,30}, actual = spawn, candidate{}, target{};
    unsigned calls = 0, tasks = 0, activeTasks = 0, maxActive = 0, arrivals = 0;
    unsigned creates = 0, removes = 0, cleanupTasks = 0, sphereCalls = 0;
    bool live = false, ownership = true, groundOkay = true, collision = false;
    bool assignmentFails = false, transferFails = false, slotsUnknown = false, externalTask = false;
    float groundHeight = 29;
    std::string lastFailure;
    std::vector<ps::Position> destinations;
    std::vector<std::string> events;
    Coordinator* coordinator = nullptr;
    bool reenter = false;
    std::uint64_t now = 0;
    int cjToken = 0;
    rm::CorridorCollisionDiagnostic diagnostic{};
    native::RemovalEvidence removal;
    Key registrySession{};
    int reference = -1;
    unsigned char poolFlag = 1;
    std::array<std::uint32_t,4> protectedSlots{};
    std::vector<std::uint32_t> reads;
    std::uint32_t partialAddress = 0, missingAddress = 0;
    std::uint32_t intelligenceBase = 0x2000;
    bool switchManagerDuringRead = false;
    bool contextAllowed = true, codeOkay = true, managerAvailable = true, ownerOkay = true;
    bool gameplayReady = true, streamingReady = true, invalidateAfterRead = false;
    bool intelligenceOwnerOkay = true, changeOwnerAfterRead = false;
    bool registeredLink = false;
    bool identityOkay = true, leavePoolLive = false, changePoolAfterRemoval = false, returnBeforeRemoval = false;
    bool partialAfterRemoval = false;
    bool occupyBeforeAssignment = false;

    native::Entity identity() const { return {registrySession,0x1000,reference}; }
    bool nativeContext() const { return contextAllowed && owner == std::this_thread::get_id(); }
    bool sessionMatches(Key key) const { return registrySession == key; }
    bool matches(const native::Entity& e) const { return identityOkay && e == identity(); }
    bool engineReady() { touch(); return codeOkay && gameplayReady && streamingReady; }
    bool registryReleased() const { return !live && reference == -1; }
    native::ReadResult readMemory(std::uint32_t address, void* output, std::size_t size) {
        touch(); reads.push_back(address);
        if (address == missingAddress) return {false,0};
        std::uint32_t value = 0;
        if (address == 0x1000 + ps::task_layout::intelligence) { check(live,"no dead PED reads"); value=managerAvailable?intelligenceBase:0u; }
        else if (address == intelligenceBase) { check(live,"no dead intelligence reads"); value=intelligenceOwnerOkay?0x1000u:0u; }
        else if (address == intelligenceBase + 4 + ps::task_layout::managerOwner) { check(live,"no dead manager reads"); value=ownerOkay?0x1000u:0x9999u; }
        else if (address >= intelligenceBase+4 && address < intelligenceBase+20 && (address-intelligenceBase-4)%4==0) {
            check(live,"no dead slot reads"); value=protectedSlots[(address-intelligenceBase-4)/4];
        }
        else if (address == ps::pedPool) value=changePoolAfterRemoval && !live?0x9000u:0x5000u;
        else if (address == 0x5000) value=0x800;
        else if (address == 0x5004) value=0x6000;
        else if (address == 0x5008) value=100;
        else if (address == 0x6001) value=poolFlag;
        else throw std::runtime_error("unexpected read: task pointer dereference or unreviewed field");
        const auto count=address==partialAddress?size-1:size;
        check(size<=sizeof(value),"fixed-width memory read"); std::memcpy(output,&value,count);
        if (invalidateAfterRead && address==0x2010) identityOkay=false;
        if (changeOwnerAfterRead && address==0x2010) ownerOkay=false;
        if (switchManagerDuringRead && address==intelligenceBase+16) { intelligenceBase+=0x1000; switchManagerDuringRead=false; }
        return {true,count}; // Includes successful-but-partial reads.
    }

    void touch() { check(owner == std::this_thread::get_id(),"backend call outside native consumer"); ++calls; }
    bool beginRemote(rp::Key key) {
        touch();
        if (coordinator && !removal.admissionAllowed(*coordinator)) return false;
        check(!live && !activeTasks && reference==-1,"registry must be released before admission");
        registrySession={key.generation,key.player}; return true;
    }
    void log(ps::State, const char*) { touch(); }
    bool requestModel() { touch(); return true; }
    bool modelLoaded() { touch(); return true; }
    bool revalidateModel() { touch(); return true; }
    bool spawnPosition(ps::Position& p) { touch(); p=spawn; return true; }
    std::uintptr_t create(const ps::Position& p) { touch(); actual=p; live=true; registeredLink=true; reference=0x101; poolFlag=1; ++creates; return 0x1000; }
    bool owns(std::uintptr_t p) {
        touch(); return live && ownership && registeredLink && p==0x1000 && reference>=0 && !(poolFlag&0x80u) && poolFlag==(reference&0xFF);
    }
    bool visible(std::uintptr_t) { touch(); return true; }
    bool remove(std::uintptr_t p) {
        check(owns(p),"never remove unknown identity");
        return removal.remove(*this,identity(),[&] {
            if (returnBeforeRemoval) return false;
            // Like NativeEngine: unlink/reset the registry BEFORE native deletion.
            registeredLink=false; live=false; reference=-1;
            check(!removal.completed(),"registry reset is not a cleanup receipt");
            cleanupTasks+=activeTasks; activeTasks=0; protectedSlots={};
            if (!leavePoolLive) poolFlag=0x81;
            if (partialAfterRemoval) partialAddress=0x6001;
            ++removes; return true;
        });
    }
    const char* movePosition(std::uintptr_t p, ps::Position& out) {
        if (!owns(p)) return "ownership_unproven";
        out=actual; return nullptr;
    }
    Coordinator::Slots slots() {
        if (slotsUnknown) missingAddress=0x2004;
        if (externalTask) protectedSlots[0]=0xFFFFFFFF;
        else protectedSlots[0]=0;
        return native::observeSlots(*this,identity()).slots;
    }
    float ground(float,float,float,bool& found) { touch(); found=groundOkay; return groundHeight; }
    rm::SphereProbeResult sphere(ps::Position) {
        touch(); ++sphereCalls;
        const void* hit=collision ? &cjToken : nullptr;
        return rm::classifySphereResult(hit,[&] {
            return rm::captureCjIdentity([&](void*& cj) { cj=&cjToken; return true; });
        });
    }
    const char* moveDestination(std::uintptr_t p, ps::Position origin, ps::Position& out) {
        if (!owns(p)) return "ownership_unproven";
        return rm::findDestination(*this,origin,candidate,out,&diagnostic);
    }
    const char* assignMove(std::uintptr_t p, const ps::Position& destination) {
        check(owns(p),"assignment ownership");
        if (occupyBeforeAssignment) protectedSlots[1]=0xFFFFFFFF;
        if (native::observeSlots(*this,identity()).slots!=Coordinator::Slots::empty)
            return "primary_task_slot_busy_or_unreadable";
        check(!activeTasks,"never overwrite an active task");
        if (assignmentFails) return "fake_assignment_refused";
        ++tasks; ++activeTasks; protectedSlots[3]=0xFFFFFFFC; maxActive=(std::max)(maxActive,activeTasks);
        target=destination; destinations.push_back(destination);
        if (transferFails) return "fake_post_transfer_refusal_engine_keeps_task";
        if (reenter) check(!coordinator->next(point(actual),Coordinator::Slots::empty,true,true,now),
            "issuing state must block reentrant command emission before assignment acknowledgment");
        return nullptr;
    }
    void moveLog(const char* event, const char* reason) {
        touch(); events.emplace_back(event);
        if (std::string_view(event)=="move_failed") lastFailure=reason;
        if (std::string_view(event)=="move_arrived") ++arrivals;
    }
    // Test driver simulates native movement/release, NEVER a network callback.
    void releaseTask() { check(activeTasks==1,"one task to release"); activeTasks=0; protectedSlots[3]=0; }
};

struct Fixture {
    rp::IntentSink sink;
    rp::Controller lifecycle;
    Coordinator core;
    FakeEngine engine;
    ps::Movement episode;
    rp::Intent desired{};
    native::Entity cleanupEntity{};
    std::uint64_t now = 0;
    bool busy = false;
    Fixture() {
        engine.coordinator=&core;
        sink.spawn(pose());
        for (int i=0;i<4;++i) tick();
        check(engine.creates==1 && !engine.tasks && core.state()==Coordinator::State::ready,
            "first remote position spawns and anchors but never walks");
    }
    Key key() const { return {lifecycle.key().generation,lifecycle.key().player}; }
    void tick(bool nativeThread=true, bool gameplay=true) {
        if (!nativeThread || !gameplay || engine.owner!=std::this_thread::get_id() || busy || core.poisoned()) return;
        busy=true;
        struct Leave { bool& busy; ~Leave(){busy=false;} } leave{busy};
        const auto incoming=sink.snapshot();
        if (incoming.key.generation>=desired.key.generation && incoming.revision>desired.revision) desired=incoming;
        else if (incoming.key==desired.key && incoming.revision==desired.revision &&
            incoming.sampleRevision>desired.sampleRevision && incoming.pose.sequence>=desired.pose.sequence) {
            desired.pose=incoming.pose; desired.sampleRevision=incoming.sampleRevision;
        }
        if (core.session().player && (!desired.present || desired.blocked ||
                core.session()!=Key{desired.key.generation,desired.key.player})) {
            const auto old=core.session();
            if (cleanupEntity.session!=old) cleanupEntity=engine.identity();
            core.cancel(old); // Drop pending BEFORE native entity cleanup, not task manipulation.
            // Do not let the lifecycle admit a replacement while evidence for
            // the previous entity is still pending, even if its registry is empty.
            if (lifecycle.state()!=ps::State::removed) lifecycle.tick(engine,desired,true,true,now);
            if (lifecycle.poisoned()) core.invalidateIdentity();
            else if (lifecycle.state()==ps::State::removed ||
                (lifecycle.entity().terminal() && !lifecycle.ped())) {
                (void)engine.removal.confirmRemoved(core,engine,cleanupEntity);
            }
            return;
        }
        lifecycle.tick(engine,desired,true,true,now); // Ownership/leave always precedes movement.
        if (lifecycle.poisoned()) { core.invalidateIdentity(); return; }
        if (lifecycle.state()!=ps::State::ped_created) return;
        if (core.state()==Coordinator::State::inactive) {
            check(engine.owns(lifecycle.ped()),"admission requires native identity");
            check(core.begin(key(),desired.initialPose,point(engine.spawn),now),"new session after confirmed cleanup");
            cleanupEntity={};
        }
        (void)core.offer(key(),desired.pose,desired.sampleRevision);
        const auto state=core.state();
        if (state==Coordinator::State::assigned || state==Coordinator::State::moving) {
            episode.tick(engine,lifecycle.entity(),true,true,false,false,now);
            Coordinator::Progress progress=Coordinator::Progress::assigned;
            if (episode.state()==ps::MoveState::moving) progress=Coordinator::Progress::moving;
            if (episode.state()==ps::MoveState::arrived) progress=Coordinator::Progress::arrived;
            if (episode.state()==ps::MoveState::failed) progress=Coordinator::Progress::failed;
            core.observe(core.command().attempt,progress,now);
        }
        if (core.state()==Coordinator::State::failed || core.state()==Coordinator::State::cancelled) return;
        ps::Position actual{};
        if (engine.movePosition(lifecycle.ped(),actual)) { core.invalidateIdentity(); return; }
        if (engine.slotsUnknown) engine.missingAddress=0x2004;
        engine.protectedSlots[0]=engine.externalTask?0xFFFFFFFFu:0u;
        const auto command=native::next(core,engine,engine.identity(),point(actual),now);
        if (!command) return;
        engine.candidate=position(command->destination);
        // Reset only AFTER observed arrival + explicit slot release, as required by next().
        // Existing Alpha 0.7 observer and all its geometry checks run for EACH leg.
        episode=ps::Movement{};
        engine.now=now;
        episode.tick(engine,lifecycle.entity(),true,true,true,false,now,
            ps::distance2D(position(command->origin),position(command->destination)));
        (void)core.acknowledge(command->attempt,episode.state()==ps::MoveState::assigned,now);
    }
    void step(std::uint64_t ms=100) { now+=ms; tick(); }
    void offer(unsigned sequence, float x, std::uint64_t id=42) { sink.move(pose(sequence,x,id)); step(); }
    void arrive() {
        unsigned frames=0;
        while (ps::distance2D(engine.actual,engine.target)>0.001f && ++frames<20) {
            const float dx=engine.target.x-engine.actual.x,dy=engine.target.y-engine.actual.y;
            const float distance=std::hypot(dx,dy), amount=(std::min)(distance,0.5f);
            engine.actual.x+=amount*dx/distance; engine.actual.y+=amount*dy/distance;
            step();
        }
        step(300);
        check(core.state()==Coordinator::State::awaiting_release && episode.state()==ps::MoveState::arrived,
            "arrival needs real displacement and stationary observation, not assignment");
    }
    void release() { engine.releaseTask(); step(); }
    void reconnect() {
        sink.despawn(42); step();
        sink.resetSession(); sink.spawn(pose(1,100,43));
        for (int i=0;i<4;++i) step();
    }
};

void run(std::string_view which) {
    static_assert(!rm::physicalExecutionApproved && !rp::physicalExecutionApproved && !ps::physicalExecutionApproved);
    static_assert(ps::task_layout::intelligence==0x47C && ps::task_layout::manager==4 &&
        ps::task_layout::managerOwner==0x2C && ps::task_layout::primarySlot==3 && ps::task_layout::pointerBytes==4);
    static_assert(sizeof(Coordinator)<512,"constant storage, never an unbounded destination queue");
    if (which.starts_with("slots_")) {
        FakeEngine e; e.beginRemote({2,42}); e.create(e.spawn);
        auto entity=e.identity();
        const auto inspect=[&] { return native::observeSlots(e,entity); };
        if (which=="slots_empty") {
            const auto result=inspect();
            check(result.slots==Coordinator::Slots::empty && !result.occupiedMask && e.reads.size()==10,
                "empty requires owners and all four complete slot reads");
            check(std::find(e.reads.begin(),e.reads.end(),0x2014)==e.reads.end(),"slot 4 never queried");
        } else if (which=="slots_each" || which=="slots_multiple") {
            for (unsigned i=0;i<4;++i) {
                e.protectedSlots={}; e.protectedSlots[i]=0xFFFFFFFF;
                if (which=="slots_multiple") e.protectedSlots[(i+1)%4]=0xFFFFFFFC;
                const auto count=e.reads.size(); const auto result=inspect();
                check(result.slots==Coordinator::Slots::busy && (result.occupiedMask&(1u<<i)) && e.reads.size()==count+10,
                    "each protected root blocks, including when slot 3 is null; no task dereference");
            }
        } else if (which=="slots_partial" || which=="slots_late_read") {
            for (const auto address : {0x147Cu,0x2000u,0x2030u,0x2004u,0x2008u,0x200Cu,0x2010u}) {
                e.partialAddress=address;
                if (which=="slots_late_read") e.protectedSlots[0]=0xFFFFFFFF;
                check(inspect().slots==Coordinator::Slots::unknown,"successful partial read never becomes empty or busy");
            }
        } else if (which=="slots_manager") {
            e.managerAvailable=false; check(inspect().slots==Coordinator::Slots::unknown,"missing intelligence");
            e.managerAvailable=true; e.missingAddress=0x2030; check(inspect().slots==Coordinator::Slots::unknown,"manager unreadable");
        } else if (which=="slots_owner") {
            e.ownerOkay=false; check(inspect().slots==Coordinator::Slots::unknown && e.reads.size()==3,"wrong manager owner stops before slots");
            e.ownerOkay=true;e.intelligenceOwnerOkay=false;
            check(inspect().slots==Coordinator::Slots::unknown,"wrong intelligence owner");
            e.intelligenceOwnerOkay=true;e.changeOwnerAfterRead=true;
            check(inspect().slots==Coordinator::Slots::unknown,"owner changed before snapshot publication");
        } else if (which=="slots_identity") {
            ++entity.session.generation; check(inspect().slots==Coordinator::Slots::unknown && e.reads.empty(),"old generation");
            entity=e.identity(); ++entity.session.player; check(inspect().slots==Coordinator::Slots::unknown && e.reads.empty(),"foreign player");
            entity=e.identity(); ++entity.reference; check(inspect().slots==Coordinator::Slots::unknown && e.reads.empty(),"wrong handle tag");
            entity=e.identity();entity.session.generation=0;check(inspect().slots==Coordinator::Slots::unknown && e.reads.empty(),"invalid generation");
            entity=e.identity(); e.ownership=false; check(inspect().slots==Coordinator::Slots::unknown && e.reads.empty(),"ownership lost");
            e.ownership=true;e.poolFlag=2;check(inspect().slots==Coordinator::Slots::unknown && e.reads.empty(),"recycled pool tag");
            e.poolFlag=1;e.registeredLink=false;check(inspect().slots==Coordinator::Slots::unknown && e.reads.empty(),"missing registered reference link");
        } else if (which=="slots_context") {
            Coordinator core;check(core.begin(entity.session,pose(),point(e.spawn),0),"closed session");
            core.offer(entity.session,pose(2,102),1);
            const auto calls=e.calls; e.contextAllowed=false;
            check(inspect().slots==Coordinator::Slots::unknown,"off-phase unknown");
            check(!native::next(core,e,entity,point(e.spawn),1),"off-phase bridge cannot emit");
            e.contextAllowed=true;
            std::thread worker([&] {
                check(inspect().slots==Coordinator::Slots::unknown,"worker unknown");
                check(!native::next(core,e,entity,point(e.spawn),1),"worker bridge cannot emit");
            });worker.join();
            check(e.calls==calls && e.reads.empty(),"no native reads or auth calls outside approved context");
            check(core.pendingCount()==1 && core.state()==Coordinator::State::ready,"unauthorized caller cannot consume pending");
        } else if (which=="slots_engine") {
            e.codeOkay=false; check(inspect().slots==Coordinator::Slots::unknown && e.reads.empty(),"code not authenticated");
            e.codeOkay=true;e.gameplayReady=false;check(inspect().slots==Coordinator::Slots::unknown && e.reads.empty(),"gameplay unavailable");
            e.gameplayReady=true;e.streamingReady=false;check(inspect().slots==Coordinator::Slots::unknown && e.reads.empty(),"streaming unavailable");
        } else if (which=="slots_changed") {
            e.invalidateAfterRead=true;check(inspect().slots==Coordinator::Slots::unknown,"identity changed before publishing snapshot");
        } else throw std::runtime_error("unknown slot case");
        return;
    }
    if (which=="feedback") {
        Coordinator c;check(c.begin({1,42},pose(),{1000,2000,30},0),"admit");
        check(c.offer({1,42},pose(2,102),2)==Coordinator::SampleResult::accepted,"pose");
        check(!c.next({1000,2000,30},Coordinator::Slots::empty,false,true,1) &&
            !c.next({1000,2000,30},Coordinator::Slots::empty,true,false,1) && c.pendingCount()==1,
            "no command emission or pending consumption outside the admitted native gameplay context");
        const auto command=c.next({1000,2000,30},Coordinator::Slots::empty,true,true,1);check(command.has_value(),"issue");
        check(!c.acknowledge(command->attempt+1,true,1),"old/foreign acknowledgment rejected");
        check(c.acknowledge(command->attempt,true,1),"assign");
        check(c.offer({1,42},pose(3,104),3)==Coordinator::SampleResult::accepted &&
            !c.next({1000,2000,30},Coordinator::Slots::empty,true,true,2) && c.pendingCount()==1,
            "even empty slots do not imply observed arrival of an assigned walk");
        c.observe(command->attempt,Coordinator::Progress::arrived,301);
        check(c.state()==Coordinator::State::failed && c.failure()==Coordinator::Failure::arrival_without_start,
            "arrival without an observed start is never sufficient");
        Coordinator cooldown;check(cooldown.begin({1,42},pose(),{1000,2000,30},0),"cooldown admit");
        (void)cooldown.offer({1,42},pose(2,102),2);
        const auto first=cooldown.next({1000,2000,30},Coordinator::Slots::empty,true,true,1);
        check(first && cooldown.acknowledge(first->attempt,true,1),"cooldown first task");
        cooldown.observe(first->attempt,Coordinator::Progress::moving,2);
        cooldown.observe(first->attempt,Coordinator::Progress::arrived,3);
        (void)cooldown.offer({1,42},pose(3,104),3);
        check(!cooldown.next({1002,2000,30},Coordinator::Slots::empty,true,true,3) && cooldown.pendingCount()==1,
            "minimum command interval also guards an excessively early external arrival report");
        check(cooldown.next({1002,2000,30},Coordinator::Slots::empty,true,true,301).has_value(),"bounded cooldown preserves latest pending destination");
        return;
    }
    Fixture f;
    if (which.starts_with("audit_")) {
        f.offer(2,102);f.offer(3,104);
        if (which=="audit_pre_arrival") {
            f.engine.releaseTask();
            for(unsigned i=0;i<20;++i)f.step();
            check(f.engine.tasks==1 && f.core.pendingCount()==1 && f.core.state()==Coordinator::State::assigned,
                "even repeated authenticated empty snapshots cannot supply start/arrival");
            return;
        }
        f.arrive();
        const auto entity=f.engine.identity();
        if (which=="audit_arrived_busy") {
            for(unsigned i=0;i<10;++i)f.step();
            check(f.engine.tasks==1 && f.core.state()==Coordinator::State::awaiting_release,"arrival with busy slots cannot issue again");
            f.release();check(f.engine.tasks==2 && f.engine.maxActive==1,"fresh release permits a second FAKE walk only after arrival");
        } else if (which=="audit_unknown") {
            f.engine.partialAddress=0x2010;f.step();
            check(f.core.state()==Coordinator::State::failed && f.core.failure()==Coordinator::Failure::task_state_unknown,
                "partial slot snapshot cannot reuse earlier successful authentication");
            f.engine.partialAddress=0;f.engine.releaseTask();f.step();
            check(f.engine.tasks==1 && !f.core.pendingCount(),"later empty cannot restart a failed generation");
        } else if (which=="audit_generation" || which=="audit_removed" || which=="audit_ownership") {
            const auto reads=f.engine.reads.size();
            if(which=="audit_generation")++f.engine.registrySession.generation;
            else if(which=="audit_removed"){f.engine.live=false;f.engine.reference=-1;}
            else f.engine.ownership=false;
            check(!native::next(f.core,f.engine,entity,point(f.engine.actual),f.now+1),"changed entity cannot emit a new command");
            check(f.core.poisoned() && f.engine.reads.size()==reads && f.engine.tasks==1,"identity loss stops before entity memory reads");
            check(!f.engine.removal.confirmRemoved(f.core,f.engine,entity),"no inferred cleanup from reset fields or empty slots");
        } else if (which=="audit_manager_changed") {
            f.engine.switchManagerDuringRead=true;
            const auto observation=native::observeSlots(f.engine,entity);
            check(observation.slots==Coordinator::Slots::unknown && observation.reason==native::Reason::changed,
                "manager changed inside probe: no mixed snapshot");
            f.engine.switchManagerDuringRead=true;
            check(!native::next(f.core,f.engine,entity,point(f.engine.actual),f.now+1) && f.core.poisoned(),
                "bridge treats changed manager during probe as terminal identity uncertainty");
        } else if (which=="audit_manager_invalid") {
            f.engine.managerAvailable=false;f.step();
            check(f.core.state()==Coordinator::State::failed && f.engine.tasks==1,"null intelligence cannot supply release");
        } else if (which=="audit_manager_scope") {
            f.engine.releaseTask();
            const auto before=native::observeSlots(f.engine,entity);
            f.engine.intelligenceBase+=0x1000; // FAKE replacement between complete probes, same valid owners.
            const auto after=native::observeSlots(f.engine,entity);
            check(before.slots==Coordinator::Slots::empty && after.slots==Coordinator::Slots::empty && after.reason==native::Reason::none,
                "characterization: current observer proves each snapshot, not manager continuity across callbacks");
            check(f.engine.tasks==1,"do not turn this limitation into second-assignment permission");
            std::puts("manager_continuity=not_exported fresh_snapshot=empty historical_snapshot_is_not_permission");
        } else if (which=="audit_stale_snapshot") {
            f.engine.releaseTask();
            const auto old=native::observeSlots(f.engine,entity);check(old.slots==Coordinator::Slots::empty,"old empty snapshot");
            f.engine.protectedSlots[2]=0xFFFFFFF0;
            check(!native::next(f.core,f.engine,entity,point(f.engine.actual),f.now+1),"fresh bridge sees reoccupied slot and holds pending");
            // Deliberate misuse of the value-only core demonstrates why its output
            // is an intention, not an authenticated native execution capability.
            const auto command=f.core.next(point(f.engine.actual),old.slots,true,true,f.now+2);
            check(command.has_value(),"bare enum contains no age/manager identity and cannot authenticate itself");
            const auto failure=f.engine.assignMove(entity.ped,position(command->destination));
            check(failure && !std::strcmp(failure,"primary_task_slot_busy_or_unreadable"),"assignment revalidation rejects stale empty");
            check(!f.core.acknowledge(command->attempt,false,f.now+2) && f.core.failure()==Coordinator::Failure::backend_refused && f.engine.tasks==1,
                "no second task after stale snapshot; no attempt to replace occupied slot");
        } else if (which=="audit_disconnect") {
            const auto old=f.key();const auto attempt=f.core.command().attempt;
            f.reconnect();
            check(f.engine.tasks==1 && f.engine.removes==1 && !f.core.pendingCount(),"leave clears pending before confirmed entity cleanup/readmission");
            const auto reads=f.engine.reads.size();
            check(!native::next(f.core,f.engine,entity,point(f.engine.actual),f.now+1) && f.engine.reads.size()==reads,
                "old generation snapshot cannot query or control reconnected PED");
            f.core.observe(attempt,Coordinator::Progress::arrived,f.now+1);
            check(f.core.offer(old,pose(9,106),99)==Coordinator::SampleResult::inactive && f.engine.tasks==1,
                "late arrival/pose cannot revive the previous generation");
        } else if (which=="audit_thread") {
            const auto calls=f.engine.calls;const auto reads=f.engine.reads.size();
            f.engine.contextAllowed=false;
            check(!native::next(f.core,f.engine,entity,point(f.engine.actual),f.now+1),"off-phase no release probe");
            f.engine.contextAllowed=true;
            std::thread worker([&]{check(!native::next(f.core,f.engine,entity,point(f.engine.actual),f.now+1),"wrong thread no release probe");});worker.join();
            check(f.engine.calls==calls && f.engine.reads.size()==reads && f.engine.tasks==1,"no native call outside authorized consumer");
        } else throw std::runtime_error("unknown audit case");
        return;
    }
    if (which=="release_early") {
        f.offer(2,102); f.offer(3,104);
        f.engine.releaseTask(); f.step();
        check(f.engine.tasks==1 && f.core.state()==Coordinator::State::assigned,"structural release alone is not arrival");
        for (unsigned i=0;i<4;++i) { f.engine.actual.x+=0.5f; f.step(); }
        f.step(300);
        check(f.engine.tasks==2 && f.engine.activeTasks==1,"observed arrival plus previously free slots allows second leg");
    } else if (which=="release_unknown") {
        f.offer(2,102); f.offer(3,104); f.arrive();
        f.engine.partialAddress=0x2010;f.step();
        check(f.core.failure()==Coordinator::Failure::task_state_unknown && f.engine.tasks==1 && f.engine.activeTasks==1,
            "unknown while awaiting release is terminal, without task destruction");
        Fixture active;active.offer(2,102);active.engine.partialAddress=0x2010;active.step();
        check(active.core.state()==Coordinator::State::failed && active.engine.activeTasks==1 && active.engine.tasks==1,
            "unknown during an active episode also fails closed without manipulating task");
    } else if (which=="snapshot_race") {
        f.offer(2,102);f.arrive();f.release();
        f.engine.occupyBeforeAssignment=true;f.offer(3,104);
        check(f.engine.tasks==1 && f.engine.protectedSlots[1] && f.engine.lastFailure=="primary_task_slot_busy_or_unreadable" &&
            f.core.failure()==Coordinator::Failure::backend_refused,"a free snapshot is not a reservation; assignMove revalidates");
    } else if (which=="leave_release") {
        f.offer(2,102);f.offer(3,104);f.arrive();
        f.sink.despawn(42);f.step();
        check(f.core.state()==Coordinator::State::inactive && !f.core.pendingCount() && f.engine.removes==1 && f.engine.tasks==1,
            "leave cancels pending wait and confirms native entity cleanup, not task cancellation");
    } else if (which.starts_with("cleanup_")) {
        f.offer(2,102);const auto old=f.key();const auto entity=f.engine.identity();
        f.core.cancel(old);
        check(!f.engine.removal.confirmRemoved(f.core,f.engine,entity),"no receipt before actual removal");
        if (which=="cleanup_pool_live") f.engine.leavePoolLive=true;
        else if (which=="cleanup_pool_changed") f.engine.changePoolAfterRemoval=true;
        else if (which=="cleanup_partial") f.engine.partialAfterRemoval=true;
        else if (which=="cleanup_no_return") f.engine.returnBeforeRemoval=true;
        else if (which!="cleanup_confirmed" && which!="cleanup_poisoned") throw std::runtime_error("unknown cleanup case");
        const bool removed=f.engine.remove(0x1000);
        if (which=="cleanup_poisoned") {
            check(removed && f.engine.removal.completed(),"complete receipt before identity failure");
            f.core.invalidateIdentity();const auto calls=f.engine.calls;
            check(!f.engine.removal.confirmRemoved(f.core,f.engine,entity) && f.engine.calls==calls,
                "poisoned coordinator never queries native cleanup or readmits");
        } else if (which=="cleanup_confirmed") {
            check(removed && f.engine.registryReleased() && f.engine.removal.completed(),"return plus old pool slot free");
            auto stale=entity;++stale.session.generation;
            check(!f.engine.removal.confirmRemoved(f.core,f.engine,stale),"receipt is generation-bound");
            check(f.engine.removal.confirmRemoved(f.core,f.engine,entity),"authenticated completion releases coordinator");
            check(!f.engine.removal.confirmRemoved(f.core,f.engine,entity),"receipt consumed once");
            check(f.engine.removal.admissionAllowed(f.core),"native admission opens only after confirmation");
            check(f.core.begin({old.generation+1,43},pose(1,100,43),{1000,2000,30},f.now),"new generation after cleanup");
        } else {
            check(!f.engine.removal.confirmRemoved(f.core,f.engine,entity),"incomplete cleanup never confirmed");
            check(!f.core.begin({old.generation+1,43},pose(1,100,43),{1000,2000,30},f.now),"readmission stays blocked");
            check(!f.engine.beginRemote({old.generation+1,43}),"native registry admission also stays blocked");
            if (removed) check(f.engine.registryReleased(),"empty registry alone remains insufficient");
        }
    } else if (which=="three_walks") {
        for (unsigned i=1;i<=3;++i) {
            f.offer(i+1,100.f+2.f*i);
            check(f.engine.tasks==i && f.engine.arrivals==i-1,"new task is not an arrival");
            f.arrive();f.release();
            check(f.engine.tasks==i && f.engine.arrivals==i && f.engine.maxActive==1,"one task/observed arrival per leg");
            std::printf("walk=%u target_x=%.1f tasks=%u observed_arrivals=%u max_simultaneous=%u probes=%u slot_reads=%zu\n",
                i,f.engine.destinations.back().x,f.engine.tasks,f.engine.arrivals,f.engine.maxActive,f.engine.sphereCalls,f.engine.reads.size());
            check(!f.engine.reads.empty(),"each release uses authenticated simulated memory, not an empty boolean");
        }
        check(f.core.state()==Coordinator::State::ready && !f.core.pendingCount() && f.engine.creates==1,
            "three walks share one entity and have no permanent one-shot latch");
        std::printf("coordinator_bytes=%zu pending_capacity=1 entities=%u state=ready\n",sizeof(Coordinator),f.engine.creates);
    } else if (which=="active_latest" || which=="coalescing" || which=="bounded") {
        f.offer(2,102);
        const unsigned updates=which=="active_latest"?2u:10000u;
        for (unsigned i=0;i<updates;++i) f.sink.move(pose(3+i,i+1==updates?106.f:104.f));
        f.step();
        check(f.engine.tasks==1 && f.core.pendingCount()==1,"only the latest destination waits while a task is active");
        f.arrive();check(f.engine.tasks==1,"arrival alone cannot replace occupied standing task");
        f.release();check(f.engine.tasks==2 && f.engine.target.x==1006 && f.engine.maxActive==1,"coalesced latest target, bounded 4m next leg");
        f.arrive();f.release();
    } else if (which=="deadband") {
        f.offer(2,101.99f);check(!f.engine.tasks,"below 2m never assigns");
        f.offer(3,102);f.arrive();f.release();
        for (unsigned i=0;i<100;++i) f.offer(4+i,102.1f);
        check(f.engine.tasks==1,"nearly unchanged destination never spams tasks");
    } else if (which=="sequence") {
        f.offer(2,102);f.sink.move(pose(2,102));f.sink.move(pose(1,104));f.sink.move(pose(5,104,99));f.step();
        check(!f.core.pendingCount() && f.engine.tasks==1,"duplicate, stale, foreign ID never queues another task");
        const auto revision=f.sink.snapshot().sampleRevision;
        check(f.core.offer(f.key(),pose(1,104),revision+1)==Coordinator::SampleResult::stale,"consumer rejects stale sequence even with newer revision");
        check(f.core.offer(f.key(),pose(3,104),revision)==Coordinator::SampleResult::stale,"consumer rejects changed data without a new sample revision");
        check(f.core.offer(f.key(),pose(99,102),revision)==Coordinator::SampleResult::stale,"higher sequence also requires an ordered new sample revision");
        f.sink.move(pose(2,102.5f));f.step();check(f.core.pendingCount()==1,"ordered Scene interpolation within the same sequence is coalesced");
    } else if (which=="generation" || which=="timeout_reconnect") {
        const auto oldKey=f.key();f.offer(2,102);const auto oldAttempt=f.core.command().attempt;
        f.offer(3,104);
        if (which=="timeout_reconnect") {
            f.step(20001);check(f.core.state()==Coordinator::State::failed && !f.core.pendingCount(),"timeout discards pending without touching task ownership");
            check(f.engine.activeTasks==1 && !f.engine.cleanupTasks,"timeout never deletes a GTA-owned task");
        }
        check(!f.core.begin({oldKey.generation+1,43},pose(1,100,43),{1000,2000,30},f.now),"packet generation cannot bypass live entity cleanup");
        f.reconnect();
        check(f.engine.creates==2 && f.engine.removes==1 && f.engine.cleanupTasks==1,"old entity lifecycle cleans active task before new admission");
        check(f.core.offer(oldKey,pose(9,106),100)==Coordinator::SampleResult::inactive,"old generation cannot control new entity");
        f.core.observe(oldAttempt,Coordinator::Progress::arrived,f.now);
        f.offer(2,102,43);check(f.engine.tasks==2 && f.core.command().attempt>oldAttempt,"new generation never reuses an attempt token or pending pose");
        f.core.cancel(oldKey);f.core.observe(oldAttempt,Coordinator::Progress::failed,f.now);
        check(f.core.state()==Coordinator::State::assigned,"stale leave/feedback cannot affect the new generation");
    } else if (which=="leave") {
        f.offer(2,102);f.offer(3,104);const auto token=f.core.command().attempt;
        f.sink.despawn(42);f.step();
        check(f.engine.removes==1 && f.engine.cleanupTasks==1 && !f.core.pendingCount() && f.core.state()==Coordinator::State::inactive,
            "leave drops pending and delegates active task destruction only to entity lifecycle");
        f.core.observe(token,Coordinator::Progress::arrived,f.now);for(int i=0;i<20;++i)f.step();
        check(f.engine.tasks==1 && f.engine.arrivals==0,"leave is not a failed walk or a late arrival; no work after cleanup");
    } else if (which=="ownership" || which=="identity") {
        f.offer(2,102);f.offer(3,104);
        if (which=="identity") { f.core.invalidateIdentity();f.lifecycle.disable(); }
        else { f.engine.ownership=false;f.step(); }
        const auto calls=f.engine.calls;
        f.sink.despawn(42);for (int i=0;i<20;++i)f.step();
        check(f.core.poisoned() && !f.core.pendingCount() && !f.engine.removes && f.engine.calls==calls,
            "lost identity is terminal, with no unknown cleanup or further engine reads");
    } else if (which=="collision" || which=="assignment" || which=="geometry") {
        f.offer(2,102);f.arrive();f.release();
        if (which=="collision")f.engine.collision=true;
        if (which=="assignment")f.engine.assignmentFails=true;
        if (which=="geometry")f.engine.groundOkay=false;
        const auto probes=f.engine.sphereCalls;f.offer(3,104);
        check(f.core.state()==Coordinator::State::failed && f.engine.tasks==1 && !f.core.pendingCount(),"each new leg must pass geometry and assignment again");
        if (which=="collision")check(f.engine.lastFailure=="corridor_sphere_hit" && f.engine.diagnostic.hitIdentity==rm::CollisionIdentity::cj_hit &&
            f.engine.diagnostic.radius==0.35f && f.engine.sphereCalls==probes+1,"FU6 identity and first-hit fail-closed probe are reused");
        for (unsigned i=0;i<100;++i)f.offer(i+4,104);
        check(f.engine.tasks==1 && f.engine.sphereCalls<=probes+4,"backend refusal is terminal for that generation, never a per-packet retry");
    } else if (which=="transfer_failure") {
        f.engine.transferFails=true;f.offer(2,102);
        check(f.core.state()==Coordinator::State::failed && f.engine.tasks==1 && f.engine.activeTasks==1,
            "post-transfer failure does not delete or release the engine-owned task");
        for (unsigned i=0;i<100;++i)f.offer(i+3,104);
        check(f.engine.tasks==1 && f.engine.activeTasks==1,"no replacement/retry after uncertain transfer result");
        f.sink.despawn(42);f.step();check(f.engine.cleanupTasks==1 && f.engine.removes==1,"only entity lifecycle cleans transferred task");
    } else if (which=="task_release") {
        f.offer(2,102);f.offer(3,104);f.arrive();
        for(int i=0;i<10;++i)f.step();check(f.engine.tasks==1,"standing task cannot be overwritten even after arrival");
        f.step(20001);check(f.core.state()==Coordinator::State::failed && f.core.failure()==Coordinator::Failure::slot_timeout &&
            f.engine.activeTasks==1 && !f.core.pendingCount(),"unreleased native task times out without delete or replacement");
    } else if (which=="thread") {
        const auto calls=f.engine.calls;
        std::thread worker([&] { f.sink.move(pose(2,102));f.tick();f.tick(false); });worker.join();
        f.tick(false);f.tick(true,false);check(f.engine.calls==calls && !f.engine.tasks,"worker publishes values only, and cannot query any backend");
        f.step();check(f.engine.tasks==1,"native consumer alone creates the task");
    } else if (which=="reentry") {
        f.engine.reenter=true;f.offer(2,102);check(f.engine.tasks==1,"reentry cannot emit a second command");
    } else if (which=="limits") {
        auto nonfinite=pose(2,102);nonfinite.x=std::numeric_limits<float>::infinity();
        check(f.core.offer(f.key(),nonfinite,2)==Coordinator::SampleResult::invalid,"nonfinite sample rejected");
        nonfinite.x=std::numeric_limits<float>::quiet_NaN();
        check(f.core.offer(f.key(),nonfinite,2)==Coordinator::SampleResult::invalid,"NaN sample rejected");
        f.offer(2,104.01f);check(!f.engine.tasks && f.core.failure()==Coordinator::Failure::invalid_target,"over 4m is refused, never clamped");
        Fixture height;auto high=pose(2,102);high.z+=1.1f;height.sink.move(high);height.step();
        check(!height.engine.tasks && height.core.failure()==Coordinator::Failure::invalid_target,"remote height limit preserved per session anchor");
        Fixture terrain;terrain.engine.groundHeight+=1.f;terrain.offer(2,102);
        check(!terrain.engine.tasks && !terrain.engine.sphereCalls && terrain.engine.lastFailure=="ground_step_or_height_limit",
            "existing terrain height/step rejection still precedes collision and assignment");
    } else if (which=="scene") {
        core::RemoteRoster roster;core::RemotePlayerScene scene;
        const auto start=core::RemoteRoster::Time{};
        check(roster.position({42,1,100,200,30},start)==core::RemoteRoster::PositionResult::added,"initial UDP-v1 pose");
        scene.synchronize(roster,start,f.sink);
        check(roster.position({42,2,102,200,30},start)==core::RemoteRoster::PositionResult::updated,"new UDP-v1 pose");
        check(roster.position({42,2,106,200,30},start)==core::RemoteRoster::PositionResult::unchanged,"duplicate UDP sequence cannot replace XYZ");
        scene.synchronize(roster,start,f.sink);f.step();check(!f.engine.tasks,"first interpolated sample does not force a task");
        scene.synchronize(roster,start+std::chrono::milliseconds(90),f.sink);f.step();check(!f.engine.tasks,"interpolated 1m stays inside deadband");
        scene.synchronize(roster,start+std::chrono::milliseconds(180),f.sink);f.step();
        check(f.engine.tasks==1 && f.engine.target.x==1002,"Roster -> unchanged Scene -> IntentSink -> coordinator -> existing observer/probes -> fake task");
    } else if (which=="unknown_slot" || which=="clock") {
        if (which=="unknown_slot") {
            f.engine.slotsUnknown=true;f.offer(2,102);check(f.core.failure()==Coordinator::Failure::task_state_unknown,"unreadable task state refuses");
            Fixture occupied;occupied.engine.externalTask=true;occupied.offer(2,102);
            check(!occupied.engine.tasks && occupied.core.pendingCount()==1,"occupied native task slots are never overwritten");
            occupied.engine.externalTask=false;occupied.step();check(occupied.engine.tasks==1,"only explicit empty slots allow assignment");
        }
        else { f.offer(2,102);f.now=0;f.tick();check(f.core.state()==Coordinator::State::failed,"backward observation time fails closed"); }
        check(f.engine.tasks<=1,"no additional task after unknown state/time");
    } else throw std::runtime_error("unknown case");
}

int main(int argc, char** argv) {
    try { check(argc==2,"case required");run(argv[1]);std::puts("continuous_move PASS");return 0; }
    catch(const std::exception& e) { std::fprintf(stderr,"continuous_move FAIL: %s\n",e.what());return 1; }
}
