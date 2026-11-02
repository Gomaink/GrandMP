// Included only by the isolated Alpha 0.11 target. No UDP, pose presentation,
// second task, shared HookCell or PlayerInfo publication is reachable here.
namespace proof=grandmp::windows::independent_control_proof;
proof::Source proofSource;
struct ProofBatch {
    static constexpr unsigned capacity=16;
    std::array<proof::Sample,capacity> samples{};
    std::atomic<bool> sealed{false},stopRequested{false};
    std::atomic<std::uint64_t> heartbeat{0};
    unsigned count=0,phaseMask=0;
    bool started=false,pending=false; // Native owner only.
    bool complete=false;
    const char* outcome="not_started"; // Static literal only, never native data.
    std::uint64_t nextSample=0,startedMs=0;
#if defined(GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD) && GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD == 1
    proof::DispatchHistory history{};
#endif
} proofBatch; // Resident through process exit; timeout NEVER disposes producer data.
void proofSeal(const char* outcome,bool complete=false) noexcept {
    if(proofBatch.sealed.load(std::memory_order_relaxed))return;
    proofBatch.outcome=outcome;proofBatch.complete=complete;
    if(!complete)proofBatch.stopRequested.store(true,std::memory_order_release);
    proofBatch.sealed.store(true,std::memory_order_release); // Last batch write.
}
bool proofPublish() {
    if(proofBatch.stopRequested.load(std::memory_order_acquire)){
        proofSeal("deferred_worker_stopped");stopped.store(true);status.store(108);return false;
    }
    proofBatch.heartbeat.store(GetTickCount64(),std::memory_order_release);
    const auto now=grandmp::core::RemoteRoster::Clock::now();
    if(!proofBatch.started){
        proofBatch.started=true; // No second admission, including a failed spawn.
        Probe probe;ps::Position spawnPosition{};
        grandmp::core::OnFootState initial{};
        if(!ps::findSpawnPosition(probe,spawnPosition) ||
           !read(ni::cameraHeading,initial.viewHeading) ||
           !read(reinterpret_cast<std::uintptr_t>(probe.cj)+0x558,initial.heading)){
            proofSeal("spawn_reference_refused");return false;
        }
        initial.heading=std::remainder(initial.heading,6.283185307f);
        initial.viewHeading=std::remainder(initial.viewHeading,6.283185307f);
        initial.x=spawnPosition.x;initial.y=spawnPosition.y;initial.z=spawnPosition.z;
        if(!proofSource.begin(initial,now,bootstrapController)){
            proofSeal("local_source_refused");return false;
        }
        proofBatch.startedMs=GetTickCount64();
        return true;
    }
    if(!proofSource.publish(now,bootstrapController)){proofSeal("local_source_ended");return false;}
    return true; // Fresh neutral input continues after the one six-second window.
}
void proofBefore(std::uint64_t serial) noexcept {
    if(proofBatch.sealed.load(std::memory_order_acquire) || !proofBatch.started)return;
    const auto now=GetTickCount64();
    if(now<proofBatch.startedMs || now-proofBatch.startedMs>UINT32_MAX){proofSeal("sample_clock_invalid");return;}
    if(proofBatch.count && now<proofBatch.nextSample && proofBatch.samples[proofBatch.count-1].phase==proofSource.phase())return;
    if(proofBatch.count>=proofBatch.capacity){proofSeal("sample_capacity");return;}
    auto& sample=proofBatch.samples[proofBatch.count];sample={};
    sample.key=binding->key();sample.serial=serial;sample.phase=proofSource.phase();
    sample.elapsedMs=static_cast<std::uint32_t>(now-proofBatch.startedMs);
    sample.beforeRead=bootstrapApi->observeControlProof(sample.before);
    proofBatch.pending=true;proofBatch.nextSample=now+500;
}
void proofAfter(const nf::ProcessingFailure& result) noexcept {
    if(proofBatch.sealed.load(std::memory_order_acquire))return;
    if(!result.returned || result.result!=np::Result::processed){proofSeal("native_processing_refused");return;}
#if defined(GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD) && GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD == 1
    proofBatch.history.completed(result.key,result.serial);
#endif
    if(proofBatch.pending){
        auto& sample=proofBatch.samples[proofBatch.count];
        sample.native=result.native;sample.processed=true;
        sample.afterRead=bootstrapApi->observeControlProof(sample.after);
        proofBatch.pending=false;++proofBatch.count;
        if(!sample.beforeRead || !sample.afterRead){proofSeal("snapshot_unknown");return;}
        if(!(sample.before.local==sample.after.local)){proofSeal("local_state_changed_during_remote_dispatch");return;}
        proofBatch.phaseMask|=1u<<static_cast<unsigned>(sample.phase);
    }
    if(proofSource.complete(grandmp::core::RemoteRoster::Clock::now())){
        const bool phases=proofBatch.phaseMask==7;
        proofSeal(phases ? "native_window_collected_not_physical_proof" : "phase_not_observed",phases);
    }
}
DWORD WINAPI proofWorker(void*) {
    // Only copied samples after release/acquire sealing. No API/Binding/PED read.
    const auto start=GetTickCount64();
    for(;;){
        reportProcessingFailure();
        if(proofBatch.sealed.load(std::memory_order_acquire)){
            bool complete=proofBatch.complete && proofBatch.count>0 && proofBatch.phaseMask==7;
            char point[200]{}; // Fits the 384-byte sink even at maximum prefix widths.
            for(unsigned i=0;i<proofBatch.count;++i){
                const auto& sample=proofBatch.samples[i];
                if(!sample.beforeRead || !sample.afterRead || !sample.processed){complete=false;continue;}
                for(const bool local:{false,true}){
                    if(!proof::format(point,sizeof(point),sample,local) ||
                       !diagnostic.proofSample(point,sample.key.player,sample.key.generation,sample.serial))complete=false;
                }
#if defined(GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD) && GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD == 1
                if(!proof::formatSemantic(point,sizeof(point),sample) ||
                   !diagnostic.semanticSample(point,sample.key.player,sample.key.generation,sample.serial))complete=false;
#endif
            }
#if defined(GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD) && GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD == 1
            const auto& history=proofBatch.history;
            const int historySize=std::snprintf(point,sizeof(point),"proof.semantic.history processed=%llu first=%llu last=%llu consecutive=%u",
                static_cast<unsigned long long>(history.count),static_cast<unsigned long long>(history.first),
                static_cast<unsigned long long>(history.last),unsigned(history.consecutive));
            if(!history.count || !history.consecutive || historySize<0 || static_cast<std::size_t>(historySize)>=sizeof(point))complete=false;
            diagnostic.emit(diag::Note::semantic_history,historySize>=0 && static_cast<std::size_t>(historySize)<sizeof(point) ? point : "proof.semantic.history.format_refused",
                0,history.key.player,history.key.generation,history.count);
#endif
            const int n=std::snprintf(point,sizeof(point),"proof.history.%s.%s",complete ? "collected" : "inconclusive",proofBatch.outcome);
            diagnostic.emit(diag::Note::proof_history,n>=0 && static_cast<std::size_t>(n)<sizeof(point) ? point : "proof.history.format_refused",
                diagnostic.error(),proofBatch.count,complete,proofBatch.phaseMask);
            // Keep draining the existing first-failure mailboxes until removal.
            while(!stopped.load()){reportProcessingFailure();Sleep(20);}
            reportProcessingFailure();return 0;
        }
        const auto now=GetTickCount64(),heartbeat=proofBatch.heartbeat.load(std::memory_order_acquire);
        if(stopped.load() || (heartbeat && now-heartbeat>3000) || now-start>180000){
            proofBatch.stopRequested.store(true,std::memory_order_release);
            diagnostic.emit(diag::Note::proof_history,"proof.history_inconclusive_no_producer_completion",0,heartbeat!=0);
            // No inspection of an unsealed array, no reclamation, no retry.
            reportProcessingFailure();return 0;
        }
        Sleep(20);
    }
}
