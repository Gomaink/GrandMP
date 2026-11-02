// Exclusive input-replay variant. Included in the resident native factory host.
// No scheduler: only a real CWorld virtual dispatch can borrow pad1.
bool inputContext(void*) noexcept {
    return gameThread && GetCurrentThreadId()==gameThread && inputActive && !phaseDispatchActive &&
        preparation.load()==3;
}
bool inputCodeMatches() noexcept {
    if (!nativeContext(nullptr) && !inputContext(nullptr)) return false;
    // Entry/epilogues, CWorld dispatches, type-selected pad, input consumers and
    // their camera reads, including the Hoodlum Zelda EH trampoline.
    constexpr std::array windows{
        ps::CodeRegion{0x60EA90,0xE13},ps::CodeRegion{0x609560,0x2B},
        ps::CodeRegion{0x568530,0x1C4},ps::CodeRegion{0x53FB70,0x20},
        ps::CodeRegion{0x53FC90,0x110},ps::CodeRegion{0x688810,0x100},
        ps::CodeRegion{0x6883D0,0x440},ps::CodeRegion{0x407C05,0xA},
        ps::CodeRegion{0x6859A0,0x18FC},ps::CodeRegion{0x50BFF0,0x42},
        ps::CodeRegion{0x53CBE0,0x84}};
    for (const auto region:windows) {
#if defined(GRANDMP_CLEO_PLAYER_SYNC_BUILD) && GRANDMP_CLEO_PLAYER_SYNC_BUILD == 1
        if (!inputWindowMatches(region)) return false;
#else
        const auto* expected=diskBytes(region.address,region.size);
        std::array<std::byte,0x1900> bytes{};ps::CodeIdentityFailure failure{};
        if (!expected || !factoryPage(region.address,region.size,failure) ||
            !ps::identityRead(failure,region.address,bytes.data(),region.size) ||
            std::memcmp(bytes.data(),expected,region.size)) return false;
#endif
    }
    return true;
}

struct InputImage {
    std::array<std::byte,nf::padStride> localPad{};
    np::OnFootPadStates remotePad{};
    float heading=0;
    std::uint16_t weaponCamera=0;
    std::uint8_t focus=0;
};
static_assert(sizeof(InputImage)<=np::ContextImage{}.bytes.size());
enum class InputNotice { none, installed, walking, turning, stopped, refused, restored_hook };
InputNotice inputNotice=InputNotice::none;
std::uint64_t inputProcessed=0;
const np::Record* inputRecord=nullptr; // Borrowed only until inputFinish on the same thread.
bool inputRestoreFailed=false;

template<class T> bool inputRead(std::uint32_t at,T& value) noexcept {
    return inputContext(nullptr) && nativeAccess.read(at,&value,sizeof(value)).complete(sizeof(value));
}
bool inputWrite(const np::OnFootPadStates& values) noexcept {
    return ni::writePadStates(nf::pad1,reinterpret_cast<void*>(0x400000),inputContext(nullptr),values);
}
bool inputImage(InputImage& image) noexcept {
    std::array<std::byte,0x60> pad{};
    if (!inputRead(nf::pad0,image.localPad) || !inputRead(nf::pad1,pad) ||
        !inputRead(ps::playerIndex,image.focus) || image.focus!=0 ||
        !inputRead(ni::cameraHeading,image.heading) || !std::isfinite(image.heading) ||
        !inputRead(ni::weaponCameraMode,image.weaponCamera)) return false;
    std::memcpy(image.remotePad.current.data(),pad.data(),0x30);
    std::memcpy(image.remotePad.previous.data(),pad.data()+0x30,0x30);return true;
}

