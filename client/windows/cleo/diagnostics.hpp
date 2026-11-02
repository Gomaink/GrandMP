#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "../tools/native_second_player.hpp"
#if defined(GRANDMP_REMOTE_TAIL_DIAGNOSTIC_BUILD) && GRANDMP_REMOTE_TAIL_DIAGNOSTIC_BUILD == 1
#include "../tools/remote_tail_diagnostic.hpp"
#endif

namespace grandmp::windows::cleo_bootstrap {
// A few bounded records, not a per-frame logger. Never called from DllMain.
enum class Note : unsigned { entered, registration, reentrant, caller, operands,
    initialise, phase, identity, ready_code, focus, code_pause, user_pause,
    streaming, disabled, scene, big_model, model, contract, pending, network,
    exception, deleted, armed, created, removed, script_name, sync_sender,
    sync_failure, replay_installed, replay_walking, replay_turning, replay_stopped,
    input_guard, input_guard_detail, input_cell, input_cell_page, created_before_replay,
    receiver_session, receiver_controls, network_end, factory_result, factory_native, factory_reads,
    processing_result, processing_stages, processing_primary, processing_restore,
    factory_admission, factory_admission_detail, factory_admission_progress,
    pad_apply, pad_apply_page, pad_apply_transfer, pad_restore, pad_restore_page, pad_restore_transfer,
    proof_source, proof_history, proof_sample,
#if defined(GRANDMP_REMOTE_TAIL_DIAGNOSTIC_BUILD) && GRANDMP_REMOTE_TAIL_DIAGNOSTIC_BUILD == 1
    remote_tail,
#endif
#if defined(GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD) && GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD == 1
    semantic_sample, semantic_history, semantic_refusal,
#endif
    count };
static_assert(static_cast<unsigned>(Note::count)<64);

inline const char* senderEndPoint(unsigned status,bool runtimeStopped,bool sourceStopped) noexcept {
    if(status==4 && runtimeStopped)return "network.local_sender_ended_operator_removed";
    if(status==105 && runtimeStopped)return "network.local_sender_ended_script_deleted";
    if(status>=100 && runtimeStopped)return "network.local_sender_ended_runtime_terminal";
    if(sourceStopped)return "network.local_sender_ended_local_source_stopped";
    return "network.local_sender_ended_unclassified_stop";
}

class OnceLog {
public:
    using PrefixResolver=const wchar_t*(*)() noexcept;
    explicit constexpr OnceLog(const wchar_t* prefix,PrefixResolver resolver=nullptr) noexcept : prefix_(prefix),resolver_(resolver) {}
    DWORD error() const noexcept { return error_.load(); }
    bool emit(Note note,const char* point,DWORD win32=0,
              std::uint64_t observed=0,std::uint64_t expected=0,std::uint64_t detail=0) noexcept {
        const auto index=static_cast<unsigned>(note);
        if(index>=static_cast<unsigned>(Note::count))return false;
        const auto bit=std::uint64_t{1}<<index;
        if(seen_.fetch_or(bit)&bit)return false;
        return append(index,point,win32,observed,expected,detail);
    }
#if defined(GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD) && GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD == 1
    // Deferred proof consumer only. At most 16 pairs; never used by gameplay.
    bool proofSample(const char* point,std::uint64_t remote,std::uint64_t generation,std::uint64_t serial) noexcept {
        if(proofRows_.fetch_add(1)>=32)return false;
        return append(static_cast<unsigned>(Note::proof_sample),point,0,remote,generation,serial);
    }
#endif
#if defined(GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD) && GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD == 1
    bool semanticSample(const char* point,std::uint64_t remote,std::uint64_t generation,std::uint64_t serial) noexcept {
        if(semanticRows_.fetch_add(1)>=16)return false;
        return append(static_cast<unsigned>(Note::semantic_sample),point,0,remote,generation,serial);
    }
#endif
#if defined(GRANDMP_REMOTE_TAIL_DIAGNOSTIC_BUILD) && GRANDMP_REMOTE_TAIL_DIAGNOSTIC_BUILD == 1
    // Called only by the deferred consumer; one cluster, at most twelve rows.
    bool claimRemoteTail() noexcept {return !tailClaimed_.exchange(true);}
    bool remoteTailRow(const char* point,std::uint64_t remote,std::uint64_t generation,std::uint64_t serial) noexcept {
        if(tailRows_.fetch_add(1)>=remote_tail_diagnostic::maxRows)return false;
        return append(static_cast<unsigned>(Note::remote_tail),point,0,remote,generation,serial);
    }
#endif
private:
    bool append(unsigned index,const char* point,DWORD win32,std::uint64_t observed,std::uint64_t expected,std::uint64_t detail) noexcept {
        const auto* prefix=resolver_ ? resolver_() : prefix_;
        if(!prefix)return fail(ERROR_INVALID_NAME);
        // PID plus creation time distinguishes sessions even after PID reuse.
        FILETIME created{},exited{},kernel{},user{};
        if(!GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user))return fail(GetLastError());
        wchar_t path[MAX_PATH]{};
        const auto birth=(std::uint64_t{created.dwHighDateTime}<<32)|created.dwLowDateTime;
        if(swprintf_s(path,L"%s.%lu.%016llX.log",prefix,GetCurrentProcessId(),birth)<0)
            return fail(ERROR_INSUFFICIENT_BUFFER);
        char line[384]{};
        const int length=std::snprintf(line,sizeof(line),
            "pid=%lu tid=%lu ms=%llu note=%u point=%s win32=%lu observed=0x%016llX expected=0x%016llX detail=0x%016llX\r\n",
            GetCurrentProcessId(),GetCurrentThreadId(),static_cast<unsigned long long>(GetTickCount64()),index,point,win32,observed,expected,detail);
        if(length<0 || static_cast<std::size_t>(length)>=sizeof(line))return fail(ERROR_INSUFFICIENT_BUFFER);
        const HANDLE file=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,
            nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)return fail(GetLastError());
        DWORD done=0;
        const BOOL written=WriteFile(file,line,static_cast<DWORD>(length),&done,nullptr);
        const DWORD writeError=written ? (done==static_cast<DWORD>(length) ? 0 : ERROR_WRITE_FAULT) : GetLastError();
        const BOOL closed=CloseHandle(file);
        const DWORD closeError=closed ? 0 : GetLastError();
        if(writeError)return fail(writeError);
        if(closeError)return fail(closeError);
        return true;
    }
    bool fail(DWORD error) noexcept {
        if(!error)error=ERROR_WRITE_FAULT;
        DWORD empty=0;
        if(error_.compare_exchange_strong(empty,error)){
            char fallback[96]{};
            std::snprintf(fallback,sizeof(fallback),"GrandMP CLEO diagnostic file unavailable win32=%lu\n",error);
            OutputDebugStringA(fallback);
        }
        return false; // No retries and no effect on any engine guard.
    }
    const wchar_t* prefix_;
    PrefixResolver resolver_;
    std::atomic<std::uint64_t> seen_{0};
    std::atomic<DWORD> error_{0};
