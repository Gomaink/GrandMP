#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <intrin.h>
#include <CLEO.h>
#include "script_image.hpp"
#include "phase_contract.hpp"
#include "diagnostics.hpp"
#if defined(GRANDMP_POOLED_REMOTE_PLAYER_BUILD) && GRANDMP_POOLED_REMOTE_PLAYER_BUILD == 1
#include "../tools/pooled_remote_player.hpp"
#include "processing_diagnostics.hpp"
#if defined(GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD) && GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD == 1
#include "control_proof_diagnostics.hpp"
#endif
#endif
#include "../tools/native_second_player.hpp"
#include "../tools/ped_smoke_code_identity.hpp"
#if defined(GRANDMP_CLEO_PLAYER_SYNC_BUILD) && GRANDMP_CLEO_PLAYER_SYNC_BUILD == 1
#include "installation_paths.hpp"
#include "../tools/native_player_replay.hpp"
#include "../tools/local_player_capture.hpp"
#include "../tools/ped_smoke_task_layout.hpp"
#endif
#include "grandmp/windows/observer_network.hpp"
#include "grandmp/windows/executable_detection.hpp"
#include <atomic>
#include <filesystem>
#include <optional>
#include <vector>

// Own DATA only: registration is observable without doing I/O under loader lock.
extern "C" {
__declspec(dllexport) DWORD GrandMPCleoSdkVersion=0;
__declspec(dllexport) LONG GrandMPCleoRegisterAttempted=0;
__declspec(dllexport) LONG GrandMPCleoRegisterResult=0;
}
namespace {
namespace np=grandmp::windows::remote_player_binding;
#if defined(GRANDMP_POOLED_REMOTE_PLAYER_BUILD) && GRANDMP_POOLED_REMOTE_PLAYER_BUILD == 1
namespace nf=np::pooled_player;
#else
namespace nf=np::second_player;
#endif
namespace ps=grandmp::windows::ped_smoke;
namespace diag=grandmp::windows::cleo_bootstrap;
#if defined(GRANDMP_CLEO_PLAYER_SYNC_BUILD) && GRANDMP_CLEO_PLAYER_SYNC_BUILD == 1
const wchar_t* diagnosticPrefix() noexcept {
    // Runs only on a new log note outside DllMain, including early refusals.
    wchar_t executable[32768]{};
    const DWORD count=GetModuleFileNameW(nullptr,executable,32768);
    if(!count || count>=32768)return nullptr;
    try { return diag::installationLogPrefix(executable); } catch(...) { return nullptr; }
}
diag::OnceLog diagnostic(nullptr,diagnosticPrefix);
#else
diag::OnceLog diagnostic(L"D:/Jogos/GrandMP-Smoke/GrandMPPlayerBootstrap.Diagnostic");
#endif
#if defined(GRANDMP_CLEO_PLAYER_SYNC_BUILD) && GRANDMP_CLEO_PLAYER_SYNC_BUILD == 1
bool diagnosingInputInstall=false; // Native thread only; no extra engine checks.
#endif
const char* exceptionPoint="tick"; // Native opcode scriptThread only; no engine pointer.
bool refuse(diag::Note note,const char* point,DWORD error=0,
            std::uint64_t observed=0,std::uint64_t expected=0) noexcept {
#if defined(GRANDMP_CLEO_PLAYER_SYNC_BUILD) && GRANDMP_CLEO_PLAYER_SYNC_BUILD == 1
    return diag::recordRefusal(diagnostic,diagnosingInputInstall,note,point,error,observed,expected);
#else
    return diag::recordRefusal(diagnostic,false,note,point,error,observed,expected);
#endif
}
constexpr WORD opcode=0x7A01;
constexpr wchar_t root[]=L"D:/Jogos/GrandMP-Smoke";
HMODULE self=nullptr;
DWORD scriptThread=0;
bool attempted=false,borrowed=false,armed=false,ended=false,f6=false,f8=false;
std::atomic<bool> stopped=false;
std::atomic<std::uintptr_t> scriptOwner=0;
std::atomic<unsigned> status=0;
std::atomic_flag inOpcode=ATOMIC_FLAG_INIT;
static_assert(offsetof(CScriptThread,baseIp)==0x10 && offsetof(CScriptThread,ip)==0x14 &&
              offsetof(CScriptThread,isActive)==0xC4 && sizeof(CScriptThread)==0xE0 &&
              offsetof(CScriptThread,threadName)==8 && sizeof(CScriptThread{}.threadName)==8);
grandmp::core::RemotePlayerController bootstrapController;
np::Binding<true>* binding=nullptr;
#if defined(GRANDMP_CLEO_PLAYER_SYNC_BUILD) && GRANDMP_CLEO_PLAYER_SYNC_BUILD == 1
namespace ni=np::replay;
#if !defined(GRANDMP_POOLED_REMOTE_PLAYER_BUILD) || GRANDMP_POOLED_REMOTE_PLAYER_BUILD != 1
ni::HookCell inputHook;
bool inputActive=false,inputOriginalAttempted=false;
#else
bool inputActive=false;
#endif
std::uint32_t inputPed=0;
std::uint64_t inputSerial=0;
std::atomic<unsigned> preparation{0};
auto& runtimeDisabled=stopped;
auto& gameThread=scriptThread;
auto& phaseDispatchActive=borrowed;
auto& nativeBinding=binding;
auto& nativePlayers=bootstrapController;
grandmp::core::LocalPlayerSnapshots localSnapshots;
std::atomic<bool> localSourceStopped{false};
np::LocalKinematics localKinematics;
std::optional<nf::LocalIdentity> localIdentity;
std::uint64_t localCaptureMs=0;
bool inputContext(void*) noexcept;
bool inputCodeMatches() noexcept;
std::uint32_t inputHookAddress() noexcept;
#endif
thread_local DWORD lastReadError=0;
template<class T> bool read(std::uintptr_t at,T& value) noexcept {
    const auto result=ps::readMemory(at,&value,sizeof(value));lastReadError=result.error;
    return result.complete(sizeof(value));
}
struct Image {
    HANDLE file=INVALID_HANDLE_VALUE; // Resident until process exit, including terminal failures.
    std::vector<std::byte> raw,mapped;
    std::uintptr_t base=0;
    std::uint32_t headers=0;
    const char* failurePoint="Image.open.not_started";
    DWORD failureError=0;
    bool failed(const char* point,DWORD error=0) noexcept { failurePoint=point;failureError=error;return false; }
    bool open(const std::filesystem::path& path,std::uintptr_t actual,const char* expectedHash) {
        file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        LARGE_INTEGER size{};DWORD done=0;
        if(file==INVALID_HANDLE_VALUE)return failed("Image.open.CreateFileW",GetLastError());
        if(!GetFileSizeEx(file,&size))return failed("Image.open.GetFileSizeEx",GetLastError());
        if(size.QuadPart<512 || size.QuadPart>20000000)return failed("Image.open.file_size");
        raw.resize(static_cast<std::size_t>(size.QuadPart));
        if(!ReadFile(file,raw.data(),static_cast<DWORD>(raw.size()),&done,nullptr))return failed("Image.open.ReadFile",GetLastError());
        if(done!=raw.size())return failed("Image.open.read_incomplete");
        BCRYPT_ALG_HANDLE algorithm=nullptr;std::array<unsigned char,32> hash{};
        if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return failed("Image.open.sha_provider");
        const auto result=BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(raw.data()),done,hash.data(),32);
        BCryptCloseAlgorithmProvider(algorithm,0);
        if(result<0)return failed("Image.open.sha_compute");
        constexpr char hex[]="0123456789ABCDEF";char text[65]{};
        for(unsigned i=0;i<32;++i){text[i*2]=hex[hash[i]>>4];text[i*2+1]=hex[hash[i]&15];}
        if(std::strcmp(text,expectedHash))return failed("Image.open.sha_mismatch");
        IMAGE_DOS_HEADER dos{};std::memcpy(&dos,raw.data(),sizeof(dos));
        if(dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<0 || static_cast<std::size_t>(dos.e_lfanew)+sizeof(IMAGE_NT_HEADERS32)>raw.size())return failed("Image.open.dos_header");
        IMAGE_NT_HEADERS32 nt{};std::memcpy(&nt,raw.data()+dos.e_lfanew,sizeof(nt));
        if(nt.Signature!=IMAGE_NT_SIGNATURE || nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_I386 || nt.OptionalHeader.Magic!=0x10B ||
           nt.OptionalHeader.SizeOfImage>64000000 || nt.OptionalHeader.SizeOfHeaders>raw.size() ||
           nt.OptionalHeader.SizeOfHeaders>nt.OptionalHeader.SizeOfImage)return failed("Image.open.nt_header");
        mapped.resize(nt.OptionalHeader.SizeOfImage);base=actual;headers=nt.OptionalHeader.SizeOfHeaders;
        std::memcpy(mapped.data(),raw.data(),nt.OptionalHeader.SizeOfHeaders);
        const auto table=static_cast<std::size_t>(dos.e_lfanew)+24+nt.FileHeader.SizeOfOptionalHeader;
        if(nt.FileHeader.NumberOfSections>32 || table+nt.FileHeader.NumberOfSections*40>raw.size())return failed("Image.open.sections_table");
        for(unsigned i=0;i<nt.FileHeader.NumberOfSections;++i){
            IMAGE_SECTION_HEADER s{};std::memcpy(&s,raw.data()+table+i*40,40);
            if(std::uint64_t{s.PointerToRawData}+s.SizeOfRawData>raw.size() || std::uint64_t{s.VirtualAddress}+s.SizeOfRawData>mapped.size())return failed("Image.open.section_bounds");
            std::memcpy(mapped.data()+s.VirtualAddress,raw.data()+s.PointerToRawData,s.SizeOfRawData);
        }
        if(actual!=nt.OptionalHeader.ImageBase){
            const auto d=nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
            if(!d.Size || std::uint64_t{d.VirtualAddress}+d.Size>mapped.size())return failed("Image.open.reloc_directory");
            for(std::size_t p=d.VirtualAddress;p<d.VirtualAddress+d.Size;){
                IMAGE_BASE_RELOCATION block{};if(p+8>d.VirtualAddress+d.Size)return failed("Image.open.reloc_header");
                std::memcpy(&block,mapped.data()+p,8);
                if(block.SizeOfBlock<8 || p+block.SizeOfBlock>d.VirtualAddress+d.Size || block.SizeOfBlock%2)return failed("Image.open.reloc_block");
                for(unsigned n=8;n<block.SizeOfBlock;n+=2){
                    WORD entry=0;std::memcpy(&entry,mapped.data()+p+n,2);if(!(entry>>12))continue;
                    if((entry>>12)!=IMAGE_REL_BASED_HIGHLOW)return failed("Image.open.reloc_type");
                    const auto at=std::uint64_t{block.VirtualAddress}+(entry&0xFFF);if(at+4>mapped.size())return failed("Image.open.reloc_target");
                    DWORD value=0;std::memcpy(&value,mapped.data()+at,4);value+=static_cast<DWORD>(actual-nt.OptionalHeader.ImageBase);
                    std::memcpy(mapped.data()+at,&value,4);
                }p+=block.SizeOfBlock;
            }
        }return true;
    }
    bool matches(std::uintptr_t at,std::size_t n,diag::Note note=diag::Note::ready_code) const noexcept {
        if(at<base || n>4096 || at-base+n>mapped.size())return refuse(note,"Image.matches.range",0,at,n);
        std::array<std::byte,4096> bytes{};
        for(auto p=at;p<at+n;){MEMORY_BASIC_INFORMATION page{};
            if(!VirtualQuery(reinterpret_cast<void*>(p),&page,sizeof(page)))return refuse(note,"Image.matches.VirtualQuery",GetLastError(),at,n);
            if(page.State!=MEM_COMMIT || page.Type!=MEM_IMAGE || reinterpret_cast<std::uintptr_t>(page.AllocationBase)!=base ||
               (page.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return refuse(note,"Image.matches.page_identity",0,at,n);
            const auto end=reinterpret_cast<std::uintptr_t>(page.BaseAddress)+page.RegionSize;
            if(end<=p)return refuse(note,"Image.matches.region_end",0,at,n);p=end;
        }
        const auto result=ps::readMemory(at,bytes.data(),n);
        if(!result.complete(n))return refuse(note,"Image.matches.read",result.error,at,n);
#if defined(GRANDMP_CLEO_PLAYER_SYNC_BUILD) && GRANDMP_CLEO_PLAYER_SYNC_BUILD == 1
#if !defined(GRANDMP_POOLED_REMOTE_PLAYER_BUILD) || GRANDMP_POOLED_REMOTE_PLAYER_BUILD != 1
        if(base==ps::imageBase && at==ps::playerVtable && n==256 && inputHook.installed()){
            std::array<std::byte,256> expected{};std::uint32_t original=0;
            std::memcpy(expected.data(),mapped.data()+at-base,n);std::memcpy(&original,expected.data()+0x28,4);
            if(original!=ni::original)return refuse(note,"Image.matches.original_control_cell");
            const auto replacement=inputHookAddress();std::memcpy(expected.data()+0x28,&replacement,4);
            return !std::memcmp(bytes.data(),expected.data(),n) || refuse(note,"Image.matches.owned_control_cell",0,at,n);
        }
#endif
#endif
        return !std::memcmp(bytes.data(),mapped.data()+at-base,n) || refuse(note,"Image.matches.bytes",0,at,n);
    }
} game,cleo;
bool context(void*) noexcept { return borrowed && scriptThread==GetCurrentThreadId() && !stopped.load()
#if defined(GRANDMP_CLEO_PLAYER_SYNC_BUILD) && GRANDMP_CLEO_PLAYER_SYNC_BUILD == 1
    && !inputActive
#endif
    ; }
bool factoryCodeMatches() noexcept {
#if defined(GRANDMP_POOLED_REMOTE_PLAYER_BUILD) && GRANDMP_POOLED_REMOTE_PLAYER_BUILD == 1
    for(auto r:nf::codeRegions){
        for(std::size_t offset=0;offset<r.size;){
            const auto count=std::min<std::size_t>(4096,r.size-offset);
            if(!game.matches(r.address+offset,count))return false;
            offset+=count;
        }
    }
    return game.matches(ps::playerVtable-4,260) && game.matches(np::second_player::factory::defaultTaskVtable,44);
#else
    for(auto r:ps::factoryCodeWindows)if(!game.matches(r.address,r.size))return false;
    for(auto r:std::array{ps::CodeRegion{ps::findGround,0x90},ps::CodeRegion{ps::testSphere,0x100},
        ps::CodeRegion{nf::setup,0x60},ps::CodeRegion{nf::remove,0x90},ps::CodeRegion{ps::registerReference,32},
        ps::CodeRegion{ps::cleanReference,32},ps::CodeRegion{ps::playerVtable,256},ps::CodeRegion{nf::factory::defaultTaskVtable,44},
        ps::CodeRegion{ps::phaseCall,5}})if(!game.matches(r.address,r.size))return false;
    return true;
#endif
}
bool ready(void*) noexcept {
    if(!context(nullptr)
#if defined(GRANDMP_CLEO_PLAYER_SYNC_BUILD) && GRANDMP_CLEO_PLAYER_SYNC_BUILD == 1
       && !inputContext(nullptr)
#endif
       )return refuse(diag::Note::contract,"ready.context");
    if(!factoryCodeMatches())return false;
#if defined(GRANDMP_CLEO_PLAYER_SYNC_BUILD) && GRANDMP_CLEO_PLAYER_SYNC_BUILD == 1
    if(!inputCodeMatches() || !game.matches(ps::imageBase,game.headers))return false;
#endif
    BYTE focus=1,pause=1,userPause=1,disabled=1,scene=1,big=1,loaded=0;int streaming=0;
    if(!read(ps::playerIndex,focus))return refuse(diag::Note::focus,"ready.focus.read",lastReadError);
    if(focus)return refuse(diag::Note::focus,"ready.focus.value",0,focus,0);
    if(!read(ps::codePause,pause))return refuse(diag::Note::code_pause,"ready.code_pause.read",lastReadError);
    if(pause)return refuse(diag::Note::code_pause,"ready.code_pause.value",0,pause,0);
    if(!read(ps::userPause,userPause))return refuse(diag::Note::user_pause,"ready.user_pause.read",lastReadError);
    if(userPause)return refuse(diag::Note::user_pause,"ready.user_pause.value",0,userPause,0);
    if(!read(ps::streamingInitialized,streaming))return refuse(diag::Note::streaming,"ready.streaming.read",lastReadError);
    if(streaming!=1)return refuse(diag::Note::streaming,"ready.streaming.value",0,streaming,1);
    if(!read(ps::streamingDisabled,disabled))return refuse(diag::Note::disabled,"ready.disabled.read",lastReadError);
    if(disabled)return refuse(diag::Note::disabled,"ready.disabled.value",0,disabled,0);
    if(!read(ps::loadingScene,scene))return refuse(diag::Note::scene,"ready.scene.read",lastReadError);
    if(scene)return refuse(diag::Note::scene,"ready.scene.value",0,scene,0);
    if(!read(ps::loadingBigModel,big))return refuse(diag::Note::big_model,"ready.big_model.read",lastReadError);
    if(big)return refuse(diag::Note::big_model,"ready.big_model.value",0,big,0);
    if(!read(ps::streamingTable+0x10,loaded))return refuse(diag::Note::model,"ready.model.read",lastReadError);
    return loaded==1 || refuse(diag::Note::model,"ready.model.value",0,loaded,1);
}
struct Probe {
    void* cj=nullptr;float floor=0;
    template<class... T> void trace(const char*,T...) noexcept {}
    bool player(ps::Position& position,ps::Position& right){
        std::uint32_t ped=0,matrix=0,data=0,type=9,vt=0;float health=0;
        if(!ready(nullptr) || !read(ps::player,ped) || !ped || !read(ped,vt) || vt!=ps::playerVtable ||
           !read(ped+nf::playerData,data) || data!=nf::slot0+4 || !read(ped+nf::pedType,type) || type ||
           !read(ped+0x540,health) || !std::isfinite(health) || health<=0 || !read(ped+0x14,matrix) || !matrix ||
           !read(matrix+0x30,position) || !read(matrix,right))return false;
        cj=reinterpret_cast<void*>(ped);return true;
    }
    float ground(float x,float y,float z,bool& found){
        floor=reinterpret_cast<float(__cdecl*)(float,float,float,bool*,void**)>(ps::findGround)(x,y,z,&found,nullptr);return floor;
    }
    bool sphere(ps::Position p){using F=void*(__cdecl*)(ps::Position,float,void*,bool,bool,bool,bool,bool,bool);
        return reinterpret_cast<F>(ps::testSphere)(p,.35f,cj,true,true,true,true,true,false)!=nullptr;}
};
#if defined(GRANDMP_POOLED_REMOTE_PLAYER_BUILD) && GRANDMP_POOLED_REMOTE_PLAYER_BUILD == 1
bool foundationSpawn(ps::Position&) noexcept;
bool spawn(void*,ps::Position& ground) noexcept {return foundationSpawn(ground);}
#else
bool spawn(void*,ps::Position& ground) noexcept {Probe p;ps::Position centre{};if(!ps::findSpawnPosition(p,centre))return false;ground=centre;ground.z=p.floor;return ps::finite(ground);}
#endif
#if defined(GRANDMP_POOLED_REMOTE_PLAYER_BUILD) && GRANDMP_POOLED_REMOTE_PLAYER_BUILD == 1
std::optional<nf::Api> bootstrapApi;
#else
nf::WindowsAccess access({nullptr,context,ready,spawn,
#if defined(GRANDMP_CLEO_PLAYER_SYNC_BUILD) && GRANDMP_CLEO_PLAYER_SYNC_BUILD == 1
    inputContext
#else
    nullptr
#endif
    });
std::optional<nf::Api> bootstrapApi;
#endif
#if defined(GRANDMP_CLEO_PLAYER_SYNC_BUILD) && GRANDMP_CLEO_PLAYER_SYNC_BUILD == 1
#if defined(GRANDMP_POOLED_REMOTE_PLAYER_BUILD) && GRANDMP_POOLED_REMOTE_PLAYER_BUILD == 1
#if defined(GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD) && GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD == 1
void proofBefore(std::uint64_t) noexcept;
void proofAfter(const nf::ProcessingFailure&) noexcept;
#endif
#include "pooled_player_sync.inl"
#if defined(GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD) && GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD == 1
#include "independent_control_proof.inl"
#endif
#else
#include "player_sync.inl"
#endif
#endif
void deleted(CScriptThread* script){if(scriptOwner.load()==reinterpret_cast<std::uintptr_t>(script)){
    stopped.store(true);status.store(105);diagnostic.emit(diag::Note::deleted,"deleted.owned_script");}}
#if !defined(GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD) || GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD != 1
DWORD WINAPI network(void*) {
#if defined(GRANDMP_CLEO_PLAYER_SYNC_BUILD) && GRANDMP_CLEO_PLAYER_SYNC_BUILD == 1
    struct SessionFence { ~SessionFence(){localSnapshots.session(0);localSnapshots.invalidate();} } fence;
#endif
    try {
    grandmp::windows::ObserverNetwork net;net.setRemotePlayerController(&bootstrapController);
    wchar_t text[64]{};const auto n=GetEnvironmentVariableW(L"GRANDMP_REMOTE_PED_SERVER",text,64);
    const DWORD endpointError=n ? 0 : GetLastError();
    grandmp::core::Ipv4Endpoint endpoint{};
    const char* failure=nullptr;
    if(!n || n>=64)failure="network.endpoint_env";
    else if(!grandmp::windows::parseLanEndpoint({text,n},endpoint))failure="network.endpoint_parse";
    else if(!net.start(endpoint))failure="network.start";
    else if(!net.beginRemoteScenePreview())failure="network.preview";
    if(failure){
        stopped.store(true);status.store(106);diagnostic.emit(diag::Note::network,failure,endpointError);return 0;
    }
#if defined(GRANDMP_CLEO_PLAYER_SYNC_BUILD) && GRANDMP_CLEO_PLAYER_SYNC_BUILD == 1
    bool receiverLogged=false,controlsLogged=false;
#endif
    while(!stopped.load()
#if defined(GRANDMP_CLEO_PLAYER_SYNC_BUILD) && GRANDMP_CLEO_PLAYER_SYNC_BUILD == 1
          && !localSourceStopped.load()
#endif
    ){
#if defined(GRANDMP_CLEO_PLAYER_SYNC_BUILD) && GRANDMP_CLEO_PLAYER_SYNC_BUILD == 1
#if defined(GRANDMP_POOLED_REMOTE_PLAYER_BUILD) && GRANDMP_POOLED_REMOTE_PLAYER_BUILD == 1
        reportProcessingFailure(); // Copied values on existing worker, never GTA reads.
#endif
        if(!net.tickLocal(localSnapshots)){stopped.store(true);status.store(106);diagnostic.emit(diag::Note::sync_failure,"network.tickLocal");break;}
        if(!receiverLogged && net.sessionId()){
            receiverLogged=true;diagnostic.emit(diag::Note::receiver_session,"network.receiver_session",0,net.sessionId());
        }
        // Copy only Controller values, not another UDP/engine read. Try-lock may
        // fail: absence of this bounded note is not proof of absent packets.
        grandmp::core::RemotePlayerFrames received{};
        if(!controlsLogged && bootstrapController.tryRead(grandmp::core::RemoteRoster::Clock::now(),received) &&
           received.count && received.count<=received.players.size()){
            for(std::size_t i=0;i<received.count;++i)if(received.players[i].freshness==grandmp::core::ControlFreshness::fresh){
                const auto& frame=received.players[i];controlsLogged=true;
                diagnostic.emit(diag::Note::receiver_controls,"network.remote_controls_seen",0,
                    frame.key.player,frame.key.generation,frame.sequence);break;
            }
        }
        if(net.sentSequence())diagnostic.emit(diag::Note::sync_sender,"network.local_sender",0,net.sessionId(),0,net.sentSequence());
#else
        net.tick(std::nullopt);
#endif
        Sleep(20);
    }
#if defined(GRANDMP_CLEO_PLAYER_SYNC_BUILD) && GRANDMP_CLEO_PLAYER_SYNC_BUILD == 1
#if defined(GRANDMP_POOLED_REMOTE_PLAYER_BUILD) && GRANDMP_POOLED_REMOTE_PLAYER_BUILD == 1
    reportProcessingFailure(); // Drain terminal evidence before sender-end.
#endif
    localSnapshots.session(0);
    const auto endStatus=status.load();
    diagnostic.emit(diag::Note::network_end,diag::senderEndPoint(endStatus,stopped.load(),localSourceStopped.load()),
        0,net.sessionId(),endStatus,net.sentSequence());
#endif
    net.stop();
    } catch(...){stopped.store(true);status.store(106);diagnostic.emit(diag::Note::network,"network.exception");}return 0;
}
#endif
bool initialise() {
    HMODULE pin=nullptr;wchar_t path[32768]{};
    exceptionPoint="initialise.pin";
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&initialise),&pin))
        return refuse(diag::Note::initialise,"initialise.GetModuleHandleExW",GetLastError());
    if(pin!=self)return refuse(diag::Note::initialise,"initialise.pin_identity");
    std::filesystem::path base(root);
    exceptionPoint="initialise.game_path";
    const DWORD gamePathLength=GetModuleFileNameW(nullptr,path,32768);
    if(!gamePathLength)return refuse(diag::Note::initialise,"initialise.game_filename",GetLastError());
    if(gamePathLength>=32768)return refuse(diag::Note::initialise,"initialise.game_filename_truncated",ERROR_INSUFFICIENT_BUFFER);