class InputApi final : public np::Api {
public:
    bool context(np::Phase phase) const noexcept override {
        return phase==np::Phase::pre_world ? nativeContext(nullptr) : inputContext(nullptr);
    }
    bool factoryProven() const noexcept override {
        return nativeContext(nullptr) && nativeApi->factoryProven() && inputCodeMatches();
    }
    bool removalProven() const noexcept override { return nativeApi->removalProven(); }
    bool replayProven() const noexcept override {
        return inputContext(nullptr) && !runtimeDisabled.load() && inputHook.installed() && inputCodeMatches();
    }
    np::Creation create(grandmp::core::RemotePlayerKey key,np::Record& record) override {
        return nativeApi->create(key,record);
    }
    bool authenticate(const np::Record& record) noexcept override { return nativeApi->authenticate(record); }
    np::Removal remove(np::Record& record) override {
        if (!nativeContext(nullptr) || !nativeApi->authenticate(record) || !inputHook.remove())
            return np::Removal::indeterminate;
        inputNotice=InputNotice::restored_hook;return nativeApi->remove(record);
    }
    bool removed(const np::Record& before,const np::Record& after) noexcept override {
        return nativeApi->removed(before,after);
    }
    bool dispatch(const np::Record& record,const np::Dispatch& call) noexcept override {
        const bool valid=inputContext(nullptr) && call.serial==inputSerial && call.ped==inputPed &&
            call.ped==record.entity.ped && call.key==record.key;
        if (valid) inputRecord=&record;
        return valid;
    }
    bool capture(np::ContextImage& saved) noexcept override {
        InputImage image{};
        if (!inputImage(image)) return false;
        std::memcpy(saved.bytes.data(),&image,sizeof(image));saved.used=sizeof(image);return true;
    }
    bool apply(const np::Record& record,const np::Controls& controls) override {
        // Deliberately no weapon, target, duck, vehicle or mission-task context.
        // Unknown/changed context is terminal, never expanded into camera/aim replay.
        std::uint8_t weapon=UINT8_MAX;std::uint16_t disabled=1,mode=1,padMode=UINT16_MAX;
        std::uint32_t weaponType=~0u,target=1,flags=0,playerFlags=0,intel=0,owner=0;
        std::array<std::uint32_t,4> tasks{};float liveView=0;
        if (!authenticate(record) || !nativeAccess.onFootDefaultOwned(record) ||
            !inputRead(inputPed+0x718,weapon) || weapon!=0 ||
            !inputRead(inputPed+0x5A0,weaponType) || weaponType!=0 ||
            !inputRead(inputPed+0x71C,target) || target ||
            !inputRead(inputPed+0x46C,flags) || (flags&0x04000000) ||
            !inputRead(record.entity.playerData+0x34,playerFlags) || (playerFlags&4) ||
            !inputRead(nf::pad1+0x10E,disabled) || disabled ||
            !inputRead(nf::pad1+0x10A,padMode) || padMode>3 ||
            !inputRead(ni::weaponCameraMode,mode) || mode!=0 ||
            !inputRead(inputPed+ps::task_layout::intelligence,intel) || !intel ||
            !inputRead(intel,owner) || owner!=inputPed ||
            !inputRead(intel+ps::task_layout::manager+ps::task_layout::managerOwner,owner) || owner!=inputPed ||
            !inputRead(intel+ps::task_layout::manager,tasks) ||
            std::any_of(tasks.begin(),tasks.end(),[](auto task){return task!=0;}) ||
            !inputRead(ni::cameraHeading,liveView)) return false;
        if ((controls.current.right!=0 || controls.current.forward!=0) &&
            !(controls.current.buttons&grandmp::core::PlayerControls::walk)) return false;
        np::Controls adjusted=controls;
        if (!ni::cameraRelative(controls.previous,controls.viewHeading,liveView,adjusted.previous) ||
            !ni::cameraRelative(controls.current,controls.viewHeading,liveView,adjusted.current) ||
            !np::prepareOnFootPad(adjusted.previous,adjusted.current,adjusted.pad) || !inputWrite(adjusted.pad)) return false;
        inputNotice=controls.current.right!=0 ? InputNotice::turning :
            controls.current.forward!=0 ? InputNotice::walking : InputNotice::stopped;
        return true;
    }
    bool original(const np::Record& record) override {
        if (!inputContext(nullptr) || runtimeDisabled.load() || inputOriginalAttempted || record.entity.ped!=inputPed) return false;
        inputOriginalAttempted=true; // BEFORE the call: exceptions must never trigger a second call.
        reinterpret_cast<void(__thiscall*)(void*)>(ni::original)(reinterpret_cast<void*>(inputPed));
        ++inputProcessed;return true;
    }
    bool restore(const np::ContextImage& saved) noexcept override {
        inputRestoreFailed=true;
        if (saved.used!=sizeof(InputImage)) return false;
        InputImage before{},after{};std::memcpy(&before,saved.bytes.data(),sizeof(before));
        // Restoration must still run if a failure set the terminal flag while inside original.
        const bool restored=inputWrite(before.remotePad);
        const bool valid=inputImage(after) && restored && before.localPad==after.localPad &&
            before.focus==after.focus && before.heading==after.heading && before.weaponCamera==after.weaponCamera;
        inputRestoreFailed=!valid;return valid;
    }
} inputApi;

