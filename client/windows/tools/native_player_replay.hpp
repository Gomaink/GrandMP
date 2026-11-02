#pragma once

#include "grandmp/windows/native_player_input.hpp"
#include "ped_smoke_runtime_read.hpp"
#include <algorithm>
#include <windows.h>
#include "native_pad_contract.hpp"

namespace grandmp::windows::remote_player_binding::replay {
namespace ps=ped_smoke;
#if defined(GRANDMP_NATIVE_INPUT_REPLAY_BUILD) && GRANDMP_NATIVE_INPUT_REPLAY_BUILD == 1 && \
    defined(GRANDMP_NATIVE_SECOND_PLAYER_EXECUTION_BUILD) && GRANDMP_NATIVE_SECOND_PLAYER_EXECUTION_BUILD == 1 && \
    defined(GRANDMP_PED_SMOKE_PHYSICAL_BUILD) && GRANDMP_PED_SMOKE_PHYSICAL_BUILD == 1
inline constexpr bool executionApproved=true;
#else
inline constexpr bool executionApproved=false;
#endif
// Exact Hoodlum image only. Instruction evidence: docs/native-onfoot-replay.md.
inline constexpr std::uint32_t original=0x60EA90, cell=0x86D190;
inline constexpr std::uint32_t cameraHeading=0xB6F178, weaponCameraMode=0xB6F858;
inline constexpr std::array<std::uint32_t,3> callers{0x568561,0x568642,0x5686F4};

enum class DispatchTarget { unproven, local_player, remote_player, other_ped };
// local/remote are current authenticated identities, or zero on failed proof.
// other_ped means only an unmanaged native dispatch, NOT ownership/type proof
// for a third party. Compare values; never dereference ped or export addresses.
inline DispatchTarget classifyDispatch(std::uint32_t ped,std::uint32_t local,
                                       std::uint32_t remote) noexcept {
    if (!ped || !local || !remote || local==remote) return DispatchTarget::unproven;
    if (ped==local) return DispatchTarget::local_player;
    return ped==remote ? DispatchTarget::remote_player : DispatchTarget::other_ped;
}

// Zelda computes atan2(-LR,-UD) - cameraHeading. Rotate the input basis,
// rather than replacing focus/camera/aim globals. No world pose/velocity writer.
inline bool cameraRelative(core::PlayerControls input,float remoteView,float liveView,
                           core::PlayerControls& output) noexcept {
    core::OnFootState checked{};checked.controls=input;checked.viewHeading=remoteView;
    if (!core::validOnFoot(checked) || !std::isfinite(liveView)) return false;
    const float length=std::hypot(input.right,input.forward);
    if (length>1) { input.right/=length;input.forward/=length; }
    const float angle=liveView-remoteView,c=std::cos(angle),s=std::sin(angle);
    output={std::clamp(input.right*c-input.forward*s,-1.f,1.f),
            std::clamp(input.right*s+input.forward*c,-1.f,1.f),input.buttons};
    return true;
}

// The caller authenticates ownership and the exact address before lending it.
// Kept separate to exercise the real Win32 writer/finally in a disposable IMAGE.
enum class PadWriteStep : unsigned { not_called, context, query, state, type, allocation,
    protection, overflow, bounds, write, write_size, readback, compare, complete, post_mapping };
inline const char* padWriteStepName(PadWriteStep step) noexcept {
    constexpr const char* names[]{"not_called","context","query","state","type","allocation",
        "protection","overflow","bounds","write","write_size","readback","compare","complete","post_mapping"};
    const auto index=static_cast<unsigned>(step);return index<std::size(names) ? names[index] : "invalid";
}
// Values from the EXISTING calls only. No addresses, pad bytes or extra query.
struct PadWriteObservation {
    PadWriteStep step=PadWriteStep::not_called;
    DWORD error=0,state=0,type=0,protection=0,allocationProtection=0;
    std::size_t queryBytes=0,written=0,read=0;
    bool allocationMatches=false,writeCalled=false,writeReturned=false,readCalled=false,readReturned=false,equal=false;
};
inline bool writePadStates(std::uint32_t at,void* imageBase,bool context,
                           const OnFootPadStates& values,PadWriteObservation* observation=nullptr,
                           const HoodlumPadContract* padContract=nullptr) noexcept {
    if(observation)*observation={};
    const auto finish=[&](PadWriteStep step,bool okay=false,DWORD error=0) noexcept {
        if(observation){observation->step=step;observation->error=error;}return okay;
    };
    if (!context) return finish(PadWriteStep::context);
    MEMORY_BASIC_INFORMATION page{};
    const auto queried=VirtualQuery(reinterpret_cast<void*>(at),&page,sizeof(page));
    const DWORD queryError=queried ? 0 : GetLastError();
    if(observation){
        observation->queryBytes=queried;observation->state=page.State;observation->type=page.Type;
        observation->protection=page.Protect;observation->allocationProtection=page.AllocationProtect;
        observation->allocationMatches=page.AllocationBase==imageBase;
    }
    if(!queried)return finish(PadWriteStep::query,false,queryError);
    if(page.State!=MEM_COMMIT)return finish(PadWriteStep::state);
    if(page.Type!=MEM_IMAGE)return finish(PadWriteStep::type);
    if(page.AllocationBase!=imageBase)return finish(PadWriteStep::allocation);
    const bool exceptional=page.Protect!=PAGE_READWRITE && page.Protect!=PAGE_WRITECOPY;
    if(exceptional){
        if(queried!=sizeof(page) || !padContract || !padContract->permits(at,imageBase,page))return finish(PadWriteStep::protection);
    }
    if(at>UINT32_MAX-0x60)return finish(PadWriteStep::overflow);
    if(at+0x60>reinterpret_cast<std::uintptr_t>(page.BaseAddress)+page.RegionSize)return finish(PadWriteStep::bounds);
    std::array<std::byte,0x60> bytes{},actual{};
    std::memcpy(bytes.data(),values.current.data(),0x30);
    std::memcpy(bytes.data()+0x30,values.previous.data(),0x30);
    SIZE_T done=0;
    const BOOL written=WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),bytes.data(),bytes.size(),&done);
    const DWORD writeError=written ? 0 : GetLastError();
    if(observation){observation->writeCalled=true;observation->writeReturned=written!=FALSE;observation->written=done;}
    if(!written)return finish(PadWriteStep::write,false,writeError);
    if(done!=bytes.size())return finish(PadWriteStep::write_size);
    const auto read=ps::readMemory(at,actual.data(),actual.size());
    if(observation){observation->readCalled=true;observation->readReturned=read.success;observation->read=read.transferred;}
    if(!read.complete(actual.size()))return finish(PadWriteStep::readback,false,read.error);
    if(observation)observation->equal=actual==bytes;
    if(actual==bytes && exceptional){
        MEMORY_BASIC_INFORMATION after{};
        const auto count=VirtualQuery(reinterpret_cast<void*>(at),&after,sizeof(after));
        const DWORD error=count ? 0 : GetLastError();
        if(count!=sizeof(after) || !padContract->permits(at,imageBase,after))return finish(PadWriteStep::post_mapping,false,error);
    }
    return finish(actual==bytes ? PadWriteStep::complete : PadWriteStep::compare,actual==bytes);
}

