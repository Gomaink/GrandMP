#pragma once
// Test host for the EXISTING capture body: simulated reads only, never GTA memory.
#include "../client/windows/tools/local_player_capture.hpp"
#include "../client/windows/tools/native_second_player.hpp"
#include "../client/windows/tools/native_player_replay.hpp"
#include <map>
#include <optional>
#include <thread>
#include <vector>
namespace capture_fixture {
namespace np=grandmp::windows::remote_player_binding;
namespace nf=np::second_player;
namespace ps=grandmp::windows::ped_smoke;
namespace ni=np::replay;
struct Host {
    static constexpr std::uint32_t ped=0x10000,matrixAddress=0x20000; // Fixture addresses, not a process mapping.
    const std::thread::id owner=std::this_thread::get_id();
    grandmp::core::LocalPlayerSnapshots localSnapshots;
    np::LocalKinematics localKinematics;
    std::optional<nf::LocalIdentity> localIdentity;
    std::atomic<bool> localSourceStopped{false};
    std::atomic<unsigned> wrongThread{0};
    std::uint64_t clockMs=1000,localCaptureMs=0;
    bool admitted=true,focused=true,identityValid=true,identityChanged=false,failed=false;
    std::uintptr_t refusedAddress=0;
    unsigned reads=0,identityReads=0;
    std::map<std::uintptr_t,std::vector<std::byte>> memory;
    struct IdentityReader {
        Host& host;
        bool localIdentity(nf::LocalIdentity& out) noexcept {
            ++host.identityReads;
            if(!host.identityValid || !host.nativeContext(nullptr))return false;
            out={ped,ped,0x30000,host.identityChanged ? 258:257,10};return true;
        }
    } identityProvider{*this};
    IdentityReader* nativeApi=&identityProvider;
    DWORD GetCurrentProcessId() const noexcept {return ::GetCurrentProcessId();}
    std::uint64_t GetTickCount64() const noexcept {return clockMs;}
    HWND GetForegroundWindow() const noexcept {return nullptr;}
    DWORD GetWindowThreadProcessId(HWND,DWORD* process) const noexcept {
        *process=focused ? ::GetCurrentProcessId():0;return 1;
    }
    bool nativeContext(void*) noexcept {
        if(owner!=std::this_thread::get_id()){++wrongThread;return false;}
        return admitted;
    }
    void writeLog(const char*,const char*) noexcept {failed=true;}
    template<class T> void store(std::uintptr_t at,const T& value){
        auto& bytes=memory[at];bytes.resize(sizeof(value));std::memcpy(bytes.data(),&value,sizeof(value));
    }
    template<class T> bool read(std::uintptr_t at,T& value) noexcept {
        if(owner!=std::this_thread::get_id()){++wrongThread;return false;}
        ++reads;const auto entry=memory.find(at);
        if(at==refusedAddress || entry==memory.end() || entry->second.size()!=sizeof(value))return false;
        std::memcpy(&value,entry->second.data(),sizeof(value));return true;
    }
    void pad(std::int16_t right,std::int16_t forward,bool walk=true){
        std::array<std::byte,0x30> bytes{};const std::int16_t flag=walk ? 1:0;
        std::memcpy(bytes.data(),&right,2);std::memcpy(bytes.data()+2,&forward,2);
        std::memcpy(bytes.data()+0x2A,&flag,2);store(nf::pad0,bytes);
    }
    Host(){
        store(ped+0x14,matrixAddress);store(matrixAddress+0x30,ps::Position{100,200,30});store(matrixAddress+0x10,ps::Position{0,1,0});
        pad(0,0);store(nf::pad0+0x10E,std::uint16_t{0});store(nf::pad0+0x10A,std::uint16_t{0});
        store(ps::playerIndex,std::uint8_t{0});store(ped+0x718,std::uint8_t{0});
        store(ped+0x5A0,std::uint32_t{0});store(ped+0x71C,std::uint32_t{0});
        store(ped+0x46C,std::uint32_t{1});store(ped+nf::vehicle,std::uint32_t{0});
        store(ni::cameraHeading,0.f);store(ni::weaponCameraMode,std::uint16_t{0});
    }
#include "../client/windows/tools/local_player_capture_runtime.inl"
};
} // namespace capture_fixture
