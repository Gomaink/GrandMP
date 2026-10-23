#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "grandmp/windows/observer_network.hpp"
#include <charconv>
#include <cstdio>
#include <string_view>
#include <map>
#include <set>

// Offline/loopback inspection host ONLY. No DLL loading, CPad, entity creation,
// native ProcessControl, SetTask or reconciliation writes are available here.
int main(int argc,char** argv) {
    if (argc!=5 || std::string_view(argv[1])!="--port" || std::string_view(argv[3])!="--duration-ms") return 2;
    const auto number=[](const char* text,unsigned& value) {
        const std::string_view s(text); const auto r=std::from_chars(s.data(),s.data()+s.size(),value);
        return r.ec==std::errc{} && r.ptr==s.data()+s.size();
    };
    unsigned port=0,duration=0;
    if (!number(argv[2],port) || !port || port>65535 || !number(argv[4],duration) || !duration || duration>60000) return 2;
    grandmp::core::RemotePlayerController controller;
    grandmp::windows::ObserverNetwork network;
    network.setRemotePlayerController(&controller);
    if (!network.start(static_cast<std::uint16_t>(port)) || !network.beginRemoteScenePreview()) return 3;
    std::puts("native_binding=blocked CPlayerPed_context_contract_missing; receive-only controller host");
    std::map<std::uint64_t,std::uint32_t> shown;
    std::set<std::pair<std::uint64_t,std::uint64_t>> ended;
    const auto start=GetTickCount64();
    while (GetTickCount64()-start<duration) {
        network.tick(std::nullopt);
        grandmp::core::RemotePlayerFrames frames;
        if (controller.tryRead(grandmp::core::RemoteRoster::Clock::now(),frames)) {
            for (std::size_t i=0;i<frames.endedCount;++i) {
                const auto& e=frames.ended[i];
                if (!ended.insert({e.key.player,e.key.generation}).second) continue;
                std::printf("controller_end remote=%llu generation=%llu reason=%s\n",
                    static_cast<unsigned long long>(e.key.player),static_cast<unsigned long long>(e.key.generation),
                    grandmp::core::remoteEndName(e.reason));
                std::fflush(stdout);
            }
            for (std::size_t i=0;i<frames.count;++i) {
                const auto& f=frames.players[i];
                if (!f.sequence || shown[f.key.player]==f.sequence) continue;
                shown[f.key.player]=f.sequence;
                std::printf("controller_input remote=%llu generation=%llu seq=%u right=%.1f forward=%.1f heading=%.3f native=blocked\n",
                    static_cast<unsigned long long>(f.key.player),static_cast<unsigned long long>(f.key.generation),
                    f.sequence,f.reference.controls.right,f.reference.controls.forward,f.reference.heading);
                std::fflush(stdout);
            }
        }
        Sleep(10);
    }
    network.stop();
    return 0;
}
