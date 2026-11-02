// Offline checks only. No game entrypoint, DLL loading, engine call or hook.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../client/windows/tools/ped_smoke_code_identity.hpp"
#include <bcrypt.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace ps=grandmp::windows::ped_smoke;
void check(bool okay,const char* reason) { if (!okay) throw std::runtime_error(reason); }
struct Image {
    HANDLE file=INVALID_HANDLE_VALUE,mapping=nullptr;
    std::byte* view=nullptr;
    explicit Image(const std::filesystem::path& path) {
        file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        check(file!=INVALID_HANDLE_VALUE,"image file read-only");
        mapping=CreateFileMappingW(file,nullptr,PAGE_READONLY|SEC_IMAGE,0,0,nullptr);
        check(mapping!=nullptr,"SEC_IMAGE mapping");
        view=static_cast<std::byte*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,0));
        check(view!=nullptr,"image view, not LoadLibrary/entrypoint execution");
    }
    ~Image() { if (view) UnmapViewOfFile(view);if (mapping) CloseHandle(mapping);if (file!=INVALID_HANDLE_VALUE) CloseHandle(file); }
    std::uintptr_t base() const { return reinterpret_cast<std::uintptr_t>(view); }
    std::uint32_t at(std::uint32_t rva) const { return static_cast<std::uint32_t>(base()+rva); }
};
std::vector<char> fileBytes(const std::filesystem::path& path) {
    std::ifstream stream(path,std::ios::binary);check(bool(stream),"file readable");
    return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
}
void run(std::string_view mode,const char* file) {
    ps::CodeIdentityFailure f{};f.function="factoryCodeMatches";
    if (mode=="callback") {
        const std::array<unsigned char,5> original{0xE8,0xFF,0xE2,0x1F,0},installed{0xE8,1,2,3,4};
        check(ps::identityPublishedBytes(f,ps::phaseCall,original.data(),original.data(),installed.data(),5,false),"original before publication");
        check(ps::identityPublishedBytes(f,ps::phaseCall,installed.data(),original.data(),installed.data(),5,true),"exact installed CALL after publication");
        check(!ps::identityPublishedBytes(f,ps::phaseCall,original.data(),original.data(),installed.data(),5,true) &&
            !std::strcmp(f.category,"bytes_mismatch") && f.address==ps::phaseCall && f.size==5 && f.mismatchOffset==1,"published CALL cannot revert to original silently");
        f={};auto corrupted=installed;corrupted[4]^=1;
        check(!ps::identityPublishedBytes(f,ps::phaseCall,corrupted.data(),original.data(),installed.data(),5,true),"unknown target refused");return;
    }
    if (mode=="first") {
        check(!ps::identityRefuse(f,"disk_mapping_failed",0x60D5B0,0x2A0),"first failure");
        const auto first=f;
        check(!ps::identityReadComplete(f,4,8,{false,3,ERROR_PARTIAL_COPY}) && f.address==first.address &&
            f.size==first.size && f.error==first.error && f.transferred==first.transferred &&
            !std::strcmp(f.category,"disk_mapping_failed"),"subsequent evidence cannot overwrite first cause");
        ps::CodeIdentityFailure other{};other.function="codeMatches";
        ps::identityRefuse(other,"bytes_mismatch",ps::phaseCall,5);
        check(!std::strcmp(f.function,"factoryCodeMatches") && !std::strcmp(other.function,"codeMatches"),"independent function failures");return;
    }
    if (mode=="read") {
        std::uint32_t source=0x12345678,out=0;
        check(ps::identityRead(f,static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&source)),&out,sizeof(out)) && out==source,"real complete read");
        check(!ps::identityRead(f,0,&out,sizeof(out)) && !std::strcmp(f.category,"read_failed") && f.error!=0,"immediate Win32 error");
        f={};check(!ps::identityReadComplete(f,12,8,{true,3,0}) && !std::strcmp(f.category,"read_incomplete") && f.transferred==3,"partial success never accepted");return;
    }
    if (mode=="format") {
        f.address=f.size=f.mismatchOffset=UINT32_MAX;
        f.error=f.expectedProtection=f.alternateProtection=f.observedProtection=MAXDWORD;
        f.transferred=SIZE_MAX;f.category="disk_mapping_failed";
        char message[320]{},final[512]{};
        check(ps::formatCodeIdentityFailure(message,sizeof(message),f),"extreme diagnostic fits");
        const int count=std::snprintf(final,sizeof(final),"[GrandMPPedSmoke] state=failed reason=%s pid=%lu tid=%lu ped=%08lX callback=%u\r\n",
            message,MAXDWORD,MAXDWORD,0UL,UINT32_MAX);
        check(count>0 && count<sizeof(final) && std::strstr(final,"function=factoryCodeMatches") &&
            std::strstr(final,"address=FFFFFFFF size=4294967295 category=disk_mapping_failed") &&
            std::strstr(final,"alternate_protect=FFFFFFFF") && !std::strstr(final,"0x12345678"),"complete final log, no memory contents");
        char small[8]{};check(!ps::formatCodeIdentityFailure(small,sizeof(small),f) && !small[0],"explicit truncation refusal");return;
    }
    if (mode=="pages") {
        wchar_t path[32768]{};check(GetModuleFileNameW(nullptr,path,32768)!=0,"own test image path");
        Image image(path);
        const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(image.view);
        const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(image.view+dos->e_lfanew);
        const auto at=image.at(nt->OptionalHeader.BaseOfCode);
        check(ps::identityPage(f,at,16,PAGE_EXECUTE_READ,0,image.base()),"real image RX");
        check(!ps::identityRefuse(f,"disk_mapping_failed",at+16,4) && !f.expectedProtection && !f.alternateProtection && !f.observedProtection,
            "successful prior page cannot supply protection evidence for a later unmapped disk window");
        f={};
        check(!ps::identityPage(f,at,16,PAGE_READONLY,0,image.base()) && !std::strcmp(f.category,"page_protection") && f.observedProtection==PAGE_EXECUTE_READ,"unexpected protection refused");
        f={};check(!ps::identityPage(f,at,16,PAGE_EXECUTE_READ,0,image.base()+4096) && !std::strcmp(f.category,"image_allocation"),"different allocation refused");
        void* memory=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);check(memory!=nullptr,"owned test allocation");
        f={};const bool okay=ps::identityPage(f,static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(memory)),4,PAGE_EXECUTE_READWRITE,PAGE_EXECUTE_WRITECOPY,reinterpret_cast<std::uintptr_t>(memory));
        VirtualFree(memory,0,MEM_RELEASE);
        check(!okay && !std::strcmp(f.category,"page_type"),"RWX private page never substitutes for image");
        check(ps::factoryHoodlumWindow(0x1566E30,0x60) && !ps::factoryHoodlumWindow(ps::phaseCall,5) && !ps::factoryHoodlumWindow(0x1576FFF,2),"COW exception bounded to original Hoodlum section");return;
    }
    if (mode=="cow") {
        const auto makePages=[] {
            std::array<ps::CodePageObservation,2> pages{};
            for (std::size_t i=0;i<pages.size();++i) {
                auto& p=pages[i];p.queriedAddress=ps::hoodlumCowPages[i];p.baseAddress=ps::hoodlumCowPages[i];
                p.regionSize=ps::hoodlumPageSize;p.allocationBase=ps::imageBase;p.state=MEM_COMMIT;p.type=MEM_IMAGE;
                p.protection=PAGE_EXECUTE_WRITECOPY;p.allocationProtection=PAGE_EXECUTE_WRITECOPY;
                p.queryComplete=true;p.workingSetComplete=true;p.workingSetValid=true;p.workingSetShared=true;
            }
            return pages;
        };
        auto pages=makePages();
        check(ps::identityHoodlumCowPages(f,ps::hoodlumCowWindowAddress,ps::hoodlumCowWindowSize,pages,true,{false,false}),"authenticated shared two-page COW profile");
        check(ps::hoodlumCowCodeWindow(0x53BEE0,380) && ps::hoodlumCowCodeWindow(0x53C061,0x39) &&
            ps::hoodlumCowCodeWindow(ps::phaseCall,5) && !ps::hoodlumCowCodeWindow(0x53BEE0,379) &&
            !ps::hoodlumCowCodeWindow(0x53B000,0x1000),"COW identity check limited to exact code windows");
        f={};check(!ps::identityHoodlumCowPages(f,ps::hoodlumCowWindowAddress+1,ps::hoodlumCowWindowSize-1,
            pages,true,{false,false}) && !std::strcmp(f.category,"cow_window_not_allowed"),
            "nearby COW range cannot borrow the authenticated exception");
        f={};pages=makePages();pages[0].workingSetShared=false;
        check(!ps::identityHoodlumCowPages(f,ps::hoodlumCowWindowAddress,ps::hoodlumCowWindowSize,pages,true,{false,false}) &&
            !std::strcmp(f.category,"working_set_private_unowned") && f.address==ps::hoodlumCowPages[0],
            "first page private before exact owned patch refused");
        f={};pages=makePages();pages[0].workingSetShared=false;
        check(ps::identityHoodlumCowPages(f,ps::hoodlumCowWindowAddress,ps::hoodlumCowWindowSize,pages,true,{true,false}),"first page private only after exact own patch publication");
        f={};pages=makePages();pages[1].workingSetShared=false;
        check(!ps::identityHoodlumCowPages(f,ps::hoodlumCowWindowAddress,ps::hoodlumCowWindowSize,pages,true,{false,false}) &&
            !std::strcmp(f.category,"working_set_private_unowned"),"second page private before exact hook publication refused");
        f={};pages=makePages();pages[1].workingSetShared=false;
        check(ps::identityHoodlumCowPages(f,ps::hoodlumCowWindowAddress,ps::hoodlumCowWindowSize,pages,true,{false,true}),"second page private only after exact own hook publication");
        f={};pages=makePages();pages[0].workingSetShared=false;pages[1].workingSetShared=false;
        check(ps::identityHoodlumCowPages(f,ps::hoodlumCowWindowAddress,ps::hoodlumCowWindowSize,pages,true,{true,true}),"both pages private only with corresponding owned patches");
        return;
    }
    if (mode=="cow_rejections") {
        const auto makePages=[] {
            std::array<ps::CodePageObservation,2> pages{};
            for (std::size_t i=0;i<pages.size();++i) {
                auto& p=pages[i];p.queriedAddress=ps::hoodlumCowPages[i];p.baseAddress=ps::hoodlumCowPages[i];
                p.regionSize=ps::hoodlumPageSize;p.allocationBase=ps::imageBase;p.state=MEM_COMMIT;p.type=MEM_IMAGE;
                p.protection=PAGE_EXECUTE_WRITECOPY;p.allocationProtection=PAGE_EXECUTE_WRITECOPY;
                p.queryComplete=true;p.workingSetComplete=true;p.workingSetValid=true;p.workingSetShared=true;
            }
            return pages;
        };
        auto refused=[&](auto alter,const char* category,const char* label) {
            auto pages=makePages();alter(pages);f={};
            check(!ps::identityHoodlumCowPages(f,ps::hoodlumCowWindowAddress,ps::hoodlumCowWindowSize,pages,true,{false,false}) && !std::strcmp(f.category,category),label);
        };
        auto pages=makePages();f={};
        check(!ps::identityHoodlumCowPages(f,ps::hoodlumCowWindowAddress,ps::hoodlumCowWindowSize,pages,false,{false,false}) &&
            !std::strcmp(f.category,"hoodlum_identity_unavailable"),"unauthenticated executable refused");
        for (std::size_t i=0;i<2;++i) {
            refused([=](auto& p){p[i].queryComplete=false;p[i].queryError=ERROR_ACCESS_DENIED;},"page_query_failed","each page query failure refused");
            refused([=](auto& p){p[i].queriedAddress+=0x1000;},"page_query_address","each wrong page address refused");
            refused([=](auto& p){p[i].state=MEM_RESERVE;},"page_state","each uncommitted page refused");
            refused([=](auto& p){p[i].type=MEM_PRIVATE;},"page_type","each private mapping refused");
            refused([=](auto& p){p[i].allocationBase=0x500000;},"image_allocation","each different allocation refused");
            refused([=](auto& p){p[i].protection=PAGE_EXECUTE_READWRITE;},"page_protection","each unexpected current protection refused");
            refused([=](auto& p){p[i].allocationProtection=PAGE_EXECUTE_READ;},"page_protection","each unexpected allocation protection refused");
            refused([=](auto& p){p[i].baseAddress=ps::hoodlumCowPages[i]+0x1000;p[i].regionSize=0x1000;},"page_span","each incomplete page region refused");
            refused([=](auto& p){p[i].workingSetComplete=false;p[i].workingSetError=ERROR_ACCESS_DENIED;},"working_set_query_failed","each working-set query failure refused");
            refused([=](auto& p){p[i].workingSetValid=false;},"working_set_not_resident","each nonresident page refused");
        }
        return;
    }
    if (mode=="image") {
        check(file!=nullptr,"authorized image argument");const auto bytes=fileBytes(file);
        check(bytes.size()==14383616,"authorized image size");
        BCRYPT_ALG_HANDLE algorithm=nullptr;std::array<unsigned char,32> digest{};
        check(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"hash provider");
        const auto status=BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),static_cast<ULONG>(bytes.size()),digest.data(),static_cast<ULONG>(digest.size()));
        BCryptCloseAlgorithmProvider(algorithm,0);check(status>=0,"hash complete");
        char hash[65]{};for (unsigned n=0;n<digest.size();++n) std::snprintf(hash+n*2,3,"%02X",digest[n]);
        check(!std::strcmp(hash,"A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26"),"exact Hoodlum, never executed");
        const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
        const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes.data()+dos->e_lfanew);
        const auto* sections=IMAGE_FIRST_SECTION(nt);
        auto disk=[&](ps::CodeRegion region)->const void* {
            const auto rva=region.address-ps::imageBase;
            for (unsigned n=0;n<nt->FileHeader.NumberOfSections;++n) {
                const auto& s=sections[n];
                if (rva>=s.VirtualAddress && std::uint64_t{rva}+region.size<=std::uint64_t{s.VirtualAddress}+s.SizeOfRawData) {
                    const auto offset=std::uint64_t{s.PointerToRawData}+rva-s.VirtualAddress;
                    if (offset+region.size<=bytes.size()) return bytes.data()+offset;
                }
            }
            return nullptr;
        };
        Image image(file);unsigned checked=0,cow=0;
        for (const auto region:ps::factoryCodeWindows) {
            f={};f.function="factoryCodeMatches";const bool hoodlum=ps::factoryHoodlumWindow(region.address,region.size);
            const auto at=image.at(region.address-ps::imageBase);
            check(ps::identityPage(f,at,region.size,hoodlum ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ,
                hoodlum ? PAGE_EXECUTE_WRITECOPY : 0,image.base()),"all 20 factory pages authenticated");
            if (hoodlum) {
                MEMORY_BASIC_INFORMATION page{};
                check(VirtualQuery(reinterpret_cast<void*>(at),&page,sizeof(page))==sizeof(page) &&
                    page.Protect==PAGE_EXECUTE_WRITECOPY,"reproduced legitimate 80 profile previously refused as not 40");++cow;
            }
            check(ps::identityBytes(f,at,reinterpret_cast<void*>(at),disk(region),region.size),"all factory bytes remain exact");++checked;
        }
        for (const auto region:std::array{ps::CodeRegion{ps::playerVtable,0x100},ps::CodeRegion{0x8708EC,44}}) {
            f={};const auto at=image.at(region.address-ps::imageBase);
            check(ps::identityPage(f,at,region.size,PAGE_READONLY,0,image.base()) && ps::identityBytes(f,at,reinterpret_cast<void*>(at),disk(region),region.size),"two original read-only vtables");++checked;
        }
        const ps::CodeRegion call{ps::phaseCall,5};
        const std::array<unsigned char,5> original{0xE8,0xFF,0xE2,0x1F,0};
        check(disk(call) && !std::memcmp(disk(call),original.data(),5),"authorized on-disk callback CALL");
        for (const auto region:ps::codeRegions)
            check(std::uint64_t{region.address}+region.size<=ps::phaseCall || region.address>=ps::phaseCall+5,
                "common disk windows exclude all five intentionally installed bytes");
        check(checked==22 && cow==1,"20 code + 2 rdata, exactly one COW exception");
        std::puts("OFFLINE: 20 code + 2 rdata exact; Hoodlum protection=0x80 (old expected=0x40); no entrypoint execution");return;
    }
    throw std::runtime_error("unknown case");
}
int main(int argc,char** argv) {
    static_assert(sizeof(void*)==4);
    try {if (argc<2) return 2;run(argv[1],argc>2 ? argv[2] : nullptr);return 0;}
    catch (const std::exception& error) {std::fprintf(stderr,"%s\n",error.what());return 1;}
}
