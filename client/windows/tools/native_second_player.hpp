#pragma once

#include "grandmp/windows/remote_player_binding.hpp"
#include "grandmp/windows/remote_player_hoodlum.hpp"
#include "ped_smoke_addresses.hpp"
#include "ped_smoke_runtime_read.hpp"
#include "native_player_factory_contract.hpp"
#include <algorithm>
#include <cstring>

namespace grandmp::windows::remote_player_binding::second_player {
namespace ps=ped_smoke;

// SA PC Hoodlum only. Sources/instruction evidence: docs/native-second-player-slot.md.
// Slots 0 and 1 exist; there is deliberately NO index parameter in this API.
inline constexpr std::uint32_t infoStride=0x190, slot0=ps::player, slot1=slot0+infoStride;
inline constexpr std::uint32_t data1=slot1+4, pad0=0xB73458, padStride=0x134, pad1=pad0+padStride;
inline constexpr std::uint32_t setup=0x60D790, remove=0x6094A0, poolStride=0x7C4;
inline constexpr std::uint32_t playerData=0x480, pedType=0x598, vehicle=0x58C, movingLink=0xB4;
inline constexpr std::size_t poolProbeLimit=1024; // Bounded policy, NOT the engine's capacity.

enum class Reason { none, closed, context, readiness, occupied, read_failed, pool_invalid,
    pool_full, local_identity, construction_unproven, state, identity, reference,
    world_link, local_changed, factory_result_unknown, cleanup_unknown, placement_refused };

// Native-thread-only value snapshots. No PED/task/module address is retained.
enum class CreateFailure { none, preparation, protection_before, group_selection,
    old_group_read, spawn_refused, spawn_nonfinite, group_seed, slot_read, slot_empty,
    group_ownership, default_task, protection_after, protected_state_changed, execution_closed };
enum class CreateSlot { not_observed, unreadable, empty, present };
enum class CreateResultFailure { none, slot_read, slot_empty, incomplete_with_ped };
struct NativeCreateObservation {
    // started, group_seeded, setup_entered, setup_returned, complete
    enum : unsigned { started=1, group_seeded=2, setup_entered=4, setup_returned=8, complete=16 };
    unsigned flags=0,changedProtection=0;
    CreateFailure failure=CreateFailure::none;
    CreateSlot slot=CreateSlot::not_observed;
    ps::MemoryRead failedRead{};
    std::size_t requested=0;
    bool readFailure=false;
    void fail(CreateFailure value) noexcept { if(failure==CreateFailure::none)failure=value; }
};
struct ApiCreateObservation {
    // command_entered, command_returned, command_complete
    unsigned flags=0;
    CreateSlot slot=CreateSlot::not_observed;
    CreateResultFailure failure=CreateResultFailure::none;
    ps::MemoryRead slotRead{};
};
inline const char* createFailureName(CreateFailure value) noexcept {
    switch(value){
    case CreateFailure::preparation:return "factory.create.preparation";
    case CreateFailure::protection_before:return "factory.create.protection_before";
    case CreateFailure::group_selection:return "factory.create.group_selection";
    case CreateFailure::old_group_read:return "factory.create.old_group_read";
    case CreateFailure::spawn_refused:return "factory.create.spawn_refused";
    case CreateFailure::spawn_nonfinite:return "factory.create.spawn_nonfinite";
    case CreateFailure::group_seed:return "factory.create.group_seed";
    case CreateFailure::slot_read:return "factory.create.slot_read";
    case CreateFailure::slot_empty:return "factory.create.slot_empty";
    case CreateFailure::group_ownership:return "factory.create.group_ownership";
    case CreateFailure::default_task:return "factory.create.default_task_post_setup";
    case CreateFailure::protection_after:return "factory.create.protection_after";
    case CreateFailure::protected_state_changed:return "factory.create.protected_state_changed";
    case CreateFailure::execution_closed:return "factory.create.execution_closed";
    default:return "factory.create.no_failure_observed";
    }
}

// Existing host must lend its admitted pre-world context, readiness/code/PIN
// proof. This interface never makes a new hook or infers phase from a clock.
class Access {
public:
    virtual ~Access()=default;
    virtual bool context() const noexcept=0; // No GTA memory reads.
    virtual bool controlContext() const noexcept { return false; } // Read/authentication only.
    virtual bool ready() const noexcept=0;
    virtual bool executorEnabled() const noexcept=0;
    virtual bool constructionProven() const noexcept=0;
    virtual ps::MemoryRead read(std::uint32_t,void*,std::size_t) noexcept=0;
    // Setup publishes the candidate; finish runs ONLY after pool/reference ownership.
    // No SCM, no ClearSpaceForMissionEntity, no additional SetTask.
    virtual bool createPlayerOne()=0;
    virtual bool finishPlayerOne(const Record&)=0;
    virtual bool registerCell(std::uint32_t,std::uint32_t*)=0;
    virtual bool cleanCell(std::uint32_t,std::uint32_t*)=0;
    virtual bool finishRemoval(const Record&)=0; // Only after slot + old pool tag prove release.
    virtual bool removeOne()=0; // void __cdecl(int), argument ALWAYS 1.
};

struct Pool { std::uint32_t objects=0,flags=0; std::int32_t size=0,cursor=0; bool operator==(const Pool&) const=default; };
struct LocalIdentity {
    std::uint32_t ped=0,objects=0,flags=0;
    std::int32_t handle=0,size=0;
    bool operator==(const LocalIdentity&) const=default;
};
struct LocalImage {
    std::array<std::byte,infoStride> info{};
    std::array<std::byte,padStride*2> pads{};
    std::uint8_t focus=0;
    bool operator==(const LocalImage&) const=default;
};

// Concrete slot-1 lifecycle algorithm shared by the Windows Access and tests.
// A simulated Access is NOT evidence that constructionProven holds in Hoodlum.
enum class Construction { not_started, partial, complete, terminal_failure };

class Api final : public remote_player_binding::Api {
public:
    explicit Api(Access& access,std::thread::id owner) noexcept : access_(access),owner_(owner) {}
    Construction construction() const noexcept { return construction_; }
    Reason reason() const noexcept { return reason_; } // Native thread only, values only.
    ApiCreateObservation creationObservation() const noexcept { return creationObservation_; }
    // Read-only slot-0 capture. Reuse the exact pool/tag and readiness checks;
    // this does not register a reference or change the remote factory state.
    bool localIdentity(LocalIdentity& result,Phase phase=Phase::pre_world) const noexcept {
        result={};Pool p;std::uint32_t ped=0,vt=0,data=0,type=~0u;
        std::uint8_t focus=1;std::int32_t handle=0;
        // Original-control permission authenticates reads only. Factory/removal
        // still require allowed()/pre_world and cannot borrow this scope.
        const bool admitted=phase==Phase::pre_world ? allowed() :
            phase==Phase::original_process_control && access_.controlContext() && allowedIdentity();
        if (!admitted || !read(ps::playerIndex,focus) || focus || !pool(p) ||
            !read(slot0,ped) || !ped || !poolPed(ped,p,handle) ||
            !read(ped,vt) || vt!=ps::playerVtable ||
            !read(ped+playerData,data) || data!=slot0+4 ||
            !read(ped+pedType,type) || type!=0) return false;
        result={ped,p.objects,p.flags,handle,p.size};return true;
    }
    bool context(Phase phase) const noexcept override {
        return phase==Phase::pre_world && owner_==std::this_thread::get_id() && access_.context();
    }
    bool factoryProven() const noexcept override {
        if (!allowed()) return false;
        if (state_!=State::idle && state_!=State::retired) return refuse(Reason::state);
        // Establish vacancy/correlation before considering a native constructor.
        LocalImage image;Pool pool;
        if (!vacant(image,pool)) return false;
        if (!access_.constructionProven()) return refuse(Reason::construction_unproven);
        return true;
    }
    bool removalProven() const noexcept override { return allowed() && (state_==State::live || state_==State::partial_owned); }
    bool replayProven() const noexcept override { return false; }
    bool dispatch(const Record&,const Dispatch&) noexcept override { return false; }
    bool capture(ContextImage&) noexcept override { return false; }
    bool apply(const Record&,const Controls&) override { return false; }
    bool original(const Record&) override { return false; }
    bool restore(const ContextImage&) noexcept override { return false; }

