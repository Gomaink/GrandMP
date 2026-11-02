// Alpha 0.10 only. Existing CLEO scheduler, capture, Controller and transport;
// no shared HookCell publication and no cooperative PlayerInfo entity.
nf::FirstProcessingFailure processingFailure;
nf::FirstFactoryAdmissionFailure factoryAdmissionFailure;
void reportProcessingFailure() noexcept {
    nf::ProcessingFailure copy{};
    if(processingFailure.take(copy))diag::recordProcessingFailure(diagnostic,copy);
    nf::FactoryAdmissionFailure admission{};
    if(factoryAdmissionFailure.take(admission))diag::recordFactoryAdmissionFailure(diagnostic,admission);
}
auto& nativeApi=bootstrapApi;
bool foundationSpawn(ps::Position& ground) noexcept {return bootstrapApi && bootstrapApi->spawnGround(ground);}
bool stageRemote() noexcept {
    grandmp::core::RemotePlayerFrames frames{};
    if(!bootstrapApi)return false;
    bootstrapApi->clearStage();
    if(!bootstrapController.tryRead(grandmp::core::RemoteRoster::Clock::now(),frames))return false;
    for(std::size_t i=0;i<frames.count && i<frames.players.size();++i){
        const auto& frame=frames.players[i];
        if((binding->state()==np::State::live ? frame.key==binding->key() : frame.freshness==grandmp::core::ControlFreshness::fresh)){
            const bool staged=bootstrapApi->stage(frame);
            return binding->state()==np::State::live || staged;
        }
    }
    return binding->state()==np::State::live; // A coherent leave must still reach consume.
}
#if !defined(GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD) || GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD != 1
bool nativeContext(void* value) noexcept {return context(value);}
#endif
bool inputContext(void*) noexcept {
    return scriptThread && GetCurrentThreadId()==scriptThread && inputActive && !borrowed && preparation.load()==3;
}
bool inputCodeMatches() noexcept {
    // Task consumers/camera basis are the already authenticated unarmed path.
    constexpr std::array regions{ps::CodeRegion{0x609560,0x2B},ps::CodeRegion{0x53FB70,0x20},
        ps::CodeRegion{0x53FC90,0x110},ps::CodeRegion{0x688810,0x100},ps::CodeRegion{0x6883D0,0x440},
        ps::CodeRegion{0x6859A0,0x18FC},ps::CodeRegion{0x50BFF0,0x42},ps::CodeRegion{0x407C05,0xA},
        ps::CodeRegion{0x568530,0x1C4}};
    for(const auto r:regions)for(std::size_t offset=0;offset<r.size;){
        const auto count=std::min<std::size_t>(4096,r.size-offset);
        if(!game.matches(r.address+offset,count))return false;
        offset+=count;
    }return true;
}
void writeLog(const char* state,const char* reason) noexcept {
    if(!std::strcmp(state,"failed")){stopped.store(true);status.store(108);diagnostic.emit(diag::Note::sync_failure,reason);}
}
#if !defined(GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD) || GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD != 1
#include "../tools/local_player_capture_runtime.inl"
#endif
void inputReport() noexcept {} // No per-frame logging or second task.
bool installInputHook() noexcept {
    // The PRIVATE data table was published before World::Add, while no World
    // executor could reach this newly allocated object. This is only a readback.
    const bool valid=bootstrapApi && bootstrapApi->privateDispatchReady();
    if(valid)diagnostic.emit(diag::Note::replay_installed,"foundation.private_dispatch_ready_no_global_hook");
    else writeLog("failed","foundation.private_dispatch_unproven");
    return valid;
}
struct PooledInvocation {nf::ProcessingFailure observation{};};
void pooledInvoke(void* value) {
    auto& call=*static_cast<PooledInvocation*>(value);
    call.observation.result=binding->process(bootstrapController,grandmp::core::RemoteRoster::Clock::now(),
        {binding->key(),inputPed,inputSerial},*bootstrapApi,&call.observation.trace);
    call.observation.returned=true;
}
void pooledFinish(void* value) noexcept {
    auto& call=*static_cast<PooledInvocation*>(value);
    call.observation.native=bootstrapApi->endProcessingObservation(); // Copy only; no engine access.
#if defined(GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD) && GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD == 1
    proofAfter(call.observation);
#endif
    inputActive=false;inputPed=0;
    if(!call.observation.returned || call.observation.result!=np::Result::processed){
        call.observation.state=binding->state();call.observation.input=binding->inputDisposition();
        processingFailure.publish(call.observation);
        stopped.store(true);status.store(108);
    }
}
__declspec(noinline) void __fastcall pooledControl(void* ped,void*) {
    // Only the newly owned object's table routes here. Local CJ's vtable, pad0,
    // focus and original ProcessControl are never hooked or replaced.
    const auto caller=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
    nf::WrapperRefusal refusal=nf::WrapperRefusal::none;
    if(scriptThread!=GetCurrentThreadId())refusal=nf::WrapperRefusal::thread;
    else if(borrowed)refusal=nf::WrapperRefusal::pre_world_overlap;
    else if(inputActive)refusal=nf::WrapperRefusal::reentrant;
    else if(stopped.load())refusal=nf::WrapperRefusal::stopped;
    else if(!binding)refusal=nf::WrapperRefusal::binding_absent;
    else if(!bootstrapApi)refusal=nf::WrapperRefusal::api_absent;
    else if(reinterpret_cast<std::uintptr_t>(ped)!=binding->entityAddress())refusal=nf::WrapperRefusal::entity_mismatch;
    else if(inputSerial==UINT64_MAX)refusal=nf::WrapperRefusal::serial_exhausted;
    else if(std::find(ni::callers.begin(),ni::callers.end(),caller)==ni::callers.end())refusal=nf::WrapperRefusal::caller;
    if(refusal!=nf::WrapperRefusal::none){
        nf::ProcessingFailure record{};record.wrapper=refusal;
        // An unadmitted entry does not access binding/Api even for diagnostics.
        processingFailure.publish(record);
        stopped.store(true);return; // Unknown lifetime/context NEVER borrows local controls.
    }
    inputActive=true;inputPed=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(ped));++inputSerial;
    PooledInvocation call{};call.observation.key=binding->key();call.observation.serial=inputSerial;
    bootstrapApi->beginProcessingObservation();
#if defined(GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD) && GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD == 1
    proofBefore(inputSerial);
#endif
    np::withFinally(pooledInvoke,pooledFinish,&call);
}
std::uint32_t inputHookAddress() noexcept {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&pooledControl));
}
#if !defined(GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD) || GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD != 1
void presentRemote() noexcept {
    grandmp::core::RemotePlayerFrames frames{};
    if(!bootstrapController.tryRead(grandmp::core::RemoteRoster::Clock::now(),frames))return;
    for(std::size_t i=0;i<frames.count && i<frames.players.size();++i){
        const auto& frame=frames.players[i];
        if(frame.key!=binding->key())continue;
        if(frame.freshness!=grandmp::core::ControlFreshness::fresh)return;
        if(!bootstrapApi->present(frame))writeLog("failed","foundation.presentation_refused_terminal");
        return;
    }
}
#endif
