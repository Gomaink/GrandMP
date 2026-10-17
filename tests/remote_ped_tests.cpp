#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../client/windows/tools/remote_ped.hpp"
#include "grandmp/windows/observer_network.hpp"
#include "../client/windows/tools/remote_ped_network.hpp"
#include <vector>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace rp = grandmp::windows::remote_ped;
namespace ps = grandmp::windows::ped_smoke;
void require(bool okay, const char* reason) { if (!okay) throw std::runtime_error(reason); }
struct FakeEngine {
    std::thread::id owner = std::this_thread::get_id();
    rp::Key key{};
    unsigned calls = 0, requests = 0, creations = 0, removals = 0, ownershipChecks = 0;
    bool owned = true, allocation = true, forbid = false, loaded = true, reenter = false, verbose = false;
    std::uintptr_t address = 0x1000;
    rp::Controller* controller = nullptr;
    rp::Intent intent{};
    void touch() { require(!forbid && owner == std::this_thread::get_id(), "engine call outside admitted thread / after ownership loss"); ++calls; }
    bool beginRemote(rp::Key value) { touch(); key = value; return true; }
    void log(ps::State state, const char*) {
        touch();
        if (verbose && (state == ps::State::ped_created || state == ps::State::removed))
            std::fwprintf(stdout, L"remote_native state=%hs id=%llu generation=%llu ped=%08lX\n", ps::name(state),
                static_cast<unsigned long long>(key.player), static_cast<unsigned long long>(key.generation), static_cast<unsigned long>(address));
    }
    bool requestModel() { touch(); ++requests; return true; }
    bool modelLoaded() { touch(); return loaded; }
    bool revalidateModel() { touch(); return loaded; }
    bool spawnPosition(ps::Position& p) { touch(); p = {1, 2, 3}; return true; }
    std::uintptr_t create(const ps::Position&) {
        touch(); ++creations;
        if (reenter) controller->tick(*this, intent, true, true, 3);
        return allocation ? address : 0;
    }
    bool owns(std::uintptr_t p) { touch(); ++ownershipChecks; return p == address && owned; }
    bool visible(std::uintptr_t) { touch(); return true; }
    bool remove(std::uintptr_t p) { touch(); require(p == address && owned, "unknown entity removal"); ++removals; return true; }
};
// Model the actual NativeEngine registry, not merely a permanent `owned=true`.
// Reference location is stable; cleanup unlinks it before RemovePed/destruction.
struct RegistryEngine final : FakeEngine {
    std::uintptr_t registeredPointer = 0;
    std::uintptr_t* link = nullptr;
    int reference = -1;
    unsigned tag = 1, cleanups = 0;
    bool poolLive = false;
    bool beginRemote(rp::Key value) {
        touch();
        if (registeredPointer || reference != -1) return false;
        key = value; return true;
    }
    std::uintptr_t create(const ps::Position& p) {
        const auto result = FakeEngine::create(p);
        require(!poolLive && !link && reference == -1, "registry empty before AddPed");
        registeredPointer = result; poolLive = result != 0;
        if (result) { link = &registeredPointer; reference = 0x100 | tag; }
        return result;
    }
    bool owns(std::uintptr_t p) {
        touch(); ++ownershipChecks;
        // Corresponds to pointer, live slot/tag, resolved handle, registered link.
        return owned && p && registeredPointer == p && poolLive &&
            reference == int(0x100 | tag) && link == &registeredPointer;
    }
    bool remove(std::uintptr_t p) {
        if (!owns(p)) return false;
        const auto victim = registeredPointer;
        // CleanUpOldReference(victim, &owned) unlinks, not destroys the PED.
        require(link == &registeredPointer && *link == victim, "cleanup reference identity");
        link = nullptr; ++cleanups;
        registeredPointer = 0; reference = -1;
        // Model RemovePed/destructor entry: registry must already be released.
        require(victim && !link && !registeredPointer && reference == -1, "released before RemovePed");
        poolLive = false; ++tag; ++removals; return true;
    }
};
struct StartupNetwork {
    bool startOkay = true, previewOkay = true, throws = false;
    unsigned starts = 0, previews = 0;
    bool start(std::uint16_t port) { ++starts; require(port == 7777, "loopback port"); return startOkay; }
    bool beginRemoteScenePreview() { ++previews; if (throws) throw std::runtime_error("injected"); return previewOkay; }
};
grandmp::core::RemotePose pose(std::uint64_t id = 42) { return {id, 1, 10, 20, 30}; }
void advance(rp::Controller& c, FakeEngine& e, rp::Intent i) {
    for (unsigned n = 0; n < 4; ++n) c.tick(e, i, true, true, n);
}
void unit(std::wstring_view which) {
    rp::IntentSink sink; rp::Controller c; FakeEngine e; e.controller = &c;
    if (which == L"registry") {
        RegistryEngine registry;
        auto step = [&](unsigned n) { c.tick(registry, sink.snapshot(), true, true, n); };
        sink.spawn(pose()); for (unsigned n=0;n<4;++n) step(n);
        const int firstHandle = registry.reference;
        require(c.state()==ps::State::ped_created && registry.link==&registry.registeredPointer, "A registry registered");
        sink.despawn(42); step(5);
        require(c.state()==ps::State::removed && !registry.registeredPointer && registry.reference==-1 &&
            !registry.link && registry.cleanups==1 && registry.removals==1, "A removed, registry released");
        sink.resetSession(); sink.spawn(pose(43)); for(unsigned n=6;n<10;++n) step(n);
        require(c.key().player==43 && c.state()==ps::State::ped_created && registry.creations==2 &&
            registry.reference!=firstHandle && registry.owns(c.ped()), "B admitted with new pool tag, even at reused address");
        // Engine cleanup may clear the registered pointer while leaving our old
        // handle. This is NOT a released registry and must never be reused.
        *registry.link=0; registry.link=nullptr; registry.poolLive=false;
        step(10); require(c.poisoned() && c.state()==ps::State::ownership_lost, "external reference clear terminal");
        require(!registry.beginRemote({99,44}) && registry.reference!=-1, "cleared pointer alone cannot admit reuse");
        sink.despawn(43); sink.spawn(pose(44)); step(11);
        require(registry.creations==2 && registry.removals==1, "no deletion/replacement of unknown identity");
        return;
    }
    if (which == L"receiver") {
        for (unsigned mode=0;mode<5;++mode) {
            StartupNetwork network; std::vector<rp::ReceiverState> stages; std::vector<std::string_view> reasons;
            network.startOkay=mode!=1; network.previewOkay=mode!=2; network.throws=mode==3;
            const bool okay=rp::initializeReceiver(network, mode==0 ? L"192.0.2.1:7777" : L"127.0.0.1:7777",
                [&](rp::ReceiverState state,const char* reason) { stages.push_back(state); reasons.push_back(reason); });
            require(okay==(mode==4), "receiver result distinct from thread creation");
            if(mode==0) require(!network.starts && reasons.back()=="remote_receiver_failed_endpoint_invalid", "endpoint rejected before socket");
            if(mode==1) require(!network.previews && reasons.back()=="remote_receiver_failed_observer_start", "start failure specific");
            if(mode==2) require(reasons.back()=="remote_receiver_failed_preview_start", "preview failure specific");
            if(mode==3) require(reasons.back()=="remote_receiver_failed_initialization_exception", "exception failure specific");
            if(mode==4) require(stages==std::vector{rp::ReceiverState::endpoint_validated,rp::ReceiverState::observer_initialized,rp::ReceiverState::ready}, "ordered success states");
            else require(stages.back()==rp::ReceiverState::failed, "failed never ready");
        }
        return;
    }
    if (which == L"cancel") {
        sink.spawn(pose()); sink.despawn(42); advance(c, e, sink.snapshot());
        require(!e.calls && !e.creations, "leave before first phase cancels intent");
        sink.spawn(pose(43)); c.tick(e, sink.snapshot(), true, true, 1);
        require(e.requests == 1, "new identity can start model request");
        sink.despawn(43); advance(c, e, sink.snapshot());
        require(!e.creations && c.state() == ps::State::removed, "leave while streaming cancels spawn");
        return;
    }
    if (which == L"thread") {
        sink.spawn(pose());
        std::thread worker([&] { c.tick(e, sink.snapshot(), false, true, 0); }); worker.join();
        c.tick(e, sink.snapshot(), true, false, 0);
        require(!e.calls, "no engine call on network thread/loading");
        advance(c, e, sink.snapshot()); require(e.creations == 1, "native phase creates later"); return;
    }
    if (which == L"invalid") {
        sink.spawn(pose(0)); auto invalid = pose(); invalid.x = std::numeric_limits<float>::infinity(); sink.spawn(invalid);
        invalid = pose(); invalid.sequence = 0; sink.spawn(invalid);
        advance(c,e,sink.snapshot()); require(!e.calls, "invalid identity/pose never publishes join"); return;
    }
    sink.resetSession(); sink.spawn(pose()); e.intent = sink.snapshot();
    if (which == L"failure") e.allocation = false;
    if (which == L"reentry") e.reenter = true;
    advance(c,e,sink.snapshot());
    require(e.creations == 1, "one native creation attempt");
    if (which == L"code") {
        c.disable();
        require(c.poisoned() && c.state()==ps::State::failed && c.ped()==e.address, "identity failure retains diagnostic identity");
        e.forbid=true; sink.despawn(42); sink.spawn(pose(43));
        for(unsigned n=0;n<1000;++n) c.tick(e,sink.snapshot(),true,true,n+5);
        require(e.creations==1 && !e.removals, "no native queries or cleanup after code failure");
    } else if (which == L"join" || which == L"reentry") {
        for (unsigned n=0;n<1000;++n) { sink.spawn(pose()); c.tick(e,sink.snapshot(),true,true,n+5); }
        require(e.creations == 1 && c.ped() == 0x1000 && c.key().player == 42, "duplicate join/reentry no second PED");
    } else if (which == L"limit") {
        sink.spawn(pose(43)); advance(c,e,sink.snapshot());
        require(sink.snapshot().key.player == 42 && e.creations == 1, "one remote slot");
        auto updated=pose(); updated.sequence=2; updated.x=88; sink.move(updated);
        advance(c,e,sink.snapshot());
        require(sink.snapshot().pose.x==88 && e.creations==1 && !e.removals, "pose recorded, no physical move/task");
    } else if (which == L"leave") {
        sink.despawn(99); advance(c,e,sink.snapshot()); require(!e.removals,"leave must match remote identity");
        sink.despawn(42); advance(c,e,sink.snapshot());
        require(e.removals==1 && c.state()==ps::State::removed,"matching leave removes once");
    } else if (which == L"stale") {
        // Exercise the consumer's generation guard even for an equal numeric
        // ID. The production v1 producer conservatively refuses that ID reuse.
        auto oldLeave = sink.snapshot(); oldLeave.present = false; ++oldLeave.revision;
        auto newSession = oldLeave; newSession.present = true; ++newSession.revision; ++newSession.key.generation;
        advance(c,e,newSession);
        require(e.creations==2 && e.removals==1 && c.key().player==42 && c.key().generation==newSession.key.generation,
            "new generation replaces binding even with equal numeric ID");
        c.tick(e,oldLeave,true,true,9);
        require(e.removals==1 && c.state()==ps::State::ped_created,"old generation leave cannot remove replacement");
    } else if (which == L"reconnect") {
        sink.despawn(42); const auto oldLeave=sink.snapshot(); sink.resetSession(); sink.spawn(pose(43));
        advance(c,e,sink.snapshot());
        require(e.creations==2 && e.removals==1 && c.key().player==43,"cleanup before new generation allocation");
        c.tick(e,oldLeave,true,true,8); sink.despawn(42); advance(c,e,sink.snapshot());
        require(e.removals==1 && c.state()==ps::State::ped_created,"obsolete intent/leave cannot remove new generation");
        sink.despawn(43); advance(c,e,sink.snapshot()); sink.resetSession(); sink.spawn(pose(43));
        advance(c,e,sink.snapshot()); require(e.creations==2,"ambiguous v1 reused identity is refused");
    } else if (which == L"ownership") {
        e.owned=false; c.tick(e,sink.snapshot(),true,true,5);
        require(c.poisoned() && c.state()==ps::State::ownership_lost,"ownership loss terminal");
        e.forbid=true; sink.despawn(42); sink.spawn(pose(43));
        for (unsigned n=0;n<1000;++n) c.tick(e,sink.snapshot(),true,true,n+6);
        require(!e.removals && e.creations==1,"unknown entity never dereferenced, removed or replaced");
    } else if (which == L"failure") {
        for (unsigned n=0;n<1000;++n) { sink.spawn(pose()); c.tick(e,sink.snapshot(),true,true,n+5); }
        require(c.state()==ps::State::failed && e.creations==1 && !e.removals,"failed spawn is not retried");
    } else if (which == L"isolation") {
        ps::Experiment local; FakeEngine localEngine; localEngine.address=0x2000;
        for(unsigned n=0;n<4;++n) local.tick(localEngine,true,true,false,n);
        sink.despawn(42); advance(c,e,sink.snapshot());
        require(e.removals==1 && !localEngine.removals && local.ped()==0x2000,"remote leave cannot remove local F6 entity");
        local.tick(localEngine,true,true,true,7);
        require(localEngine.removals==1 && e.removals==1,"local F8 remains independent");
    } else throw std::runtime_error("unknown case");
}
void loader(const wchar_t* path, bool enabled = false) {
    require(!rp::physicalExecutionApproved && !ps::physicalExecutionApproved,"remote build closed");
    SetEnvironmentVariableW(L"GRANDMP_PED_SMOKE_ENABLE",L"1");
    SetEnvironmentVariableW(L"GRANDMP_REMOTE_PED_PHYSICAL_BUILD",L"1");
    SetEnvironmentVariableW(L"GRANDMP_REMOTE_PED_SERVER",L"127.0.0.1:7777");
    HMODULE module=LoadLibraryW(path); require(module!=nullptr,"load closed remote ASI in disposable host");
    using Query=unsigned(__cdecl*)(); using Entry=void(__cdecl*)();
    auto status=reinterpret_cast<Query>(GetProcAddress(module,"GrandMPPedSmokeStatus"));
    auto count=reinterpret_cast<Query>(GetProcAddress(module,"GrandMPPedSmokeCallbackCount"));
    auto initialize=reinterpret_cast<Entry>(GetProcAddress(module,"InitializeASI"));
    require(status && count && initialize && status()==0 && count()==0,"loader lock inert");
    initialize(); const auto until=GetTickCount64()+5000;
    while(status()==1 && GetTickCount64()<until) Sleep(1);
    const unsigned expected = enabled ? 4 : 2;
    require(status()==expected && count()==0,"closed gate or enabled identity refusal before dispatch");
    initialize(); initialize();
    require(status()==expected && count()==0,"no retry or callback after refusal");
    std::ifstream log(std::filesystem::path(path).parent_path()/"GrandMPRemotePedExperimental.log");
    const auto pid=std::string(" pid=")+std::to_string(GetCurrentProcessId())+" ";
    bool diagnosed=false;
    for(std::string line;std::getline(log,line);) {
        if(line.find(pid)==std::string::npos) continue;
        if(line.find(enabled ? "reason=hoodlum_identity_path_or_live_signature" :
            "reason=installation_blocked_physical_execution_review_missing")!=std::string::npos) diagnosed=true;
        require(line.find("callback_executed")==std::string::npos &&
            line.find("remote_network_thread_")==std::string::npos &&
            line.find("model_requested")==std::string::npos &&
            line.find("post_population_phase_installed")==std::string::npos,"identity refusal precedes engine/hook/receiver");
    }
    require(diagnosed,"actual loaded artifact must log the expected refusal for this PID");
    require(FreeLibrary(module)!=FALSE,"unload disposable module reference");
}
int loopback(std::uint16_t port) {
    rp::IntentSink sink; rp::Controller controller; FakeEngine e; e.verbose=true;
    std::atomic<bool> stop{false}, ready{false};
    std::thread worker([&] {
        grandmp::windows::ObserverNetwork network; network.setRemoteSceneSink(&sink);
        if(network.start(port) && network.beginRemoteScenePreview()) {
            ready.store(true);
            while(!stop.load()) { network.tick(std::nullopt); Sleep(10); }
        }
        network.stop();
    });
    const auto started=GetTickCount64();
    while(GetTickCount64()-started<4500) { controller.tick(e,sink.snapshot(),true,true,GetTickCount64()); Sleep(10); }
    stop.store(true); worker.join(); advance(controller,e,sink.snapshot());
    std::fwprintf(stdout,L"remote_loopback creates=%u removals=%u ready=%u owner_only=1\n",e.creations,e.removals,unsigned(ready.load()));
    return ready.load() && e.creations==1 && e.removals==1 && !controller.poisoned() ? 0:1;
}
int wmain(int argc,wchar_t** argv) {
    if(argc<2) return 2;
    try {
        const std::wstring_view which=argv[1];
        if(which==L"loopback" && argc==3) return loopback(static_cast<std::uint16_t>(std::wcstoul(argv[2],nullptr,10)));
        if((which==L"loader" || which==L"loader_enabled") && argc==3) loader(argv[2],which==L"loader_enabled"); else unit(which);
        std::fwprintf(stdout,L"remote_ped %ls PASS\n",argv[1]);return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"remote_ped failure: %s\n",e.what());return 1; }
}