#if defined(GRANDMP_CLEO_PLAYER_SYNC_BUILD) && GRANDMP_CLEO_PLAYER_SYNC_BUILD == 1
    const auto* selectedRoot=diag::installationRoot(path);
    if(!selectedRoot)return refuse(diag::Note::initialise,"initialise.game_path");
    if(!diag::plainPath(path))return refuse(diag::Note::initialise,"initialise.game_path_reparse_or_unreadable");
    base=selectedRoot;
#endif
    if(std::filesystem::canonical(path)!=std::filesystem::canonical(base/L"gta_sa.exe"))return refuse(diag::Note::initialise,"initialise.game_path");
    exceptionPoint="initialise.plugin_path";
    const DWORD pluginPathLength=GetModuleFileNameW(self,path,32768);
    if(!pluginPathLength)return refuse(diag::Note::initialise,"initialise.plugin_filename",GetLastError());
    if(pluginPathLength>=32768)return refuse(diag::Note::initialise,"initialise.plugin_filename_truncated",ERROR_INSUFFICIENT_BUFFER);
#if defined(GRANDMP_CLEO_PLAYER_SYNC_BUILD) && GRANDMP_CLEO_PLAYER_SYNC_BUILD == 1
    if(std::filesystem::path(path)!=base/L"cleo/GrandMPPlayerBootstrap.cleo" || !diag::plainPath(path))
        return refuse(diag::Note::initialise,"initialise.plugin_path_reparse_or_wrong_installation");