#if defined(GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD) && GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD == 1
    std::atomic<unsigned> semanticRows_{0};
#endif
#if defined(GRANDMP_REMOTE_TAIL_DIAGNOSTIC_BUILD) && GRANDMP_REMOTE_TAIL_DIAGNOSTIC_BUILD == 1
    std::atomic<bool> tailClaimed_{false};
    std::atomic<unsigned> tailRows_{0};
#endif
#if defined(GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD) && GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD == 1
    std::atomic<unsigned> proofRows_{0};
#endif
};
inline bool recordRefusal(OnceLog& log,bool inputInstall,Note category,const char* point,
                          DWORD error,std::uint64_t observed=0,std::uint64_t expected=0) noexcept {
    // Image.matches passes code/module addresses, not scalar diagnostic values.
    // Drop BOTH untyped payloads, including from the original note, so a duplicate
    // record cannot leak what the installation detail deliberately withheld.
    const bool redact=inputInstall || std::strncmp(point,"Image.matches.",14)==0;
    if(inputInstall)log.emit(Note::input_guard_detail,point,error,static_cast<unsigned>(category));
    log.emit(category,point,error,redact ? 0 : observed,redact ? 0 : expected);
    return false; // Logging never changes the refusal or retries a guard.
}
inline void recordFactoryFailure(OnceLog& log,
    const remote_player_binding::second_player::ApiCreateObservation& api,
    const remote_player_binding::second_player::NativeCreateObservation& native) noexcept {
    namespace sp=remote_player_binding::second_player;
    const char* result=api.failure==sp::CreateResultFailure::slot_read ? "factory.result.slot_read_failed" :
        api.failure==sp::CreateResultFailure::slot_empty ? "factory.result.slot_empty" :
        api.failure==sp::CreateResultFailure::incomplete_with_ped ? "factory.result.false_with_ped" : "factory.result.not_observed";
    log.emit(Note::factory_result,result,api.slotRead.error,api.flags,static_cast<unsigned>(api.slot),api.slotRead.transferred);
    const char* stage=native.failure==sp::CreateFailure::none && (native.flags&sp::NativeCreateObservation::complete) ?
        "factory.create.complete" : sp::createFailureName(native.failure);
    log.emit(Note::factory_native,stage,0,native.flags,static_cast<unsigned>(native.slot),native.changedProtection);
    log.emit(Note::factory_reads,"factory.create.first_failed_read",native.failedRead.error,
        native.readFailure,native.requested,native.failedRead.transferred);
}
inline std::uint64_t operandImage(const unsigned char* bytes) noexcept {
    std::uint64_t value=0;
    for(unsigned i=0;i<6;++i)value=(value<<8)|bytes[i];
    return value;
}
}
