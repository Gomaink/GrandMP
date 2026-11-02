#include "../tools/native_second_player.hpp"

namespace grandmp::windows::remote_player_binding::second_player {
bool WindowsAccess::context() const noexcept {
    return admission_.preWorld && admission_.preWorld(admission_.context);
}
bool WindowsAccess::ready() const noexcept {
    return (context() || controlContext()) && admission_.readyCodeAndResidence && admission_.readyCodeAndResidence(admission_.context);
}
bool WindowsAccess::controlContext() const noexcept {
    return admission_.originalControl && admission_.originalControl(admission_.context);
}
bool WindowsAccess::executorEnabled() const noexcept {
#if defined(GRANDMP_NATIVE_SECOND_PLAYER_EXECUTION_BUILD) && GRANDMP_NATIVE_SECOND_PLAYER_EXECUTION_BUILD == 1
    return true;
#else
    return false;
#endif
}
namespace {
template<class T> bool writeOwn(std::uint32_t at,const T& value) noexcept {
    SIZE_T done=0;
    return WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),&value,sizeof(value),&done) && done==sizeof(value);
}
// A bounded protection snapshot, not a claim about the CCamera C++ layout.
// No writes/restoration of camera or pointer-containing third-party memory.
struct Protected {
    std::array<std::byte,0x1000> camera{};
    factory::GroupImage cjGroup{};
    std::uint32_t cjGroupId=8,anim=0,timeScale=0;
    bool operator==(const Protected&) const=default;
};
bool protection(WindowsAccess& access,Protected& image) noexcept {
    auto get=[&](std::uint32_t at,auto& value) { return access.read(at,&value,sizeof(value)).complete(sizeof(value)); };
    return get(slot0+4+0x38,image.cjGroupId) && image.cjGroupId<factory::groups &&
        get(factory::groupBase+image.cjGroupId*factory::groupStride,image.cjGroup) &&
        get(0xB6F028,image.camera) && get(0xC0BC10,image.anim) && get(0xB7CB64,image.timeScale) &&
        image.timeScale==0x3F800000;
}
}
bool WindowsAccess::groupVacant(unsigned& chosen) const noexcept {
    std::array<std::uint8_t,factory::groups> flags{};
    unsigned cj=8;
    if (!get(factory::groupFlags,flags) || !get(slot0+4+0x38,cj) || cj>=flags.size() || flags[cj]!=1) return false;
    for (unsigned n=0;n<flags.size();++n) {
        if (flags[n]) continue;
        factory::GroupImage image{};
        if (n==cj || !get(factory::groupBase+n*factory::groupStride,image) ||
            !factory::initialGroup(image,factory::groupBase+n*factory::groupStride)) return false;
        chosen=n;return true; // MUST be the first inactive: native 5FB800.
    }
    return false;
}
bool WindowsAccess::taskCapacity() const noexcept {
    std::uint32_t pool=0;
    struct Header { std::uint32_t objects,flags;std::int32_t size; } p{};
    if (!get(factory::taskPool,pool) || !pool || !get(pool,p) || !p.objects || !p.flags ||
        p.size<=0 || p.size>16384 || p.objects>UINT32_MAX-static_cast<unsigned>(p.size)*factory::taskStride ||
        p.flags>UINT32_MAX-static_cast<unsigned>(p.size)) return false;
    unsigned free=0;
    for (int n=0;n<p.size && free<2;++n) {
        std::uint8_t tag=0;if (!get(p.flags+static_cast<unsigned>(n),tag)) return false;
        if (tag&0x80) ++free;
    }
    return free>=2; // PlayerOnFoot and HoldEntity; a check, NOT a reservation.
}
bool WindowsAccess::constructionProven() const noexcept {
    unsigned group=8;std::uint32_t occupant=1,scale=0,clothes=1,targeting=1;
    return context() && executorEnabled() && ready() && !attempted_ && admission_.spawnGround &&
        get(slot1,occupant) && !occupant && get(data1,clothes) && !clothes && get(data1+4,targeting) && !targeting &&
        get(0xB7CB64,scale) && scale==0x3F800000 && groupVacant(group) && taskCapacity();
}
ps::MemoryRead WindowsAccess::read(std::uint32_t address,void* output,std::size_t size) noexcept {
    if (!context() && !controlContext()) return observeRead({false,0,ERROR_ACCESS_DENIED},size);
    return observeRead(ps::readMemory(address,output,size),size);
}
bool WindowsAccess::putGroup(unsigned value) noexcept {
    return writeOwn(data1+0x38,value) && [&] { unsigned observed=8;return get(data1+0x38,observed) && observed==value; }();
}
bool WindowsAccess::ownGroup(std::uint32_t ped) const noexcept {
    unsigned current=8,cj=8;std::uint8_t active=0;factory::GroupImage image{};
    const auto base=factory::groupBase+group_*factory::groupStride;
    return seeded_ && group_<factory::groups && ped==ownPed_ &&
        get(data1+0x38,current) && current==group_ && get(slot0+4+0x38,cj) && cj<factory::groups && cj!=group_ &&
        get(factory::groupFlags+group_,active) && active==1 && get(base,image) && factory::membersOwned(image,base,ped);
}
bool WindowsAccess::defaultTask(std::uint32_t ped) const noexcept {
    std::uint32_t intel=0,owner=0,managerOwner=0,task=0,vt=0,pool=0;
    struct TaskPool { std::uint32_t objects,flags;std::int32_t size; } p{};
    if (!get(ped+0x47C,intel) || !intel || !get(intel,owner) || owner!=ped ||
        !get(intel+4+0x2C,managerOwner) || managerOwner!=ped || !get(intel+4+4*4,task) || !task ||
        !get(factory::taskPool,pool) || !pool || !get(pool,p) || !p.objects || !p.flags || p.size<=0 || p.size>16384 ||
        task<p.objects || (task-p.objects)%factory::taskStride) return false;
    const auto index=(task-p.objects)/factory::taskStride;std::uint8_t tag=0;
    return index<static_cast<unsigned>(p.size) && get(p.flags+index,tag) && !(tag&0x80) &&
        get(task,vt) && vt==factory::defaultTaskVtable;
}
bool WindowsAccess::onFootDefaultOwned(const Record& record) const noexcept {
    return controlContext() && ready() && record.entity.ped==ownPed_ && record.reference==ownPed_ &&
        ownGroup(ownPed_) && defaultTask(ownPed_);
}
bool WindowsAccess::createPlayerOne() {
    const bool previousObservation=observingCreation_;
    observingCreation_=creationObservation_.flags==0;
    struct EndObservation { bool& active;bool before;~EndObservation(){active=before;} } endObservation{observingCreation_,previousObservation};
    if(observingCreation_)creationObservation_.flags|=NativeCreateObservation::started;
    if (!constructionProven()) return creationRefused(CreateFailure::preparation);
    attempted_=true; // Every subsequent failure is terminal, including pre-Setup.
    Protected before{};unsigned selected=8;
    if (!protection(*this,before)) return creationRefused(CreateFailure::protection_before);
    if (!groupVacant(selected)) return creationRefused(CreateFailure::group_selection);
    if (!get(data1+0x38,oldGroup_)) return creationRefused(CreateFailure::old_group_read);
    if (!admission_.spawnGround(admission_.context,ground_)) return creationRefused(CreateFailure::spawn_refused);
    if (!ps::finite(ground_)) return creationRefused(CreateFailure::spawn_nonfinite);
#if defined(GRANDMP_NATIVE_SECOND_PLAYER_EXECUTION_BUILD) && GRANDMP_NATIVE_SECOND_PLAYER_EXECUTION_BUILD == 1
    // SetInitialState(false) runs BEFORE the constructor allocates a group.
    // Seed only our PlayerData field with the first authenticated empty group.
    Protected after{};
    group_=selected;
    if (!putGroup(group_)) return creationRefused(CreateFailure::group_seed);
    seeded_=true;
    if(observingCreation_)creationObservation_.flags|=NativeCreateObservation::group_seeded|NativeCreateObservation::setup_entered;
    reinterpret_cast<void(__cdecl*)(int)>(setup)(1);
    if(observingCreation_)creationObservation_.flags|=NativeCreateObservation::setup_returned;
    const bool slotRead=get(slot1,ownPed_);
    if(observingCreation_)creationObservation_.slot=!slotRead ? CreateSlot::unreadable : !ownPed_ ? CreateSlot::empty : CreateSlot::present;
    if (!slotRead) return creationRefused(CreateFailure::slot_read);
    if (!ownPed_) return creationRefused(CreateFailure::slot_empty);
    if (!ownGroup(ownPed_)) return creationRefused(CreateFailure::group_ownership);
    if (!defaultTask(ownPed_)) return creationRefused(CreateFailure::default_task);
    if (!protection(*this,after)) return creationRefused(CreateFailure::protection_after);
    if (!(before==after)) {
        // Compare only the two snapshots ALREADY captured, no extra GTA reads.
        if(observingCreation_)creationObservation_.changedProtection=
            (before.camera!=after.camera ? 1u:0u) | (before.cjGroup!=after.cjGroup ? 2u:0u) |
            (before.cjGroupId!=after.cjGroupId ? 4u:0u) | (before.anim!=after.anim ? 8u:0u) |
            (before.timeScale!=after.timeScale ? 16u:0u);
        return creationRefused(CreateFailure::protected_state_changed);
    }
    if(observingCreation_)creationObservation_.flags|=NativeCreateObservation::complete;
    return true;
#else
    return creationRefused(CreateFailure::execution_closed);
#endif
}
bool WindowsAccess::finishPlayerOne(const Record& record) {
    if (!context() || !executorEnabled() || !ready() || finished_ || record.entity.ped!=ownPed_ ||
        record.reference!=ownPed_ || !ownGroup(ownPed_) || !defaultTask(ownPed_)) return false;
#if defined(GRANDMP_NATIVE_SECOND_PLAYER_EXECUTION_BUILD) && GRANDMP_NATIVE_SECOND_PLAYER_EXECUTION_BUILD == 1
    Protected before{},after{};
    std::uint16_t model=0;std::uint32_t info=0,col=0;
    if (!protection(*this,before) || !get(ownPed_+0x22,model) || model!=0 ||
        !get(factory::modelTable+model*4,info) || !info || !get(info+0x14,col) || !col) return false;
    using Height=float(__thiscall*)(void*);
    const float height=reinterpret_cast<Height>(factory::baseHeight)(reinterpret_cast<void*>(ownPed_));
    if (!std::isfinite(height) || height<=0 || height>2.f) return false;
    ps::Position target=ground_;target.z+=height;
    // Test the model's actual sphere volumes, not its broad-phase bound sphere:
    // the latter may intersect the supporting ground even for a valid standing PED.
    // No unsupported shape is skipped; no contact is excused as 'probably ground'.
    struct ShapeHeader { std::uint16_t spheres,boxes,triangles;std::uint8_t lines,flags;std::uint32_t pointer; } shape{};
    static_assert(sizeof(ShapeHeader)==12);
    std::uint32_t data=0,matrix=0;ps::Position right{},forward{},up{};
    std::array<factory::CollisionSphere,32> spheres{};
    if (!ps::finite(target) || !get(col+0x2C,data) || !data || !get(data,shape) ||
        !shape.spheres || shape.spheres>spheres.size() || shape.boxes || shape.triangles || shape.lines || !shape.pointer ||
        shape.pointer>UINT32_MAX-shape.spheres*sizeof(factory::CollisionSphere) ||
        !read(shape.pointer,spheres.data(),shape.spheres*sizeof(factory::CollisionSphere)).complete(shape.spheres*sizeof(factory::CollisionSphere)) ||
        !get(ownPed_+0x14,matrix) || !matrix || !get(matrix,right) || !get(matrix+0x10,forward) || !get(matrix+0x20,up)) return false;
    // Setup sets orientation to exactly zero. Require that actual frame; no guessed
    // transform convention or tolerance that can scale the collision volumes.
    if (right.x!=1 || right.y!=0 || right.z!=0 || forward.x!=0 || forward.y!=1 || forward.z!=0 ||
        up.x!=0 || up.y!=0 || up.z!=1) return false;
    using Sphere=void*(__cdecl*)(ps::Position,float,void*,bool,bool,bool,bool,bool,bool);
    for (unsigned n=0;n<shape.spheres;++n) {
        const auto& sphere=spheres[n];
        if (!factory::sphereValid(sphere)) return false;
        const ps::Position centre{target.x+sphere.centre.x,target.y+sphere.centre.y,target.z+sphere.centre.z};
        if (!ps::finite(centre) || centre.z-sphere.radius<ground_.z ||
            reinterpret_cast<Sphere>(ps::testSphere)(centre,sphere.radius,reinterpret_cast<void*>(ownPed_),true,true,true,true,true,false)) return false;
    }
    if (!ownGroup(ownPed_) || !defaultTask(ownPed_)) return false;
    // Original SetCharCreatedBy(2), authenticated thiscall, own entity only.
    reinterpret_cast<void(__thiscall*)(void*,int)>(0x5E47E0)(reinterpret_cast<void*>(ownPed_),2);
    std::uint8_t createdBy=0;
    if (!get(ownPed_+0x484,createdBy) || createdBy!=2) return false;
    reinterpret_cast<void(__cdecl*)(int)>(factory::deactivate)(1);
    using SetPosition=void(__thiscall*)(void*,float,float,float);
    reinterpret_cast<SetPosition>(factory::setPosition)(reinterpret_cast<void*>(ownPed_),target.x,target.y,target.z);
    reinterpret_cast<void(__cdecl*)(int)>(factory::activate)(1);
    ps::Position actual{};
    if (!get(ownPed_+0x14,matrix) || !matrix || !get(matrix+0x30,actual) ||
        actual.x!=target.x || actual.y!=target.y || actual.z!=target.z || !defaultTask(ownPed_) ||
        !ownGroup(ownPed_) || !protection(*this,after) || before!=after) return false;
    finished_=true;return true;
#else
    (void)record;return false;
#endif
}
bool WindowsAccess::registerCell(std::uint32_t ped,std::uint32_t* cell) {
    if (!context() || !executorEnabled() || !ready()) return false;
#if defined(GRANDMP_NATIVE_SECOND_PLAYER_EXECUTION_BUILD) && GRANDMP_NATIVE_SECOND_PLAYER_EXECUTION_BUILD == 1
    reinterpret_cast<void(__thiscall*)(void*,void**)>(ps::registerReference)(
        reinterpret_cast<void*>(ped),reinterpret_cast<void**>(cell));
    return true;
#else
    (void)ped;(void)cell;return false;
#endif
}
bool WindowsAccess::cleanCell(std::uint32_t ped,std::uint32_t* cell) {
    if (!context() || !executorEnabled() || !ready()) return false;
#if defined(GRANDMP_NATIVE_SECOND_PLAYER_EXECUTION_BUILD) && GRANDMP_NATIVE_SECOND_PLAYER_EXECUTION_BUILD == 1
    reinterpret_cast<void(__thiscall*)(void*,void**)>(ps::cleanReference)(
        reinterpret_cast<void*>(ped),reinterpret_cast<void**>(cell));
    return true;
#else
    (void)ped;(void)cell;return false;
#endif
}
bool WindowsAccess::removeOne() {
    if (!context() || !executorEnabled() || !ready() || !ownGroup(ownPed_)) return false;
#if defined(GRANDMP_NATIVE_SECOND_PLAYER_EXECUTION_BUILD) && GRANDMP_NATIVE_SECOND_PLAYER_EXECUTION_BUILD == 1
    Protected before{},after{};std::uint32_t current=0,car=1;
    if (!get(slot1,current) || current!=ownPed_ || !get(current+vehicle,car) || car ||
        !protection(*this,before)) return false;
    reinterpret_cast<void(__cdecl*)(int)>(remove)(1);
    // No entity dereferences after RemovePlayerPed. Api separately verifies pool tag.
    std::uint8_t active=1;
    if (!get(slot1,current) || current || !get(factory::groupFlags+group_,active) || active ||
        !protection(*this,after) || before!=after) return false;
    return true;
#else
    return false;
#endif
}
bool WindowsAccess::finishRemoval(const Record& record) {
    // Api has already authenticated returned destruction, slot and old pool tag.
    // This stage never dereferences the former PED.
    unsigned currentGroup=8;std::uint32_t occupant=1;std::uint8_t active=1;
    if (!context() || !executorEnabled() || !ready() || !seeded_ || group_>=factory::groups ||
        record.entity.ped!=ownPed_ || !get(slot1,occupant) || occupant ||
        !get(data1+0x38,currentGroup) || currentGroup!=group_ ||
        !get(factory::groupFlags+group_,active) || active) return false;
#if defined(GRANDMP_NATIVE_SECOND_PLAYER_EXECUTION_BUILD) && GRANDMP_NATIVE_SECOND_PLAYER_EXECUTION_BUILD == 1
    if (!putGroup(oldGroup_)) return false;
    seeded_=false;ownPed_=0;return true;
#else
    return false;
#endif
}
} // namespace grandmp::windows::remote_player_binding::second_player