struct InputInvocation { void* ped;np::Result result=np::Result::indeterminate;bool complete=false; };
void inputInvoke(void* value) {
    auto& invocation=*static_cast<InputInvocation*>(value);
    invocation.result=nativeBinding->process(nativePlayers,grandmp::core::RemoteRoster::Time::clock::now(),
        {nativeBinding->key(),inputPed,inputSerial},inputApi);
    if (!inputOriginalAttempted) {
        // A refused input context still permits ORIGINAL once, but only after
        // successful restore (if captured), code and live ownership proof.
        // Unknown identity or failed restore must not execute an unsafe fallback.
        if (!inputRestoreFailed && inputRecord && nativeApi->authenticate(*inputRecord) &&
            inputCodeMatches() && codeMatches() && factoryCodeMatches()) {
            inputOriginalAttempted=true;
            reinterpret_cast<void(__thiscall*)(void*)>(ni::original)(invocation.ped);
        }
    }
    invocation.complete=true;
}
void inputFinish(void* value) noexcept {
    const auto& invocation=*static_cast<InputInvocation*>(value);
    inputActive=false;inputPed=0;inputRecord=nullptr;
    if (!invocation.complete || invocation.result!=np::Result::processed) {
        inputNotice=InputNotice::refused;runtimeDisabled.store(true);
    }
}
#if defined(GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD) && GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD == 1
void inputPassthrough(void* value) {
    auto& invocation=*static_cast<InputInvocation*>(value);
    inputOriginalAttempted=true;
    reinterpret_cast<void(__thiscall*)(void*)>(ni::original)(invocation.ped);
    invocation.complete=true;
}
#endif
__declspec(noinline) void __fastcall inputControl(void* ped,void*) {
    const auto caller=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
    // Alpha 0.8 retains its direct foreign/local passthrough. Alpha 0.9 proves
    // slot identities below; neither path changes local pad/focus/camera.
    if (GetCurrentThreadId()!=gameThread || !nativeBinding ||
#if !defined(GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD) || GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD != 1
        reinterpret_cast<std::uintptr_t>(ped)!=nativeBinding->entityAddress() ||
#endif
        !inputHook.installed() || runtimeDisabled.load()) {
        reinterpret_cast<void(__thiscall*)(void*)>(ni::original)(ped);return;
    }
    if (inputActive || phaseDispatchActive ||
        std::find(ni::callers.begin(),ni::callers.end(),caller)==ni::callers.end() ||
        inputSerial==UINT64_MAX) {
        runtimeDisabled.store(true);inputNotice=InputNotice::refused;
        reinterpret_cast<void(__thiscall*)(void*)>(ni::original)(ped);return;
    }
    inputActive=true;inputOriginalAttempted=false;inputRestoreFailed=false;inputRecord=nullptr;
    inputPed=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(ped));
