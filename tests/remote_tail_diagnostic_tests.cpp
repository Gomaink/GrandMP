#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../client/windows/tools/pooled_remote_player.hpp"
#include "../client/windows/cleo/processing_diagnostics.hpp"
#include "../client/windows/tools/semantic_pad_context.hpp"
#include "../client/windows/cleo/control_proof_diagnostics.hpp"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string_view>

namespace tail=grandmp::windows::remote_tail_diagnostic;
namespace fp=grandmp::windows::remote_player_binding::pooled_player;
namespace rb=grandmp::windows::remote_player_binding;
namespace diag=grandmp::windows::cleo_bootstrap;
void check(bool okay,const char* why){if(!okay)throw std::runtime_error(why);}
void putTouched(tail::Tail& bytes,std::uint32_t value){
    for(unsigned n=0;n<4;++n)bytes[tail::lastTouched-tail::begin+n]=std::byte((value>>(n*8))&255);
}
void run(std::string_view name){
    tail::Tail before{},after{};char line[176]{};
    namespace semantic=grandmp::windows::semantic_pad_context;
    using Decision=semantic::Decision;
    if(name.starts_with("semantic_")){
        putTouched(after,14552);tail::Boundary clock{7,14552,4,0,true};
        const auto decide=[&](bool returned=true){return semantic::evaluate(before,after,clock,clock,returned);};
        if(name=="semantic_positive"){
            check(decide()==Decision::native_touch && semantic::accepted(decide()),"physical 0 -> 14552 accepted using full clock and timestamp");
            putTouched(before,14500);check(decide()==Decision::native_touch,"existing nonzero engine history persists");
            before=after;check(semantic::evaluate(before,after,{},{},false)==Decision::unchanged,"exact equality retains old path, no exception needed");return;
        }
        if(name=="semantic_other_bytes"){
            unsigned count=0;
            for(std::size_t n=0;n<tail::extent;++n){
                if(n+tail::begin>=0x120 && n+tail::begin<0x124)continue;
                after[n]=std::byte{1};check(decide()==Decision::other_tail,"each of the other 208 bytes remains protected despite valid timestamp");
                after[n]=std::byte{0};++count;
            }
            check(count==208,"full complementary extent checked");
            putTouched(after,14553);after.back()=std::byte{3};
            check(decide()==Decision::other_tail,"simultaneous unknown/timestamp divergence never accepted");return;
        }
        if(name=="semantic_clock"){
            auto bad=clock;bad.clockComplete=false;check(!semantic::accepted(semantic::evaluate(before,after,clock,bad,true)),"unknown clock refused");
            bad=clock;bad.transferred=2;check(semantic::evaluate(before,after,bad,clock,true)==Decision::clock_unavailable,"partial read refused even if complete flag is wrong");
            bad=clock;bad.error=ERROR_ACCESS_DENIED;check(semantic::evaluate(before,after,clock,bad,true)==Decision::clock_unavailable,"recorded read error refused");
            bad=clock;++bad.engineMs;check(semantic::evaluate(before,after,clock,bad,true)==Decision::clock_changed,"no interval guess when engine clock advanced");
            bad=clock;--bad.engineMs;check(semantic::evaluate(before,after,clock,bad,true)==Decision::clock_changed,"clock regression refused");return;
        }
        if(name=="semantic_identity"){
            check(decide(false)==Decision::native_not_returned,"no normal native return cannot use exception");
            auto bad=clock;bad.captureTid=0;check(semantic::evaluate(before,after,bad,clock,true)==Decision::thread_mismatch,"unknown capture thread refused");
            bad=clock;++bad.captureTid;check(semantic::evaluate(before,after,clock,bad,true)==Decision::thread_mismatch,"different producer threads refused");return;
        }
        if(name=="semantic_full_value"){
            putTouched(after,14553);check(decide()==Decision::timestamp_mismatch,"arbitrary in-field change refused");
            putTouched(after,14552u+65536u);check(decide()==Decision::timestamp_mismatch,"matching low bytes cannot hide a high-word change");
            putTouched(before,14553);putTouched(after,14552);check(decide()==Decision::timestamp_regressed,"future prior value refused");
            putTouched(before,UINT32_MAX);putTouched(after,1);clock.engineMs=1;
            check(decide()==Decision::timestamp_regressed,"wrap/reset not inferred");return;
        }
#if defined(GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD) && GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD == 1
        if(name=="semantic_diagnostics"){
            namespace proof=grandmp::windows::independent_control_proof;
            proof::DispatchHistory history{};
            for(std::uint64_t serial=1;serial<=300;++serial)history.completed({42,1},serial);
            check(history.count==300 && history.first==1 && history.last==300 && history.consecutive,"all completed dispatches counted without logging each frame");
            auto gap=history;gap.completed({42,1},302);check(!gap.consecutive,"gap reported honestly");
            auto generation=history;generation.completed({42,2},301);check(!generation.consecutive,"generation change never claims continuous identity");
            proof::Sample sample{};sample.beforeRead=sample.afterRead=sample.processed=true;sample.native.semantic=Decision::native_touch;
            sample.native.tail=tail::compare(before,after,{UINT32_MAX,UINT32_MAX,4,0,true},{UINT32_MAX,UINT32_MAX,4,0,true});
            sample.native.tail.touchedBefore=UINT32_MAX-1;sample.native.tail.touchedAfter=UINT32_MAX;
            check(proof::formatSemantic(line,sizeof(line),sample),"bounded semantic values formatted");
            char envelope[384]{};const auto wide=static_cast<unsigned long long>(UINT64_MAX);
            const int size=std::snprintf(envelope,sizeof(envelope),"pid=%u tid=%u ms=%llu note=%u point=%s win32=%u observed=0x%016llX expected=0x%016llX detail=0x%016llX\r\n",UINT32_MAX,UINT32_MAX,wide,63u,line,UINT32_MAX,wide,wide,wide);
            check(size>0 && static_cast<std::size_t>(size)<sizeof(envelope),"full final logger envelope fits");
            check(!proof::formatSemantic(line,4,sample) && !line[0],"truncation explicit");
            wchar_t temporary[MAX_PATH]{};check(GetTempPathW(MAX_PATH,temporary)!=0,"semantic log fixture root");
            const auto directory=std::filesystem::path(temporary)/(L"GrandMP-semantic-pad-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
            check(std::filesystem::create_directory(directory),"exclusive semantic fixture");
            const auto prefix=(directory/L"semantic").wstring();diag::OnceLog log(prefix.c_str());
            check(proof::formatSemantic(line,sizeof(line),sample),"valid final semantic message");
            for(unsigned n=0;n<16;++n)check(log.semanticSample(line,UINT64_MAX,UINT64_MAX,n+1),"actual deferred sink accepts bounded samples");
            check(!log.semanticSample("unexpected_extra_sample",1,1,17),"no semantic log flood after capacity");
            check(log.emit(diag::Note::semantic_history,"proof.semantic.history",0,42,1,300) &&
                !log.emit(diag::Note::semantic_history,"duplicate_history"),"summary once only");
            std::string output;for(const auto& file:std::filesystem::directory_iterator(directory)){
                std::ifstream input(file.path(),std::ios::binary);output.append(std::istreambuf_iterator<char>(input),{});
            }
            std::size_t rows=0,pos=0;while((pos=output.find("point=proof.pad_semantic.",pos))!=std::string::npos){++rows;++pos;}
            check(rows==16 && !log.error() && output.find("clock_reads=1")!=std::string::npos &&
                output.find("unexpected_extra_sample")==std::string::npos && output.find("duplicate_history")==std::string::npos &&
                output.find("0xB735")==std::string::npos,"bounded final output with explicit clocks and no native addresses");
            sample.processed=false;check(!proof::formatSemantic(line,sizeof(line),sample),"failed processing never formatted as success");return;
        }
#endif
        throw std::runtime_error("unknown semantic test");
    }
    if(name=="equal"){
        const auto value=tail::compare(before,after,{1,10,4,0,true},{1,11,4,0,true});
        check(value.compared && !value.changed && !value.listed,"clock changes do not become tail differences");
        check(!tail::format(line,sizeof(line),value,0) && !line[0],"no output for equal tail");return;
    }
    if(name=="boundaries"){
        after.front()=std::byte{1};after.back()=std::byte{255};
        const auto value=tail::compare(before,after,{},{});
        check(value.first==0x60 && value.changed==2 && value.listed==2 && value.differences[1].offset==0x133 &&
            value.differences[1].before==0 && value.differences[1].after==255,"exact inclusive range and byte values");
        check(!value.touchedBefore && !value.touchedAfter,"boundary edits cannot invent timestamp change");return;
    }
    if(name=="touched"){
        putTouched(before,0xFFFFFFFEu);putTouched(after,7);
        const auto value=tail::compare(before,after,{11,0xFFFFFFFEu,4,0,true},{11,7,4,0,true});
        check(value.first==0x120 && value.touchedBefore==0xFFFFFFFEu && value.touchedAfter==7 && value.changed==4,"little endian timestamp and timer wrap preserved, no causal inference");
        check(tail::format(line,sizeof(line),value,1) && std::string_view(line).find("before=4294967294 after=7 changed=1")!=std::string_view::npos,"timestamp reported separately");return;
    }
    if(name=="bounded"){
        after.fill(std::byte{255});const auto value=tail::compare(before,after,{},{});
        check(value.changed==212 && value.listed==8 && value.first==0x60 && value.differences.back().offset==0x67 && value.touchedAfter==UINT32_MAX,
            "all changed bytes counted, only first eight copied, timestamp survives omission");
        check(tail::format(line,sizeof(line),value,0) && std::string_view(line).find("omitted=204")!=std::string_view::npos,"omissions explicit");
        check(!tail::format(line,sizeof(line),value,tail::maxRows),"no thirteenth row or full dump");return;
    }
    if(name=="unknown_clock"){
        after.front()=std::byte{2};const auto value=tail::compare(before,after,{7,0xDEADBEEF,2,ERROR_PARTIAL_COPY,false},{7,99,4,0,true});
        check(value.changed==1 && tail::format(line,sizeof(line),value,2),"clock failure does not discard valid comparison");
        check(std::string_view(line).find("complete=0 engine_ms=0 bytes=2 error=299")!=std::string_view::npos,"partial clock never emitted as valid time");return;
    }
    if(name=="format"){
        after.fill(std::byte{255});const auto value=tail::compare(before,after,{UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,true},{UINT32_MAX,UINT32_MAX,4,0,true});
        for(unsigned row=0;row<tail::maxRows;++row){
            check(tail::format(line,sizeof(line),value,row),"maximum scalar widths fit diagnostic buffer");
            char finalLine[384]{};
            const int count=std::snprintf(finalLine,sizeof(finalLine),
                "pid=%u tid=%u ms=%llu note=%u point=%s win32=%u observed=0x%016llX expected=0x%016llX detail=0x%016llX\r\n",
                UINT32_MAX,UINT32_MAX,static_cast<unsigned long long>(UINT64_MAX),63u,line,UINT32_MAX,
                static_cast<unsigned long long>(UINT64_MAX),static_cast<unsigned long long>(UINT64_MAX),static_cast<unsigned long long>(UINT64_MAX));
            check(count>0 && static_cast<std::size_t>(count)<sizeof(finalLine),"complete final logger envelope fits at extreme values");
            check(!tail::format(line,4,value,row) && !line[0],"explicit truncation, no partial message");
        }
        check(!tail::format(nullptr,0,value,0),"null destination refused");return;
    }
    if(name=="async_log"){
        wchar_t temporary[MAX_PATH]{};check(GetTempPathW(MAX_PATH,temporary)!=0,"temporary fixture root");
        const auto directory=std::filesystem::path(temporary)/(L"GrandMP-tail-D1-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        check(std::filesystem::create_directory(directory),"exclusive disposable log fixture");
        const auto prefix=(directory/L"tail").wstring();diag::OnceLog log(prefix.c_str());
        fp::FirstProcessingFailure mailbox;DWORD captureTid=0,emissionTid=0;
        fp::ProcessingFailure copy{};check(!mailbox.take(copy),"no unsealed payload exposed");
        std::thread producer([&]{
            captureTid=GetCurrentThreadId();after.fill(std::byte{255});
            fp::ProcessingFailure record{};record.key={UINT64_MAX,UINT64_MAX};record.serial=UINT64_MAX;record.returned=true;record.result=rb::Result::restore_failed;
            record.native.tail=tail::compare(before,after,{captureTid,123,4,0,true},{captureTid,123,4,0,true});
            check(mailbox.publish(record),"first native-value record published");
            check(!mailbox.publish({}),"never rearm or retry");
        });
        producer.join();
        emissionTid=GetCurrentThreadId();check(captureTid!=emissionTid && mailbox.take(copy),"separate producer and deferred consumer");
        diag::recordProcessingFailure(log,copy);diag::recordProcessingFailure(log,copy);
        check(!mailbox.take(copy) && !mailbox.publish({}),"one-shot mailbox remains terminal after drain");
        check(!log.remoteTailRow("unexpected_thirteenth_row",1,1,1),"sink independently bounds output");
        std::string text;for(const auto& file:std::filesystem::directory_iterator(directory)){
            std::ifstream input(file.path(),std::ios::binary);text.append(std::istreambuf_iterator<char>(input),{});
        }
        check(text.find("capture_before_tid="+std::to_string(captureTid))!=std::string::npos &&
            text.find("tid="+std::to_string(emissionTid)+" ms=")!=std::string::npos,"capture and writer identities explicit in actual log");
        const auto first=text.find("foundation.tail.summary");
        check(first!=std::string::npos && text.find("foundation.tail.summary",first+1)==std::string::npos,"no duplicated cluster");
        std::size_t rows=0,pos=0;while((pos=text.find("point=foundation.tail.",pos))!=std::string::npos){++rows;++pos;}
        check(rows==12 && text.find("LastTimeTouched offset=0x120 width=4 before=0 after=4294967295 changed=1")!=std::string::npos,"actual sink emits bounded list plus timestamp");
        check(text.find("0xB735")==std::string::npos && text.find("address=")==std::string::npos && text.find("pointer")==std::string::npos &&
            text.find("unexpected_thirteenth_row")==std::string::npos && !log.error(),"no native addresses, extra rows or logger truncation");return;
    }
    if(name=="closed_context"){
        unsigned queries=0;auto queried=[](void* value) noexcept {++*static_cast<unsigned*>(value);return true;};
        fp::Api api({&queries,queried,queried,nullptr,queried},std::this_thread::get_id(),1);rb::ContextImage image{};
        check(!fp::executionEnabled,"diagnostic branch compiled but physical execution closed");
        api.beginProcessingObservation();check(!api.capture(image) && !api.restore(image),"no capture or clock query outside admitted native context");
        const auto value=api.endProcessingObservation();
#if defined(GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD) && GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD == 1
        check(value.semantic==Decision::not_observed,"no semantic authorization outside context");
#endif
        check(!queries && !value.tail.compared && !value.tail.before.captureTid && !value.tail.after.captureTid &&
            value.primary.guard==fp::ProcessingGuard::replay_context && value.restoration.guard==fp::ProcessingGuard::restore_context,"guards unchanged and no invented tail evidence");return;
    }
    throw std::runtime_error("unknown test");
}
int main(int argc,char** argv){try{if(argc!=2)return 2;run(argv[1]);return 0;}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}}
