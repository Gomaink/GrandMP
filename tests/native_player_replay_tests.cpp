#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../client/windows/tools/native_player_replay.hpp"
#include "../client/windows/cleo/phase_contract.hpp"
#include "../client/windows/cleo/diagnostics.hpp"
#include "../client/windows/cleo/installation_paths.hpp"
#include "grandmp/windows/remote_player_binding.hpp"
#include <cstdio>
#include <stdexcept>
#include <string_view>

namespace ni=grandmp::windows::remote_player_binding::replay;
namespace np=grandmp::windows::remote_player_binding;
void check(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
unsigned originals=0,wrappers=0;
void* observedThis=nullptr;
void __fastcall original(void* self,void*) { ++originals;observedThis=self; }
void __fastcall wrapper(void* self,void*) {
    ++wrappers;
    // ABI-shaped stand-in, not an engine fake: test actual Win32 ECX/RET0 and cell.
    reinterpret_cast<void(__thiscall*)(void*)>(&original)(self);
}
using Entry=void(__fastcall*)(void*,void*);
// A real read-only IMAGE cell in this disposable host, not GTA memory.
Entry const dispatchTable[1]{original};
std::array<std::byte,0x134> hostPad{};
// Separate disposable IMAGE section, so protection tests cannot affect CRT data.
#pragma section(".gmpad",read,write)
__declspec(allocate(".gmpad")) alignas(4096) std::array<std::byte,8192> diagnosticPad{};
struct Borrow {
    np::OnFootPadStates before{},input{};
    bool cpp=false,seh=false,restored=false;
    unsigned calls=0;
};
bool write(const np::OnFootPadStates& values) noexcept {
    return ni::writePadStates(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(hostPad.data())),
        GetModuleHandleW(nullptr),true,values);
}
void borrowBody(void* value) {
    auto& b=*static_cast<Borrow*>(value);
    check(write(b.input),"real pad writer");++b.calls;
    if (b.cpp) throw std::runtime_error("original exception");
    if (b.seh) RaiseException(0xE1470001,0,0,nullptr);
}
void borrowRestore(void* value) noexcept {
    auto& b=*static_cast<Borrow*>(value);b.restored=write(b.before);
}
bool sehBorrow(Borrow* value) {
    __try { np::withFinally(borrowBody,borrowRestore,value); }
    __except(GetExceptionCode()==0xE1470001 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return true; }
    return false;
}
__declspec(noinline) void dispatch(void* self) {
    const auto* cell=reinterpret_cast<const volatile std::uint32_t*>(dispatchTable);
    reinterpret_cast<void(__thiscall*)(void*)>(*cell)(self);
}
void run(std::string_view which) {
    if(which.starts_with("pad_diagnostic_")){
        const auto image=GetModuleHandleW(nullptr);
        const auto at=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(diagnosticPad.data()));
        ni::PadWriteObservation seen{};np::OnFootPadStates values{};
        check(np::prepareOnFootPad({},{0,1,1},values),"same native encoder");
        if(which=="pad_diagnostic_context"){
            check(!ni::writePadStates(1,nullptr,false,values,&seen) && seen.step==ni::PadWriteStep::context &&
                !seen.queryBytes && !seen.writeCalled && !seen.readCalled,"denied before VirtualQuery, write or read");return;
        }
        if(which=="pad_diagnostic_query"){
            check(!ni::writePadStates(UINT32_MAX,image,true,values,&seen) && seen.step==ni::PadWriteStep::query &&
                seen.error && !seen.writeCalled,"real VirtualQuery failure and immediate Win32 error");return;
        }
        if(which=="pad_diagnostic_private"){
            void* memory=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);check(memory!=nullptr,"own disposable allocation");
            const bool result=ni::writePadStates(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(memory)),memory,true,values,&seen);
            check(VirtualFree(memory,0,MEM_RELEASE)!=FALSE,"free only own fixture");
            check(!result && seen.step==ni::PadWriteStep::type && seen.type==MEM_PRIVATE && !seen.writeCalled,"private memory still refused");return;
        }
        if(which=="pad_diagnostic_base"){
            check(!ni::writePadStates(at,nullptr,true,values,&seen) && seen.step==ni::PadWriteStep::allocation &&
                !seen.allocationMatches && !seen.writeCalled,"wrong allocation base remains blocked");return;
        }
        if(which=="pad_diagnostic_protection" || which=="pad_diagnostic_bounds"){
            const auto testAt=which=="pad_diagnostic_bounds" ? at+4096-48 : at;
            void* const protectAt=diagnosticPad.data()+(which=="pad_diagnostic_bounds" ? 4096 : 0);
            for(const DWORD protection: which=="pad_diagnostic_bounds" ? std::initializer_list<DWORD>{PAGE_READONLY} :
                std::initializer_list<DWORD>{PAGE_READONLY,PAGE_EXECUTE_READWRITE,PAGE_EXECUTE_WRITECOPY,PAGE_READWRITE|PAGE_GUARD}){
                DWORD old=0,ignored=0;check(VirtualProtect(protectAt,4096,protection,&old)!=FALSE,"disposable IMAGE only");
                const bool result=ni::writePadStates(testAt,image,true,values,&seen);
                check(VirtualProtect(protectAt,4096,old,&ignored)!=FALSE,"restore fixture protection before assertions");
                check(!result && seen.step==(which=="pad_diagnostic_bounds" ? ni::PadWriteStep::bounds : ni::PadWriteStep::protection) &&
                    !seen.writeCalled && !seen.readCalled && !seen.error,"predicate failure, no invented Win32 error or write");
            }return;
        }
        check(which=="pad_diagnostic_success","known diagnostic case");
        const auto before=diagnosticPad;
        check(ni::writePadStates(at,image,true,values,&seen) && seen.step==ni::PadWriteStep::complete && !seen.error &&
            seen.written==96 && seen.read==96 && seen.equal && seen.writeCalled && seen.readCalled && seen.allocationMatches &&
            seen.state==MEM_COMMIT && seen.type==MEM_IMAGE,"real write and exact readback; value-only trace");
        np::OnFootPadStates restore{};ni::PadWriteObservation cleanup{};
        check(ni::writePadStates(at,image,true,restore,&cleanup) && diagnosticPad==before &&
            seen.step==ni::PadWriteStep::complete && cleanup.step==ni::PadWriteStep::complete,"separate apply/restore observations, original memory restored");return;
    }
    if(which=="cleo_factory_diagnostics") {
        namespace cd=grandmp::windows::cleo_bootstrap;
        namespace sp=np::second_player;
        static_assert(std::is_trivially_copyable_v<sp::ApiCreateObservation> &&
            std::is_trivially_copyable_v<sp::NativeCreateObservation>);
        wchar_t temporary[MAX_PATH]{},directory[MAX_PATH]{};
        check(GetTempPathW(MAX_PATH,temporary) && GetTempFileNameW(temporary,L"gmf",0,directory),"exclusive factory diagnostic fixture");
        check(DeleteFileW(directory) && CreateDirectoryW(directory,nullptr),"own fixture only");
        const std::wstring prefix=std::wstring(directory)+L"/factory";
        cd::OnceLog log(prefix.c_str());
        sp::ApiCreateObservation api{};api.flags=3;api.slot=sp::CreateSlot::present;
        api.failure=sp::CreateResultFailure::incomplete_with_ped;api.slotRead={true,4,ERROR_SUCCESS};
        sp::NativeCreateObservation native{};native.flags=15;native.slot=sp::CreateSlot::present;
        native.fail(sp::CreateFailure::protected_state_changed);native.changedProtection=8;
        native.readFailure=true;native.requested=4;native.failedRead={false,3,ERROR_PARTIAL_COPY};
        cd::recordFactoryFailure(log,api,native);
        // Repeated callbacks must not overwrite the first independent results.
        native.failure=sp::CreateFailure::default_task;
        for(unsigned n=0;n<128;++n)cd::recordFactoryFailure(log,api,native);
        WIN32_FIND_DATAW entry{};const auto search=FindFirstFileW((prefix+L".*.log").c_str(),&entry);
        check(search!=INVALID_HANDLE_VALUE,"real final factory log exists");FindClose(search);
        const auto name=std::wstring(directory)+L"/"+entry.cFileName;
        const auto file=CreateFileW(name.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        check(file!=INVALID_HANDLE_VALUE,"read own factory log");
        char bytes[2048]{};DWORD size=0;check(ReadFile(file,bytes,sizeof(bytes)-1,&size,nullptr)!=FALSE,"read complete bounded output");CloseHandle(file);
        const std::string_view content(bytes,size);
        check(std::count(content.begin(),content.end(),'\n')==3 && !log.error(),"exactly three records per process, no truncation or per-frame logs");
        check(content.find("factory.result.false_with_ped win32=0 observed=0x0000000000000003 expected=0x0000000000000003 detail=0x0000000000000004")!=content.npos,
            "result bool, slot state and existing read length preserved independently");
        check(content.find("factory.create.protected_state_changed win32=0 observed=0x000000000000000F expected=0x0000000000000003 detail=0x0000000000000008")!=content.npos,
            "first native refusal and change mask retained");
        check(content.find("factory.create.first_failed_read win32=299 observed=0x0000000000000001 expected=0x0000000000000004 detail=0x0000000000000003")!=content.npos,
            "exact captured read error and byte counts, no fabricated Win32 error");
        check(content.find("default_task")==content.npos && content.find("pointer")==content.npos && content.find("address")==content.npos,
            "no overwritten diagnosis or address fields");
        // The helper accepts only enums, bounded flags/counts and MemoryRead,
        // never the retained PED, group image, camera image or a native address.
        for(const auto stage:{sp::CreateFailure::preparation,sp::CreateFailure::group_selection,sp::CreateFailure::spawn_refused,
            sp::CreateFailure::slot_read,sp::CreateFailure::slot_empty,sp::CreateFailure::group_ownership,
            sp::CreateFailure::default_task,sp::CreateFailure::protection_after})
            check(std::string_view(sp::createFailureName(stage)).starts_with("factory.create."),"fixed stage names only");
        check(DeleteFileW(name.c_str()) && RemoveDirectoryW(directory),"remove own fixture only");return;
    }
    if(which=="cleo_guard_redaction") {
        namespace cd=grandmp::windows::cleo_bootstrap;
        wchar_t temporary[MAX_PATH]{},directory[MAX_PATH]{};
        check(GetTempPathW(MAX_PATH,temporary) && GetTempFileNameW(temporary,L"gmr",0,directory),"exclusive redaction fixture");
        check(DeleteFileW(directory) && CreateDirectoryW(directory,nullptr),"own fixture only");
        unsigned sample=0;
        for(const bool installing:{false,true})for(const auto* reason:{"Image.matches.range","Image.matches.VirtualQuery",
            "Image.matches.page_identity","Image.matches.region_end","Image.matches.read",
            "Image.matches.original_control_cell","Image.matches.owned_control_cell","Image.matches.bytes"}){
            const std::wstring prefix=std::wstring(directory)+L"/redacted"+std::to_wstring(sample++);
            cd::OnceLog log(prefix.c_str());
            // Same sink called by the real refuse()/Image.matches() path. Both
            // a Hoodlum address and an ASLR-style module address are poison.
            check(!cd::recordRefusal(log,installing,cd::Note::ready_code,reason,ERROR_PARTIAL_COPY,
                0x0053C05C,0x6AB1EF00),"guard remains refused regardless of logging result");
            check(!cd::recordRefusal(log,installing,cd::Note::ready_code,"must_not_retry_or_replace",0,
                0xFFFFFFFF,0xFFFFFFFF),"second emission cannot replace first failure");
            WIN32_FIND_DATAW entry{};const auto search=FindFirstFileW((prefix+L".*.log").c_str(),&entry);
            check(search!=INVALID_HANDLE_VALUE,"real formatted redaction log exists");FindClose(search);
            const auto name=std::wstring(directory)+L"/"+entry.cFileName;
            const auto file=CreateFileW(name.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
            check(file!=INVALID_HANDLE_VALUE,"read own redaction output");
            char bytes[2048]{};DWORD size=0;check(ReadFile(file,bytes,sizeof(bytes)-1,&size,nullptr)!=FALSE,"read complete bounded log");CloseHandle(file);
            const std::string_view content(bytes,size);
            check(content.find(reason)!=content.npos && content.find("win32=299")!=content.npos,"reason and captured Win32 error retained");
            check(content.find("0053C05C")==content.npos && content.find("6AB1EF00")==content.npos &&
                content.find("FFFFFFFF")==content.npos,"neither original nor detail record exports untyped addresses");
            check(content.find("must_not_retry_or_replace")==content.npos,"first refusal only");
            check(static_cast<unsigned>(std::count(content.begin(),content.end(),'\n'))==(installing ? 2u : 1u),"one original plus at most one detail, never a retry");
            check(DeleteFileW(name.c_str()),"remove own log only");
        }
        check(RemoveDirectoryW(directory),"remove own empty fixture");return;
    }
    if(which=="cleo_init_diagnostics") {
        namespace cd=grandmp::windows::cleo_bootstrap;
        check(std::string_view(cd::senderEndPoint(108,true,false))=="network.local_sender_ended_runtime_terminal","hook refusal is not a normal disconnect");
        check(std::string_view(cd::senderEndPoint(4,true,false))=="network.local_sender_ended_operator_removed","operator removal distinguished");
        check(std::string_view(cd::senderEndPoint(105,true,false))=="network.local_sender_ended_script_deleted","script deletion distinguished");
        check(std::string_view(cd::senderEndPoint(2,false,true))=="network.local_sender_ended_local_source_stopped","capture fence distinguished");
        check(std::string_view(cd::senderEndPoint(2,false,false))=="network.local_sender_ended_unclassified_stop","no invented normal termination");
        wchar_t temporary[MAX_PATH]{},directory[MAX_PATH]{};
        check(GetTempPathW(MAX_PATH,temporary) && GetTempFileNameW(temporary,L"gmi",0,directory),"exclusive diagnostic fixture");
        check(DeleteFileW(directory) && CreateDirectoryW(directory,nullptr),"own fixture only");
        const std::wstring prefix=std::wstring(directory)+L"/init";
        cd::OnceLog log(prefix.c_str());
        check(log.emit(cd::Note::sync_failure,"input_hook_install_terminal_no_retry"),"original terminal event");
        check(log.emit(cd::Note::input_guard,"input_hook.guard.cell"),"independent guard event");
        check(log.emit(cd::Note::input_cell,"input_hook.cell_first_failure",0,static_cast<unsigned>(ni::CellFailure::page_protection)),"predicate failure even without a Win32 error");
        check(log.emit(cd::Note::input_cell_page,"input_hook.cell_page",0,PAGE_READONLY,PAGE_READWRITE|PAGE_WRITECOPY,
            (std::uint64_t{MEM_COMMIT}<<32)|MEM_IMAGE),"mapping values only");
        check(log.emit(cd::Note::network,"network.start"),"earlier network category already occupied");
        check(log.emit(cd::Note::network_end,cd::senderEndPoint(108,true,false),0,42,108,7),"terminal worker evidence cannot be suppressed by old network note");
        check(log.emit(cd::Note::receiver_session,"network.receiver_session",0,42),"session differs from first outgoing sample");
        check(log.emit(cd::Note::receiver_controls,"network.remote_controls_seen",0,43,1,9),"remote controls differ from local sender");
        for(unsigned n=0;n<32;++n)check(!log.emit(cd::Note::input_cell,"must_not_repeat"),"bounded first failure only");
        WIN32_FIND_DATAW entry{};const auto search=FindFirstFileW((prefix+L".*.log").c_str(),&entry);
        check(search!=INVALID_HANDLE_VALUE,"file emitted");FindClose(search);
        const auto name=std::wstring(directory)+L"/"+entry.cFileName;
        const auto file=CreateFileW(name.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        check(file!=INVALID_HANDLE_VALUE,"read own diagnostic output");
        char bytes[4096]{};DWORD size=0;check(ReadFile(file,bytes,sizeof(bytes)-1,&size,nullptr)!=FALSE,"read log");CloseHandle(file);
        const std::string_view content(bytes,size);
        check(content.find("input_hook.guard.cell")!=content.npos && content.find("input_hook.cell_first_failure")!=content.npos &&
            content.find("network.local_sender_ended_runtime_terminal")!=content.npos && content.find("network.remote_controls_seen")!=content.npos,"final log preserves independent stages");
        check(content.find(" ms=")!=content.npos && content.find("must_not_repeat")==content.npos && content.find("pointer")==content.npos && content.find("address")==content.npos,"bounded values and monotonic time, no native address fields");
        check(DeleteFileW(name.c_str()) && RemoveDirectoryW(directory),"remove only own fixture");return;
    }
    if(which=="cleo_installation_paths") {
        namespace cd=grandmp::windows::cleo_bootstrap;
        namespace fs=std::filesystem;
        const fs::path a(cd::installationA),b(cd::installationB);
        check(cd::installationRoot(a/L"gta_sa.exe")==cd::installationA,"exact A executable");
        check(cd::installationRoot(fs::path(L"D:\\Jogos\\GrandMP-Smoke-B\\gta_sa.exe"))==cd::installationB,"exact B executable, Windows separators");
        for(const auto* wrong:{L"D:/Jogos/GrandMP-Smoke-C/gta_sa.exe",L"D:/Jogos/GrandMP-Smoke-B-copy/gta_sa.exe",
            L"D:/Jogos/GrandMP-Smoke/../GrandMP-Smoke-B/gta_sa.exe",L"D:/alias/gta_sa.exe",
            L"C:/Jogos/GrandMP-Smoke/gta_sa.exe",L"gta_sa.exe",L"D:/Jogos/GrandMP-Smoke-B/gta_sa_compact.exe",
            L"//?/D:/Jogos/GrandMP-Smoke/gta_sa.exe"})check(!cd::installationRoot(wrong),"no arbitrary, alias, relative or dot-segment admission");
        check(a/L"cleo/GrandMPPlayerBootstrap.cleo"!=b/L"cleo/GrandMPPlayerBootstrap.cleo" &&
              a/L"CLEO.asi"!=b/L"CLEO.asi","modules from A cannot satisfy B's bound paths");
        const auto plain=[](const wchar_t*){return static_cast<DWORD>(FILE_ATTRIBUTE_NORMAL);};
        check(std::wstring_view(cd::installationLogPrefix(a/L"gta_sa.exe",plain))==L"D:/Jogos/GrandMP-Smoke/GrandMPPlayerBootstrap.Diagnostic","A log stays in A");
        check(std::wstring_view(cd::installationLogPrefix(b/L"gta_sa.exe",plain))==L"D:/Jogos/GrandMP-Smoke-B/GrandMPPlayerBootstrap.Diagnostic","B log stays in B");
        const auto file=b/L"cleo/GrandMPPlayerBootstrap.cleo";
        for(const auto& component:{file,b/L"cleo",b,fs::path(L"D:/Jogos"),fs::path(L"D:/")}){
            bool queried=false;
            const auto reparse=[&](const wchar_t* item){
                if(fs::path(item)==component){queried=true;return static_cast<DWORD>(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY);}
                return static_cast<DWORD>(FILE_ATTRIBUTE_NORMAL);
            };
            check(!cd::plainPath(file,reparse) && queried,"reject reparse at every level including ancestors");
        }
        check(!cd::plainPath(file,[](const wchar_t*){return INVALID_FILE_ATTRIBUTES;}),"unreadable/missing path fails closed");
        check(!cd::installationLogPrefix(b/L"gta_sa.exe",[](const wchar_t*){return static_cast<DWORD>(FILE_ATTRIBUTE_REPARSE_POINT);}),"no diagnostic redirection through a junction");
        cd::OnceLog noFallback(L"this-prefix-must-not-be-used",[]() noexcept -> const wchar_t* {return nullptr;});
        check(!noFallback.emit(cd::Note::entered,"denied_path") && noFallback.error()==ERROR_INVALID_NAME,"unknown root never falls back to A log");
        return;
    }
    if(which=="cleo_script_name") {
        namespace cd=grandmp::windows::cleo_bootstrap;
        constexpr char lower[8]="gmscm09",upper[8]="GMSCM09",mixed[8]="Gmscm09";
        check(cd::scriptNameMatches(lower),"exact native lowercase name admitted");
        check(!cd::scriptNameMatches(upper) && !cd::scriptNameMatches(mixed),"no generic case-insensitive match");
        check(cd::scriptNameBytes(lower)==0x676D73636D303900ULL,"all eight native bytes including trailing zero");
        check(cd::scriptNameBytes(upper)==0x474D53434D303900ULL,"old uppercase expectation differs");
        for(unsigned i=0;i<8;++i){
            char changed[8]{};std::memcpy(changed,lower,8);changed[i]^=1;
            check(!cd::scriptNameMatches(changed),"each byte authenticated, including byte seven");
        }
        char foreign[8]="gmscm08";
        check(!cd::scriptNameMatches(foreign),"another name never admitted");
        char highByte[8]="gmscm09";highByte[0]=static_cast<char>(0xFF);
        check(!cd::scriptNameMatches(highByte),"no locale or arbitrary byte normalization");
        check(static_cast<unsigned>(cd::Note::phase)==6,"existing failure note unchanged");return;
    }
    if(which=="cleo_diagnostics") {
        namespace cd=grandmp::windows::cleo_bootstrap;
        wchar_t temporary[MAX_PATH]{},directory[MAX_PATH]{};
        check(GetTempPathW(MAX_PATH,temporary)!=0,"test temp path");
        check(GetTempFileNameW(temporary,L"gmc",0,directory)!=0,"exclusive test fixture");
        check(DeleteFileW(directory) && CreateDirectoryW(directory,nullptr),"own fixture directory");
        const std::wstring prefix=std::wstring(directory)+L"/trace";
        cd::OnceLog log(prefix.c_str());
        check(log.emit(cd::Note::entered,"tick.entered"),"entry reaches file before guards");
        check(log.emit(cd::Note::registration,"DllMain.CLEO_RegisterOpcode.return",0,1,1,0x04040400),"raw SDK success recorded");
        check(log.emit(cd::Note::caller,"tick.return_address_mismatch",0,0x469EF7,0x469FF7),"early interrupt return mismatch recorded");
        const unsigned char operands[]{3,0,0,3,1,0};
        check(cd::operandImage(operands)==0x030000030100ULL,"six operand bytes, no script address");
        check(log.emit(cd::Note::operands,"tick.operands_read",ERROR_PARTIAL_COPY,3,6),"early operand interrupt recorded");
        check(log.emit(cd::Note::initialise,"initialise.GetModuleHandleExW",ERROR_ACCESS_DENIED),"init Win32 failure recorded");
        check(log.emit(cd::Note::phase,"phase.return_15",0,0x100,0x53BFCC),"phase refusal recorded");
        const char nativeName[8]="gmscm09";
        check(log.emit(cd::Note::script_name,"phase.script_name.bytes",0,cd::scriptNameBytes(nativeName),cd::scriptNameBytes(cd::nativeScriptName)),"eight actual and expected bytes recorded");
        check(log.emit(cd::Note::contract,"tick.consume",0,5,0,7),"existing contract result recorded");
        for(int n=0;n<100;++n)check(!log.emit(cd::Note::caller,"tick.return_address_mismatch"),"one record per failure, not per callback");
        check(!log.error(),"all bounded writes succeeded");
        WIN32_FIND_DATAW found{};const std::wstring pattern=std::wstring(directory)+L"/*.log";
        const HANDLE search=FindFirstFileW(pattern.c_str(),&found);
        check(search!=INVALID_HANDLE_VALUE,"session file produced");
        const std::wstring fileName=std::wstring(directory)+L"/"+found.cFileName;
        check(!FindNextFileW(search,&found) && GetLastError()==ERROR_NO_MORE_FILES,"one session filename");FindClose(search);
        const HANDLE file=CreateFileW(fileName.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        check(file!=INVALID_HANDLE_VALUE,"read own test log");
        char bytes[4096]{};DWORD count=0;
        check(ReadFile(file,bytes,sizeof(bytes)-1,&count,nullptr)!=0,"read full bounded fixture");CloseHandle(file);
        const std::string_view lines(bytes,count);
        unsigned records=0;for(char c:lines)if(c=='\n')++records;
        check(records==8,"no repeats or hidden frame log");
        check(lines.find("point=tick.entered")!=lines.npos && lines.find("win32=299")!=lines.npos,"entry and exact read error survive");
        check(lines.find("observed=0x0000000000469EF7 expected=0x0000000000469FF7")!=lines.npos,"observed/expected return address preserved");
        check(lines.find("observed=0x676D73636D303900 expected=0x676D73636D303900")!=lines.npos,"full hex name retains zero padding");
        check(lines.find("point=DllMain.CLEO_RegisterOpcode.return win32=0 observed=0x0000000000000001")!=lines.npos,"loading a plugin is not SDK success proof");
        const std::wstring badPrefix=std::wstring(directory)+L"/absent/trace";
        cd::OnceLog refused(badPrefix.c_str());
        check(!refused.emit(cd::Note::caller,"tick.return_address_read",ERROR_PARTIAL_COPY),"file failure remains observable, no throw");
        check(refused.error()==ERROR_PATH_NOT_FOUND,"file-create Win32 error captured");
        check(!refused.emit(cd::Note::caller,"retry_not_allowed"),"failed logging point not retried");
        cd::OnceLog truncated(prefix.c_str());const std::string longPoint(400,'X');
        check(!truncated.emit(cd::Note::entered,longPoint.c_str()) && truncated.error()==ERROR_INSUFFICIENT_BUFFER,"explicit truncation refusal");
        check(DeleteFileW(fileName.c_str()) && RemoveDirectoryW(directory),"only own fixture removed");return;
    }
    if(which=="cleo_scheduler") {
        constexpr std::uintptr_t base=0x10000000,script=0x12345678;
        std::array<std::uint32_t,16> good{0x469FF7,script,0,0,base+0xEF63,0,0x46A220};
        good[15]=0x53BFCC;
        using grandmp::windows::cleo_bootstrap::schedulerStack;
        check(schedulerStack(good,base,script),"authenticated fixed scheduler stack shape");
        for(unsigned field:{0u,1u,4u,6u,15u}) {auto bad=good;++bad[field];check(!schedulerStack(bad,base,script),"wrong phase/VM/thread identity refused");}
        check(!schedulerStack(good,0,script) && !schedulerStack(good,base,0),"no presumed module or script");
        auto standalone=good;standalone[0]=0x469EF7;
        check(!schedulerStack(standalone,base,script),"ProcessOneCommand is not scheduler proof");return;
    }
    if (which=="gate") { check(!ni::executionApproved,"default physical input gate closed");return; }
    if (which=="dispatch_roles") {
        using Target=ni::DispatchTarget;
        check(ni::classifyDispatch(101,101,202)==Target::local_player,"authenticated CJ remains local");
        check(ni::classifyDispatch(202,101,202)==Target::remote_player,"authenticated owned remote");
        check(ni::classifyDispatch(303,101,202)==Target::other_ped,"foreign original only");
        check(ni::classifyDispatch(0,101,202)==Target::unproven,"null dispatch refused");
        check(ni::classifyDispatch(202,0,202)==Target::unproven,"unproven local identity");
        check(ni::classifyDispatch(202,101,0)==Target::unproven,"unproven remote identity");
        check(ni::classifyDispatch(101,101,101)==Target::unproven,"slot alias never selects remote input");
        check(ni::classifyDispatch(UINT32_MAX,101,202)==Target::other_ped,"comparison never dereferences an address");
        check(!originals && !wrappers,"routing itself never synthesizes engine dispatch");return;
    }
    if (which=="pad_scope" || which=="pad_exception" || which=="pad_seh" || which=="pad_context") {
        if (which=="pad_context") {
            check(!ni::writePadStates(1,nullptr,false,{}),"denied context returns before any invalid memory access");return;
        }
        hostPad.fill(std::byte{0x45});const auto before=hostPad;
        Borrow b{};b.cpp=which=="pad_exception";b.seh=which=="pad_seh";
        std::memcpy(b.before.current.data(),hostPad.data(),0x30);
        std::memcpy(b.before.previous.data(),hostPad.data()+0x30,0x30);
        check(np::prepareOnFootPad({},{0,1,1},b.input),"same codec as runtime");
        if (b.seh) check(sehBorrow(&b),"SEH propagated after finally");
        else { try { np::withFinally(borrowBody,borrowRestore,&b); } catch (const std::runtime_error&) { check(b.cpp,"expected original exception"); } }
        check(b.calls==1 && b.restored && hostPad==before,"one original-shaped body and exact restore, untouched tail");return;
    }
    if (which=="basis" || which=="neutral" || which=="invalid") {
        grandmp::core::PlayerControls result{};
        if (which=="invalid") {
            check(!ni::cameraRelative({0,1,1},0,NAN,result),"no invalid live camera");
            check(!ni::cameraRelative({INFINITY,0,1},0,0,result),"no invalid input");return;
        }
        if (which=="neutral") {
            check(ni::cameraRelative({},1,-2,result) && result==grandmp::core::PlayerControls{},"neutral stays neutral");return;
        }
        check(ni::cameraRelative({0,1,1},0,0,result) && std::abs(result.forward-1)<1e-5f &&
            std::abs(result.right)<1e-5f,"same basis forward");
        check(ni::cameraRelative({0,1,1},0,1.5707963f,result) && std::abs(result.right+1)<1e-5f &&
            std::abs(result.forward)<1e-5f,"camera rotation compensated without writing camera");
        np::OnFootPadStates pad{};
        check(np::prepareOnFootPad({},result,pad),"actual pad codec");
        std::int16_t lr=0;std::memcpy(&lr,pad.current.data(),2);check(lr==-60,"native axis sign");
        check(ni::cameraRelative({1,1,1},0,2.3f,result) && std::hypot(result.right,result.forward)<=1.00001f,"bounded diagonal");
        return;
    }
    ni::HookCell hook;
    const auto at=const_cast<std::uint32_t*>(reinterpret_cast<const std::uint32_t*>(dispatchTable));
    const auto from=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&original));
    const auto to=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&wrapper));
    const auto image=GetModuleHandleW(nullptr);
    if(which.starts_with("cleo_cell_")){
        DWORD old=0,ignored=0;
        check(VirtualProtect(at,4,PAGE_READWRITE,&old)!=FALSE,"disposable .rdata fixture only");
        if(which=="cleo_cell_badpage")check(VirtualProtect(at,4,PAGE_EXECUTE_READWRITE,&ignored)!=FALSE,"unexpected fixture protection");
        if(which=="cleo_cell_readonly")check(VirtualProtect(at,4,PAGE_READONLY,&ignored)!=FALSE,"read-only fixture");
        const auto policy=ni::CellPage::cleo_writable_rdata;
        if(which=="cleo_cell_rw"){
            ni::HookCell legacy;
            check(!legacy.install(at,from,to,image),"legacy RO contract remains closed on RW");
            MEMORY_BASIC_INFORMATION before{};check(VirtualQuery(at,&before,sizeof(before))!=0,"observe own fixture mapping");
            check(hook.install(at,from,to,image,policy) && hook.installed(),"authenticated writable DATA cell CAS");
            check(hook.observation().failure==ni::CellFailure::none && hook.observation().readBytes==4,"diagnostic does not invent a failure on successful install");
            int local=0;dispatch(&local);check(originals==1 && wrappers==1,"original once through writable-cell wrapper");
            check(hook.remove() && !hook.installed() && *at==from,"CAS restores original without changing page protection");
            MEMORY_BASIC_INFORMATION page{};check(VirtualQuery(at,&page,sizeof(page)) &&
                (page.Protect==before.Protect || (before.Protect==PAGE_WRITECOPY && page.Protect==PAGE_READWRITE)),"only own successful COW write promotes mapping to private RW");
        }else if(which=="cleo_cell_protection_change"){
            check(hook.install(at,from,to,image,policy),"publish before competing protection change");
            check(VirtualProtect(at,4,PAGE_READONLY,&ignored)!=FALSE,"test-only competing protection change");
            check(!hook.remove() && hook.installed() && *at==to,"unrelated protection drift blocks removal, not normalized");
            check(hook.observation().failure==ni::CellFailure::page_protection,"observed guard refusal preserved without a Win32 call failure");
            check(VirtualProtect(at,4,PAGE_READWRITE,&ignored)!=FALSE,"restore fixture access");
            InterlockedExchange(reinterpret_cast<volatile LONG*>(at),static_cast<LONG>(from));
        }else if(which=="cleo_cell_tamper"){
            check(hook.install(at,from,to,image,policy),"publish own host cell");
            InterlockedExchange(reinterpret_cast<volatile LONG*>(at),static_cast<LONG>(from+1));
            check(!hook.remove() && hook.installed() && *at==from+1,"unknown competing writer is never overwritten");
            check(hook.observation().failure==ni::CellFailure::preimage_mismatch,"unknown target diagnosed without exporting its value");
            InterlockedExchange(reinterpret_cast<volatile LONG*>(at),static_cast<LONG>(from)); // Test-owned fixture only.
        }else{
            const auto target=which=="cleo_cell_misaligned" ? reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uintptr_t>(at)+1) : at;
            const auto expected=which=="cleo_cell_mismatch" ? from+1 : from;
            const auto base=which=="cleo_cell_wrongbase" ? reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(image)+4096) : image;
            check(!hook.install(target,expected,to,base,policy) && !hook.installed() && *at==from,"bad preimage/protection/alignment refused before CAS");
            const auto reason=which=="cleo_cell_mismatch" ? ni::CellFailure::preimage_mismatch :
                which=="cleo_cell_misaligned" ? ni::CellFailure::source_rdata :
                which=="cleo_cell_wrongbase" ? ni::CellFailure::allocation_base : ni::CellFailure::page_protection;
            check(hook.observation().failure==reason,"first exact cell predicate captured");
            check(!hook.install(at,from,to,image,policy),"no retry after refusal");
            check(hook.observation().failure==reason,"refused retry cannot overwrite the primary diagnosis");
        }
        check(VirtualProtect(at,4,old,&ignored)!=FALSE,"restore own fixture page");return;
    }
    if (which=="mismatch") {
        check(!hook.install(at,from+1,to,image) && !hook.installed() && *at==from,"unknown original refused");
        check(!hook.install(at,from,to,image),"no second installation after refusal");return;
    }
    check(hook.install(at,from,to,image) && hook.installed(),"exact cell publication");
    if (which=="tamper") {
        DWORD old=0,ignored=0;check(VirtualProtect(at,4,PAGE_READWRITE,&old)!=FALSE,"host permission");
        *at=from+1;check(VirtualProtect(at,4,old,&ignored)!=FALSE,"host permission restore");
        check(!hook.remove() && hook.installed() && *at==from+1,"never overwrite unknown cell during cleanup");return;
    }
    int local=0,remote=1;
    for (unsigned n=0;n<1000;++n) { dispatch(&local);check(observedThis==&local,"local ECX");dispatch(&remote);check(observedThis==&remote,"remote ECX"); }
    check(originals==2000 && wrappers==2000,"exactly one original per engine-shaped dispatch, balanced stack");
    check(!hook.install(at,from,to,image),"no duplicate installation");
    check(hook.remove() && !hook.installed() && *at==from,"original cell restored");
    dispatch(&local);check(originals==2001 && wrappers==2000,"passthrough after restoration");
}
int main(int argc,char** argv) {
    try { if (argc!=2) return 2;run(argv[1]);return 0; }
    catch (const std::exception& e) { std::fprintf(stderr,"%s\n",e.what());return 1; }
}
