#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../client/windows/tools/native_player_replay.hpp"
#include <fstream>
#include <iterator>
#include <vector>
#include <string_view>
#include <stdexcept>
#include <cstdio>
namespace rp=grandmp::windows::remote_player_binding::replay;
void check(bool result,const char* why){if(!result)throw std::runtime_error(why);}
#pragma section(".gmcow",read,write)
__declspec(allocate(".gmcow")) std::array<std::byte,128> disposableData{};
int main(int argc,char** argv){try{
    check(argc==3,"case and authorized offline file required");
    std::ifstream input(argv[2],std::ios::binary);std::vector<char> raw((std::istreambuf_iterator<char>(input)),{});
    check(!raw.empty(),"read authorized disk file only, no game process");
    auto file=std::as_bytes(std::span(raw));rp::HoodlumPadContract contract;
    const std::string_view which=argv[1];
    if(which=="identity"){
        raw.back()^=1;check(!contract.admit(std::as_bytes(std::span(raw))),"one changed byte rejects entire image");
        raw.back()^=1;check(!contract.admit(std::as_bytes(std::span(raw))),"no retry after failed admission");return 0;
    }
    check(contract.admit(file),"exact Hoodlum SHA and non-executable data section");
    MEMORY_BASIC_INFORMATION page{};page.BaseAddress=reinterpret_cast<void*>(contract.page);
    page.AllocationBase=reinterpret_cast<void*>(contract.base);page.RegionSize=4096;
    page.State=MEM_COMMIT;page.Type=MEM_IMAGE;page.Protect=PAGE_EXECUTE_READWRITE;page.AllocationProtect=PAGE_EXECUTE_WRITECOPY;
    const auto accepted=[&](const MEMORY_BASIC_INFORMATION& p){return contract.mapping(contract.pad,page.AllocationBase,p);};
    check(accepted(page),"value fixture admits only pad1 COW-compatible IMAGE mapping, NOT native execution proof");
    if(which=="mapping"){
        for(const auto protection:{PAGE_READONLY,PAGE_EXECUTE_READ,PAGE_EXECUTE_WRITECOPY,PAGE_READWRITE,PAGE_WRITECOPY,PAGE_EXECUTE_READWRITE|PAGE_GUARD}){
            auto altered=page;altered.Protect=protection;check(!accepted(altered),"exact current protection only");
        }
        for(const auto protection:{PAGE_READWRITE,PAGE_WRITECOPY,PAGE_EXECUTE_READWRITE}){
            auto altered=page;altered.AllocationProtect=protection;check(!accepted(altered),"exact initial protection required");
        }
        auto altered=page;altered.State=MEM_RESERVE;check(!accepted(altered),"committed only");
        altered=page;altered.Type=MEM_PRIVATE;check(!accepted(altered),"COW image must still be MEM_IMAGE");
        altered=page;altered.AllocationBase=nullptr;check(!accepted(altered),"wrong allocation refused");
    }else if(which=="range"){
        for(const auto at:{contract.pad-1,contract.pad+1,0xB73458u,0x53FB70u})
            check(!contract.mapping(at,page.AllocationBase,page),"no pad0, code, shifted or arbitrary target");
        check(!contract.mapping(contract.pad,nullptr,page),"exact base required");
        auto altered=page;altered.RegionSize=4095;check(!accepted(altered),"entire known page must be mapped");
        altered=page;altered.BaseAddress=reinterpret_cast<void*>(contract.page+4096);check(!accepted(altered),"page outside region refused");
        rp::HoodlumPadContract absent;check(!absent.mapping(contract.pad,page.AllocationBase,page),"no identity token, no exception");
    }else if(which=="live"){
        check(!contract.permits(contract.pad,page.AllocationBase,page),"authentic DISK file cannot authenticate this non-GTA host's live headers");
        rp::PadWriteObservation seen{};
        check(!rp::writePadStates(contract.pad,page.AllocationBase,false,{},&seen,&contract) &&
            seen.step==rp::PadWriteStep::context && !seen.queryBytes && !seen.writeCalled,"denied context does not access any GTA address");
    }else if(which=="generic"){
        const auto before=disposableData;rp::PadWriteObservation seen{};
        const auto at=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(disposableData.data()));
        check(!rp::writePadStates(at,GetModuleHandleW(nullptr),true,{},&seen,&contract) && !seen.writeCalled &&
            disposableData==before,"real executable IMAGE data remains unwritable through generic writer, even with valid disk token");
        check(seen.protection==PAGE_EXECUTE_READWRITE || seen.protection==PAGE_EXECUTE_WRITECOPY,"actual executable fixture, no protection-changing API");
    }else check(false,"unknown case");
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