    Creation create(core::RemotePlayerKey key,Record& record) override {
        if (!key.player || !key.generation || key.generation<=generationFloor_ || record.key!=key ||
            record.reference || record.entity.ped || !factoryProven()) return Creation::absent;
        LocalImage before;Pool pool;
        if (!vacant(before,pool)) return Creation::absent; // Snapshot is not a reservation.
        state_=State::indeterminate;construction_=Construction::partial;generationFloor_=key.generation;tracked_=&record;
        struct Finalize {
            Construction& state;
            ~Finalize() { if (state!=Construction::complete) state=Construction::terminal_failure; }
        } finalize{construction_};
        local_=before;pool_=pool;removeReturned_=false;
        std::uint32_t occupant=0;
        if (!allowed()) return Creation::indeterminate;
        if (!access_.constructionProven()) { refuse(Reason::construction_unproven);return Creation::indeterminate; }
        if (!localUnchanged() || !read(slot1,occupant)) return Creation::indeterminate;
        if (occupant) { refuse(Reason::occupied);return Creation::indeterminate; }
        // From here, failure remains terminal, including exceptions. No retries.
        const bool observing=creationObservation_.flags==0;
        if(observing)creationObservation_.flags=1;
        const bool commandComplete=access_.createPlayerOne();
        if(observing)creationObservation_.flags|=2u|(commandComplete ? 4u : 0u);
        std::uint32_t ped=0;
        ps::MemoryRead resultRead{};
        const bool slotRead=read(slot1,ped,&resultRead);
        if(observing){creationObservation_.slotRead=resultRead;creationObservation_.slot=!slotRead ? CreateSlot::unreadable : !ped ? CreateSlot::empty : CreateSlot::present;}
        if (!slotRead || !ped) {
            if(observing)creationObservation_.failure=!slotRead ? CreateResultFailure::slot_read : CreateResultFailure::slot_empty;
            refuse(Reason::factory_result_unknown);return Creation::indeterminate;
        }
        record.entity.ped=ped;record.reference=ped; // Retain result BEFORE any further probe/call.
        record.entity.slot=1;record.entity.playerData=data1;record.entity.pad=pad1;
        // Setup can publish a PED before later stages fail. Retaining
        // that value is NOT ownership proof and must not trigger cleanup.
        if (!commandComplete) {
            if(observing)creationObservation_.failure=CreateResultFailure::incomplete_with_ped;
            refuse(Reason::factory_result_unknown);return Creation::indeterminate;
        }
        if (!localUnchanged() || !correlate(record,false,true)) return Creation::indeterminate;
        // No native GetPed/GetPedRef call: bounded pool arithmetic authenticates the tag.
        if (!access_.registerCell(ped,&record.reference) || record.reference!=ped ||
            !referenceState(record,true) || !correlate(record,true,true) || !localUnchanged())
            return Creation::indeterminate;
        identity_=record.entity;key_=key;state_=State::partial_owned;
        construction_=Construction::terminal_failure;
        if (!access_.finishPlayerOne(record) || !localUnchanged() || !correlate(record,true,true)) {
            refuse(Reason::placement_refused);return Creation::indeterminate;
        }
        state_=State::live;construction_=Construction::complete;reason_=Reason::none;
        return Creation::complete;
    }
    bool authenticate(const Record& record) noexcept override {
        const bool admitted=allowedIdentity();
        return admitted && (state_==State::live || state_==State::partial_owned) && tracked_==&record && record.key==key_ &&
            record.entity==identity_ && record.reference==identity_.ped &&
            correlate(record,true,false) && referenceState(record,true);
    }
    Removal remove(Record& record) override {
        if (!allowed() || !authenticate(record)) { state_=State::indeterminate;return Removal::indeterminate; }
        LocalImage before;
        if (!localImage(before)) { state_=State::indeterminate;return Removal::indeterminate; }
        local_=before;state_=State::indeterminate; // Before first mutation; no second cleanup.
        const auto victim=record.entity.ped;
        if (!access_.cleanCell(victim,&record.reference) || !referenceState(record,false)) {
            refuse(Reason::cleanup_unknown);return Removal::indeterminate;
        }
        // Reauthenticate slot/pool after reference cleanup; no entity dereference
        // is allowed following the destruction call below.
        std::uint32_t occupant=0;
        if (record.reference!=victim || !correlate(record,true,false) || !localUnchanged() ||
            !read(slot1,occupant) || occupant!=victim || !access_.removeOne()) {
            refuse(Reason::cleanup_unknown);return Removal::indeterminate;
        }
        removeReturned_=true;
        if (!released(record) || !access_.finishRemoval(record) || !released(record)) { refuse(Reason::cleanup_unknown);return Removal::indeterminate; }
        record.reference=0;state_=State::retired;reason_=Reason::none;
        return Removal::complete;
    }
    bool removed(const Record& before,const Record& after) noexcept override {
        return allowed() && state_==State::retired && removeReturned_ && &after==tracked_ &&
            before.key==key_ && before.entity==identity_ && before.reference==identity_.ped &&
            after.key==before.key && after.entity==before.entity && !after.reference && released(before);
    }
private:
    enum class State { idle,indeterminate,partial_owned,live,retired };
    bool refuse(Reason reason) const noexcept { reason_=reason;return false; }
    bool allowed() const noexcept {
        if (!context(Phase::pre_world)) return refuse(Reason::context);
        if (!access_.executorEnabled()) return refuse(Reason::closed);
        if (!access_.ready()) return refuse(Reason::readiness);
        return true;
    }
    bool allowedIdentity() const noexcept {
        if (owner_!=std::this_thread::get_id() || (!access_.context() && !access_.controlContext()))
            return refuse(Reason::context);
        if (!access_.executorEnabled()) return refuse(Reason::closed);
        return access_.ready() || refuse(Reason::readiness);
    }
    template<class T> bool read(std::uint32_t at,T& value,ps::MemoryRead* observed=nullptr) const noexcept {
        const auto result=access_.read(at,&value,sizeof(value));
        if(observed)*observed=result;
        return result.complete(sizeof(value)) || refuse(Reason::read_failed);
    }
    bool localImage(LocalImage& image) const noexcept {
        return read(slot0,image.info) && read(pad0,image.pads) && read(ps::playerIndex,image.focus);
    }
    bool localUnchanged() const noexcept {
        LocalImage now;return (localImage(now) && now==local_) || refuse(Reason::local_changed);
    }
    bool pool(Pool& value) const noexcept {
        std::uint32_t pointer=0;
        if (!read(ps::pedPool,pointer) || !pointer || !read(pointer,value) || !value.objects || !value.flags ||
            value.size<=0 || value.size>static_cast<int>(poolProbeLimit) || value.cursor< -1 || value.cursor>=value.size ||
            value.objects>UINT32_MAX-static_cast<unsigned>(value.size)*poolStride ||
            value.flags>UINT32_MAX-static_cast<unsigned>(value.size)) return refuse(Reason::pool_invalid);
        return true;
    }
    bool poolPed(std::uint32_t ped,const Pool& p,std::int32_t& handle) const noexcept {
        if (ped<p.objects || (ped-p.objects)%poolStride) return refuse(Reason::identity);
        const auto index=(ped-p.objects)/poolStride;
        if (index>=static_cast<unsigned>(p.size)) return refuse(Reason::identity);
        std::uint8_t tag=0;
        if (!read(p.flags+index,tag) || (tag&0x80)) return refuse(Reason::identity);
        handle=static_cast<std::int32_t>((index<<8)|tag);return true;
    }
    bool vacant(LocalImage& image,Pool& p) const noexcept {
        std::uint32_t other=0,cj=0,data=0,vt=0,type=~0u;
        if (!read(slot1,other)) return false;
        if (other) return refuse(Reason::occupied); // No dereference of an unknown occupant.
        std::uint8_t model=0;
        if (!read(ps::streamingTable+0x10,model) || model!=1) return refuse(Reason::readiness);
        if (!localImage(image) || !read(slot0,cj) || !cj || !pool(p)) return false;
        std::int32_t handle=-1;
        if (!poolPed(cj,p,handle) || !read(cj,vt) || vt!=ps::playerVtable ||
            !read(cj+playerData,data) || data!=slot0+4 || !read(cj+pedType,type) || type!=0 ||
            image.focus!=0) return refuse(Reason::local_identity);
        std::array<std::uint8_t,poolProbeLimit> tags{};
        if (!access_.read(p.flags,tags.data(),p.size).complete(p.size)) return refuse(Reason::read_failed);
        if (std::none_of(tags.begin(),tags.begin()+p.size,[](auto tag){return (tag&0x80)!=0;})) return refuse(Reason::pool_full);
        return true;
    }
    bool correlate(Record& record,bool handleKnown,bool requireWorld) const noexcept {
        Pool current;std::uint32_t occupant=0,cj=0,vt=0,data=0,type=0,car=0;
        if (!pool(current) || current.objects!=pool_.objects || current.flags!=pool_.flags || current.size!=pool_.size ||
            !read(slot1,occupant) || !read(slot0,cj) || !cj || occupant!=record.entity.ped || occupant==cj ||
            record.entity.slot!=1 || record.entity.playerData!=data1 || record.entity.pad!=pad1)
            return refuse(Reason::identity);
        std::int32_t handle=-1;
        if (!poolPed(occupant,current,handle) || (handleKnown && handle!=record.entity.handle) ||
            !read(occupant,vt) || vt!=ps::playerVtable || !read(occupant+playerData,data) || data!=data1 ||
            !read(occupant+pedType,type) || type!=1 || !read(occupant+vehicle,car) || car)
            return refuse(Reason::identity);
        record.entity.handle=handle;
        if (requireWorld) {
            std::uint32_t node=0,entity=0;
            if (!read(occupant+movingLink,node) || !node || !read(node,entity) || entity!=occupant)
                return refuse(Reason::world_link);
        }
        return true;
    }
    bool correlate(const Record& record,bool known,bool world) const noexcept {
        auto copy=record;return correlate(copy,known,world);
    }
    bool referenceState(const Record& record,bool expected) const noexcept {
        std::uint32_t node=0;
        if (!read(record.entity.ped+0x24,node)) return false;
        bool found=false;
        for (unsigned n=0;node && n<128;++n) {
            std::uint32_t next=0,destination=0;
            if (!read(node,next) || !read(node+4,destination)) return false;
            if (destination==reinterpret_cast<std::uintptr_t>(&record.reference)) found=true;
            node=next;
        }
        return (!node && found==expected) || refuse(Reason::reference);
    }
    bool released(const Record& record) const noexcept {
        // NO read of ped, reference nodes or moving link after destruction.
        Pool current;std::uint32_t occupant=0;std::uint8_t tag=0;
        const auto index=static_cast<std::uint32_t>(record.entity.handle)>>8;
        return localUnchanged() && read(slot1,occupant) && !occupant && pool(current) &&
            current.objects==pool_.objects && current.flags==pool_.flags && current.size==pool_.size &&
            index<static_cast<unsigned>(current.size) && read(current.flags+index,tag) &&
            tag==static_cast<std::uint8_t>((record.entity.handle&0x7f)|0x80);
    }
    Access& access_;
    std::thread::id owner_;
    Record* tracked_=nullptr; // Never transported to workers/logs.
    core::RemotePlayerKey key_{};
    Identity identity_{};
    Pool pool_{};
    LocalImage local_{};
    std::uint64_t generationFloor_=0;
    State state_=State::idle;
    Construction construction_=Construction::not_started;
    bool removeReturned_=false;
    mutable Reason reason_=Reason::none;
    ApiCreateObservation creationObservation_{};
};

// Uses the existing strict admission primitives supplied by the native host;
// no duplicate implementation of phase, executable identity or module pinning.
struct Admission {
    void* context=nullptr;
    bool (*preWorld)(void*) noexcept=nullptr;
    bool (*readyCodeAndResidence)(void*) noexcept=nullptr;
    bool (*spawnGround)(void*,ps::Position&) noexcept=nullptr;
    bool (*originalControl)(void*) noexcept=nullptr; // Never admits factory/removal writes.
};
class WindowsAccess final : public Access {
public:
    explicit WindowsAccess(Admission admission) noexcept : admission_(admission) {}
    bool context() const noexcept override;
    bool controlContext() const noexcept override;
    bool ready() const noexcept override;
    bool executorEnabled() const noexcept override;
    bool constructionProven() const noexcept override;
    ps::MemoryRead read(std::uint32_t,void*,std::size_t) noexcept override;
    bool createPlayerOne() override;
    bool finishPlayerOne(const Record&) override;
    bool registerCell(std::uint32_t,std::uint32_t*) override;
    bool cleanCell(std::uint32_t,std::uint32_t*) override;
    bool removeOne() override;
    bool finishRemoval(const Record&) override;
    bool onFootDefaultOwned(const Record&) const noexcept;
    NativeCreateObservation creationObservation() const noexcept { return creationObservation_; }
private:
    ps::MemoryRead observeRead(ps::MemoryRead result,std::size_t requested) noexcept {
        if(observingCreation_ && !result.complete(requested) && !creationObservation_.readFailure){
            creationObservation_.readFailure=true;creationObservation_.failedRead=result;creationObservation_.requested=requested;
        }
        return result;
    }
    bool creationRefused(CreateFailure value) noexcept {
        if(observingCreation_)creationObservation_.fail(value);
        return false;
    }
    template<class T> bool get(std::uint32_t at,T& value) const noexcept {
        return const_cast<WindowsAccess*>(this)->read(at,&value,sizeof(value)).complete(sizeof(value));
    }
    bool groupVacant(unsigned&) const noexcept;
    bool taskCapacity() const noexcept;
    bool ownGroup(std::uint32_t ped) const noexcept;
    bool defaultTask(std::uint32_t ped) const noexcept;
    bool putGroup(unsigned value) noexcept;
    Admission admission_;
    ps::Position ground_{};
    unsigned group_=8,oldGroup_=0;
    bool attempted_=false,seeded_=false,finished_=false;
    std::uint32_t ownPed_=0;
    NativeCreateObservation creationObservation_{};
    bool observingCreation_=false;
};
} // namespace grandmp::windows::remote_player_binding::second_player
