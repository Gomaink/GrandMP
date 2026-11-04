#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../tools/pooled_remote_player.hpp"
#if defined(GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD) && GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD == 1
#include "../tools/native_movement_diagnostic.hpp"
#endif

namespace grandmp::windows::remote_player_binding::pooled_player {
namespace f=layout::factory;
bool Api::context(Phase phase) const noexcept {
    if(!executionEnabled || owner_!=std::this_thread::get_id())return false;
    const auto allowed=phase==Phase::pre_world ? admission_.preWorld : admission_.originalControl;
    return allowed && allowed(admission_.context);
}
bool Api::ready() const noexcept {
    return (context(Phase::pre_world) || context(Phase::original_process_control)) &&
        admission_.readyCodeAndResidence && admission_.readyCodeAndResidence(admission_.context);
}
bool Api::pool(Pool& p) const noexcept {
    std::uint32_t at=0;
    return get(ps::pedPool,at) && at && get(at,p) && p.objects && p.flags && p.size>0 &&
        p.size<=static_cast<int>(layout::poolProbeLimit) && p.cursor>=-1 && p.cursor<p.size &&
        p.objects<=UINT32_MAX-static_cast<unsigned>(p.size)*layout::poolStride &&
        p.flags<=UINT32_MAX-static_cast<unsigned>(p.size);
}
bool Api::poolIdentity(std::uint32_t ped,const Pool& p,std::int32_t& handle) const noexcept {
    if(ped<p.objects || (ped-p.objects)%layout::poolStride)return false;
    const auto index=(ped-p.objects)/layout::poolStride;std::uint8_t tag=0;
    if(index>=static_cast<unsigned>(p.size) || !get(p.flags+index,tag) || (tag&0x80))return false;
    handle=static_cast<std::int32_t>((index<<8)|tag);return true;
}
bool Api::localIdentity(LocalIdentity& out,Phase phase) const noexcept {
    out={};if(!context(phase) || !ready())return false;
    Pool p{};std::uint32_t ped=0,vt=0,data=0,type=9;std::int32_t handle=-1;std::uint8_t focus=1;
    if(!get(ps::playerIndex,focus) || focus || !get(slot0,ped) || !ped || !pool(p) ||
        !poolIdentity(ped,p,handle) || !get(ped,vt) || vt!=ps::playerVtable ||
        !get(ped+playerData,data) || data!=slot0+4 || !get(ped+pedType,type) || type)return false;
    out={ped,p.objects,p.flags,handle,p.size};return true;
}
bool Api::factoryProven() const noexcept {
    admissionObservation_.flags|=FactoryAdmissionObservation::proof_entered;
    if(admissionObservation_.proofCalls<UINT32_MAX)++admissionObservation_.proofCalls;
    if(!admissionCheck(context(Phase::pre_world),FactoryStage::proof_context,FactoryCause::precondition) ||
        !admissionCheck(ready(),FactoryStage::proof_ready,FactoryCause::precondition) ||
        !admissionCheck(!attempted_,FactoryStage::proof_attempted,FactoryCause::precondition) ||
        !admissionCheck(control_!=0,FactoryStage::proof_control,FactoryCause::missing) ||
        !admissionCheck(admission_.spawnGround!=nullptr,FactoryStage::proof_spawn_provider,FactoryCause::missing) ||
        !admissionCheck(staged_,FactoryStage::proof_staged,FactoryCause::precondition))
        return refused(Reason::construction_unproven,"pooled.factory.context_or_state");
    LocalIdentity local{};Pool p{};std::uint8_t model=0,cjGroupActive=0;std::uint32_t unused=1,scale=0,cjGroup=8;
    if(!admissionCheck(localIdentity(local),FactoryStage::local_identity,FactoryCause::precondition) ||
        !admissionRead(layout::slot1,unused,FactoryStage::slot1_vacancy) ||
        !admissionCheck(!unused,FactoryStage::slot1_vacancy,FactoryCause::occupied,unused!=0,0) ||
        !admissionCheck(pool(p),FactoryStage::ped_pool,FactoryCause::invalid) ||
        !admissionRead(ps::streamingTable+0x10,model,FactoryStage::model) ||
        !admissionCheck(model==1,FactoryStage::model,FactoryCause::precondition,model,1) ||
        !admissionRead(0xB7CB64,scale,FactoryStage::time_scale) ||
        !admissionCheck(scale==0x3F800000,FactoryStage::time_scale,FactoryCause::precondition) ||
        !admissionRead(slot0+4+0x38,cjGroup,FactoryStage::local_group) ||
        !admissionCheck(cjGroup<f::groups,FactoryStage::local_group,FactoryCause::invalid) ||
        !admissionRead(f::groupFlags+cjGroup,cjGroupActive,FactoryStage::local_group_active,cjGroup) ||
        !admissionCheck(cjGroupActive==1,FactoryStage::local_group_active,FactoryCause::precondition,cjGroupActive==1,1,cjGroup))
        return refused(Reason::readiness,"pooled.factory.local_streaming_or_coop");
    bool available=false;
    for(int i=0;i<p.size;++i){std::uint8_t tag=0;if(!admissionRead(p.flags+i,tag,FactoryStage::ped_slot,i))return false;available|=(tag&0x80)!=0;}
    if(!admissionCheck(available,FactoryStage::ped_available,FactoryCause::exhausted))return refused(Reason::pool_full,"pooled.factory.pool_full");
    // Same native resources as the original constructor. Observation is not a reservation.
    std::uint32_t taskPool=0;struct TaskPool {std::uint32_t objects,flags;int size;};TaskPool tasks{};
    if(!admissionRead(f::taskPool,taskPool,FactoryStage::task_pool) ||
        !admissionCheck(taskPool!=0,FactoryStage::task_pool,FactoryCause::missing) ||
        !admissionRead(taskPool,tasks,FactoryStage::task_header) ||
        !admissionCheck(tasks.objects!=0,FactoryStage::task_objects,FactoryCause::missing) ||
        !admissionCheck(tasks.flags!=0,FactoryStage::task_flags,FactoryCause::missing) ||
        !admissionCheck(tasks.size>0 && tasks.size<=16384,FactoryStage::task_capacity,FactoryCause::invalid,static_cast<std::uint32_t>(tasks.size),16384) ||
        !admissionCheck(tasks.flags<=UINT32_MAX-static_cast<unsigned>(tasks.size),FactoryStage::task_bounds,FactoryCause::invalid))return false;
    unsigned free=0;
    for(int i=0;i<tasks.size && free<2;++i){std::uint8_t tag=0;if(!admissionRead(tasks.flags+i,tag,FactoryStage::task_slot,i))return false;free+=(tag&0x80)!=0;}
    if(!admissionCheck(free>=2,FactoryStage::task_available,FactoryCause::exhausted,free,2))return refused(Reason::pool_full,"pooled.factory.task_pool");
    for(unsigned i=0;i<f::groups;++i){
        std::uint8_t active=1;f::GroupImage image{};
        if(!admissionRead(f::groupFlags+i,active,FactoryStage::group_flag,i))return false;
        if(active)continue;
        const auto base=f::groupBase+i*f::groupStride;
        return (admissionRead(base,image,FactoryStage::group_image,i) &&
            admissionCheck(f::initialGroup(image,base),FactoryStage::group_resources,FactoryCause::invalid,0,1,i)) ||
            refused(Reason::construction_unproven,"pooled.factory.group_resources");
    }
    admissionCheck(false,FactoryStage::group_available,FactoryCause::exhausted);
    return refused(Reason::construction_unproven,"pooled.factory.no_group");
}
bool Api::ownGroup() const noexcept {
    if(group_>=f::groups)return false;
    std::uint8_t active=0;std::uint32_t current=8;f::GroupImage image{};
    const auto base=f::groupBase+group_*f::groupStride;
    return get(dataAddress()+0x38,current) && current==group_ && get(f::groupFlags+group_,active) && active==1 &&
        get(base,image) && exclusiveGroupMember(image,base,ped_);
}
bool Api::defaultTask() const noexcept {
    std::uint32_t intel=0,owner=0,task=0,vt=0,at=0;
    struct TaskPool {std::uint32_t objects,flags;int size;};TaskPool p{};
    if(!get(ped_+0x47C,intel) || !intel || !get(intel,owner) || owner!=ped_ ||
        !get(intel+4+0x2C,owner) || owner!=ped_ || !get(intel+4+16,task) || !task ||
        !get(f::taskPool,at) || !at || !get(at,p) || !p.objects || !p.flags || p.size<=0 || p.size>16384 ||
        task<p.objects || (task-p.objects)%f::taskStride)return false;
    const auto index=(task-p.objects)/f::taskStride;std::uint8_t tag=0;
    return index<static_cast<unsigned>(p.size) && get(p.flags+index,tag) && !(tag&0x80) && get(task,vt) && vt==f::defaultTaskVtable;
}
bool Api::reference(bool expected) const noexcept {
    if(!tracked_)return false;
    std::uint32_t node=0;
    if(!get(ped_+0x24,node))return false;
    for(unsigned n=0;node && n<256;++n){
        std::uint32_t next=0,cell=0;
        if(!get(node,next) || !get(node+4,cell))return false;
        if(cell==reinterpret_cast<std::uintptr_t>(&tracked_->reference))return expected;
        node=next;
    }
    return !node && !expected;
}
Creation Api::create(core::RemotePlayerKey key,Record& record) {
    admissionObservation_.flags|=FactoryAdmissionObservation::create_entered;
    if(!admissionCheck(key.player!=0,FactoryStage::create_player,FactoryCause::precondition) ||
        !admissionCheck(key.generation!=0,FactoryStage::create_generation,FactoryCause::precondition) ||
        !admissionCheck(key==record.key,FactoryStage::create_record_key,FactoryCause::changed) ||
        !admissionCheck(key==stagedFrame_.key,FactoryStage::create_staged_key,FactoryCause::changed) ||
        !admissionCheck(!record.reference,FactoryStage::create_record_reference,FactoryCause::occupied,record.reference!=0,0) ||
        !admissionCheck(!record.entity.ped,FactoryStage::create_record_entity,FactoryCause::occupied,record.entity.ped!=0,0) ||
        !factoryProven())return Creation::absent;
    ps::Position ground{};
    std::array<std::byte,4096> cameraBefore{},cameraAfter{};f::GroupImage cjGroupBefore{},cjGroupAfter{};
    std::uint32_t cjGroup=8,groupAfter=8,scale=0,idlesBefore=0,idlesAfter=0;
    if(!admissionCheck(admission_.spawnGround(admission_.context,ground),FactoryStage::create_spawn,FactoryCause::precondition) ||
        !admissionCheck(ps::finite(ground),FactoryStage::create_ground,FactoryCause::non_finite) ||
        !admissionCheck(localIdentity(local_),FactoryStage::create_local,FactoryCause::precondition) ||
        !admissionCheck(pool(pool_),FactoryStage::create_pool,FactoryCause::invalid) ||
        !admissionRead(slot0,localBefore_.info,FactoryStage::create_info) ||
        !admissionRead(pad0,localBefore_.pads,FactoryStage::create_pads) ||
        !admissionRead(ps::playerIndex,localBefore_.focus,FactoryStage::create_focus) ||
        !admissionRead(layout::slot1,secondBefore_,FactoryStage::create_slot1) ||
        !admissionRead(0xB6F028,cameraBefore,FactoryStage::create_camera) ||
        !admissionRead(slot0+4+0x38,cjGroup,FactoryStage::create_group) ||
        !admissionCheck(cjGroup<f::groups,FactoryStage::create_group,FactoryCause::invalid) ||
        !admissionRead(f::groupBase+cjGroup*f::groupStride,cjGroupBefore,FactoryStage::create_group_image,cjGroup) ||
        !admissionRead(0xC0BC10,idlesBefore,FactoryStage::create_idles))return Creation::absent;
    const int idles=reinterpret_cast<int(__cdecl*)(const char*)>(0x4D3990)("playidles");
    if(!admissionCheck(idles>=0,FactoryStage::create_animation,FactoryCause::missing) ||
        !admissionCheck(idlesBefore==static_cast<unsigned>(idles),FactoryStage::create_animation,FactoryCause::changed))return Creation::absent;
    attempted_=true;tracked_=&record;key_=key; // All partial native results are terminal and retained.
    auto failed=[&](const char* point){refused(Reason::factory_result_unknown,point);return Creation::indeterminate;};
    // Native construction with PRIVATE PlayerData: no Setup, no CPlayerPed(int),
    // no PlayerInfo publication, no SCM command and no ClearSpaceForMissionEntity.
    ped_=reinterpret_cast<std::uint32_t(__cdecl*)(unsigned)>(allocatePed)(0x7A4);
    record.entity.ped=ped_; // Allocation identity retained BEFORE construction.
    if(!ped_)return failed("pooled.create.allocate");
    Pool allocated{};std::int32_t allocatedHandle=-1;
    if(ped_==local_.ped || !pool(allocated) || allocated.objects!=pool_.objects || allocated.flags!=pool_.flags ||
        allocated.size!=pool_.size || !poolIdentity(ped_,allocated,allocatedHandle))return failed("pooled.create.allocation_identity");
    record.entity.handle=allocatedHandle; // Authenticate allocator output BEFORE its first constructor write.
    const auto base=reinterpret_cast<std::uint32_t(__thiscall*)(void*,int)>(baseConstructor)(reinterpret_cast<void*>(ped_),0);
    if(base!=ped_)return failed("pooled.create.base_constructor");
    reinterpret_cast<void(__thiscall*)(void*)>(dataConstructor)(data_.data());
    reinterpret_cast<void(__thiscall*)(void*)>(allocateData)(data_.data());
    std::uint32_t cjWanted=0,cjClothes=0,partialFlags=0;
    if(!get(dataAddress(),wanted_) || !wanted_ || !get(dataAddress()+4,clothes_) || !clothes_ ||
        !get(slot0+4,cjWanted) || !get(slot0+8,cjClothes) || wanted_==cjWanted || clothes_==cjClothes)return failed("pooled.create.data");
    // Until our group exists, the player destructor must NOT dispose group zero.
    // Complete the additional fields/data first; only then publish the player type.
    if(!get(ped_+0x478,partialFlags) || !put(ped_+0x79C,std::uint32_t{0}) || !put(ped_+0x7A0,std::uint32_t{0}) ||
        !put(ped_+playerData,dataAddress()) || !put(ped_+0x478,partialFlags|0x200000u) ||
        !put(ped_,ps::playerVtable))return failed("pooled.create.private_data");
    reinterpret_cast<void(__thiscall*)(void*,int)>(setModel)(reinterpret_cast<void*>(ped_),0);
    std::uint32_t rw=0,frame=0;
    if(!get(ped_+0x18,rw) || !rw || !get(rw+4,frame) || !frame)return failed("pooled.create.model_clump");
    // true skips ALL group operations in SetInitialState. It does not itself set
    // bUsedForReplay. Our own group is created below; normal physics remains ON.
    reinterpret_cast<void(__thiscall*)(void*,bool)>(initialState)(reinterpret_cast<void*>(ped_),true);
    unsigned expectedGroup=f::groups;
    for(unsigned i=0;i<f::groups;++i){
        std::uint8_t active=1;f::GroupImage image{};const auto at=f::groupBase+i*f::groupStride;
        if(!get(f::groupFlags+i,active))return failed("pooled.create.group_revalidation");
        if(active)continue;
        if(i==cjGroup || !get(at,image) || !f::initialGroup(image,at))return failed("pooled.create.group_revalidation");
        expectedGroup=i;break;
    }
    if(expectedGroup>=f::groups)return failed("pooled.create.group_unavailable");
    group_=reinterpret_cast<unsigned(__cdecl*)()>(0x5FB800)();
    if(group_!=expectedGroup || !put(dataAddress()+0x38,group_))return failed("pooled.create.group");
    const auto groupBase=f::groupBase+group_*f::groupStride;
    reinterpret_cast<void(__thiscall*)(void*,int)>(0x5FBB70)(reinterpret_cast<void*>(groupBase+0x30),5);
    if(!put(groupBase+0x2D0,std::uint8_t{1}))return failed("pooled.create.group_mission");
    reinterpret_cast<void(__thiscall*)(void*,void*)>(0x5FB9C0)(reinterpret_cast<void*>(groupBase+8),reinterpret_cast<void*>(ped_));
    reinterpret_cast<void(__thiscall*)(void*)>(0x5FC7E0)(reinterpret_cast<void*>(groupBase));
    const float health=reinterpret_cast<float(__cdecl*)(int)>(0x559AF0)(9);
    std::uint32_t intel=0,flags=0;
    if(!std::isfinite(health) || health<=0 || !get(ped_+0x47C,intel) || !intel || !get(ped_+0x478,flags) ||
        !put(ped_+0x540,health) || !put(ped_+0x544,health) || !put(ped_+0x72D,std::uint16_t{0x0F0F}) ||
        !put(ped_+0x79C,std::uint32_t{0}) || !put(ped_+0x7A0,std::uint32_t{0}) ||
        !put(intel+0xC8,30.f) || !put(intel+0xC4,std::uint32_t{2}) ||
        !put(ped_+pedType,std::uint32_t{1}) ||
        !put(ped_+0x71A,std::uint8_t{100}))return failed("pooled.create.player_fields");
    reinterpret_cast<void(__thiscall*)(void*,void*)>(0x4E68D0)(reinterpret_cast<void*>(ped_+0x294),reinterpret_cast<void*>(ped_));
    reinterpret_cast<void(__thiscall*)(void*,int)>(0x5E47E0)(reinterpret_cast<void*>(ped_),2);
    std::int32_t handle=-1;Pool current{};
    if(!pool(current) || current.objects!=pool_.objects || current.flags!=pool_.flags || current.size!=pool_.size ||
        !poolIdentity(ped_,current,handle) || handle!=allocatedHandle || !ownGroup() || !defaultTask())return failed("pooled.create.pool_group_task");
    if(!put(ped_+0x478,flags&~0x200000u))return failed("pooled.create.enable_native_physics");
    record.entity.playerData=dataAddress();record.entity.pad=pad1;record.entity.handle=handle;
    record.entity.slot=std::numeric_limits<unsigned>::max();record.reference=ped_;
    reinterpret_cast<void(__thiscall*)(void*,void**)>(ps::registerReference)(reinterpret_cast<void*>(ped_),reinterpret_cast<void**>(&record.reference));
    if(record.reference!=ped_ || !reference(true))return failed("pooled.create.reference");
    std::uint16_t actualModel=1;std::uint32_t modelInfo=0,col=0,shapeData=0;
    std::array<std::byte,0x30> colHeader{};
    if(!get(ped_+0x22,actualModel) || actualModel || !get(f::modelTable,modelInfo) || !modelInfo ||
        !get(modelInfo+0x14,col) || !col || !get(col,colHeader))return failed("pooled.create.height_identity");
    height_=reinterpret_cast<float(__thiscall*)(void*)>(f::baseHeight)(reinterpret_cast<void*>(ped_));
    if(!std::isfinite(height_) || height_<=0 || height_>2.f)return failed("pooled.create.height");
    spawnPosition_={ground.x,ground.y,ground.z+height_};
    if(!clearAt(spawnPosition_,spawnPosition_))return failed("pooled.create.spawn_collision");
    // Same sphere-only MODEL_PLAYER placement contract as the reviewed factory.
    // Do not replace model volumes by only its broad-phase bounding sphere.
    struct ShapeHeader {std::uint16_t spheres,boxes,triangles;std::uint8_t lines,flags;std::uint32_t pointer;};
    static_assert(sizeof(ShapeHeader)==12);
    ShapeHeader shape{};std::array<f::CollisionSphere,32> spheres{};
    if(!get(f::modelTable,modelInfo) || !modelInfo || !get(modelInfo+0x14,col) || !col ||
        !get(col+0x2C,shapeData) || !shapeData || !get(shapeData,shape) || !shape.spheres || shape.spheres>spheres.size() ||
        shape.boxes || shape.triangles || shape.lines || !shape.pointer ||
        shape.pointer>UINT32_MAX-shape.spheres*sizeof(f::CollisionSphere) ||
        !ps::readMemory(shape.pointer,spheres.data(),shape.spheres*sizeof(f::CollisionSphere)).complete(shape.spheres*sizeof(f::CollisionSphere)))
        return failed("pooled.create.model_collision_data");
    using Sphere=void*(__cdecl*)(ps::Position,float,void*,bool,bool,bool,bool,bool,bool);
    for(unsigned n=0;n<shape.spheres;++n){
        const auto& sphere=spheres[n];
        const ps::Position p{spawnPosition_.x+sphere.centre.x,spawnPosition_.y+sphere.centre.y,spawnPosition_.z+sphere.centre.z};
        if(!f::sphereValid(sphere) || !ps::finite(p) || p.z-sphere.radius<ground.z ||
            reinterpret_cast<Sphere>(ps::testSphere)(p,sphere.radius,reinterpret_cast<void*>(ped_),true,true,true,true,true,false))return failed("pooled.create.model_collision_hit");
    }
    reinterpret_cast<void(__thiscall*)(void*,float,float,float)>(orientation)(reinterpret_cast<void*>(ped_),0,0,0);
    reinterpret_cast<void(__thiscall*)(void*,float,float,float)>(f::setPosition)(reinterpret_cast<void*>(ped_),spawnPosition_.x,spawnPosition_.y,spawnPosition_.z);
    layout::LocalImage localAfter{};std::array<std::byte,layout::infoStride> secondAfter{};
    auto localStable=[&]{return get(slot0,localAfter.info) && get(pad0,localAfter.pads) && get(ps::playerIndex,localAfter.focus) &&
        localAfter==localBefore_ && get(layout::slot1,secondAfter) && secondAfter==secondBefore_ &&
        get(0xB6F028,cameraAfter) && cameraAfter==cameraBefore && get(slot0+4+0x38,groupAfter) && groupAfter==cjGroup &&
        get(f::groupBase+cjGroup*f::groupStride,cjGroupAfter) && cjGroupAfter==cjGroupBefore &&
        get(0xB7CB64,scale) && scale==0x3F800000 && get(0xC0BC10,idlesAfter) && idlesAfter==idlesBefore;};
    auto placed=[&]{std::uint32_t matrix=0;ps::Position p{};return get(ped_+0x14,matrix) && matrix &&
        get(matrix+0x30,p) && p.x==spawnPosition_.x && p.y==spawnPosition_.y && p.z==spawnPosition_.z;};
    if(!localStable() || !placed())return failed("pooled.create.pre_world_publication");
    if(!ps::readMemory(ps::playerVtable-4,table_.data(),sizeof(table_)).complete(sizeof(table_)) || table_[11]!=replay::original)return failed("pooled.create.vtable_source");
    table_[11]=control_;
    if(!publishPrivateTable(reinterpret_cast<volatile LONG*>(ped_),ps::playerVtable,tableAddress(),context(Phase::pre_world)))return failed("pooled.create.private_vtable");
    reinterpret_cast<void(__cdecl*)(void*)>(worldAdd)(reinterpret_cast<void*>(ped_));
    identity_=record.entity;live_=true;
    if(!localStable() || !placed())return failed("pooled.create.local_changed");
    if(!authenticate(record))return failed("pooled.create.postcondition");
    reason_=Reason::none;failure_="none";return Creation::complete;
}
bool Api::authenticate(const Record& record) noexcept {
    if(!ready() || !live_ || destroyed_ || tracked_!=&record || record.key!=key_ || record.entity!=identity_ ||
        record.reference!=ped_ || record.entity.ped!=ped_ || record.entity.playerData!=dataAddress() ||
        record.entity.pad!=pad1 || record.entity.slot!=std::numeric_limits<unsigned>::max())return false;
    LocalIdentity local{};Pool p{};std::int32_t handle=-1;std::uint32_t vt=0,data=0,type=9,car=1,node=0,entity=0,flags=0;
    std::uint16_t model=1;std::uint8_t created=0,weapon=255;
    std::uint32_t wanted=0,clothes=0,cjWanted=0,cjClothes=0,weaponType=1,target=1;float health=0;
    std::array<std::byte,layout::infoStride> second{};
    const auto phase=context(Phase::pre_world) ? Phase::pre_world : Phase::original_process_control;
    return localIdentity(local,phase) && local==local_ && ped_!=local.ped && pool(p) &&
        p.objects==pool_.objects && p.flags==pool_.flags && p.size==pool_.size &&
        poolIdentity(ped_,p,handle) && handle==record.entity.handle && get(ped_,vt) && vt==tableAddress() &&
        table_[11]==control_ && get(ped_+playerData,data) && data==dataAddress() &&
        get(ped_+0x22,model) && model==0 && get(ped_+0x484,created) && created==2 &&
        get(ped_+0x540,health) && std::isfinite(health) && health>0 &&
        get(ped_+0x718,weapon) && !weapon && get(ped_+0x5A0,weaponType) && !weaponType && get(ped_+0x71C,target) && !target &&
        get(ped_+0x478,flags) && !(flags&0x200000) &&
        get(dataAddress(),wanted) && wanted==wanted_ && get(dataAddress()+4,clothes) && clothes==clothes_ &&
        get(slot0+4,cjWanted) && wanted!=cjWanted && get(slot0+8,cjClothes) && clothes!=cjClothes &&
        get(ped_+pedType,type) && type==1 && get(ped_+vehicle,car) && !car &&
        get(ped_+layout::movingLink,node) && node && get(node,entity) && entity==ped_ && reference(true) && ownGroup() &&
        get(layout::slot1,second) && second==secondBefore_;
}
#if defined(GRANDMP_MINIMAL_NATIVE_LIFECYCLE_BUILD) && GRANDMP_MINIMAL_NATIVE_LIFECYCLE_BUILD == 1
#include "../tools/minimal_player_lifecycle.inl"
#else
bool Api::removalProven() const noexcept {return context(Phase::pre_world) && ready() && live_ && !destroyed_;}
Removal Api::remove(Record& record) {
    if(!removalProven() || !authenticate(record))return Removal::indeterminate;
    // No slot-indexed RemovePlayerPed. Only the registered owned object is removed.
    live_=false;
    reinterpret_cast<void(__thiscall*)(void*,void**)>(ps::cleanReference)(reinterpret_cast<void*>(ped_),reinterpret_cast<void**>(&record.reference));
    Pool current{};std::int32_t handle=-1;std::uint32_t vt=0,data=0,wanted=0,clothes=0;
    if(record.reference!=ped_ || !reference(false) || !ownGroup() || !pool(current) ||
        current.objects!=pool_.objects || current.flags!=pool_.flags || current.size!=pool_.size ||
        !poolIdentity(ped_,current,handle) || handle!=identity_.handle || !get(ped_,vt) || vt!=tableAddress() ||
        !get(ped_+playerData,data) || data!=dataAddress() || !get(dataAddress(),wanted) || wanted!=wanted_ ||
        !get(dataAddress()+4,clothes) || clothes!=clothes_)return Removal::indeterminate;
    reinterpret_cast<void(__cdecl*)(void*)>(worldRemove)(reinterpret_cast<void*>(ped_));
    reinterpret_cast<void(__thiscall*)(void*,unsigned)>(deletingDestructor)(reinterpret_cast<void*>(ped_),1);
    destroyed_=true; // NEVER dereference the former PED after this call.
    Pool p{};std::uint8_t tag=0,groupActive=1;std::array<std::byte,layout::infoStride> second{};
    if(!pool(p) || p.objects!=pool_.objects || p.flags!=pool_.flags || p.size!=pool_.size ||
        !get(p.flags+(static_cast<unsigned>(record.entity.handle)>>8),tag) || tag!=(static_cast<unsigned>(record.entity.handle&0x7F)|0x80) ||
        !get(f::groupFlags+group_,groupActive) || groupActive || !get(layout::slot1,second) || second!=secondBefore_ ||
        !get(dataAddress(),wanted) || wanted!=wanted_ || !get(dataAddress()+4,clothes) || clothes!=clothes_)return Removal::indeterminate;
    reinterpret_cast<void(__thiscall*)(void*)>(dataDestructor)(data_.data());
    if(!get(dataAddress(),wanted) || wanted || !get(dataAddress()+4,clothes) || clothes)return Removal::indeterminate;
    record.reference=0;return Removal::complete;
}
bool Api::removed(const Record& before,const Record& after) noexcept {
    if(!context(Phase::pre_world) || !ready() || !destroyed_ || live_ || &after!=tracked_ ||
        before.key!=after.key || before.entity!=after.entity || after.reference)return false;
    Pool p{};std::uint8_t tag=0,groupActive=1;std::uint32_t wanted=1,clothes=1;
    return before.key==key_ && before.entity==identity_ && pool(p) && p.objects==pool_.objects && p.flags==pool_.flags && p.size==pool_.size &&
        get(p.flags+(static_cast<unsigned>(before.entity.handle)>>8),tag) && tag==(static_cast<unsigned>(before.entity.handle&0x7F)|0x80) &&
        get(f::groupFlags+group_,groupActive) && !groupActive && get(dataAddress(),wanted) && !wanted &&
        get(dataAddress()+4,clothes) && !clothes;
}
#endif

struct InputImage {
    std::array<std::byte,padStride> local{};
    OnFootPadStates remote{};
#if defined(GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD) && GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD == 1
    std::array<std::byte,padStride-0x60> remoteTail{}; // Witness only; never repaired by guessing.
#if defined(GRANDMP_REMOTE_TAIL_DIAGNOSTIC_BUILD) && GRANDMP_REMOTE_TAIL_DIAGNOSTIC_BUILD == 1
    remote_tail_diagnostic::Boundary tailBoundary{}; // Diagnostic metadata, deliberately excluded from equality.
    static_assert(padStride-0x60==remote_tail_diagnostic::extent);
#endif
#endif
    std::uint8_t focus=1;
    float heading=0;
    std::uint16_t camera=1;
    bool operator==(const InputImage& other) const noexcept {
        return local==other.local && remote.current==other.remote.current && remote.previous==other.remote.previous &&
#if defined(GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD) && GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD == 1
            remoteTail==other.remoteTail &&
#endif
            focus==other.focus && heading==other.heading && camera==other.camera;
    }
};
static_assert(sizeof(InputImage)<=ContextImage{}.bytes.size());
bool Api::replayProven() const noexcept {
    if(!context(Phase::original_process_control))return processingRefused(ProcessingGuard::replay_context);
    if(!ready())return processingRefused(ProcessingGuard::replay_ready);
    if(!live_)return processingRefused(ProcessingGuard::replay_live);
    return !destroyed_ || processingRefused(ProcessingGuard::replay_destroyed);
}
bool Api::dispatch(const Record& record,const Dispatch& call) noexcept {
    originalCalled_=false;
#if defined(GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD) && GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD == 1
    originalReturned_=false;
#endif
    return replayProven() && authenticate(record) && call.key==record.key && call.ped==ped_ && call.serial;
}
bool Api::capture(ContextImage& saved) noexcept {
    if(!replayProven())return processingRefused(ProcessingGuard::capture_contract);
    InputImage image{};std::array<std::byte,0x60> pads{};
    if(!processingRead(pad0,image.local,ProcessingGuard::capture_pad0) || !processingRead(pad1,pads,ProcessingGuard::capture_pad1) ||
        !processingRead(ps::playerIndex,image.focus,ProcessingGuard::capture_focus_read))return false;
    if(image.focus)return processingRefused(ProcessingGuard::capture_focus,image.focus);
    if(!processingRead(replay::cameraHeading,image.heading,ProcessingGuard::capture_heading_read))return false;
    if(!std::isfinite(image.heading))return processingRefused(ProcessingGuard::capture_heading);
    if(!processingRead(replay::weaponCameraMode,image.camera,ProcessingGuard::capture_camera_read))return false;
    if(image.camera)return processingRefused(ProcessingGuard::capture_camera,image.camera);
#if defined(GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD) && GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD == 1
    if(!processingRead(pad1+0x60,image.remoteTail,ProcessingGuard::capture_pad1_tail))return false;
#if defined(GRANDMP_REMOTE_TAIL_DIAGNOSTIC_BUILD) && GRANDMP_REMOTE_TAIL_DIAGNOSTIC_BUILD == 1
    if(observing_){
        image.tailBoundary.captureTid=GetCurrentThreadId();
        const auto clock=ps::readMemory(remote_tail_diagnostic::engineClock,&image.tailBoundary.engineMs,sizeof(image.tailBoundary.engineMs));
        image.tailBoundary.clockComplete=clock.complete(sizeof(image.tailBoundary.engineMs));
        image.tailBoundary.transferred=static_cast<std::uint32_t>(clock.transferred);image.tailBoundary.error=clock.error;
    } // A failed diagnostic clock read is NOT a new processing refusal.
#endif
#endif
    std::memcpy(image.remote.current.data(),pads.data(),0x30);std::memcpy(image.remote.previous.data(),pads.data()+0x30,0x30);
    std::memcpy(saved.bytes.data(),&image,sizeof(image));saved.used=sizeof(image);return true;
}
bool Api::apply(const Record& record,const Controls& controls) {
    if(!replayProven())return processingRefused(ProcessingGuard::apply_contract);
    if(!authenticate(record))return processingRefused(ProcessingGuard::apply_identity);
    if(!defaultTask())return processingRefused(ProcessingGuard::apply_task);
    std::uint8_t weapon=255;std::uint32_t type=1,target=1,intel=0,flags=0,playerFlags=0;float view=0;std::array<std::uint32_t,4> tasks{};
    std::uint16_t disabled=1,mode=1,padMode=65535;
    if(!processingRead(ped_+0x718,weapon,ProcessingGuard::weapon_slot_read))return false;
    if(weapon)return processingRefused(ProcessingGuard::weapon_slot,weapon);
    if(!processingRead(ped_+0x5A0,type,ProcessingGuard::weapon_type_read))return false;
    if(type)return processingRefused(ProcessingGuard::weapon_type,type);
    if(!processingRead(ped_+0x71C,target,ProcessingGuard::target_read))return false;
    if(target)return processingRefused(ProcessingGuard::target,1); // Presence only, never the pointer.
    if(!processingRead(pad1+0x10E,disabled,ProcessingGuard::disabled_read))return false;
    if(disabled)return processingRefused(ProcessingGuard::disabled,disabled);
    if(!processingRead(replay::weaponCameraMode,mode,ProcessingGuard::camera_read))return false;
    if(mode)return processingRefused(ProcessingGuard::camera,mode);
    if(!processingRead(pad1+0x10A,padMode,ProcessingGuard::pad_mode_read))return false;
    if(padMode>3)return processingRefused(ProcessingGuard::pad_mode,padMode,3);
    if(!processingRead(ped_+0x46C,flags,ProcessingGuard::flags_read))return false;
    if(flags&0x04000100)return processingRefused(ProcessingGuard::flags,flags&0x04000100);
    if(!processingRead(dataAddress()+0x34,playerFlags,ProcessingGuard::player_flags_read))return false;
    if(playerFlags&4)return processingRefused(ProcessingGuard::player_flags,playerFlags&4);
    if(!processingRead(ped_+0x47C,intel,ProcessingGuard::intelligence_read))return false;
    if(!intel)return processingRefused(ProcessingGuard::intelligence);
    if(!processingRead(intel+4,tasks,ProcessingGuard::slots_read))return false;
    if(std::any_of(tasks.begin(),tasks.end(),[](auto p){return p!=0;})){
        unsigned mask=0;for(unsigned n=0;n<tasks.size();++n)if(tasks[n])mask|=1u<<n;
        return processingRefused(ProcessingGuard::slots_busy,mask);
    }
    if(!processingRead(replay::cameraHeading,view,ProcessingGuard::view_read))return false;
    Controls adjusted=controls;
    if((adjusted.current.right || adjusted.current.forward) && !(adjusted.current.buttons&core::PlayerControls::walk))return processingRefused(ProcessingGuard::walk_only);
    if(!replay::cameraRelative(controls.previous,controls.viewHeading,view,adjusted.previous))return processingRefused(ProcessingGuard::previous_basis);
    if(!replay::cameraRelative(controls.current,controls.viewHeading,view,adjusted.current))return processingRefused(ProcessingGuard::current_basis);
    if(!prepareOnFootPad(adjusted.previous,adjusted.current,adjusted.pad))return processingRefused(ProcessingGuard::pad_encode);
    return replay::writePadStates(pad1,reinterpret_cast<void*>(ps::imageBase),true,adjusted.pad
#if defined(GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD) && GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD == 1
        ,&processing_.padApply
#if defined(GRANDMP_AUTHENTICATED_PAD_COW_BUILD) && GRANDMP_AUTHENTICATED_PAD_COW_BUILD == 1
        ,&padContract_
#endif
#endif
        ) || processingRefused(ProcessingGuard::pad_write);
}
bool Api::original(const Record& record) {
    if(!replayProven())return processingRefused(ProcessingGuard::original_contract);
    if(originalCalled_)return processingRefused(ProcessingGuard::original_duplicate);
    if(!authenticate(record))return processingRefused(ProcessingGuard::original_identity);
    originalCalled_=true;
    // The independent remote deliberately has no PlayerInfo index. Use the
    // normal CPed task/physics path, not CPlayerPed's slot-indexed weapon/camera layer.
    reinterpret_cast<void(__thiscall*)(void*)>(baseControl)(reinterpret_cast<void*>(ped_));
#if defined(GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD) && GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD == 1
    originalReturned_=true; // Only after normal native return, never during unwind.
#endif
    return true;
}
bool Api::restore(const ContextImage& saved) noexcept {
    restoring_=true;
    struct End {bool& flag;~End(){flag=false;}} end{restoring_};
    if(!context(Phase::original_process_control))return processingRefused(ProcessingGuard::restore_context);
    if(saved.used!=sizeof(InputImage))return processingRefused(ProcessingGuard::restore_size,static_cast<std::uint32_t>(saved.used),sizeof(InputImage));
    InputImage before{};std::memcpy(&before,saved.bytes.data(),sizeof(before));
    if(!replay::writePadStates(pad1,reinterpret_cast<void*>(ps::imageBase),true,before.remote
#if defined(GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD) && GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD == 1
        ,&processing_.padRestore
#if defined(GRANDMP_AUTHENTICATED_PAD_COW_BUILD) && GRANDMP_AUTHENTICATED_PAD_COW_BUILD == 1
        ,&padContract_
#endif
#endif
        ))return processingRefused(ProcessingGuard::restore_write);
    ContextImage after{};
    if(!capture(after))return false;
    InputImage checked{};std::memcpy(&checked,after.bytes.data(),sizeof(checked));
#if defined(GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD) && GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD == 1
    const auto semantic=semantic_pad_context::evaluate(before.remoteTail,checked.remoteTail,
        before.tailBoundary,checked.tailBoundary,originalReturned_);
    if(observing_){
        processing_.semantic=semantic;
        processing_.tail=remote_tail_diagnostic::compare(before.remoteTail,checked.remoteTail,before.tailBoundary,checked.tailBoundary);
    }
#else
    if(checked==before)return true;
#if defined(GRANDMP_REMOTE_TAIL_DIAGNOSTIC_BUILD) && GRANDMP_REMOTE_TAIL_DIAGNOSTIC_BUILD == 1
    if(observing_ && checked.remoteTail!=before.remoteTail)
        processing_.tail=remote_tail_diagnostic::compare(before.remoteTail,checked.remoteTail,before.tailBoundary,checked.tailBoundary);
#endif
#endif
    const unsigned mask=(checked.local!=before.local ? 1u:0u) |
#if defined(GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD) && GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD == 1
#if defined(GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD) && GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD == 1
        (!semantic_pad_context::accepted(semantic) ? 32u:0u) |
#else
        (checked.remoteTail!=before.remoteTail ? 32u:0u) |
#endif
#endif
        (checked.remote.current!=before.remote.current || checked.remote.previous!=before.remote.previous ? 2u:0u) |
        (checked.focus!=before.focus ? 4u:0u) | (checked.heading!=before.heading ? 8u:0u) | (checked.camera!=before.camera ? 16u:0u);
#if defined(GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD) && GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD == 1
    if(!mask)return true; // Binding still requires body success and post-original identity before State::live.
#endif
    return processingRefused(ProcessingGuard::restore_compare,mask);
}
#if defined(GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD) && GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD == 1
bool Api::observeControlProof(ControlProofSnapshot& out) noexcept {
    // On the owned object's real World dispatch only. This is NOT a sample
    // immediately before/after UpdatePosition, nor proof of a rendered clip.
    if(!context(Phase::original_process_control) || !tracked_ || !authenticate(*tracked_))return false;
    namespace offsets=native_movement_diagnostic::layout;
    ControlProofSnapshot value{};
    const auto actor=[&](std::uint32_t ped,ControlProofActor& a) noexcept {
        std::uint32_t matrix=0,current=0;
        if(!get(ped+offsets::matrix,matrix) || !matrix || !get(matrix+offsets::position,a.position) ||
           !get(ped+offsets::states,a.states) || !get(ped+offsets::localShift,a.localShift) ||
           !get(ped+offsets::worldShift,a.worldShift) || !get(ped+offsets::health,a.health) ||
           !get(ped+0x558,a.heading) || !get(ped+offsets::matrix,current) || current!=matrix)return false;
        return ps::finite(a.position) && std::isfinite(a.health) && a.health>0 && std::isfinite(a.heading) &&
            std::isfinite(a.localShift[0]) && std::isfinite(a.localShift[1]) &&
            std::isfinite(a.worldShift[0]) && std::isfinite(a.worldShift[1]);
    };
    if(!actor(ped_,value.remote) || !actor(local_.ped,value.local) || !authenticate(*tracked_))return false;
    out=value;return true; // Only values; no matrix/PED/association pointer escapes.
}
#endif
bool Api::clearAt(ps::Position from,ps::Position to) const noexcept {
    const float distance=std::hypot(to.x-from.x,to.y-from.y);
    if(!validPoseStep(from,to))return false;
    const auto samples=std::max(1u,static_cast<unsigned>(std::ceil(distance/.5f)));
    for(unsigned n=1;n<=samples;++n){
        const float t=static_cast<float>(n)/samples;
        const ps::Position p{from.x+(to.x-from.x)*t,from.y+(to.y-from.y)*t,from.z+(to.z-from.z)*t};
        bool found=false;
        const float ground=reinterpret_cast<float(__cdecl*)(float,float,float,bool*,void**)>(ps::findGround)(p.x,p.y,p.z+1.f,&found,nullptr);
        if(!found || !std::isfinite(ground) || std::abs(ground+height_-p.z)>.75f)return false;
        using Sphere=void*(__cdecl*)(ps::Position,float,void*,bool,bool,bool,bool,bool,bool);
        if(reinterpret_cast<Sphere>(ps::testSphere)(p,.35f,reinterpret_cast<void*>(ped_),true,true,true,true,true,false))return false;
    }return true;
}
bool Api::pose(ps::Position target,float heading) noexcept {
    if(!context(Phase::pre_world) || !tracked_ || !authenticate(*tracked_) || !std::isfinite(heading))return false;
    std::uint32_t matrix=0;ps::Position before{};
    if(!get(ped_+0x14,matrix) || !matrix || !get(matrix+0x30,before) || !clearAt(before,target))return false;
    // Coordinated transform/world update, only while the World iterator is absent.
    // CWorld::Add performs RW matrix/frame update. No direct matrix/velocity writes.
    reinterpret_cast<void(__cdecl*)(void*)>(worldRemove)(reinterpret_cast<void*>(ped_));
    reinterpret_cast<void(__thiscall*)(void*,float,float,float)>(orientation)(reinterpret_cast<void*>(ped_),0,0,heading);
    if(!put(ped_+0x558,heading) || !put(ped_+0x55C,heading))return false;
    reinterpret_cast<void(__thiscall*)(void*,float,float,float)>(f::setPosition)(reinterpret_cast<void*>(ped_),target.x,target.y,target.z);
    reinterpret_cast<void(__cdecl*)(void*)>(worldAdd)(reinterpret_cast<void*>(ped_));
    ps::Position actual{};
    return authenticate(*tracked_) && get(ped_+0x14,matrix) && matrix && get(matrix+0x30,actual) &&
        actual.x==target.x && actual.y==target.y && actual.z==target.z;
}
bool Api::present(const core::RemotePlayerFrame& frame) noexcept {
    if(!context(Phase::pre_world) || !tracked_ || frame.key!=tracked_->key || !core::validOnFoot(frame.reference) ||
        frame.presentation.id!=frame.key.player || frame.freshness!=core::ControlFreshness::fresh || !authenticate(*tracked_))return false;
    const ps::Position remote{frame.presentation.x,frame.presentation.y,frame.presentation.z};
    if(!ps::finite(remote))return false;
    if(frame.sequence<poseSequence_)return false; // Same monotonic policy as RemotePlayerController.
    if(hasPresentation_ && frame.sequence==poseSequence_ && remote.x==lastTarget_.x && remote.y==lastTarget_.y &&
        remote.z==lastTarget_.z && frame.reference.heading==lastHeading_)return true;
    if(!pose(remote,frame.reference.heading))return false;
    poseSequence_=frame.sequence;lastTarget_=remote;lastHeading_=frame.reference.heading;hasPresentation_=true;return true;
}
bool Api::spawnGround(ps::Position& out) const noexcept {
    admissionObservation_.flags|=FactoryAdmissionObservation::spawn_entered;
    if(!admissionCheck(context(Phase::pre_world),FactoryStage::spawn_context,FactoryCause::precondition) ||
        !admissionCheck(ready(),FactoryStage::spawn_ready,FactoryCause::precondition) ||
        !admissionCheck(staged_,FactoryStage::spawn_staged,FactoryCause::precondition))return false;
    LocalIdentity local{};std::uint32_t matrix=0;ps::Position at{};
    const ps::Position target{stagedFrame_.presentation.x,stagedFrame_.presentation.y,stagedFrame_.presentation.z};
    // Initial milestone is same-region, nearby, on-foot only. Never invent a
    // local offset: positions remain the actual shared GTA world coordinates.
    if(!admissionCheck(localIdentity(local),FactoryStage::spawn_local,FactoryCause::precondition) ||
        !admissionRead(local.ped+0x14,matrix,FactoryStage::spawn_matrix) ||
        !admissionCheck(matrix!=0,FactoryStage::spawn_matrix,FactoryCause::missing) ||
        !admissionRead(matrix+0x30,at,FactoryStage::spawn_position) ||
        !admissionCheck(validPoseStep(at,target),FactoryStage::spawn_delta,FactoryCause::invalid))return false;
    bool found=false;
    const float ground=reinterpret_cast<float(__cdecl*)(float,float,float,bool*,void**)>(ps::findGround)(target.x,target.y,target.z+1.f,&found,nullptr);
    if(!admissionCheck(found,FactoryStage::spawn_ground,FactoryCause::missing) ||
        !admissionCheck(std::isfinite(ground),FactoryStage::spawn_ground,FactoryCause::non_finite) ||
        !admissionCheck(!(ground>target.z),FactoryStage::spawn_height,FactoryCause::invalid) ||
        !admissionCheck(!(target.z-ground>2.f),FactoryStage::spawn_height,FactoryCause::invalid))return false;
    using Sphere=void*(__cdecl*)(ps::Position,float,void*,bool,bool,bool,bool,bool,bool);
    // No PED allocated yet: exclude NONE, including CJ. A conflicting spawn is
    // refused, never moved to another point and never cleared by deleting entities.
    if(!admissionCheck(reinterpret_cast<Sphere>(ps::testSphere)(target,.35f,nullptr,true,true,true,true,true,false)==nullptr,
        FactoryStage::spawn_sphere,FactoryCause::collision,1,0))return false;
    out={target.x,target.y,ground};return true;
}
}
