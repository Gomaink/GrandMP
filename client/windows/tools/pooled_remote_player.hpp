#pragma once
#include "native_second_player.hpp"
#include "native_player_replay.hpp"
#include <atomic>
#include <type_traits>
#if defined(GRANDMP_REMOTE_TAIL_DIAGNOSTIC_BUILD) && GRANDMP_REMOTE_TAIL_DIAGNOSTIC_BUILD == 1
#include "remote_tail_diagnostic.hpp"
#if !defined(GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD) || GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD != 1
#error Remote tail diagnostic requires the independent control proof
#endif
#endif
#if defined(GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD) && GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD == 1
#if !defined(GRANDMP_REMOTE_TAIL_DIAGNOSTIC_BUILD) || GRANDMP_REMOTE_TAIL_DIAGNOSTIC_BUILD != 1
#error Semantic pad context requires the isolated D1 capture boundaries
#endif
#include "semantic_pad_context.hpp"
#endif

namespace grandmp::windows::remote_player_binding::pooled_player {
namespace layout=second_player;
namespace ps=ped_smoke;
using Reason=layout::Reason;
using LocalIdentity=layout::LocalIdentity;
using Pool=layout::Pool;
using Admission=layout::Admission;
inline constexpr auto pad0=layout::pad0,pad1=layout::pad1,padStride=layout::padStride;
static_assert(pad1==replay::HoodlumPadContract::pad && sizeof(OnFootPadStates)==replay::HoodlumPadContract::extent);
inline constexpr auto slot0=layout::slot0,playerData=layout::playerData,pedType=layout::pedType,vehicle=layout::vehicle;

#if defined(GRANDMP_POOLED_REMOTE_PLAYER_BUILD) && GRANDMP_POOLED_REMOTE_PLAYER_BUILD == 1
inline constexpr bool executionEnabled=true;
#else
inline constexpr bool executionEnabled=false;
#endif

// Original instructions and provenance: docs/remote-player-foundation.md.
inline constexpr std::uint32_t allocatePed=0x5E4720,baseConstructor=0x5E8030,
    dataConstructor=0x56F810,allocateData=0x571880,dataDestructor=0x571840,
    setModel=0x5E4880,initialState=0x60CD20,orientation=0x439A80,
    worldAdd=0x563220,worldRemove=0x563280,baseControl=0x5E8CD0,deletingDestructor=0x60A9A0;
inline constexpr std::array codeRegions{
    ps::CodeRegion{allocatePed,0xC0},ps::CodeRegion{baseConstructor,0x9F0},
    ps::CodeRegion{dataConstructor,0x34},ps::CodeRegion{allocateData,0xA0},
    ps::CodeRegion{dataDestructor,0x10},ps::CodeRegion{0x56F890,0x50},
    ps::CodeRegion{0x56F210,0x280},ps::CodeRegion{setModel,0x180},
    ps::CodeRegion{initialState,0x2D7},ps::CodeRegion{0x6093B0,0xD9},
    ps::CodeRegion{deletingDestructor,0x30},ps::CodeRegion{baseControl,0xC60},
    ps::CodeRegion{orientation,0xB0},ps::CodeRegion{worldAdd,0x60},ps::CodeRegion{worldRemove,0x70},
    ps::CodeRegion{0x5FB800,0x80},ps::CodeRegion{0x5FBB70,0x40},
    ps::CodeRegion{0x5FB9C0,0x80},ps::CodeRegion{0x5FC7E0,0x320},
    ps::CodeRegion{0x4E68D0,0x80},ps::CodeRegion{0x559AF0,0x100},ps::CodeRegion{0x4D3990,0x80},
    ps::CodeRegion{0x5E52E0,0x40},
    ps::CodeRegion{layout::factory::setPosition,0x20},ps::CodeRegion{layout::factory::baseHeight,0x40},
    ps::CodeRegion{0x1566E30,0x60},ps::CodeRegion{0x446F90,5},ps::CodeRegion{0x1569BF0,0x40},
    ps::CodeRegion{0x532B00,0x40},ps::CodeRegion{0x7F0910,0x90},
    ps::CodeRegion{ps::registerReference,0x90},ps::CodeRegion{ps::cleanReference,0x90},
    ps::CodeRegion{ps::testSphere,0x180},ps::CodeRegion{ps::findGround,0x100}
};

// Changes one owned object's data pointer BEFORE World::Add. Never a global
// vtable or instruction patch. Caller must have authenticated the allocated PED.
inline bool publishPrivateTable(volatile LONG* cell,LONG expected,LONG desired,bool admitted) noexcept {
    if(!admitted || !cell || (reinterpret_cast<std::uintptr_t>(cell)&3) || !expected || !desired || expected==desired)return false;
    return InterlockedCompareExchange(cell,desired,expected)==expected;
}
inline bool validPoseStep(ps::Position from,ps::Position to) noexcept {
    return ps::finite(from) && ps::finite(to) && std::hypot(to.x-from.x,to.y-from.y)<=4.f && std::abs(to.z-from.z)<=.75f;
}
inline bool exclusiveGroupMember(const layout::factory::GroupImage& image,std::uint32_t base,std::uint32_t ped) noexcept {
    // Membership receiver is group+8; SetLeader uses receiver+20, member 7.
    // Hoodlum 5FBA02 and 5FBA12, reached by 60D6A9.
    return layout::factory::membersOwned(image,base,ped) && layout::factory::word(image,0x28)==ped;
}

// Value-only, first refusal and restoration kept independently. No native
// address, ContextImage, complete task/pad image or pointer can reach the log worker.
enum class ProcessingGuard : unsigned { none, replay_context, replay_ready, replay_live, replay_destroyed, capture_contract, capture_pad0, capture_pad1,
    capture_focus_read, capture_focus, capture_heading_read, capture_heading, capture_camera_read, capture_camera,
    apply_contract, apply_identity, apply_task, weapon_slot_read, weapon_slot, weapon_type_read, weapon_type,
    target_read, target, disabled_read, disabled, camera_read, camera, pad_mode_read, pad_mode,
    flags_read, flags, player_flags_read, player_flags, intelligence_read, intelligence,
    slots_read, slots_busy, view_read, walk_only, previous_basis, current_basis, pad_encode, pad_write,
    original_contract, original_duplicate, original_identity, restore_context, restore_size,
    restore_write, restore_compare, capture_pad1_tail, count };
inline const char* processingGuardName(ProcessingGuard guard) noexcept {
    constexpr const char* names[]{"none","replay_context","replay_ready","replay_live","replay_destroyed","capture_contract","capture_pad0","capture_pad1",
        "capture_focus_read","capture_focus","capture_heading_read","capture_heading","capture_camera_read","capture_camera",
        "apply_contract","apply_identity","apply_task","weapon_slot_read","weapon_slot","weapon_type_read","weapon_type",
        "target_read","target","disabled_read","disabled","camera_read","camera","pad_mode_read","pad_mode",
        "flags_read","flags","player_flags_read","player_flags","intelligence_read","intelligence",
        "slots_read","slots_busy","view_read","walk_only","previous_basis","current_basis","pad_encode","pad_write",
        "original_contract","original_duplicate","original_identity","restore_context","restore_size","restore_write","restore_compare","capture_pad1_tail"};
    static_assert(std::size(names)==static_cast<unsigned>(ProcessingGuard::count));
    const auto index=static_cast<unsigned>(guard);return index<std::size(names) ? names[index] : "invalid_guard";
}
struct ProcessingRefusal { ProcessingGuard guard=ProcessingGuard::none;DWORD error=0;std::uint32_t observed=0,expected=0; };
struct NativeProcessingObservation {
    ProcessingRefusal primary{},restoration{};
    replay::PadWriteObservation padApply{},padRestore{};
#if defined(GRANDMP_REMOTE_TAIL_DIAGNOSTIC_BUILD) && GRANDMP_REMOTE_TAIL_DIAGNOSTIC_BUILD == 1
    remote_tail_diagnostic::Observation tail{}; // Bounded values only, never the native InputImage.
#endif
#if defined(GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD) && GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD == 1
    semantic_pad_context::Decision semantic=semantic_pad_context::Decision::not_observed;
#endif
};
struct ControlProofActor {
    ps::Position position{};
    std::array<float,2> localShift{},worldShift{};
    float heading=0,health=0;
    std::array<std::int32_t,2> states{}; // PED state, move state; not animation clip IDs.
    bool operator==(const ControlProofActor& other) const noexcept {
        return position.x==other.position.x && position.y==other.position.y && position.z==other.position.z &&
            localShift==other.localShift && worldShift==other.worldShift && heading==other.heading && health==other.health && states==other.states;
    }
};
struct ControlProofSnapshot { ControlProofActor remote{},local{}; };
enum class WrapperRefusal : unsigned { none, thread, pre_world_overlap, reentrant, stopped,
    binding_absent, api_absent, entity_mismatch, serial_exhausted, caller };
struct ProcessingFailure {
    core::RemotePlayerKey key{};
    std::uint64_t serial=0;
    ProcessObservation trace{};
    NativeProcessingObservation native{};
    Result result=Result::indeterminate;
    State state=State::empty;
    InputDisposition input=InputDisposition::neutral_missing;
    WrapperRefusal wrapper=WrapperRefusal::none;
    bool returned=false;
};
static_assert(std::is_trivially_copyable_v<ProcessingFailure>);
class FirstProcessingFailure {
public:
    bool publish(const ProcessingFailure& value) noexcept {
        unsigned empty=0;
        if(!state_.compare_exchange_strong(empty,1,std::memory_order_acq_rel))return false;
        value_=value;state_.store(2,std::memory_order_release);return true;
    }
    bool take(ProcessingFailure& value) noexcept {
        unsigned ready=2;
        if(!state_.compare_exchange_strong(ready,3,std::memory_order_acquire))return false;
        value=value_;return true; // Immutable, resident; never rearmed after consumption.
    }
private:
    ProcessingFailure value_{};
    std::atomic<unsigned> state_{0};
};

enum class FactoryStage : unsigned { none, proof_context, proof_ready, proof_attempted, proof_control,
    proof_spawn_provider, proof_staged, local_identity, slot1_vacancy, ped_pool, model, time_scale,
    local_group, local_group_active, ped_slot, ped_available, task_pool, task_header, task_objects,
    task_flags, task_capacity, task_bounds, task_slot, task_available, group_flag, group_image,
    group_resources, group_available, create_player, create_generation, create_record_key,
    create_staged_key, create_record_reference, create_record_entity, spawn_context, spawn_ready,
    spawn_staged, spawn_local, spawn_matrix, spawn_position, spawn_delta, spawn_ground, spawn_height,
    spawn_sphere, create_spawn, create_ground, create_local, create_pool, create_info, create_pads,
    create_focus, create_slot1, create_camera, create_group, create_group_image, create_idles, create_animation, count };
enum class FactoryCause : unsigned { none, precondition, read_failed, missing, invalid, occupied,
    exhausted, non_finite, collision, changed, count };
inline const char* factoryStageName(FactoryStage stage) noexcept {
    constexpr const char* names[]{"none","proof_context","proof_ready","proof_attempted","proof_control",
        "proof_spawn_provider","proof_staged","local_identity","slot1_vacancy","ped_pool","model","time_scale",
        "local_group","local_group_active","ped_slot","ped_available","task_pool","task_header","task_objects",
        "task_flags","task_capacity","task_bounds","task_slot","task_available","group_flag","group_image",
        "group_resources","group_available","create_player","create_generation","create_record_key",
        "create_staged_key","create_record_reference","create_record_entity","spawn_context","spawn_ready",
        "spawn_staged","spawn_local","spawn_matrix","spawn_position","spawn_delta","spawn_ground","spawn_height",
        "spawn_sphere","create_spawn","create_ground","create_local","create_pool","create_info","create_pads",
        "create_focus","create_slot1","create_camera","create_group","create_group_image","create_idles","create_animation"};
    static_assert(std::size(names)==static_cast<unsigned>(FactoryStage::count));
    const auto index=static_cast<unsigned>(stage);return index<std::size(names) ? names[index] : "invalid_stage";
}
inline const char* factoryCauseName(FactoryCause cause) noexcept {
    constexpr const char* names[]{"none","precondition","read_failed","missing","invalid","occupied","exhausted","non_finite","collision","changed"};
    static_assert(std::size(names)==static_cast<unsigned>(FactoryCause::count));
    const auto index=static_cast<unsigned>(cause);return index<std::size(names) ? names[index] : "invalid_cause";
}
struct FactoryAdmissionObservation {
    enum : unsigned { proof_entered=1, create_entered=2, spawn_entered=4, allocation_boundary=8, allocated=16 };
    FactoryStage stage=FactoryStage::none;
    FactoryCause cause=FactoryCause::none;
    DWORD error=0;
    std::uint32_t observed=0,expected=0,index=UINT32_MAX,proofCalls=0,flags=0;
    core::RemotePlayerKey key{};
    std::uint32_t sequence=0;
    Reason reason=Reason::none; // Legacy Reason, observed only; never repaired by diagnostics.
    bool check(bool okay,FactoryStage at,FactoryCause why,std::uint32_t value=0,std::uint32_t wanted=1,
               std::uint32_t item=UINT32_MAX,DWORD win32=0) noexcept {
        if(!okay && stage==FactoryStage::none){stage=at;cause=why;observed=value;expected=wanted;index=item;error=win32;}
        return okay;
    }
};
struct FactoryAdmissionFailure {
    FactoryAdmissionObservation observation{};
    Result result=Result::indeterminate;
    State before=State::empty,after=State::empty; // Binding values, not native memory probes.
};
static_assert(std::is_trivially_copyable_v<FactoryAdmissionFailure>);
class FirstFactoryAdmissionFailure {
public:
    bool publish(const FactoryAdmissionFailure& value) noexcept {
        unsigned empty=0;
        if(!state_.compare_exchange_strong(empty,1,std::memory_order_acq_rel))return false;
        value_=value;state_.store(2,std::memory_order_release);return true;
    }
    bool take(FactoryAdmissionFailure& value) noexcept {
        unsigned ready=2;
        if(!state_.compare_exchange_strong(ready,3,std::memory_order_acquire))return false;
        value=value_;return true; // No rearm, retries, pointer payload or lifetime change.
    }
private:
    FactoryAdmissionFailure value_{};
    std::atomic<unsigned> state_{0};
};

class Api final : public remote_player_binding::Api {
public:
    explicit Api(Admission admission,std::thread::id owner,std::uint32_t control) noexcept
        : admission_(admission),owner_(owner),control_(control) {}
    Api(const Api&)=delete;
    Api& operator=(const Api&)=delete;
    Reason reason() const noexcept {return reason_;}
    const char* failure() const noexcept {return failure_;}
    void beginProcessingObservation() noexcept {processing_={};observing_=true;restoring_=false;}
    NativeProcessingObservation endProcessingObservation() noexcept {observing_=false;return processing_;}
    FactoryAdmissionObservation factoryObservation() const noexcept {
        auto copy=admissionObservation_;copy.key=stagedFrame_.key;copy.sequence=stagedFrame_.sequence;copy.reason=reason_;
        if(attempted_)copy.flags|=FactoryAdmissionObservation::allocation_boundary;
        if(ped_)copy.flags|=FactoryAdmissionObservation::allocated;
        return copy; // Native-thread copy, never a new engine probe.
    }
    bool context(Phase) const noexcept override;
    bool factoryProven() const noexcept override;
    bool removalProven() const noexcept override;
    bool replayProven() const noexcept override;
    Creation create(core::RemotePlayerKey,Record&) override;
    bool authenticate(const Record&) noexcept override;
    Removal remove(Record&) override;
    bool removed(const Record&,const Record&) noexcept override;
    bool dispatch(const Record&,const Dispatch&) noexcept override;
    bool capture(ContextImage&) noexcept override;
    bool apply(const Record&,const Controls&) override;
    bool original(const Record&) override;
    bool restore(const ContextImage&) noexcept override;
    bool localIdentity(LocalIdentity&,Phase=Phase::pre_world) const noexcept;
    bool stage(const core::RemotePlayerFrame& frame) noexcept {
        staged_=context(Phase::pre_world) && frame.key.player && frame.key.generation && frame.sequence &&
            frame.freshness==core::ControlFreshness::fresh && frame.presentation.id==frame.key.player && core::validOnFoot(frame.reference) &&
            ps::finite({frame.presentation.x,frame.presentation.y,frame.presentation.z});
        if(staged_)stagedFrame_=frame;
        return staged_;
    }
    bool spawnGround(ps::Position&) const noexcept;
    void clearStage() noexcept {staged_=false;}
    bool present(const core::RemotePlayerFrame&) noexcept;
    bool privateDispatchReady() noexcept {return tracked_ && authenticate(*tracked_);}
#if defined(GRANDMP_AUTHENTICATED_PAD_COW_BUILD) && GRANDMP_AUTHENTICATED_PAD_COW_BUILD == 1
    bool admitPadImage(std::span<const std::byte> file) noexcept {
        return owner_==std::this_thread::get_id() && !attempted_ && padContract_.admit(file);
    }
#endif
#if defined(GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD) && GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD == 1
    bool observeControlProof(ControlProofSnapshot&) noexcept;
#endif
private:
    bool admissionCheck(bool okay,FactoryStage stage,FactoryCause cause,std::uint32_t observed=0,
                        std::uint32_t expected=1,std::uint32_t index=UINT32_MAX) const noexcept {
        return admissionObservation_.check(okay,stage,cause,observed,expected,index);
    }
    template<class T> bool admissionRead(std::uint32_t at,T& value,FactoryStage stage,std::uint32_t index=UINT32_MAX) const noexcept {
        const auto result=ps::readMemory(at,&value,sizeof(value));
        return admissionObservation_.check(result.complete(sizeof(value)),stage,FactoryCause::read_failed,
            static_cast<std::uint32_t>(result.transferred),sizeof(value),index,result.error);
    }
    bool processingRefused(ProcessingGuard guard,std::uint32_t observed=0,std::uint32_t expected=0,DWORD error=0) const noexcept {
        if(observing_){auto& first=restoring_ ? processing_.restoration : processing_.primary;
            if(first.guard==ProcessingGuard::none)first={guard,error,observed,expected};}
        return false;
    }
    template<class T> bool processingRead(std::uint32_t at,T& value,ProcessingGuard guard) const noexcept {
        const auto result=ps::readMemory(at,&value,sizeof(value));
        return result.complete(sizeof(value)) || processingRefused(guard,static_cast<std::uint32_t>(result.transferred),sizeof(value),result.error);
    }
    bool ready() const noexcept;
    bool refused(Reason reason,const char* point) const noexcept {reason_=reason;failure_=point;return false;}
    template<class T> bool get(std::uint32_t at,T& value) const noexcept {
        return ps::readMemory(at,&value,sizeof(value)).complete(sizeof(value));
    }
    template<class T> bool put(std::uint32_t at,const T& value) const noexcept {
        SIZE_T count=0;
        return WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),&value,sizeof(value),&count) && count==sizeof(value);
    }
    bool pool(Pool&) const noexcept;
    bool poolIdentity(std::uint32_t,const Pool&,std::int32_t&) const noexcept;
    bool reference(bool) const noexcept;
    bool ownGroup() const noexcept;
    bool defaultTask() const noexcept;
    bool pose(ps::Position,float) noexcept;
    bool clearAt(ps::Position,ps::Position) const noexcept;
    std::uint32_t dataAddress() const noexcept {return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(data_.data()));}
    std::uint32_t tableAddress() const noexcept {return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(table_.data()+1));}
    Admission admission_;
    std::thread::id owner_;
    std::uint32_t control_=0,ped_=0,wanted_=0,clothes_=0;
    Record* tracked_=nullptr;
    Pool pool_{};
    core::RemotePlayerKey key_{};
    Identity identity_{};
    LocalIdentity local_{};
    alignas(4) std::array<std::byte,0xAC> data_{};
    // Preserve the preceding native metadata word too; the object points at +1.
    alignas(4) std::array<std::uint32_t,65> table_{};
    layout::LocalImage localBefore_{};
    std::array<std::byte,layout::infoStride> secondBefore_{};
    unsigned group_=8;
    bool attempted_=false,live_=false,destroyed_=false,originalCalled_=false;
#if defined(GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD) && GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD == 1
    bool originalReturned_=false;
#endif
    bool staged_=false,hasPresentation_=false;
    float height_=0;
    ps::Position spawnPosition_{},lastTarget_{};
    core::RemotePlayerFrame stagedFrame_{};
    float lastHeading_=0;
    std::uint32_t poseSequence_=0;
    mutable Reason reason_=Reason::none;
    mutable const char* failure_="none";
    mutable NativeProcessingObservation processing_{};
    mutable bool observing_=false,restoring_=false;
    mutable FactoryAdmissionObservation admissionObservation_{};
#if defined(GRANDMP_AUTHENTICATED_PAD_COW_BUILD) && GRANDMP_AUTHENTICATED_PAD_COW_BUILD == 1
    replay::HoodlumPadContract padContract_;
#endif
};
}
