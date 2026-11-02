#pragma once
#include "independent_control_proof.hpp"
#include "../tools/pooled_remote_player.hpp"
#include <cstdio>

namespace grandmp::windows::independent_control_proof {
namespace fp=remote_player_binding::pooled_player;
struct Sample {
    core::RemotePlayerKey key{};
    std::uint64_t serial=0;
    std::uint32_t elapsedMs=0;
    Phase phase=Phase::walking;
    fp::ControlProofSnapshot before{},after{};
    fp::NativeProcessingObservation native{};
    bool beforeRead=false,afterRead=false,processed=false;
};
static_assert(std::is_trivially_copyable_v<Sample>);
#if defined(GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD) && GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD == 1
// Native owner writes until the existing batch seal. Consumer sees a value copy
// only after release/acquire. Counts completed bindings, never physical movement.
struct DispatchHistory {
    core::RemotePlayerKey key{};
    std::uint64_t first=0,last=0,count=0;
    bool consecutive=true;
    void completed(core::RemotePlayerKey current,std::uint64_t serial) noexcept {
        if(!count){key=current;first=serial;}
        else if(current!=key || last==UINT64_MAX || serial!=last+1)consecutive=false;
        if(!serial)consecutive=false;
        last=serial;
        if(count<UINT64_MAX)++count;else consecutive=false;
    }
};
inline bool formatSemantic(char* point,std::size_t capacity,const Sample& sample) noexcept {
    if(!point || !capacity)return false;
    point[0]=0;
    const auto& value=sample.native.tail;
    if(!sample.processed || !sample.beforeRead || !sample.afterRead || !value.compared ||
       !semantic_pad_context::accepted(sample.native.semantic))return false;
    const bool clocks=value.before.clockComplete && value.after.clockComplete && value.before.transferred==4 && value.after.transferred==4 &&
        !value.before.error && !value.after.error;
    const int n=std::snprintf(point,capacity,
        "proof.pad_semantic.%s result=%s old=%u new=%u clock0=%u clock1=%u clock_reads=%u capture_tid=%u",
        name(sample.phase),semantic_pad_context::name(sample.native.semantic),value.touchedBefore,value.touchedAfter,
        value.before.clockComplete ? value.before.engineMs:0u,value.after.clockComplete ? value.after.engineMs:0u,unsigned(clocks),value.before.captureTid);
    if(n<0 || static_cast<std::size_t>(n)>=capacity){point[0]=0;return false;}return true;
}
#endif
inline bool format(char* point,std::size_t capacity,const Sample& sample,bool local) noexcept {
    if(!point || !capacity || !sample.beforeRead || !sample.afterRead || !sample.processed)return false;
    const auto& r=sample.after.remote;
    const auto& cj=sample.after.local;
    const auto& a=sample.native;
    const int n=local ? std::snprintf(point,capacity,
        "proof.local.%s t=%u pos=(%.6g,%.6g,%.6g) h=%.6g health=%.6g unchanged=%u pad_apply=%s pad_restore=%s",
        name(sample.phase),sample.elapsedMs,cj.position.x,cj.position.y,cj.position.z,cj.heading,cj.health,
        unsigned(sample.before.local==cj),remote_player_binding::replay::padWriteStepName(a.padApply.step),
        remote_player_binding::replay::padWriteStepName(a.padRestore.step)) : std::snprintf(point,capacity,
        "proof.remote.%s t=%u pos=(%.6g,%.6g,%.6g) dxy=%.6g h=%.6g state=%d move=%d shift=(%.6g,%.6g)",
        name(sample.phase),sample.elapsedMs,r.position.x,r.position.y,r.position.z,
        std::hypot(double(r.position.x)-sample.before.remote.position.x,double(r.position.y)-sample.before.remote.position.y),
        r.heading,r.states[0],r.states[1],r.localShift[0],r.localShift[1]);
    if(n<0 || static_cast<std::size_t>(n)>=capacity){point[0]=0;return false;}return true;
}
}
