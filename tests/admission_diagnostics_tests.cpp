// Actual admitExecutable(), read-only. Win32/filesystem failures are injected;
// the optional admitted Hoodlum image is only read/hashed, never loaded/executed.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <psapi.h>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string_view>
#include <string>
#include <limits>
#include "../client/windows/tools/ped_smoke_addresses.hpp"
namespace ps = grandmp::windows::ped_smoke;
namespace {
constexpr std::array<std::uint32_t,2> cowPages{0x53B000,0x53C000};
std::string_view scenario;
std::wstring fixtureImagePath;
unsigned canonicalCalls=0, opens=0, reads=0, hashes=0, memoryReads=0;
unsigned workingSetReads=0;
DWORD WINAPI admissionPath(HMODULE m, LPWSTR out, DWORD capacity) {
    const bool own=m!=nullptr;
    if(scenario==(own ? "own_path" : "game_path")){SetLastError(ERROR_BAD_PATHNAME);return 0;}
    if(scenario==(own ? "own_truncated" : "game_truncated")){SetLastError(ERROR_INSUFFICIENT_BUFFER);return capacity;}
    const wchar_t* text=own ? L"D:/Jogos/GrandMP-Smoke/scripts/GrandMPNativePlayerSyncExperimental.asi" : L"D:/Jogos/GrandMP-Smoke/gta_sa.exe";
    wcscpy_s(out,capacity,text);return static_cast<DWORD>(wcslen(text));
}
HMODULE WINAPI admissionModule(LPCWSTR) {
    if(scenario=="module_missing"){SetLastError(ERROR_MOD_NOT_FOUND);return nullptr;}
    return reinterpret_cast<HMODULE>(scenario=="image_base" ? 0x500000 : 0x400000);
}
HANDLE WINAPI admissionOpen(LPCWSTR, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security, DWORD creation, DWORD flags, HANDLE t) {
    ++opens;
    if(access!=GENERIC_READ || share!=FILE_SHARE_READ || creation!=OPEN_EXISTING)ExitProcess(90);
    if(scenario=="open"){SetLastError(ERROR_SHARING_VIOLATION);return INVALID_HANDLE_VALUE;}
    return CreateFileW(fixtureImagePath.c_str(),access,share,security,creation,flags,t);
}
BOOL WINAPI admissionSize(HANDLE file, PLARGE_INTEGER size) {
    if(scenario=="size_read"){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    const BOOL okay=GetFileSizeEx(file,size);if(okay && scenario=="size")--size->QuadPart;return okay;
}
BOOL WINAPI admissionRead(HANDLE file, LPVOID out, DWORD bytes, LPDWORD got, LPOVERLAPPED overlap) {
    ++reads;
    if(scenario=="file_read"){*got=0;SetLastError(ERROR_READ_FAULT);return FALSE;}
    const BOOL okay=ReadFile(file,out,bytes,got,overlap);
    if(okay && scenario=="file_partial")--*got;
    return okay;
}
NTSTATUS WINAPI admissionProvider(BCRYPT_ALG_HANDLE* h,LPCWSTR a,LPCWSTR i,ULONG flags) {
    if(scenario=="provider")return static_cast<NTSTATUS>(0xC0000001UL);
    return BCryptOpenAlgorithmProvider(h,a,i,flags);
}
NTSTATUS WINAPI admissionHash(BCRYPT_ALG_HANDLE h,PUCHAR secret,ULONG n,PUCHAR in,ULONG size,PUCHAR out,ULONG bytes) {
    ++hashes;
    if(scenario=="hash")return static_cast<NTSTATUS>(0xC0000001UL);
    const auto status=BCryptHash(h,secret,n,in,size,out,bytes);
    if(BCRYPT_SUCCESS(status) && scenario=="identity")out[0]^=1;
    return status;
}
BOOL WINAPI admissionMemory(HANDLE,LPCVOID,LPVOID,SIZE_T,SIZE_T*);
SIZE_T WINAPI admissionQuery(LPCVOID address,PMEMORY_BASIC_INFORMATION page,SIZE_T size) {
    if(size!=sizeof(*page))ExitProcess(90);
    *page={};page->State=MEM_COMMIT;page->Type=MEM_IMAGE;
    page->AllocationBase=reinterpret_cast<void*>(0x400000);
    page->BaseAddress=reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(address)&~std::uintptr_t{0xFFF});
    page->RegionSize=0x10000;
    const auto pageAddress=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(page->BaseAddress));
    const bool cowScenario=scenario.starts_with("live_cow") || scenario=="post_hook";
    page->Protect=scenario=="live_page" ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
    page->AllocationProtect=PAGE_EXECUTE_READ;
    if(cowScenario && (pageAddress==cowPages[0] || pageAddress==cowPages[1])) {
        page->Protect=PAGE_EXECUTE_WRITECOPY;page->AllocationProtect=PAGE_EXECUTE_WRITECOPY;page->RegionSize=0x1000;
        if(scenario=="live_cow_page2_bad" && pageAddress==cowPages[1])page->Protect=PAGE_EXECUTE_READ;
        if(scenario=="live_cow_page2_type" && pageAddress==cowPages[1])page->Type=MEM_PRIVATE;
        if(scenario=="live_cow_page2_base" && pageAddress==cowPages[1])page->AllocationBase=reinterpret_cast<void*>(0x500000);
        if(scenario=="live_cow_page2_alloc_protect" && pageAddress==cowPages[1])page->AllocationProtect=PAGE_EXECUTE_READ;
        if(scenario=="live_cow_page2_span" && pageAddress==cowPages[1])page->RegionSize=0x800;
    }
    return sizeof(*page);
}
BOOL WINAPI admissionWorkingSet(HANDLE process,PVOID buffer,DWORD size) {
    ++workingSetReads;
    if(process!=GetCurrentProcess() || size!=sizeof(PSAPI_WORKING_SET_EX_INFORMATION))ExitProcess(90);
    auto& info=*static_cast<PSAPI_WORKING_SET_EX_INFORMATION*>(buffer);
    const auto address=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(info.VirtualAddress));
    info.VirtualAttributes.Flags=0;
    if(scenario=="live_cow_ws_failed") {SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    info.VirtualAttributes.Valid=1;
    info.VirtualAttributes.Shared=1;
    if(scenario=="live_cow_ws_invalid")info.VirtualAttributes.Valid=0;
    if(scenario=="live_cow_private_before_patch" && address==cowPages[0])info.VirtualAttributes.Shared=0;
    if(scenario=="post_hook" && address==cowPages[1])info.VirtualAttributes.Shared=0;
    return TRUE;
}
}
#define ReadProcessMemory admissionMemory
#define VirtualQuery admissionQuery
#define K32QueryWorkingSetEx admissionWorkingSet
#include "../client/windows/tools/ped_smoke_admission_diagnostics.hpp"
namespace grandmp::windows::ped_smoke {
std::filesystem::path admissionCanonicalProbe(const std::filesystem::path& input) {
    ++canonicalCalls;
    if(scenario==(canonicalCalls==1 ? "own_canonical" : "game_canonical"))
        throw std::filesystem::filesystem_error("fixture",std::error_code(ERROR_ACCESS_DENIED,std::system_category()));
    if(scenario=="own_mismatch" && canonicalCalls==1)return L"D:/unknown/own.asi";
    if(scenario=="game_mismatch" && canonicalCalls==2)return L"D:/unknown/gta_sa.exe";
    if(scenario=="pair" && canonicalCalls==1)return L"D:/Jogos/GrandMP-Smoke-B/scripts/GrandMPNativePlayerSyncExperimental.asi";
    return input;
}
}
#define GetModuleFileNameW admissionPath
#define GetModuleHandleW admissionModule
#define CreateFileW admissionOpen
#define GetFileSizeEx admissionSize
#define ReadFile admissionRead
#define BCryptOpenAlgorithmProvider admissionProvider
#define BCryptHash admissionHash
#define canonicalForAdmission admissionCanonicalProbe
#include "../client/windows/tools/ped_smoke.cpp"
#undef GetModuleFileNameW
#undef GetModuleHandleW
#undef CreateFileW
#undef GetFileSizeEx
#undef ReadFile
#undef BCryptOpenAlgorithmProvider
#undef BCryptHash
#undef ReadProcessMemory
#undef VirtualQuery
#undef K32QueryWorkingSetEx
#undef canonicalForAdmission
namespace {
BOOL WINAPI admissionMemory(HANDLE,LPCVOID address,LPVOID out,SIZE_T size,SIZE_T* got) {
    ++memoryReads;
    if(scenario=="live_read"){*got=0;SetLastError(ERROR_PARTIAL_COPY);return FALSE;}
    const auto at=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(address));
    if(at==0x408B25 && size==4)std::memcpy(out,&ps::streamingTable,size);
    else if(at==ps::imageBase && size<=executable.size())std::memcpy(out,executable.data(),size);
    else if(scenario=="post_hook" && at==ps::phaseCall && size==installedCall.size())std::memcpy(out,installedCall.data(),size);
    else {
        const auto* data=diskBytes(at,size);if(!data)ExitProcess(90);
        std::memcpy(out,data,size);
    }
    *got=(scenario=="live_partial" || (scenario=="live_cow_partial" && at==0x53BEE0)) ? size-1 : size;
    if((scenario=="live_bytes" && at!=0x408B25) || (scenario=="live_cow_bytes" && at==0x53BEE0))
        static_cast<unsigned char*>(out)[0]^=1;
    return TRUE;
}
void require(bool okay,const char* why){if(!okay)throw std::runtime_error(why);}
}
int wmain(int argc,wchar_t** argv) {
    if(argc<2 || argc>3)return 2;
    const std::wstring_view wide=argv[1];scenario=std::string_view{};
    std::string name;for(const auto c:wide){if(c>127)return 2;name.push_back(static_cast<char>(c));}scenario=name;
    if(argc==3)fixtureImagePath=argv[2];
    try {
        static_assert(!ps::physicalExecutionApproved);
        module=reinterpret_cast<HMODULE>(1);
        ps::AdmissionFailure f{};
        if(scenario=="format") {
            ps::admissionRefuse(f,"asi_canonical","canonical_failed",ERROR_ACCESS_DENIED);
            ps::admissionRefuse(f,"later","lost",123);
            require(std::string_view(f.stage)=="asi_canonical" && f.error==ERROR_ACCESS_DENIED,"first failure retained");
            f.requested=f.transferred=MAXDWORD;f.status=MAXDWORD;f.filesystemError=std::numeric_limits<int>::min();
            char line[352]{};require(ps::formatAdmissionFailure(line,sizeof(line),f),"full formatting");
            require(std::string_view(line).find("win32=5")!=std::string_view::npos,"Win32 retained");
            char shortLine[8]{};require(!ps::formatAdmissionFailure(shortLine,sizeof(shortLine),f) && !shortLine[0],"truncation explicit");
            return 0;
        }
        if(scenario=="post_hook") {
            require(ps::encodePhaseCall(reinterpret_cast<std::uintptr_t>(&ps::phaseThunk),installedCall),"test exact own hook encoding");
            hookPublished.store(true);
        }
        const bool okay=admitExecutable(&f);
        if(okay!=(scenario=="success" || scenario=="live_cow" || scenario=="post_hook")) {
            std::fprintf(stderr,"admission observed stage=%s category=%s code=%s address=%08lX protect=%02lX error=%lu\n",
                f.stage,f.category,f.code.category,static_cast<unsigned long>(f.code.address),
                f.code.observedProtection,f.code.error);
        }
        require(okay==(scenario=="success" || scenario=="live_cow" || scenario=="post_hook"),"admission result");
        require(callbacks.load()==0 && hookPublished.load()==(scenario=="post_hook") && preparation.load()==0,"admission cannot drive engine");
        if(scenario=="success")require(hashes==1 && memoryReads>0 && canonicalCalls==2,"full read-only success path");
        else if(scenario=="live_cow" || scenario=="post_hook")
            require(hashes==1 && memoryReads>0 && workingSetReads>=2,"both authenticated image pages and complete code are checked");
        else {
            const auto expected=[&]() -> std::string_view {
                if(scenario=="own_path" || scenario=="own_truncated")return "asi_module_path";
                if(scenario=="game_path" || scenario=="game_truncated")return "game_module_path";
                if(scenario=="image_base" || scenario=="module_missing")return "image_base";
                if(scenario=="own_canonical")return "asi_canonical";
                if(scenario=="game_canonical")return "game_canonical";
                if(scenario=="own_mismatch" || scenario=="game_mismatch" || scenario=="pair")return "canonical_paths";
                if(scenario=="open")return "executable_open";
                if(scenario=="size" || scenario=="size_read")return "executable_size";
                if(scenario=="file_read" || scenario=="file_partial")return "executable_read";
                if(scenario=="provider")return "sha256_provider";
                if(scenario=="hash")return "sha256";
                if(scenario=="identity")return "executable_identity";
                return "live_code";
            }();
            require(f.stage==expected,"specific failed stage");
            if(opens==0)require(reads==0 && hashes==0 && memoryReads==0,"early failure short circuits");
            if(scenario=="own_canonical")require(canonicalCalls==1,"no game canonical after own failure");
            if(scenario=="image_base" || scenario=="module_missing")require(canonicalCalls==0,"no canonical after base failure");
            if(scenario=="own_mismatch" || scenario=="game_mismatch" || scenario=="pair")require(opens==0,"no file after path mismatch");
            if(scenario=="open")require(f.error==ERROR_SHARING_VIOLATION,"open immediate error");
            if(scenario=="file_read")require(f.error==ERROR_READ_FAULT && f.transferred==0,"read immediate error");
            if(scenario=="file_partial")require(f.error==0 && f.transferred+1==f.requested && hashes==0,"successful short read rejected");
            if(scenario=="own_canonical" || scenario=="game_canonical")require(f.filesystemError==5 && f.filesystemSystem,"filesystem error domain");
            if(scenario=="hash" || scenario=="provider")require(f.status==0xC0000001UL && f.error==0,"NTSTATUS not Win32");
                if(scenario=="live_page")require(std::string_view(f.code.category)=="page_protection" && f.code.observedProtection==PAGE_EXECUTE_READWRITE && f.code.expectedProtection==PAGE_EXECUTE_READ,"RWX remains refused");
            if(scenario=="live_bytes")require(std::string_view(f.code.category)=="bytes_mismatch","live bytes refused");
            if(scenario=="live_read")require(f.code.error==ERROR_PARTIAL_COPY,"live read immediate error");
            if(scenario=="live_partial")require(std::string_view(f.code.category)=="read_incomplete","live partial rejected");
            if(scenario=="live_cow_page2_bad")require(std::string_view(f.code.category)=="page_protection" && f.code.address==cowPages[1],"second page unexpected protection refused");
            if(scenario=="live_cow_page2_type")require(std::string_view(f.code.category)=="page_type" && f.code.address==cowPages[1],"second page non-image mapping refused");
            if(scenario=="live_cow_page2_base")require(std::string_view(f.code.category)=="image_allocation" && f.code.address==cowPages[1],"second page allocation mismatch refused");
            if(scenario=="live_cow_page2_alloc_protect")require(std::string_view(f.code.category)=="page_protection" && f.code.address==cowPages[1],"second page allocation protection mismatch refused");
            if(scenario=="live_cow_page2_span")require(std::string_view(f.code.category)=="page_span" && f.code.address==cowPages[1],"second page incomplete region refused");
            if(scenario=="live_cow_ws_failed")require(std::string_view(f.code.category)=="working_set_query_failed" && f.code.error==ERROR_ACCESS_DENIED,"working set failure retained");
            if(scenario=="live_cow_ws_invalid")require(std::string_view(f.code.category)=="working_set_not_resident","nonresident page refused");
            if(scenario=="live_cow_private_before_patch")require(std::string_view(f.code.category)=="working_set_private_unowned","unowned COW page refused");
            if(scenario=="live_cow_bytes")require(std::string_view(f.code.category)=="bytes_mismatch","COW byte mismatch refused");
            if(scenario=="live_cow_partial")require(std::string_view(f.code.category)=="read_incomplete","COW partial read refused");
        }
        if(executableFile!=INVALID_HANDLE_VALUE)CloseHandle(executableFile);
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}
}