#endif
    if(std::filesystem::canonical(path)!=std::filesystem::canonical(base/L"cleo/GrandMPPlayerBootstrap.cleo"))return refuse(diag::Note::initialise,"initialise.plugin_path");
    const auto library=GetModuleHandleW(L"CLEO.asi");
    if(!library)return refuse(diag::Note::initialise,"initialise.CLEO_module",GetLastError());
    const auto version=CLEO_GetVersion();
    if(version!=0x04040400)return refuse(diag::Note::initialise,"initialise.CLEO_version",0,version,0x04040400);
    const auto gameVersion=CLEO_GetGameVersion();
    if(gameVersion!=0)return refuse(diag::Note::initialise,"initialise.CLEO_game_version",0,gameVersion,0);
    exceptionPoint="initialise.CLEO_path";
    const DWORD cleoPathLength=GetModuleFileNameW(library,path,32768);
    if(!cleoPathLength)return refuse(diag::Note::initialise,"initialise.CLEO_filename",GetLastError());
    if(cleoPathLength>=32768)return refuse(diag::Note::initialise,"initialise.CLEO_filename_truncated",ERROR_INSUFFICIENT_BUFFER);
#if defined(GRANDMP_CLEO_PLAYER_SYNC_BUILD) && GRANDMP_CLEO_PLAYER_SYNC_BUILD == 1
    if(std::filesystem::path(path)!=base/L"CLEO.asi" || !diag::plainPath(path))
        return refuse(diag::Note::initialise,"initialise.CLEO_path_reparse_or_wrong_installation");
