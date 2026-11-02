// Included only by GrandMPNativePlayerExperimental. No civil AddPed/GoToPoint path.
namespace np=grandmp::windows::remote_player_binding;
namespace nf=np::second_player;
namespace nr=grandmp::windows::remote_ped;
grandmp::core::RemotePlayerController nativePlayers;
std::atomic<nr::ReceiverState> nativeReceiver{nr::ReceiverState::idle};
np::Binding<true>* nativeBinding=nullptr; // Resident; prepared on the admitted window/game thread.
std::optional<nf::Api> nativeApi;
bool nativeFinished=false,nativeF8Down=false;
#if defined(GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD) && GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD == 1
grandmp::core::LocalPlayerSnapshots localSnapshots; // Resident, like nativePlayers.
std::atomic<bool> localSourceStopped{false};
np::LocalKinematics localKinematics; // Native-thread private; no native addresses.
std::optional<nf::LocalIdentity> localIdentity;
std::uint64_t localCaptureMs=0;
#endif
#if defined(GRANDMP_NATIVE_INPUT_REPLAY_BUILD) && GRANDMP_NATIVE_INPUT_REPLAY_BUILD == 1
namespace ni=np::replay;
ni::HookCell inputHook;
bool inputActive=false,inputOriginalAttempted=false;
std::uint32_t inputPed=0;
std::uint64_t inputSerial=0;
bool inputContext(void*) noexcept;
bool inputCodeMatches() noexcept;
std::uint32_t inputHookAddress() noexcept;
#endif

