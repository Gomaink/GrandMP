#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>

namespace grandmp::windows::remote_tail_diagnostic {
// Exact CPad extent from native_second_player.hpp; CPad layout: Plugin-SDK CPad.h.
// Hoodlum 53F200 reads CTimer at B7CB84 and writes CPad+120. Read-only evidence;
// no assertion that this writer caused a particular observed difference.
inline constexpr std::size_t begin=0x60,extent=0x134-begin,lastTouched=0x120;
inline constexpr std::uint32_t engineClock=0xB7CB84;
inline constexpr unsigned capacity=8,maxRows=capacity+4;
using Tail=std::array<std::byte,extent>;
struct Boundary {
    std::uint32_t captureTid=0,engineMs=0,transferred=0,error=0;
    bool clockComplete=false;
};
struct Difference {std::uint16_t offset=0;std::uint8_t before=0,after=0;};
struct Observation {
    Boundary before{},after{};
    std::array<Difference,capacity> differences{};
    std::uint32_t touchedBefore=0,touchedAfter=0;
    std::uint16_t first=0,changed=0,listed=0;
    bool compared=false;
};
static_assert(std::is_trivially_copyable_v<Observation> && sizeof(Observation)<=128);
inline std::uint32_t touched(const Tail& value) noexcept {
    std::uint32_t result=0;
    for(unsigned n=0;n<4;++n)result|=std::uint32_t(std::to_integer<std::uint8_t>(value[lastTouched-begin+n]))<<(8*n);
    return result;
}
inline Observation compare(const Tail& before,const Tail& after,Boundary start,Boundary finish) noexcept {
    Observation out{};out.compared=true;out.before=start;out.after=finish;
    out.touchedBefore=touched(before);out.touchedAfter=touched(after);
    for(std::size_t n=0;n<extent;++n){
        if(before[n]==after[n])continue;
        const auto offset=static_cast<std::uint16_t>(begin+n);
        if(!out.changed)out.first=offset;
        ++out.changed;
        if(out.listed<capacity)out.differences[out.listed++]={offset,std::to_integer<std::uint8_t>(before[n]),std::to_integer<std::uint8_t>(after[n])};
    }
    return out;
}
// Worker only. Offsets and individual changed bytes, never native addresses or
// the full tail. Clock failure remains unknown, without changing any guard.
inline bool format(char* line,std::size_t size,const Observation& value,unsigned row) noexcept {
    if(!line || !size)return false;
    line[0]='\0';
    if(!value.compared || !value.changed || value.changed>extent || value.listed>capacity ||
       value.listed>value.changed || row>=4u+value.listed)return false;
    int n=-1;
    if(row==0)n=std::snprintf(line,size,
        "foundation.tail.summary first=0x%03X changed=%u listed=%u omitted=%u capture_before_tid=%u capture_after_tid=%u",
        unsigned(value.first),unsigned(value.changed),unsigned(value.listed),unsigned(value.changed-value.listed),value.before.captureTid,value.after.captureTid);
    else if(row==1)n=std::snprintf(line,size,
        "foundation.tail.LastTimeTouched offset=0x120 width=4 before=%u after=%u changed=%u",
        value.touchedBefore,value.touchedAfter,unsigned(value.touchedBefore!=value.touchedAfter));
    else if(row<4){
        const auto& boundary=row==2 ? value.before:value.after;
        n=std::snprintf(line,size,"foundation.tail.clock.%s capture_tid=%u complete=%u engine_ms=%u bytes=%u error=%u",
            row==2 ? "before_apply":"after_restore",boundary.captureTid,unsigned(boundary.clockComplete),
            boundary.clockComplete ? boundary.engineMs:0u,boundary.transferred,boundary.error);
    }else{
        const auto& d=value.differences[row-4];
        if(d.offset<begin || d.offset>=begin+extent)return false;
        n=std::snprintf(line,size,"foundation.tail.byte offset=0x%03X before=0x%02X after=0x%02X",unsigned(d.offset),unsigned(d.before),unsigned(d.after));
    }
    if(n<0 || static_cast<std::size_t>(n)>=size){line[0]='\0';return false;}
    return true;
}
}