#endif
    if(std::filesystem::canonical(path)!=std::filesystem::canonical(base/L"CLEO.asi"))return refuse(diag::Note::initialise,"initialise.CLEO_path");
    if(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))!=ps::imageBase)return refuse(diag::Note::initialise,"initialise.game_base");
    exceptionPoint="initialise.game_image_auth";
    if(!game.open(base/L"gta_sa.exe",ps::imageBase,"A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26"))
        return refuse(diag::Note::initialise,game.failurePoint,game.failureError,ps::imageBase);
    exceptionPoint="initialise.CLEO_image_auth";
    if(!cleo.open(base/L"CLEO.asi",reinterpret_cast<std::uintptr_t>(library),"748EA0B36A1580652146C8B443066BDD81EF14556431805B2102F0F1F44CAA08"))
        return refuse(diag::Note::initialise,cleo.failurePoint,cleo.failureError,reinterpret_cast<std::uintptr_t>(library));
    // Expected CLEO-owned dispatch operand and script-call redirection only.
    DWORD table=static_cast<DWORD>(cleo.base+0x4E9B8);std::memcpy(game.mapped.data()+0x69FEE,&table,4);
    const DWORD displacement=static_cast<DWORD>(cleo.base+0xEF00-0x46A220);
    game.mapped[0x6A21B]=std::byte{0xE8};std::memcpy(game.mapped.data()+0x6A21C,&displacement,4);
    scriptThread=GetCurrentThreadId();binding=np::Binding<true>::prepare(std::this_thread::get_id());
    if(!binding)return refuse(diag::Note::initialise,"initialise.Binding_prepare");
    exceptionPoint="initialise.Api";
