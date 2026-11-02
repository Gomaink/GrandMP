#pragma once
#include "remote_tail_diagnostic.hpp"

namespace grandmp::windows::semantic_pad_context {
namespace tail=remote_tail_diagnostic;
enum class Decision { not_observed, unchanged, native_touch, other_tail, native_not_returned,
    clock_unavailable, thread_mismatch, clock_changed, timestamp_mismatch, timestamp_regressed };
inline const char* name(Decision value) noexcept {
    switch(value){
    case Decision::unchanged:return "unchanged";
    case Decision::native_touch:return "native_touch";
    case Decision::other_tail:return "other_tail";
    case Decision::native_not_returned:return "native_not_returned";
    case Decision::clock_unavailable:return "clock_unavailable";
    case Decision::thread_mismatch:return "thread_mismatch";
    case Decision::clock_changed:return "clock_changed";
    case Decision::timestamp_mismatch:return "timestamp_mismatch";
    case Decision::timestamp_regressed:return "timestamp_regressed";
    default:return "not_observed";
    }
}
inline bool accepted(Decision value) noexcept {return value==Decision::unchanged || value==Decision::native_touch;}
// Engine-owned activity history in the borrowed pad1, NOT input to restore.
// Only the authenticated single-remote native dispatch may use this exception.
// No writes, persistence cache, clock interval guess, wrap allowance or retry.
inline Decision evaluate(const tail::Tail& before,const tail::Tail& after,
                         tail::Boundary start,tail::Boundary finish,bool originalReturned) noexcept {
    for(std::size_t n=0;n<tail::extent;++n){
        const auto offset=n+tail::begin;
        if(offset>=tail::lastTouched && offset<tail::lastTouched+4)continue;
        if(before[n]!=after[n])return Decision::other_tail; // All other 208 bytes, including padding.
    }
    const auto old=tail::touched(before),now=tail::touched(after);
    if(old==now)return Decision::unchanged; // Preserve the original exact-equality path.
    if(!originalReturned)return Decision::native_not_returned;
    if(!start.clockComplete || !finish.clockComplete || start.transferred!=4 || finish.transferred!=4 || start.error || finish.error)
        return Decision::clock_unavailable;
    if(!start.captureTid || start.captureTid!=finish.captureTid)return Decision::thread_mismatch;
    if(start.engineMs!=finish.engineMs)return Decision::clock_changed;
    if(now!=start.engineMs)return Decision::timestamp_mismatch; // Full uint32, not just the changed bytes.
    if(now<old)return Decision::timestamp_regressed; // Reset/wrap requires a new review; never infer temporal order.
    return Decision::native_touch;
}
}
