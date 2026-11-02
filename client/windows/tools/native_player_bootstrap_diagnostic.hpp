#pragma once
#include <windows.h>
#include <type_traits>

namespace grandmp::windows::bootstrap_diagnostic {
// Values only, in this module's DATA. No pointers, handles or game state.
enum class Event : unsigned { initialize, reference, thread, prepare, module_path,
    log_path, create_file, write_file, prepare_exit, initialize_exit, count };
enum : LONG { unseen=0, entered=1, succeeded=2, failed=3, publishing=-1 };
struct Record { volatile LONG state=0, win32=0, detail=0, tid=0; };
struct Data {
    LONG magic=0x39424D47, version=1, bytes=sizeof(Data);
    volatile LONG attached=0, initializeCalls=0, logClaimed=0;
    Record events[static_cast<unsigned>(Event::count)]{};
};
static_assert(std::is_standard_layout_v<Data> && sizeof(Data)==184);
// A record has one writer. -1 fences a publication; readers must reject it.
inline void publish(Data& data, Event event, LONG state, DWORD error=0, LONG detail=0) noexcept {
    auto& r=data.events[static_cast<unsigned>(event)];
    InterlockedExchange(&r.state,publishing);
    r.win32=static_cast<LONG>(error);r.detail=detail;r.tid=static_cast<LONG>(GetCurrentThreadId());
    InterlockedExchange(&r.state,state);
}
} // namespace grandmp::windows::bootstrap_diagnostic