#if defined(GRANDMP_POOLED_REMOTE_PLAYER_BUILD) && GRANDMP_POOLED_REMOTE_PLAYER_BUILD == 1
    bootstrapApi.emplace(nf::Admission{nullptr,context,ready,spawn,inputContext},std::this_thread::get_id(),inputHookAddress());
#if defined(GRANDMP_AUTHENTICATED_PAD_COW_BUILD) && GRANDMP_AUTHENTICATED_PAD_COW_BUILD == 1
    if(!bootstrapApi->admitPadImage(game.raw))return refuse(diag::Note::initialise,"initialise.pad_image_contract");
#endif
#else
    bootstrapApi.emplace(access,std::this_thread::get_id());
#endif
    CLEO_AddScriptDeleteDelegate(deleted);
    HANDLE thread=CreateThread(nullptr,0,
#if defined(GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD) && GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD == 1
        proofWorker,
#else
        network,
#endif
        nullptr,0,nullptr);
    if(!thread)return refuse(diag::Note::initialise,"initialise.CreateThread",GetLastError());CloseHandle(thread);return true;
}
bool phase(CScriptThread* script,const std::uint32_t* returns) noexcept {
    std::array<std::uint32_t,16> stack{};CScriptThread current{};
    const auto stackRead=ps::readMemory(reinterpret_cast<std::uintptr_t>(returns),stack.data(),sizeof(stack));
    if(!stackRead.complete(sizeof(stack)))return refuse(diag::Note::phase,"phase.stack_read",stackRead.error);
    if(!grandmp::windows::cleo_bootstrap::schedulerStack(stack,cleo.base,reinterpret_cast<std::uintptr_t>(script))){
        if(!cleo.base || cleo.base>UINT32_MAX-0xEF63)return refuse(diag::Note::phase,"phase.CLEO_base");
        if(!script)return refuse(diag::Note::phase,"phase.script_null");
        if(stack[0]!=0x469FF7)return refuse(diag::Note::phase,"phase.return_0",0,stack[0],0x469FF7);
        if(stack[1]!=reinterpret_cast<std::uintptr_t>(script))return refuse(diag::Note::phase,"phase.script_argument");
        if(stack[4]!=cleo.base+0xEF63)return refuse(diag::Note::phase,"phase.return_4",0,stack[4],cleo.base+0xEF63);
        if(stack[6]!=0x46A220)return refuse(diag::Note::phase,"phase.return_6",0,stack[6],0x46A220);
        return refuse(diag::Note::phase,"phase.return_15",0,stack[15],0x53BFCC);
    }
    if(!read(reinterpret_cast<std::uintptr_t>(script),current))return refuse(diag::Note::phase,"phase.script_read",lastReadError);
    if(!current.isActive)return refuse(diag::Note::phase,"phase.script_inactive");
    const auto nameBytes=diag::scriptNameBytes(current.threadName);
    constexpr auto expectedNameBytes=diag::scriptNameBytes(diag::nativeScriptName);
    diagnostic.emit(diag::Note::script_name,"phase.script_name.bytes",0,nameBytes,expectedNameBytes);
    if(!diag::scriptNameMatches(current.threadName))return refuse(diag::Note::phase,"phase.script_name",0,nameBytes,expectedNameBytes);
    std::array<unsigned char,bootstrapScript.size()> code{};
    if(current.ip!=current.baseIp+bootstrapOpcodeOffset+2)return refuse(diag::Note::phase,"phase.script_ip");
    const auto codeRead=ps::readMemory(reinterpret_cast<std::uintptr_t>(current.baseIp),code.data(),code.size());
    if(!codeRead.complete(code.size()))return refuse(diag::Note::phase,"phase.script_image_read",codeRead.error);
    if(code!=bootstrapScript)return refuse(diag::Note::phase,"phase.script_image_bytes");
    DWORD window=0;HWND hwnd=nullptr;
    if(!read(ps::platformGlobal,window))return refuse(diag::Note::phase,"phase.platform_read",lastReadError);
    if(!window)return refuse(diag::Note::phase,"phase.platform_null");
    if(!read(window,hwnd))return refuse(diag::Note::phase,"phase.window_read",lastReadError);
    if(!hwnd)return refuse(diag::Note::phase,"phase.window_null");
    if(!IsWindow(hwnd))return refuse(diag::Note::phase,"phase.window_invalid");
    if(!IsWindowVisible(hwnd))return refuse(diag::Note::phase,"phase.window_hidden");
    DWORD pid=0;const DWORD thread=GetWindowThreadProcessId(hwnd,&pid);
    const DWORD threadError=thread ? 0 : GetLastError();
    if(thread!=GetCurrentThreadId())return refuse(diag::Note::phase,"phase.window_thread",threadError,thread,GetCurrentThreadId());
    if(pid!=GetCurrentProcessId())return refuse(diag::Note::phase,"phase.window_process",0,pid,GetCurrentProcessId());
    return game.matches(0x469F00,256,diag::Note::phase) && game.matches(0x46A000,0x230,diag::Note::phase) &&
        game.matches(0x53BFC7,5,diag::Note::phase) && cleo.matches(cleo.base+0x32B0,39,diag::Note::phase) &&
        cleo.matches(cleo.base+0xEF00,0x79,diag::Note::phase);
}
__declspec(noinline) OpcodeResult CALLBACK tick(CScriptThread* script) {
    if(inOpcode.test_and_set()){
        stopped.store(true);status.store(102);
        diagnostic.emit(diag::Note::entered,"tick.entered");
        diagnostic.emit(diag::Note::registration,"DllMain.CLEO_RegisterOpcode.return",0,GrandMPCleoRegisterResult,1,GrandMPCleoSdkVersion);
        diagnostic.emit(diag::Note::reentrant,"tick.reentrant");return OR_INTERRUPT;
    }
    struct LeaveOpcode { ~LeaveOpcode(){inOpcode.clear();} } leaveOpcode;
    diagnostic.emit(diag::Note::entered,"tick.entered");
    diagnostic.emit(diag::Note::registration,"DllMain.CLEO_RegisterOpcode.return",0,GrandMPCleoRegisterResult,1,GrandMPCleoSdkVersion);
    const auto* returns=static_cast<const std::uint32_t*>(_AddressOfReturnAddress());
    std::uint32_t caller=0;
    const bool callerRead=read(reinterpret_cast<std::uintptr_t>(returns),caller);
    if(!callerRead || caller!=0x469FF7){
        stopped.store(true);status.store(102);
        diagnostic.emit(diag::Note::caller,callerRead ? "tick.return_address_mismatch" : "tick.return_address_read",
            callerRead ? 0 : lastReadError,caller,0x469FF7);return OR_INTERRUPT;
    }
    CScriptThread image{};std::array<unsigned char,6> operands{};
    constexpr std::array<unsigned char,6> outputs{3,0,0,3,1,0};
    if(!read(reinterpret_cast<std::uintptr_t>(script),image)){
        stopped.store(true);status.store(107);
        diagnostic.emit(diag::Note::operands,"tick.operands_script_read",lastReadError);return OR_INTERRUPT;
    }
    const auto operandRead=ps::readMemory(reinterpret_cast<std::uintptr_t>(image.ip),operands.data(),operands.size());
    if(!operandRead.complete(operands.size()) || operands!=outputs){
        stopped.store(true);status.store(107);
        diagnostic.emit(diag::Note::operands,operandRead.complete(operands.size()) ? "tick.operands_mismatch" : "tick.operands_read",
            operandRead.error,diag::operandImage(operands.data()),diag::operandImage(outputs.data()));
        return OR_INTERRUPT; // Never write through unknown operands.
    }
    try {
        if(!attempted){attempted=true;wchar_t enabled[4]{};GetEnvironmentVariableW(L"GRANDMP_CLEO_BOOTSTRAP_ENABLE",enabled,4);
            bool admitted=false;
            if(std::wstring_view(enabled)!=L"1")diagnostic.emit(diag::Note::initialise,"initialise.opt_in");
            else admitted=initialise();
            if(!admitted){stopped.store(true);status.store(101);}else status.store(1);}
#if defined(GRANDMP_CLEO_PLAYER_SYNC_BUILD) && GRANDMP_CLEO_PLAYER_SYNC_BUILD == 1
        inputReport(); // Copied counters/notices only, never queries GTA.
#endif
        if(!stopped.load()){
            exceptionPoint="tick.phase";
            if(borrowed || !phase(script,returns)){
                if(borrowed)diagnostic.emit(diag::Note::phase,"phase.borrowed_scope");
                stopped.store(true);status.store(102);
            }
            else {
                const auto address=reinterpret_cast<std::uintptr_t>(script),prior=scriptOwner.load();
                if(prior && prior!=address){stopped.store(true);status.store(103);diagnostic.emit(diag::Note::identity,"tick.script_owner_changed");}
                else {scriptOwner.store(address);borrowed=true;
                    struct Release{~Release(){borrowed=false;}} release;
                    exceptionPoint="tick.ready";
                    const bool gameplayReady=ready(nullptr);
#if defined(GRANDMP_CLEO_PLAYER_SYNC_BUILD) && GRANDMP_CLEO_PLAYER_SYNC_BUILD == 1
                    if(gameplayReady)preparation.store(3);
#endif
                    if(gameplayReady){
                        const bool press6=(GetAsyncKeyState(VK_F6)&0x8000)!=0,press8=(GetAsyncKeyState(VK_F8)&0x8000)!=0;
                        if(press6 && !f6 && !armed && !ended){armed=true;status.store(2);diagnostic.emit(diag::Note::armed,"tick.F6_armed");}
                        const bool remove=press8 && !f8;f6=press6;f8=press8;
#if defined(GRANDMP_CLEO_PLAYER_SYNC_BUILD) && GRANDMP_CLEO_PLAYER_SYNC_BUILD == 1
#if !defined(GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD) || GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD != 1
                        captureLocalPlayer(armed && !ended); // Before any remote pad1 scope.
#endif
#endif
                        if(armed && (!ended || remove)){
#if defined(GRANDMP_POOLED_REMOTE_PLAYER_BUILD) && GRANDMP_POOLED_REMOTE_PLAYER_BUILD == 1
#if defined(GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD) && GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD == 1
                            if(remove)proofSeal("operator_removal");
                            else if(!proofPublish()){
                                stopped.store(true);status.store(108);
                                diagnostic.emit(diag::Note::proof_source,"proof.local_source_refused_terminal_no_retry");
                                CLEO_SetIntOpcodeParam(script,status.load());CLEO_SetIntOpcodeParam(script,UINT32_MAX);return OR_CONTINUE;
                            }
#endif
                            const bool foundationSnapshot=stageRemote();
#endif
                            exceptionPoint=remove ? "tick.requestRemoval" : "tick.consume";
#if defined(GRANDMP_CLEO_PLAYER_SYNC_BUILD) && GRANDMP_CLEO_PLAYER_SYNC_BUILD == 1
                            const auto previousKey=binding->key();
#endif
                            np::Api& activeApi=
#if defined(GRANDMP_POOLED_REMOTE_PLAYER_BUILD) && GRANDMP_POOLED_REMOTE_PLAYER_BUILD == 1
                                *bootstrapApi;
#else
#if defined(GRANDMP_CLEO_PLAYER_SYNC_BUILD) && GRANDMP_CLEO_PLAYER_SYNC_BUILD == 1
                                inputApi;
#else
                                *bootstrapApi;
#endif
#endif
#if defined(GRANDMP_POOLED_REMOTE_PLAYER_BUILD) && GRANDMP_POOLED_REMOTE_PLAYER_BUILD == 1
                            const auto beforeAdmissionState=binding->state();
#endif
                            const auto r=remove ? binding->requestRemoval(activeApi) :
#if defined(GRANDMP_POOLED_REMOTE_PLAYER_BUILD) && GRANDMP_POOLED_REMOTE_PLAYER_BUILD == 1
                                !foundationSnapshot ? np::Result::no_input :
#endif
                                binding->consume(bootstrapController,grandmp::core::RemoteRoster::Clock::now(),activeApi);
#if defined(GRANDMP_POOLED_REMOTE_PLAYER_BUILD) && GRANDMP_POOLED_REMOTE_PLAYER_BUILD == 1
                            if(!remove && (r==np::Result::blocked_contract || r==np::Result::indeterminate)){
                                const auto observation=bootstrapApi->factoryObservation(); // Values only; no new engine reads.
                                factoryAdmissionFailure.publish({observation,r,beforeAdmissionState,binding->state()});
                            }
                            if(r==np::Result::indeterminate)diagnostic.emit(diag::Note::factory_native,bootstrapApi->failure());
#if !defined(GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD) || GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD != 1
                            if(r==np::Result::created || r==np::Result::updated)presentRemote();
#endif
#else
                            if(!remove && bootstrapApi->reason()==nf::Reason::factory_result_unknown)
                                diag::recordFactoryFailure(diagnostic,bootstrapApi->creationObservation(),access.creationObservation());
#endif
                            if(r==np::Result::no_input)diagnostic.emit(diag::Note::pending,"tick.consume_no_input",0,
                                static_cast<unsigned>(binding->state()),0,static_cast<unsigned>(binding->inputDisposition()));
                            bool reportLive=r==np::Result::created || r==np::Result::updated;
#if defined(GRANDMP_POOLED_REMOTE_PLAYER_BUILD) && GRANDMP_POOLED_REMOTE_PLAYER_BUILD == 1
                            if(stopped.load())reportLive=false; // Never mask a refused presentation with status=live.
#endif
#if defined(GRANDMP_CLEO_PLAYER_SYNC_BUILD) && GRANDMP_CLEO_PLAYER_SYNC_BUILD == 1
                            if(r==np::Result::created){
                                const auto key=binding->key();
                                diagnostic.emit(diag::Note::created_before_replay,"tick.remote_created_before_replay",0,key.player,0,key.generation);
                                if(!installInputHook()){stopped.store(true);status.store(108);reportLive=false;}
                            }
#endif
                            if(reportLive){status.store(3);
#if defined(GRANDMP_CLEO_PLAYER_SYNC_BUILD) && GRANDMP_CLEO_PLAYER_SYNC_BUILD == 1
                                const auto key=binding->key();diagnostic.emit(diag::Note::created,"tick.live",0,key.player,0,key.generation);
#else
                                diagnostic.emit(diag::Note::created,"tick.live");
#endif
                            }
                            if(r==np::Result::removed){status.store(4);ended=true;stopped.store(true);
#if defined(GRANDMP_CLEO_PLAYER_SYNC_BUILD) && GRANDMP_CLEO_PLAYER_SYNC_BUILD == 1
                                diagnostic.emit(diag::Note::removed,"tick.removed",0,previousKey.player,0,previousKey.generation);
#else
                                diagnostic.emit(diag::Note::removed,"tick.removed");
#endif
                            }
                            if(r==np::Result::ownership_lost || r==np::Result::indeterminate || r==np::Result::blocked_contract){
                                status.store(200+static_cast<unsigned>(bootstrapApi->reason()));ended=true;
                                diagnostic.emit(diag::Note::contract,exceptionPoint,0,static_cast<unsigned>(r),0,static_cast<unsigned>(bootstrapApi->reason()));}
                            else if(r!=np::Result::no_input && r!=np::Result::created && r!=np::Result::updated && r!=np::Result::removed)
                                diagnostic.emit(diag::Note::contract,exceptionPoint,0,static_cast<unsigned>(r),0,static_cast<unsigned>(bootstrapApi->reason()));
                        }
                    }
#if defined(GRANDMP_CLEO_PLAYER_SYNC_BUILD) && GRANDMP_CLEO_PLAYER_SYNC_BUILD == 1
#if !defined(GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD) || GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD != 1
                    else captureLocalPlayer(false); // Invalid context never reads native state.
#endif
#endif
                }
            }
        }
    }catch(...){borrowed=false;stopped.store(true);status.store(104);diagnostic.emit(diag::Note::exception,exceptionPoint);}
    CLEO_SetIntOpcodeParam(script,status.load());
    // Return a pool handle, never a raw PED pointer. Only after live correlation.
    const DWORD handle=scriptThread==GetCurrentThreadId() && binding && binding->state()==np::State::live && status.load()==3 ?
        static_cast<DWORD>(binding->entityHandle()) : UINT32_MAX;
    CLEO_SetIntOpcodeParam(script,handle);return OR_CONTINUE;
}
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID){
    if(reason==DLL_PROCESS_ATTACH){self=module;
        // Documented CLEO4 registration only. No game calls, I/O or worker here.
        GrandMPCleoSdkVersion=CLEO_GetVersion();
        if(GrandMPCleoSdkVersion!=0x04040400)return FALSE;
        GrandMPCleoRegisterAttempted=1;
        const BOOL registered=CLEO_RegisterOpcode(opcode,tick);
        GrandMPCleoRegisterResult=registered;
        return registered; // FALSE still makes LoadLibrary fail; no I/O here.
    }return TRUE;
}
