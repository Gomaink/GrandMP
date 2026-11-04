// Removal only. Creation and native input processing remain the b94ef54 bodies.
bool Api::cleanupContext() const noexcept {
    if(!context(Phase::pre_world) || !cleanupReady_ || !cleanupReady_(admission_.context) ||
        !completedNeutral_ || !originalReturned_ || observing_ || restoring_)return false;
    // Existing CJ identity and untouched PlayerInfo[1], not a new witness image.
    Pool p{};std::uint32_t ped=0,vt=0,data=0,type=9;std::int32_t handle=-1;
    std::array<std::byte,layout::infoStride> second{};
    return pool(p) && p.objects==local_.objects && p.flags==local_.flags && p.size==local_.size &&
        get(slot0,ped) && ped==local_.ped && poolIdentity(ped,p,handle) && handle==local_.handle &&
        get(ped,vt) && vt==ps::playerVtable && get(ped+playerData,data) && data==slot0+4 &&
        get(ped+pedType,type) && !type && get(layout::slot1,second) && second==secondBefore_;
}
bool Api::stoppedForRemoval() const noexcept {
    namespace o=native_movement_diagnostic::layout;
    std::array<float,2> speed{},local{},world{};
    const auto zero=[](const auto& xy){return std::isfinite(xy[0]) && std::isfinite(xy[1]) && xy[0]==0 && xy[1]==0;};
    // Current native motion, not a historical log or equality of snapshot XYZ.
    return get(ped_+o::speed,speed) && get(ped_+o::localShift,local) && get(ped_+o::worldShift,world) &&
        zero(speed) && zero(local) && zero(world);
}
bool Api::removalProven() const noexcept {
    return context(Phase::pre_world) && ready() && live_ && !destroyed_ && !removal_.requested &&
        completedNeutral_ && originalReturned_ && !observing_ && !restoring_;
}
bool Api::retainedForRemoval(const Record& record) const noexcept {
    if(!cleanupContext() || live_ || destroyed_ || tracked_!=&record || record.key!=key_ ||
        record.entity!=identity_ || record.reference!=ped_ || removal_.key!=key_ ||
        removal_.entered<minimal_lifecycle::Boundary::clean_reference)return false;
    Pool p{};std::int32_t handle=-1;std::uint32_t vt=0,data=0,wanted=0,clothes=0,flags=0,type=0,car=1;
    std::uint8_t entityType=0;
    return pool(p) && p.objects==pool_.objects && p.flags==pool_.flags && p.size==pool_.size &&
        poolIdentity(ped_,p,handle) && handle==identity_.handle &&
        get(ped_,vt) && vt==tableAddress() && table_[4]==0x5424C0 && table_[11]==control_ &&
        get(ped_+playerData,data) && data==dataAddress() && ownGroup() && reference(false) &&
        get(dataAddress(),wanted) && wanted==wanted_ && get(dataAddress()+4,clothes) && clothes==clothes_ &&
        get(ped_+pedType,type) && type==1 && get(ped_+vehicle,car) && !car &&
        get(ped_+0x1C,flags) && !(flags&0x100) && get(ped_+0x36,entityType) && (entityType&7)==3;
}
bool Api::releasedForRemoval(const Record& record,bool dataReleased) const noexcept {
    // NEVER dereference the old PED, its task, group member or pool object here.
    if(!cleanupContext() || !destroyed_ || live_ || tracked_!=&record || record.key!=key_ ||
        record.entity!=identity_ || removal_.key!=key_ || identity_.handle<0 || group_>=f::groups)return false;
    Pool p{};std::uint8_t tag=0,active=1;std::uint32_t wanted=1,clothes=1;
    return pool(p) && p.objects==pool_.objects && p.flags==pool_.flags && p.size==pool_.size &&
        get(p.flags+(static_cast<unsigned>(identity_.handle)>>8),tag) && tag==(static_cast<unsigned>(identity_.handle&0x7F)|0x80) &&
        get(f::groupFlags+group_,active) && !active &&
        get(dataAddress(),wanted) && wanted==(dataReleased ? 0:wanted_) &&
        get(dataAddress()+4,clothes) && clothes==(dataReleased ? 0:clothes_);
}
Removal Api::remove(Record& record) {
    if(!context(Phase::pre_world))return Removal::indeterminate;
    struct Operations {
        Api& a;Record& record;
        bool preflight() {
            // Strict readiness still applies BEFORE CleanReference. No new
            // animation lease, streaming gate, diagnostic or worker dependency.
            std::uint32_t flags=0;std::uint8_t type=0;
            return a.ready() && a.live_ && !a.destroyed_ && a.completedNeutral_ && a.originalReturned_ &&
                !a.observing_ && !a.restoring_ && a.authenticate(record) && a.stoppedForRemoval() &&
                a.get(a.ped_+0x1C,flags) && !(flags&0x100) && a.get(a.ped_+0x36,type) && (type&7)==3 &&
                a.table_[4]==0x5424C0 && a.cleanupContext() && a.authenticate(record) &&
                a.removalBarrier_ && !a.removalBarrier_->closed();
        }
        void cleanReference() {
            a.removalBarrier_->close(); // Full preflight passed; boundary recorded; no native call in between.
            a.live_=false;
            reinterpret_cast<void(__thiscall*)(void*,void**)>(ps::cleanReference)(reinterpret_cast<void*>(a.ped_),reinterpret_cast<void**>(&record.reference));
        }
        bool referenceUnlinked() {return a.retainedForRemoval(record);}
        void removeWorld() {reinterpret_cast<void(__cdecl*)(void*)>(worldRemove)(reinterpret_cast<void*>(a.ped_));}
        bool worldDetached() {
            // Hoodlum 563280 -> virtual +0C (5424C0) -> moving unlink 542860.
            // Authenticate the still-live tagged allocation on BOTH sides.
            std::uint32_t sectors=1,moving=1;
            return a.retainedForRemoval(record) && a.get(a.ped_+0xB0,sectors) && !sectors &&
                a.get(a.ped_+layout::movingLink,moving) && !moving && a.retainedForRemoval(record);
        }
        void destroyPed() {
            reinterpret_cast<void(__thiscall*)(void*,unsigned)>(deletingDestructor)(reinterpret_cast<void*>(a.ped_),1);
            a.destroyed_=true; // No old-PED access from here onward, even on failure.
        }
        bool pedDestroyed() {return a.releasedForRemoval(record,false);}
        void destroyData() {reinterpret_cast<void(__thiscall*)(void*)>(dataDestructor)(a.data_.data());}
        bool resourcesReleased() {return a.releasedForRemoval(record,true);}
        bool verified() {
            if(!a.releasedForRemoval(record,true))return false;
            record.reference=0;return true;
        }
    } operations{*this,record};
    struct Call {Api& api;Operations& operations;bool complete=false;} call{*this,operations};
    const auto body=[](void* value){auto& c=*static_cast<Call*>(value);c.complete=minimal_lifecycle::removeOnce(c.api.removal_,c.api.key_,c.operations);};
    const auto finish=[](void* value) noexcept {static_cast<Call*>(value)->api.removal_.finishUnknown();};
    withFinally(body,finish,&call); // Includes SEH: seal uncertainty, never retry.
    if(!call.complete){refused(Reason::cleanup_unknown,"pooled.removal.terminal");return Removal::indeterminate;}
    return Removal::complete;
}
bool Api::removed(const Record& before,const Record& after) noexcept {
    if(removal_.confirmed!=minimal_lifecycle::State::removed_verified || removal_.failure!=minimal_lifecycle::Failure::none ||
        before.key!=after.key || before.entity!=after.entity || after.reference || !releasedForRemoval(after,true))
        return removal_.fail(minimal_lifecycle::Failure::verification);
    return true;
}