#if defined(GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD) && GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD == 1
    // Alpha 0.9 only: the same HookCell now has explicit authenticated routing.
    // This is a control-phase read scope, NEVER permission to run the factory.
    nf::LocalIdentity local{};
    const auto localPed=nativeApi && nativeApi->localIdentity(local,np::Phase::original_process_control) ? local.ped : 0;
    const auto remotePed=nativeBinding->entityAddress();
    const bool remoteKnown=nativeBinding->authenticatesDispatchEntity(remotePed,inputApi);
    const auto target=ni::classifyDispatch(inputPed,localPed,remoteKnown ? remotePed : 0);
    if (target!=ni::DispatchTarget::remote_player) {
        InputInvocation invocation{ped,target==ni::DispatchTarget::unproven ? np::Result::ownership_lost : np::Result::processed};
        // Even an unproven target keeps the engine's own original dispatch once;
        // no remote controls/pose/lifecycle writes. Finally closes the scope on
        // exceptions; unproven identity also disables subsequent GrandMP work.
        np::withFinally(inputPassthrough,inputFinish,&invocation);return;
    }
#endif
    ++inputSerial;InputInvocation invocation{ped};
    np::withFinally(inputInvoke,inputFinish,&invocation);
}
std::uint32_t inputHookAddress() noexcept {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&inputControl));
}
bool installInputHook() noexcept {
#if defined(GRANDMP_CLEO_PLAYER_SYNC_BUILD) && GRANDMP_CLEO_PLAYER_SYNC_BUILD == 1
    // Same short-circuit order and same checks as below. Observe the FIRST
    // failing predicate; never call a refused guard a second time for logging.
    diagnosingInputInstall=true;
    const char* refusal=nullptr;
    if(!ni::executionApproved)refusal="input_hook.guard.execution";
    else if(!nativeContext(nullptr))refusal="input_hook.guard.context";
    else if(!nativeReady(nullptr))refusal="input_hook.guard.ready";
    else if(!inputCodeMatches())refusal="input_hook.guard.code";
    else if(!inputHook.install(reinterpret_cast<std::uint32_t*>(ni::cell),ni::original,inputHookAddress(),
        reinterpret_cast<void*>(0x400000),ni::CellPage::cleo_writable_rdata))refusal="input_hook.guard.cell";
    diagnosingInputInstall=false;
    if(refusal){inputInstallRefused(refusal);writeLog("failed","input_hook_install_terminal_no_retry");return false;}
#else
    if (!ni::executionApproved || !nativeContext(nullptr) || !nativeReady(nullptr) || !inputCodeMatches() ||
        !inputHook.install(reinterpret_cast<std::uint32_t*>(ni::cell),ni::original,inputHookAddress(),
            reinterpret_cast<void*>(0x400000)
#if defined(GRANDMP_CLEO_PLAYER_SYNC_BUILD) && GRANDMP_CLEO_PLAYER_SYNC_BUILD == 1
            ,ni::CellPage::cleo_writable_rdata
#endif
            )) {
        writeLog("failed","input_hook_install_terminal_no_retry");return false;
    }
#endif
    inputNotice=InputNotice::installed;writeLog("waiting","input_hook_installed_slot28_original_thiscall_pad1_only");return true;
}
void inputReport() noexcept {
    static auto last=InputNotice::none;
    if (inputNotice==last) return;
    last=inputNotice;char text[160]{};
    std::snprintf(text,sizeof(text),"input_replay=%u dispatches=%llu replayed_originals=%llu no_focus_or_camera_write",
        static_cast<unsigned>(last),static_cast<unsigned long long>(inputSerial),static_cast<unsigned long long>(inputProcessed));
    writeLog(last==InputNotice::refused ? "failed" : "waiting",text);
}