bool nativeContext(void*) noexcept {
    return phaseDispatchActive && preparation.load()==3 && !runtimeDisabled.load() &&
        gameThread && gameThread==GetCurrentThreadId()
#if defined(GRANDMP_NATIVE_INPUT_REPLAY_BUILD) && GRANDMP_NATIVE_INPUT_REPLAY_BUILD == 1
        && !inputActive // Lifecycle writes never borrow a nested original-control scope.
#endif
        ;
}
bool factoryPage(std::uint32_t at,std::size_t size,ps::CodeIdentityFailure& failure) noexcept {
    if (!ps::factoryHoodlumWindow(at,size)) return ps::identityPage(failure,at,size,PAGE_EXECUTE_READ);
    // The exact admitted .HOODLUM section is RWX (E0000020). SEC_IMAGE maps
    // untouched writable image pages as EXECUTE_WRITECOPY (80), not necessarily
    // EXECUTE_READWRITE (40). Both retain image/base/span and exact-byte checks.
    // No other section or protection (including GUARD modifiers) is admitted.
    return ps::identityPage(failure,at,size,PAGE_EXECUTE_READWRITE,PAGE_EXECUTE_WRITECOPY);
}
bool factoryCodeMatches(ps::CodeIdentityFailure* detail = nullptr) noexcept {
    ps::CodeIdentityFailure local{};
    auto& failure=detail ? *detail : local;
    failure={};failure.function="factoryCodeMatches";
    for (const auto region:ps::factoryCodeWindows) {
        std::array<std::byte,0x400> current{};
        const auto* expected=diskBytes(region.address,region.size);
        if (!expected) return ps::identityRefuse(failure,"disk_mapping_failed",region.address,region.size);
        if (region.size>current.size()) return ps::identityRefuse(failure,"buffer_capacity",region.address,region.size);
        if (!factoryPage(region.address,region.size,failure) ||
            !ps::identityRead(failure,region.address,current.data(),region.size) ||
            !ps::identityBytes(failure,region.address,current.data(),expected,region.size)) return false;
    }
    for (const auto region:std::array{ps::CodeRegion{ps::playerVtable,0x100},ps::CodeRegion{nf::factory::defaultTaskVtable,44}}) {
        const auto* expected=diskBytes(region.address,region.size);std::array<std::byte,0x100> current{};
        if (!expected) return ps::identityRefuse(failure,"disk_mapping_failed",region.address,region.size);
#if defined(GRANDMP_NATIVE_INPUT_REPLAY_BUILD) && GRANDMP_NATIVE_INPUT_REPLAY_BUILD == 1
        std::array<std::byte,0x100> installed{};
        if (region.address==ps::playerVtable && inputHook.installed()) {
            std::uint32_t diskOriginal=0;
            std::memcpy(&diskOriginal,expected+0x28,4);
            if (diskOriginal!=ni::original) return ps::identityRefuse(failure,"disk_original_dispatch",ni::cell,4);
            std::memcpy(installed.data(),expected,region.size);
            const auto replacement=inputHookAddress();std::memcpy(installed.data()+0x28,&replacement,4);
            expected=installed.data(); // Only our published cell differs; every other byte remains exact.
        }
#endif
        if (!ps::identityPage(failure,region.address,region.size,PAGE_READONLY) ||
            !ps::identityRead(failure,region.address,current.data(),region.size) ||
            !ps::identityBytes(failure,region.address,current.data(),expected,region.size)) return false;
    }
    return true;
}
bool nativeReady(void*) noexcept {
    void* cj=nullptr;
    return (nativeContext(nullptr)
#if defined(GRANDMP_NATIVE_INPUT_REPLAY_BUILD) && GRANDMP_NATIVE_INPUT_REPLAY_BUILD == 1
        || inputContext(nullptr)
#endif
        ) && hookPublished.load() && codeMatches() && factoryCodeMatches() &&
        playerReady(cj) && streamingReady();
}
struct NativeSpawnProbe : SpawnProbe {
    float floor=0;
    bool player(ps::Position& position,ps::Position& right) { return playerReady(cj,&position,&right); }
    std::uintptr_t sphere(ps::Position target) {
        using Sphere=void*(__cdecl*)(ps::Position,float,void*,bool,bool,bool,bool,bool,bool);
        return reinterpret_cast<std::uintptr_t>(reinterpret_cast<Sphere>(ps::testSphere)(target,.35f,cj,true,true,true,true,true,false));
    }
    template<class... Args> void trace(const char*,Args...) {} // Values only in the factory log.
    float ground(float x,float y,float z,bool& found) {
        floor=SpawnProbe::ground(x,y,z,found);return floor;
    }
};
bool nativeSpawn(void*,ps::Position& ground) noexcept {
    NativeSpawnProbe probe;ps::Position centre{};
    if (!ps::findSpawnPosition(probe,centre)) return false;
    ground=centre;ground.z=probe.floor;return ps::finite(ground);
}
nf::WindowsAccess nativeAccess({nullptr,nativeContext,nativeReady,nativeSpawn
#if defined(GRANDMP_NATIVE_INPUT_REPLAY_BUILD) && GRANDMP_NATIVE_INPUT_REPLAY_BUILD == 1
    ,inputContext
#endif
});
#if defined(GRANDMP_NATIVE_INPUT_REPLAY_BUILD) && GRANDMP_NATIVE_INPUT_REPLAY_BUILD == 1
#include "native_player_input_runtime.inl"
#endif
#if defined(GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD) && GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD == 1
#include "local_player_capture_runtime.inl"
#endif
bool prepareNativeBinding() noexcept {
    nativeBinding=np::Binding<true>::prepare(std::this_thread::get_id());
    if (!nativeBinding) return false;
    nativeApi.emplace(nativeAccess,std::this_thread::get_id());return true;
}
DWORD WINAPI nativeNetwork(void*) {
    grandmp::windows::ObserverNetwork network;
    network.setRemotePlayerController(&nativePlayers);
    wchar_t endpoint[64]{};
    const DWORD length=GetEnvironmentVariableW(L"GRANDMP_REMOTE_PED_SERVER",endpoint,64);
    const auto report=[](nr::ReceiverState state,const char* reason) noexcept {
        nativeReceiver.store(state);
        if (state==nr::ReceiverState::failed) runtimeDisabled.store(true);
        writeLog(state==nr::ReceiverState::failed ? "failed" : "waiting",reason);
    };
    if (!nr::initializeReceiver(network,length && length<64 ? std::wstring_view(endpoint,length) : std::wstring_view{},report
#if defined(GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD) && GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD == 1
        ,true // Alpha 0.9 only; Alpha 0.8 keeps its strict loopback initializer.
#endif
        )) return 0;
    // Module is PINned by the original bootstrap. Only values cross this seam.
    // No native calls, entity pointers or context changes occur on this worker.
#if defined(GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD) && GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD == 1
    std::uint64_t sourceLoggedSession=0;
#endif
    try { while (!runtimeDisabled.load()
#if defined(GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD) && GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD == 1
        && !localSourceStopped.load()
#endif
    ) {
#if defined(GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD) && GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD == 1
        if (!network.tickLocal(localSnapshots)) {
            report(nr::ReceiverState::failed,"local_snapshot_send_failed_terminal");break;
        }
        if (network.sentSequence() && network.sessionId()!=sourceLoggedSession) {
            sourceLoggedSession=network.sessionId();char reason[160]{};
            std::snprintf(reason,sizeof(reason),"local_snapshot_sender_ready session=%llu seq=%u source=CJ_pad0_pre_world",
                static_cast<unsigned long long>(sourceLoggedSession),network.sentSequence());
            writeLog("waiting",reason);
        }
#else
        network.tick(std::nullopt);
#endif
        Sleep(20);
    } }
    catch (...) { report(nr::ReceiverState::failed,"native_player_receiver_exception_terminal"); }
#if defined(GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD) && GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD == 1
    localSnapshots.session(0);
    char summary[160]{};
    std::snprintf(summary,sizeof(summary),"local_snapshot_sender_ended session=%llu last_seq=%u",
        static_cast<unsigned long long>(network.sessionId()),network.sentSequence());
    writeLog("waiting",summary);
#endif
    network.stop();return 0;
}
bool startNativeNetwork() {
    const HANDLE worker=CreateThread(nullptr,0,nativeNetwork,nullptr,0,nullptr);
    const DWORD error=worker ? ERROR_SUCCESS : GetLastError();
    if (!worker) { nativeReceiver.store(nr::ReceiverState::failed);writeLog("failed","native_player_receiver_thread_failed");(void)error;return false; }
    CloseHandle(worker);writeLog("waiting","native_player_receiver_thread_created_not_ready");return true;
}
void nativeTick(bool gameplay) {
#if defined(GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD) && GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD == 1
    captureLocalPlayer(gameplay); // Same pre-world callback, before any remote pad scope.
#endif
#if defined(GRANDMP_NATIVE_INPUT_REPLAY_BUILD) && GRANDMP_NATIVE_INPUT_REPLAY_BUILD == 1
    inputReport();
#endif
    if (!nativeBinding || !nativeApi || nativeFinished || nativeReceiver.load()!=nr::ReceiverState::ready || !gameplay) return;
    DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    const bool down=pid==GetCurrentProcessId() && (GetAsyncKeyState(VK_F8)&0x8000)!=0;
    const bool remove=removalRequested.exchange(false) || (down && !nativeF8Down);nativeF8Down=down;
#if defined(GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD) && GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD == 1
    if (remove) { localSnapshots.invalidate();localSourceStopped.store(true); }
#endif
    const auto before=nativeBinding->key();
    np::Api& api=
#if defined(GRANDMP_NATIVE_INPUT_REPLAY_BUILD) && GRANDMP_NATIVE_INPUT_REPLAY_BUILD == 1
        inputApi;
#else
        *nativeApi;
#endif
    const auto result=remove ? nativeBinding->requestRemoval(api) :
        nativeBinding->consume(nativePlayers,grandmp::core::RemoteRoster::Time::clock::now(),api);
    if (result==np::Result::created) {
        const auto key=nativeBinding->key();char reason[224]{};
        std::snprintf(reason,sizeof(reason),
#if defined(GRANDMP_NATIVE_INPUT_REPLAY_BUILD) && GRANDMP_NATIVE_INPUT_REPLAY_BUILD == 1
            "slot=1 pool_reference_world_default_task_verified_input_hook_pending remote=%llu generation=%llu",
#else
            "slot=1 pool_reference_world_default_task_verified_no_replay remote=%llu generation=%llu",
#endif
            static_cast<unsigned long long>(key.player),static_cast<unsigned long long>(key.generation));
        writeLog("player_created",reason);
#if defined(GRANDMP_NATIVE_INPUT_REPLAY_BUILD) && GRANDMP_NATIVE_INPUT_REPLAY_BUILD == 1
        if (!installInputHook()) runtimeDisabled.store(true);
#else
        writeLog("waiting","native_input_replay_blocked camera_context_and_original_dispatch_scope_unproven");
#endif
    }
    else if (result==np::Result::removed) {
#if defined(GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD) && GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD == 1
        localSnapshots.invalidate();localSourceStopped.store(true);
#endif
        nativeFinished=true;char reason[256]{};
        std::snprintf(reason,sizeof(reason),"slot=1 release_pool_and_group_verified_no_respawn remote=%llu generation=%llu cause=%s",
            static_cast<unsigned long long>(before.player),static_cast<unsigned long long>(before.generation),nativeBinding->retirementReason());
        writeLog("removed",reason);
    }
    else if (result==np::Result::blocked_contract || result==np::Result::indeterminate || result==np::Result::ownership_lost) {
        static bool reported=false;
        if (!reported) {
            reported=true;char reason[128]{};
            std::snprintf(reason,sizeof(reason),"slot=1 factory_reason=%u construction=%u no_retry_no_unknown_cleanup",
                static_cast<unsigned>(nativeApi->reason()),static_cast<unsigned>(nativeApi->construction()));
            writeLog("failed",reason);
        }
        // An explicit F8 may still clean an authenticated partial construction.
    }
    if (result==np::Result::created || result==np::Result::updated) {
        static auto last=np::InputDisposition::neutral_missing;
        const auto current=nativeBinding->inputDisposition();
        if (current!=last) {
            last=current;
            writeLog("waiting",current==np::InputDisposition::fresh ?
#if defined(GRANDMP_NATIVE_INPUT_REPLAY_BUILD) && GRANDMP_NATIVE_INPUT_REPLAY_BUILD == 1
                "remote_controls_fresh_original_dispatch_only" :
#else
                "remote_controls_fresh_replay_still_blocked" :
#endif
                "remote_controls_expired_neutral_session_not_removed");
        }
    }
}
