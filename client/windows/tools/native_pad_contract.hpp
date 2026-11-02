#pragma once
#include "ped_smoke_runtime_read.hpp"
#include <array>
#include <span>
#include <cstring>
#include <bcrypt.h>

namespace grandmp::windows::remote_player_binding::replay {
// A single DATA range in the exact admitted image, never an executable-page
// write capability. No causal claim about which component changed protection.
class HoodlumPadContract final {
public:
    static constexpr std::uint32_t base=0x400000,pad=0xB7358C,page=0xB73000;
    static constexpr std::size_t extent=0x60;
    static_assert(pad>=0x8A4000 && pad+extent<=0xC9E000 && pad+extent<=page+0x1000);
    bool admit(std::span<const std::byte> file) noexcept {
        if(attempted_)return false;
        attempted_=true;
        if(file.size()>ULONG_MAX)return false;
        std::array<unsigned char,32> digest{};
        if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,
            reinterpret_cast<PUCHAR>(const_cast<std::byte*>(file.data())),static_cast<ULONG>(file.size()),digest.data(),32)<0)return false;
        constexpr char expected[]="A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26",hex[]="0123456789ABCDEF";
        for(unsigned i=0;i<32;++i)if(hex[digest[i]>>4]!=expected[2*i] || hex[digest[i]&15]!=expected[2*i+1])return false;
        IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS32 nt{};
        if(!copy(file,0,dos) || dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<0 ||
           !copy(file,static_cast<unsigned>(dos.e_lfanew),nt) || nt.Signature!=IMAGE_NT_SIGNATURE ||
           nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_I386 || nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
           nt.FileHeader.SizeOfOptionalHeader!=sizeof(IMAGE_OPTIONAL_HEADER32) || nt.OptionalHeader.ImageBase!=base ||
           nt.OptionalHeader.SizeOfImage!=0x1177000 || !nt.OptionalHeader.SizeOfHeaders ||
           nt.OptionalHeader.SizeOfHeaders>headers_.size() || nt.OptionalHeader.SizeOfHeaders>file.size() ||
           !nt.FileHeader.NumberOfSections || nt.FileHeader.NumberOfSections>32)return false;
        const auto table=static_cast<std::size_t>(dos.e_lfanew)+24+sizeof(IMAGE_OPTIONAL_HEADER32);
        unsigned found=0;
        for(unsigned i=0;i<nt.FileHeader.NumberOfSections;++i){
            IMAGE_SECTION_HEADER section{};
            if(!copy(file,table+i*sizeof(section),section))return false;
            constexpr char data[8]{'.','d','a','t','a',0,0,0};
            if(std::memcmp(section.Name,data,8))continue;
            // Hoodlum has TWO .data sections. Select by the target RVA too;
            // the later executable .data is never a pad write authorization.
            if(pad-base<section.VirtualAddress || pad-base-section.VirtualAddress>=section.Misc.VirtualSize)continue;
            if(section.VirtualAddress!=0x4A4000 || section.Misc.VirtualSize!=0x3FA000 ||
               section.SizeOfRawData!=0x40000 || section.Characteristics!=
                   (IMAGE_SCN_CNT_INITIALIZED_DATA|IMAGE_SCN_MEM_READ|IMAGE_SCN_MEM_WRITE))return false;
            ++found;
        }
        if(found!=1)return false;
        headerSize_=nt.OptionalHeader.SizeOfHeaders;
        std::memcpy(headers_.data(),file.data(),headerSize_);valid_=true;return true;
    }
    // Same predicate is tested with value-only query fixtures. No process access.
    bool mapping(std::uint32_t at,void* image,const MEMORY_BASIC_INFORMATION& memory) const noexcept {
        const auto begin=reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
        return valid_ && at==pad && reinterpret_cast<std::uintptr_t>(image)==base &&
            memory.AllocationBase==image && memory.State==MEM_COMMIT && memory.Type==MEM_IMAGE &&
            memory.AllocationProtect==PAGE_EXECUTE_WRITECOPY && memory.Protect==PAGE_EXECUTE_READWRITE &&
            begin<=page && memory.RegionSize>=0x1000 && begin<=UINTPTR_MAX-memory.RegionSize &&
            begin+memory.RegionSize>=page+0x1000;
    }
    bool permits(std::uint32_t at,void* image,const MEMORY_BASIC_INFORMATION& memory) const noexcept {
        if(!mapping(at,image,memory))return false;
        // Never trust live PE metadata instead of the authenticated disk image.
        std::array<std::byte,4096> current{};
        if(!ped_smoke::readMemory(base,current.data(),headerSize_).complete(headerSize_) ||
           std::memcmp(current.data(),headers_.data(),headerSize_))return false;
        // Original GetPad(index): index*134h+B73458h; RET0, caller removes index.
        constexpr unsigned char expected[]{0x8B,0x44,0x24,0x04,0x69,0xC0,0x34,0x01,0,0,0x05,0x58,0x34,0xB7,0,0xC3};
        std::array<unsigned char,sizeof(expected)> code{};
        return ped_smoke::readMemory(0x53FB70,code.data(),code.size()).complete(code.size()) &&
            !std::memcmp(code.data(),expected,code.size());
    }
private:
    template<class T> static bool copy(std::span<const std::byte> file,std::size_t at,T& value) noexcept {
        if(at>file.size() || sizeof(value)>file.size()-at)return false;
        std::memcpy(&value,file.data()+at,sizeof(value));return true;
    }
    std::array<std::byte,4096> headers_{};
    std::size_t headerSize_=0;
    bool attempted_=false,valid_=false;
};
}
