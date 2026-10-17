// Included only by the separate remote ASI, after the reviewed NativeEngine.
// No IPC and no new wire protocol. ObserverNetwork/RemoteRoster/Scene are reused.
namespace rp = grandmp::windows::remote_ped;
rp::IntentSink remoteIntents;
#if defined(GRANDMP_REMOTE_PED_MOVE_VARIANT)
namespace rm = grandmp::windows::remote_ped_move;
rm::Controller remoteController;
#else
rp::Controller remoteController;
#endif
std::atomic<rp::ReceiverState> remoteReceiver{rp::ReceiverState::idle};
struct RemoteEngine final : NativeEngine {
    rp::Key key{};
    bool beginRemote(rp::Key value) {
        // Reference storage stays at the same address throughout the process.
        if (owned || reference != -1) {
            writeLog("failed", "remote_registry_not_empty_no_reuse", creationResult);
            return false;
        }
        key = value;
        creationResult = 0;
#if defined(GRANDMP_REMOTE_PED_MOVE_VARIANT)
        spawnAnchorValid = false;
#endif
        log(ps::State::waiting, "remote_join_intent_position_not_applied");
        return true;
    }
#if defined(GRANDMP_REMOTE_PED_MOVE_VARIANT)
    ps::Position spawnAnchor{}, remoteDestination{};
    bool spawnAnchorValid = false;
    grandmp::core::RemotePose received{};
    std::uint64_t receivedRevision = 0;
    std::uintptr_t create(const ps::Position& position) {
        const auto result = NativeEngine::create(position);
        spawnAnchor = position; spawnAnchorValid = result != 0;
        return result;
    }
    bool initialPosition(ps::Position& position) {
        if (!spawnAnchorValid || !owns(creationResult)) return false;
        position = spawnAnchor; return true;
    }
    void recordRemoteSample(const grandmp::core::RemotePose& pose, std::uint64_t revision) {
        received = pose; receivedRevision = revision;
    }
    void prepareRemoteMove(ps::Position candidate) { remoteDestination = candidate; }
    const char* moveDestination(std::uintptr_t ped, ps::Position origin, ps::Position& destination) {
        if (!owns(ped)) return "ownership_unproven";
        struct Probe {
            void* ignore;
            float ground(float x,float y,float z,bool& found) {
                return reinterpret_cast<float(__cdecl*)(float,float,float,bool*,void**)>(ps::findGround)(x,y,z,&found,nullptr);
            }
            std::uintptr_t sphere(ps::Position center) {
                using Sphere=void*(__cdecl*)(ps::Position,float,void*,bool,bool,bool,bool,bool,bool);
                return reinterpret_cast<std::uintptr_t>(reinterpret_cast<Sphere>(ps::testSphere)(center,0.35f,ignore,true,true,true,true,true,false));
            }
        } probe{owned};
        if (const char* reason=rm::findDestination(probe,origin,remoteDestination,destination)) return reason;
        void* cj=nullptr; ps::Position playerPosition{};
        if (!playerReady(cj,&playerPosition) || ps::distance2D(destination,playerPosition)>12.f || std::abs(destination.z-playerPosition.z)>3.f)
            return "destination_cj_proximity_limit";
        return nullptr;
    }
    void moveLog(const char* state, const char* reason) {
        char event[64]{}, detail[208]{};
        if (std::strncmp(state,"remote_",7)) { std::snprintf(event,sizeof(event),"remote_%s",state); state=event; }
        char facts[112]{};
        if (!std::strcmp(state,"remote_move_anchor_set")) {
            std::snprintf(facts,sizeof(facts),"remote=(%.2f,%.2f,%.2f) local=(%.2f,%.2f,%.2f)",
                received.x,received.y,received.z,spawnAnchor.x,spawnAnchor.y,spawnAnchor.z); reason=facts;
        } else if (!std::strcmp(state,"remote_move_requested")) {
            std::snprintf(facts,sizeof(facts),"UDP_one_walk target=(%.2f,%.2f,%.2f)",remoteDestination.x,remoteDestination.y,remoteDestination.z); reason=facts;
        }
        std::snprintf(detail,sizeof(detail),"remote=%llu generation=%llu seq=%u sample=%llu %s",
            static_cast<unsigned long long>(key.player),static_cast<unsigned long long>(key.generation),received.sequence,
            static_cast<unsigned long long>(receivedRevision),reason);
        writeLog(state,detail,creationResult);
    }
#endif
    void log(ps::State state, const char* reason) {
        char detail[208]{};
        std::snprintf(detail, sizeof(detail), "remote=%llu generation=%llu %s",
            static_cast<unsigned long long>(key.player), static_cast<unsigned long long>(key.generation), reason);
        writeLog(ps::name(state), detail, creationResult);
    }
} remoteEngine; // Distinct registry/reference from the local F6 engine.

DWORD WINAPI remoteNetwork(void*) {
    writeLog("waiting", "remote_network_thread_entered");
    grandmp::windows::ObserverNetwork network;
    network.setRemoteSceneSink(&remoteIntents);
    wchar_t endpoint[64]{};
    const DWORD length = GetEnvironmentVariableW(L"GRANDMP_REMOTE_PED_SERVER", endpoint, 64);
    const auto report = [](rp::ReceiverState state, const char* reason) noexcept {
        remoteReceiver.store(state);
        if (state == rp::ReceiverState::failed) runtimeDisabled.store(true);
        writeLog(state == rp::ReceiverState::failed ? "failed" : "waiting", reason);
    };
    if (!rp::initializeReceiver(network, length && length < 64 ? std::wstring_view(endpoint, length) : std::wstring_view{}, report)) {
        remoteIntents.resetSession();
        network.stop();
        return 0;
    }
    // The installer has already PINned this ASI. Process termination owns this
    // worker's lifetime; it never publishes a native pointer or calls the engine.
    for (;;) { network.tick(std::nullopt); Sleep(20); }
}
bool startRemoteNetwork() {
    if (!rp::physicalExecutionApproved || !hookPublished.load()) return false;
    remoteReceiver.store(rp::ReceiverState::starting);
    const HANDLE worker = CreateThread(nullptr, 0, remoteNetwork, nullptr, 0, nullptr);
    if (!worker) {
        const DWORD error = GetLastError();
        remoteReceiver.store(rp::ReceiverState::failed);
        runtimeDisabled.store(true);
        char reason[96]{};
        std::snprintf(reason, sizeof(reason), "remote_network_thread_create_failed_win32=%lu", error);
        writeLog("failed", reason);
        return false;
    }
    writeLog("waiting", "remote_network_thread_created_not_receiver_ready");
    CloseHandle(worker);
    return true;
}