// One publication, one removal, no retry. A failed cleanup does NOT erase the
// fact that the cell was published. Resident target survives outstanding calls.
enum class CellPage { legacy_read_only, cleo_writable_rdata };
// Values copied from checks already performed; never contains native addresses.
enum class CellFailure { none, repeated_install, repeated_remove, page_query,
    page_protection, protection_changed, allocation_base, page_state, page_type,
    cell_bounds, source_rdata, read_failed, preimage_mismatch, atomic_access,
    atomic_preimage, make_writable, restore_protection };
struct CellObservation {
    CellFailure failure=CellFailure::none;
    DWORD protection=0,state=0,type=0;
    std::size_t readBytes=0;
};
class HookCell final {
public:
    bool installed() const noexcept { return published_; }
    DWORD error() const noexcept { return error_; }
    CellObservation observation() const noexcept { return observation_; }
    bool install(std::uint32_t* cellAddress,std::uint32_t expected,std::uint32_t replacement,
                 void* imageBase,CellPage policy=CellPage::legacy_read_only) noexcept {
        if (attempted_) return refused(CellFailure::repeated_install);
        attempted_=true;cell_=cellAddress;original_=expected;hook_=replacement;base_=imageBase;policy_=policy;
        if (!page() || !matches(original_)) return false;
        return exchange(original_,hook_,true);
    }
    bool remove() noexcept {
        if (!published_) return !failed_;
        if (removalAttempted_) return refused(CellFailure::repeated_remove);
        removalAttempted_=true;
        if (!page() || !matches(hook_)) return false;
        return exchange(hook_,original_,false);
    }
private:
    bool refused(CellFailure failure) noexcept {
        if(observation_.failure==CellFailure::none)observation_.failure=failure;
        return false; // Diagnostic only: do not change failed_, published_ or error_.
    }
    static bool compareData(volatile LONG* address,LONG to,LONG from,LONG& observed,DWORD& error) noexcept {
        __try { observed=InterlockedCompareExchange(address,to,from);return true; }
        __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            error=ERROR_NOACCESS;return false;
        }
    }
    bool sourceRdata() noexcept {
        // The host authenticates the on-disk image and live headers first.
        // Independently bound this four-byte cell to non-executable, originally
        // read-only .rdata. No task/entity pointer is dereferenced here.
        const auto base=reinterpret_cast<std::uintptr_t>(base_),at=reinterpret_cast<std::uintptr_t>(cell_);
        IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS32 nt{};
        if ((at&3) || base>UINT32_MAX-4096 ||
            !ps::readMemory(base,&dos,sizeof(dos)).complete(sizeof(dos)) || dos.e_magic!=IMAGE_DOS_SIGNATURE ||
            dos.e_lfanew<0 || static_cast<unsigned>(dos.e_lfanew)>4096-sizeof(nt) ||
            !ps::readMemory(base+dos.e_lfanew,&nt,sizeof(nt)).complete(sizeof(nt)) ||
            nt.Signature!=IMAGE_NT_SIGNATURE || nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_I386 ||
            nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
            nt.FileHeader.SizeOfOptionalHeader!=sizeof(IMAGE_OPTIONAL_HEADER32) ||
            !nt.FileHeader.NumberOfSections || nt.FileHeader.NumberOfSections>32) return false;
        const auto table=static_cast<unsigned>(dos.e_lfanew)+24+nt.FileHeader.SizeOfOptionalHeader;
        if (table+nt.FileHeader.NumberOfSections*sizeof(IMAGE_SECTION_HEADER)>4096 ||
            at<base || at-base>nt.OptionalHeader.SizeOfImage || nt.OptionalHeader.SizeOfImage-(at-base)<4) return false;
        for(unsigned n=0;n<nt.FileHeader.NumberOfSections;++n){
            IMAGE_SECTION_HEADER section{};
            if(!ps::readMemory(base+table+n*sizeof(section),&section,sizeof(section)).complete(sizeof(section)))return false;
            if(at-base<section.VirtualAddress || at-base-section.VirtualAddress>=section.Misc.VirtualSize)continue;
            constexpr unsigned char name[8]{'.','r','d','a','t','a',0,0};
            return section.Misc.VirtualSize-(at-base-section.VirtualAddress)>=4 &&
                std::memcmp(section.Name,name,8)==0 && section.Characteristics==
                    (IMAGE_SCN_CNT_INITIALIZED_DATA|IMAGE_SCN_MEM_READ);
        }return false;
    }
    bool matches(std::uint32_t expected) noexcept {
        std::uint32_t actual=0;
        const auto read=ps::readMemory(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(cell_)),&actual,4);
        observation_.readBytes=read.transferred;
        if (!read.complete(4)) { error_=read.error;return refused(CellFailure::read_failed); }
        return actual==expected || refused(CellFailure::preimage_mismatch);
    }
    bool page(bool ownWriteCompleted=false) noexcept {
        MEMORY_BASIC_INFORMATION page{};
        if (!VirtualQuery(cell_,&page,sizeof(page))) { error_=GetLastError();return refused(CellFailure::page_query); }
        observation_.protection=page.Protect;observation_.state=page.State;observation_.type=page.Type;
        const auto at=reinterpret_cast<std::uintptr_t>(cell_);
        if(policy_==CellPage::cleo_writable_rdata){
            // SEC_IMAGE may report non-executable WRITECOPY for the CLEO RW
            // request. Only our completed CAS may promote COW to private RW.
            if(page.Protect!=PAGE_READWRITE && page.Protect!=PAGE_WRITECOPY)return refused(CellFailure::page_protection);
            if(!dataProtection_)dataProtection_=page.Protect;
            if(page.Protect!=dataProtection_){
                if(ownWriteCompleted && dataProtection_==PAGE_WRITECOPY && page.Protect==PAGE_READWRITE)
                    dataProtection_=PAGE_READWRITE;
                else return refused(CellFailure::protection_changed);
            }
        }
        if(page.AllocationBase!=base_)return refused(CellFailure::allocation_base);
        if(page.State!=MEM_COMMIT)return refused(CellFailure::page_state);
        if(page.Protect!=(policy_==CellPage::legacy_read_only ? static_cast<DWORD>(PAGE_READONLY) : dataProtection_))
            return refused(CellFailure::page_protection);
        if(page.Type!=MEM_IMAGE)return refused(CellFailure::page_type);
        if(at<reinterpret_cast<std::uintptr_t>(page.BaseAddress) ||
           at+4>reinterpret_cast<std::uintptr_t>(page.BaseAddress)+page.RegionSize)return refused(CellFailure::cell_bounds);
        return policy_==CellPage::legacy_read_only || sourceRdata() || refused(CellFailure::source_rdata);
    }
    bool exchange(std::uint32_t from,std::uint32_t to,bool publish) noexcept {
        if(policy_==CellPage::cleo_writable_rdata){
            // CLEO already mapped this DATA page RW. Never change protection,
            // never write instructions. Aligned CAS publishes old OR new target.
            LONG observed=0;
            const bool accessed=compareData(reinterpret_cast<volatile LONG*>(cell_),static_cast<LONG>(to),static_cast<LONG>(from),observed,error_);
            const bool swapped=accessed && static_cast<std::uint32_t>(observed)==from;
            if(swapped)published_=publish;
            if(!accessed)refused(CellFailure::atomic_access);
            else if(!swapped)refused(CellFailure::atomic_preimage);
            const bool okay=swapped && page(true) && matches(to);
            failed_=failed_ || !okay;return okay;
        }
        DWORD old=0;
        if (!VirtualProtect(cell_,4,PAGE_READWRITE,&old)) { error_=GetLastError();failed_=true;return refused(CellFailure::make_writable); }
        bool swapped=false;
        if (old==PAGE_READONLY) {
            swapped=static_cast<std::uint32_t>(InterlockedCompareExchange(
                reinterpret_cast<volatile LONG*>(cell_),static_cast<LONG>(to),static_cast<LONG>(from)))==from;
            if (swapped) published_=publish;
        }
        DWORD ignored=0;
        const bool restored=VirtualProtect(cell_,4,old,&ignored)!=FALSE;
        if (!restored) { error_=GetLastError();refused(CellFailure::restore_protection); }
        if(!swapped)refused(CellFailure::atomic_preimage);
        const bool okay=swapped && restored && page() && matches(to);
        failed_=failed_ || !okay;return okay;
    }
    std::uint32_t* cell_=nullptr;
    std::uint32_t original_=0,hook_=0;
    void* base_=nullptr;
    CellPage policy_=CellPage::legacy_read_only;
    DWORD error_=ERROR_SUCCESS;
    DWORD dataProtection_=0;
    CellObservation observation_{};
    bool attempted_=false,published_=false,removalAttempted_=false,failed_=false;
};
} // namespace grandmp::windows::remote_player_binding::replay
