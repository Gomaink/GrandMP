#pragma once
#include "diagnostics.hpp"
#include "../tools/pooled_remote_player.hpp"

namespace grandmp::windows::cleo_bootstrap {
// Existing network worker only. No formatting/file I/O in ProcessControl.
inline void recordProcessingFailure(OnceLog& log,const remote_player_binding::pooled_player::ProcessingFailure& record) noexcept {
    namespace rb=remote_player_binding;
    namespace fp=rb::pooled_player;
    constexpr const char* results[]{"closed","context_refused","no_input","occupied","stale","blocked_contract",
        "created","updated","removed","ownership_lost","indeterminate","duplicate","reentrant",
        "processed","context_failed","original_failed","restore_failed"};
    const auto result=static_cast<unsigned>(record.result);
    char point[96]{};
    const int n=std::snprintf(point,sizeof(point),"foundation.process.%s",
        record.wrapper!=fp::WrapperRefusal::none ? "wrapper_refused" : !record.returned ? "unwound" : result<std::size(results) ? results[result] : "invalid_result");
    if(n<0 || static_cast<std::size_t>(n)>=sizeof(point)){
        log.emit(Note::processing_result,"foundation.process.format_refused",ERROR_INSUFFICIENT_BUFFER);return;
    }
    log.emit(Note::processing_result,point,0,record.key.player,record.key.generation,record.serial);
    // Bytes: state/input/wrapper/body/primary; bits 40/41: caught-C++/returned.
    const auto flags=static_cast<std::uint64_t>(record.state) |
        (static_cast<std::uint64_t>(record.input)<<8) | (static_cast<std::uint64_t>(record.wrapper)<<16) |
        (static_cast<std::uint64_t>(record.trace.body)<<24) | (static_cast<std::uint64_t>(record.trace.primary)<<32) |
        (static_cast<std::uint64_t>(record.trace.cppException)<<40) | (static_cast<std::uint64_t>(record.returned)<<41);
    log.emit(Note::processing_stages,"foundation.process.stages",0,record.trace.entered,record.trace.passed,flags);
    const auto emit=[&](Note note,const char* prefix,const fp::ProcessingRefusal& refusal) noexcept {
        const int size=std::snprintf(point,sizeof(point),"foundation.process.%s.%s",prefix,fp::processingGuardName(refusal.guard));
        if(size<0 || static_cast<std::size_t>(size)>=sizeof(point))log.emit(note,"foundation.process.format_refused",ERROR_INSUFFICIENT_BUFFER);
        else log.emit(note,point,refusal.error,refusal.observed,refusal.expected);
    };
    emit(Note::processing_primary,"guard",record.native.primary);
    emit(Note::processing_restore,"restore",record.native.restoration);
    const auto pad=[&](const char* scope,Note resultNote,Note pageNote,Note transferNote,const rb::replay::PadWriteObservation& value) noexcept {
        if(value.step==rb::replay::PadWriteStep::not_called)return;
        const int size=std::snprintf(point,sizeof(point),"foundation.pad.%s.%s",scope,rb::replay::padWriteStepName(value.step));
        if(size<0 || static_cast<std::size_t>(size)>=sizeof(point))log.emit(resultNote,"foundation.pad.format_refused",ERROR_INSUFFICIENT_BUFFER);
        else log.emit(resultNote,point,value.error,value.protection,value.allocationProtection,value.queryBytes);
        log.emit(pageNote,"foundation.pad.page",0,value.state,value.type,value.allocationMatches);
        const unsigned flags=unsigned(value.writeCalled) | (unsigned(value.writeReturned)<<1) | (unsigned(value.readCalled)<<2) |
            (unsigned(value.readReturned)<<3) | (unsigned(value.equal)<<4);
        log.emit(transferNote,"foundation.pad.transfer",0,value.written,value.read,flags);
    };
    pad("apply",Note::pad_apply,Note::pad_apply_page,Note::pad_apply_transfer,record.native.padApply);
    pad("restore",Note::pad_restore,Note::pad_restore_page,Note::pad_restore_transfer,record.native.padRestore);
#if defined(GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD) && GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD == 1
    const int semanticSize=std::snprintf(point,sizeof(point),"foundation.pad_semantic.restore.%s",semantic_pad_context::name(record.native.semantic));
    log.emit(Note::semantic_refusal,semanticSize>=0 && static_cast<std::size_t>(semanticSize)<sizeof(point) ? point : "foundation.pad_semantic.format_refused",
        0,static_cast<unsigned>(record.native.semantic),record.native.tail.touchedBefore,record.native.tail.touchedAfter);
#endif
#if defined(GRANDMP_REMOTE_TAIL_DIAGNOSTIC_BUILD) && GRANDMP_REMOTE_TAIL_DIAGNOSTIC_BUILD == 1
    const auto& tail=record.native.tail;
    if(tail.compared && tail.changed && log.claimRemoteTail()){
        char line[176]{};
        for(unsigned row=0;row<4u+tail.listed && row<remote_tail_diagnostic::maxRows;++row){
            if(!remote_tail_diagnostic::format(line,sizeof(line),tail,row)){
                log.remoteTailRow("foundation.tail.format_refused",record.key.player,record.key.generation,record.serial);break;
            }
            log.remoteTailRow(line,record.key.player,record.key.generation,record.serial);
        }
    }
#endif
}
inline void recordFactoryAdmissionFailure(OnceLog& log,const remote_player_binding::pooled_player::FactoryAdmissionFailure& record) noexcept {
    namespace fp=remote_player_binding::pooled_player;
    const auto& value=record.observation;
    log.emit(Note::factory_admission,value.stage==fp::FactoryStage::none ? "foundation.factory.no_refusal_observed" : "foundation.factory.refused",
        0,value.key.player,value.key.generation,value.sequence);
    char point[128]{};
    const int n=std::snprintf(point,sizeof(point),"foundation.factory.%s.%s",fp::factoryStageName(value.stage),fp::factoryCauseName(value.cause));
    if(n<0 || static_cast<std::size_t>(n)>=sizeof(point))log.emit(Note::factory_admission_detail,"foundation.factory.format_refused",ERROR_INSUFFICIENT_BUFFER);
    else log.emit(Note::factory_admission_detail,point,value.error,value.observed,value.expected,value.index);
    const auto outcome=static_cast<std::uint64_t>(record.result) | (static_cast<std::uint64_t>(value.reason)<<8) |
        (static_cast<std::uint64_t>(record.before)<<16) | (static_cast<std::uint64_t>(record.after)<<24);
    log.emit(Note::factory_admission_progress,"foundation.factory.progress",0,value.flags,value.proofCalls,outcome);
}
}
