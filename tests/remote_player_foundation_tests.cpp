#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../client/windows/tools/pooled_remote_player.hpp"
#include "../client/windows/cleo/processing_diagnostics.hpp"
#include "../client/windows/cleo/control_proof_diagnostics.hpp"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cstdio>
#include <stdexcept>
#include <string_view>

namespace fp=grandmp::windows::remote_player_binding::pooled_player;
namespace rb=grandmp::windows::remote_player_binding;
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void run(std::string_view test){
    if(test=="proof_format"){
        namespace proof=grandmp::windows::independent_control_proof;
        proof::Sample sample{};char point[200]{};
        check(!proof::format(point,sizeof(point),sample,false),"unknown reads never become zero-valued motion evidence");
        sample.beforeRead=sample.afterRead=sample.processed=true;sample.key={UINT64_MAX,UINT64_MAX};sample.serial=UINT64_MAX;
        sample.elapsedMs=UINT32_MAX;
        auto& actor=sample.after.remote;actor.position={-FLT_MAX,FLT_MAX,-FLT_MAX};actor.heading=-FLT_MAX;
        actor.states={INT_MIN,INT_MAX};actor.localShift={-FLT_MAX,FLT_MAX};sample.before.remote.position={FLT_MAX,-FLT_MAX,FLT_MAX};
        sample.after.local=actor;sample.after.local.health=FLT_MAX;
        sample.native.padApply.step=grandmp::windows::remote_player_binding::replay::PadWriteStep::protection;
        sample.native.padRestore.step=grandmp::windows::remote_player_binding::replay::PadWriteStep::allocation;
        wchar_t temporary[MAX_PATH]{};check(GetTempPathW(MAX_PATH,temporary)!=0,"own temporary directory");
        const auto directory=std::filesystem::path(temporary)/(L"GrandMP-proof-format-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        check(std::filesystem::create_directory(directory),"exclusive fixture");
        const auto prefix=(directory/L"proof").wstring();grandmp::windows::cleo_bootstrap::OnceLog log(prefix.c_str());
        for(const bool local:{false,true}){
            check(proof::format(point,sizeof(point),sample,local),"extreme finite values fit bounded formatter");
            check(log.emit(local ? grandmp::windows::cleo_bootstrap::Note::proof_sample : grandmp::windows::cleo_bootstrap::Note::proof_source,
                point,0,sample.key.player,sample.key.generation,sample.serial),"complete final logger line fits with maximum correlation values");
        }
        check(!proof::format(point,4,sample,true) && !point[0],"truncation explicit; no partial output");
        check(!(sample.before.local==sample.after.local),"changed local values cannot assert integrity");return;
    }
    if(test=="factory_observation"){
        fp::FactoryAdmissionFailure first{};auto& value=first.observation;
        value.flags=fp::FactoryAdmissionObservation::proof_entered;value.proofCalls=1;
        check(value.check(true,fp::FactoryStage::task_pool,fp::FactoryCause::missing) && value.stage==fp::FactoryStage::none,
            "successful admission checks are not failures");
        check(!value.check(false,fp::FactoryStage::task_slot,fp::FactoryCause::read_failed,0,1,7,ERROR_PARTIAL_COPY),"original false retained");
        check(!value.check(false,fp::FactoryStage::spawn_sphere,fp::FactoryCause::collision,1,0) &&
            value.stage==fp::FactoryStage::task_slot && value.cause==fp::FactoryCause::read_failed &&
            value.index==7 && value.error==ERROR_PARTIAL_COPY && value.reason==fp::Reason::none,"first refusal never overwritten; legacy Reason stays none");
        fp::FirstFactoryAdmissionFailure mailbox;fp::FactoryAdmissionFailure copy{};
        check(!mailbox.take(copy) && mailbox.publish(first) && !mailbox.publish({}) && mailbox.take(copy) &&
            copy.observation.stage==fp::FactoryStage::task_slot && !mailbox.take(copy) && !mailbox.publish(first),"value copy once, no rearm or retry");return;
    }
    if(test=="factory_preconditions"){
        unsigned queries=0;
        auto queried=[](void* value) noexcept {++*static_cast<unsigned*>(value);return true;};
        const auto owner=std::this_thread::get_id();
        fp::Api missingPlayer({&queries,queried,queried,nullptr,queried},owner,1);
        rb::Record player{{0,1},{},0};
        check(missingPlayer.create(player.key,player)==rb::Creation::absent && missingPlayer.reason()==fp::Reason::none &&
            missingPlayer.factoryObservation().stage==fp::FactoryStage::create_player,"missing player remains absent/Reason none");
        fp::Api missingGeneration({&queries,queried,queried,nullptr,queried},owner,1);rb::Record generation{{42,0},{},0};
        check(missingGeneration.create(generation.key,generation)==rb::Creation::absent && missingGeneration.reason()==fp::Reason::none &&
            missingGeneration.factoryObservation().stage==fp::FactoryStage::create_generation,"missing generation identified before any engine access");
        fp::Api mismatchedRecord({&queries,queried,queried,nullptr,queried},owner,1);rb::Record record{{42,1},{},0};
        check(mismatchedRecord.create({43,1},record)==rb::Creation::absent && mismatchedRecord.reason()==fp::Reason::none &&
            mismatchedRecord.factoryObservation().stage==fp::FactoryStage::create_record_key,"record mismatch preserves absent outcome");
        fp::Api unstaged({&queries,queried,queried,nullptr,queried},owner,1);
        check(unstaged.create(record.key,record)==rb::Creation::absent && unstaged.reason()==fp::Reason::none &&
            unstaged.factoryObservation().stage==fp::FactoryStage::create_staged_key,"no staged key never calls factory or allocator");
        const auto observation=unstaged.factoryObservation();
        check(observation.flags==fp::FactoryAdmissionObservation::create_entered && !observation.proofCalls &&
            !record.entity.ped && !record.reference && !queries,"no queries, allocation, writes or retry from early diagnostics");return;
    }
    if(test=="factory_context"){
        check(fp::executionEnabled,"real gate-ON backend in harmless host; context always denied");
        unsigned calls=0;
        auto denied=[](void* value) noexcept {++*static_cast<unsigned*>(value);return false;};
        fp::Api api({&calls,denied,denied,nullptr,denied},std::this_thread::get_id(),1);
        check(!api.factoryProven() && calls==1 && api.reason()==fp::Reason::construction_unproven,"one original context call, no readiness or memory access");
        const auto observed=api.factoryObservation();
        check(observed.stage==fp::FactoryStage::proof_context && observed.proofCalls==1 &&
            observed.flags==fp::FactoryAdmissionObservation::proof_entered,"precondition distinguished from create/allocation");
        fp::Api spawn({&calls,denied,denied,nullptr,denied},std::this_thread::get_id(),1);fp::ps::Position position{};
        check(!spawn.spawnGround(position) && calls==2 && spawn.reason()==fp::Reason::none &&
            spawn.factoryObservation().stage==fp::FactoryStage::spawn_context,"spawn refusal does not repair Reason or read outside context");return;
    }
    if(test=="factory_log"){
        namespace diag=grandmp::windows::cleo_bootstrap;
        wchar_t temporary[MAX_PATH]{};check(GetTempPathW(MAX_PATH,temporary)!=0,"temporary path");
        const auto directory=std::filesystem::path(temporary)/(L"GrandMP-factory-admission-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        check(std::filesystem::create_directory(directory),"exclusive test fixture");
        const auto prefix=(directory/L"diagnostic").wstring();diag::OnceLog log(prefix.c_str());
        fp::FactoryAdmissionFailure failure{};failure.result=rb::Result::blocked_contract;failure.after=rb::State::blocked;
        auto& value=failure.observation;value.key={42,2};value.sequence=1;value.flags=fp::FactoryAdmissionObservation::proof_entered;value.proofCalls=1;
        value.check(false,fp::FactoryStage::task_capacity,fp::FactoryCause::invalid,0,16384);
        diag::recordFactoryAdmissionFailure(log,failure);diag::recordFactoryAdmissionFailure(log,failure);
        unsigned files=0;std::string text;
        for(const auto& entry:std::filesystem::directory_iterator(directory)){
            ++files;std::ifstream input(entry.path(),std::ios::binary);text.assign(std::istreambuf_iterator<char>(input),{});
        }
        check(!log.error() && files==1 && std::count(text.begin(),text.end(),'\n')==3 &&
            text.find("foundation.factory.task_capacity.invalid")!=std::string::npos &&
            text.find("foundation.factory.progress")!=std::string::npos && text.find("detail=0x0000000003000005")!=std::string::npos,
            "bounded final log once, blocked_contract/Reason none preserved, allocation absent");
        check(text.find("DEADBEEF")==std::string::npos && text.find("address")==std::string::npos &&
            text.find("snapshot")==std::string::npos,"formatter emits only named scalar fields, no buffers or native addresses");return;
    }
    if(test=="processing_mailbox" || test=="processing_race"){
        fp::FirstProcessingFailure mailbox;fp::ProcessingFailure first{},other{},copy{};
        first.key={42,2};first.serial=1;first.trace.primary=rb::ProcessStep::apply;
        first.native.primary={fp::ProcessingGuard::slots_busy,0,8,0};
        first.native.restoration={fp::ProcessingGuard::restore_write,ERROR_WRITE_FAULT,0,0};
        other.key={99,3};other.serial=2;
        check(!mailbox.take(copy),"empty producer never fabricates processing evidence");
        if(test=="processing_race"){
            bool a=false,b=false;
            std::thread one([&]{a=mailbox.publish(first);});std::thread two([&]{b=mailbox.publish(other);});one.join();two.join();
            check(a!=b && mailbox.take(copy) && copy.key==(a ? first.key:other.key),"one complete first-winner record, never a mixed snapshot");
        }else{
            check(mailbox.publish(first) && !mailbox.publish(other) && mailbox.take(copy) && copy.key==first.key &&
                copy.native.primary.guard==fp::ProcessingGuard::slots_busy && copy.native.restoration.guard==fp::ProcessingGuard::restore_write,
                "slow consumer receives primary plus cleanup without overwrite");
        }
        check(!mailbox.take(copy) && !mailbox.publish(other),"one consumer, no restart/retry after terminal drain");return;
    }
    if(test=="processing_log" || test=="pad_trace_log"){
        namespace diag=grandmp::windows::cleo_bootstrap;
        wchar_t temporary[MAX_PATH]{};check(GetTempPathW(MAX_PATH,temporary)!=0,"temporary path");
        const auto directory=std::filesystem::path(temporary)/(L"GrandMP-processing-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        check(std::filesystem::create_directory(directory),"exclusive harmless test directory");
        const auto prefix=(directory/L"diagnostic").wstring();diag::OnceLog log(prefix.c_str());
        fp::ProcessingFailure value{};value.key={42,2};value.serial=1;value.returned=true;value.result=rb::Result::restore_failed;
        value.trace.enter(rb::ProcessStep::apply);value.trace.finish(rb::ProcessStep::apply,false);
        value.trace.enter(rb::ProcessStep::restore);value.trace.finish(rb::ProcessStep::restore,false);
        value.native.primary={fp::ProcessingGuard::target,0,1,0}; // Presence, not an entity pointer.
        value.native.restoration={fp::ProcessingGuard::restore_write,ERROR_WRITE_FAULT,0,0};
        if(test=="pad_trace_log"){
            auto& apply=value.native.padApply;apply.step=rb::replay::PadWriteStep::protection;
            apply.protection=PAGE_EXECUTE_WRITECOPY;apply.allocationProtection=PAGE_EXECUTE_WRITECOPY;
            apply.state=MEM_COMMIT;apply.type=MEM_IMAGE;apply.queryBytes=sizeof(MEMORY_BASIC_INFORMATION);apply.allocationMatches=true;
            auto& restore=value.native.padRestore;restore.step=rb::replay::PadWriteStep::readback;restore.error=ERROR_PARTIAL_COPY;
            restore.writeCalled=restore.writeReturned=restore.readCalled=true;restore.written=96;restore.read=47;
        }
        diag::recordProcessingFailure(log,value);diag::recordProcessingFailure(log,value);
        check(!log.error(),"bounded final formatting fits existing 384-byte logger");
        unsigned files=0;std::string text;
        for(const auto& entry:std::filesystem::directory_iterator(directory)){
            ++files;std::ifstream input(entry.path(),std::ios::binary);text.assign(std::istreambuf_iterator<char>(input),{});
        }
        check(files==1 && std::count(text.begin(),text.end(),'\n')==(test=="pad_trace_log" ? 10 : 4) &&
            text.find("foundation.process.restore_failed")!=std::string::npos &&
            text.find("foundation.process.guard.target")!=std::string::npos &&
            text.find("foundation.process.restore.restore_write")!=std::string::npos,"four first-only records with separate primary/restoration");
        if(test=="pad_trace_log")check(text.find("foundation.pad.apply.protection win32=0 observed=0x0000000000000080")!=std::string::npos &&
            text.find("foundation.pad.restore.readback win32=299")!=std::string::npos &&
            text.find("observed=0x0000000000000060 expected=0x000000000000002F")!=std::string::npos,
            "complete deferred output preserves distinct page predicate and partial restoration read, no invented write success");
        check(text.find("000000000000002A")!=std::string::npos && text.find("0000000000000002")!=std::string::npos &&
            text.find("DEADBEEF")==std::string::npos && text.find("address")==std::string::npos,"only explicit correlation/scalar fields formatted");
        // Retain the tiny test output in this exclusive fixture; no historical cleanup.
        return;
    }
    if(test=="group_ownership"){
        fp::layout::factory::GroupImage image{};constexpr std::uint32_t base=0x12000,ped=0x34000,cj=0x56000;
        auto put=[&](unsigned offset,std::uint32_t value){std::memcpy(image.data()+offset,&value,4);};
        put(0x30,base);put(0x28,ped);
        check(fp::exclusiveGroupMember(image,base,ped),"owned leader plus empty followers");
        put(0x28,0);check(!fp::exclusiveGroupMember(image,base,ped),"empty/recycled group is not ownership proof");
        put(0x28,ped);put(0xC,cj);check(!fp::exclusiveGroupMember(image,base,ped),"CJ or any foreign follower blocks cleanup");
        put(0xC,0);put(0x30,base+4);check(!fp::exclusiveGroupMember(image,base,ped),"manager self-link must remain authenticated");return;
    }
    if(test=="runtime_context"){
        check(fp::executionEnabled,"isolated host compiles the real gate-ON context check, never admits native access");
        unsigned queries=0;
        auto denied=[](void* value) noexcept {++*static_cast<unsigned*>(value);return false;};
        fp::Api api({&queries,denied,denied,nullptr,denied},std::this_thread::get_id(),1);
        rb::Record record{{42,1},{},0};fp::LocalIdentity local{};rb::ContextImage image{};
        check(!api.factoryProven() && api.create(record.key,record)==rb::Creation::absent && !api.localIdentity(local) &&
            !api.authenticate(record) && api.remove(record)==rb::Removal::indeterminate && !api.apply(record,{}) &&
            !api.capture(image) && !api.restore(image) && !api.original(record) && !api.present({}),"denied phase cannot reach a GTA read or call");
        api.beginProcessingObservation();
        check(!api.capture(image) && !api.restore(image),"denied context retains refusal and restoration independently");
        const auto observation=api.endProcessingObservation();
        check(observation.primary.guard==fp::ProcessingGuard::replay_context &&
            observation.restoration.guard==fp::ProcessingGuard::restore_context,"real backend observation records no memory values outside context");
        const auto count=queries;bool refused=false;
        std::thread other([&]{refused=!api.context(rb::Phase::pre_world) && !api.context(rb::Phase::original_process_control) &&
            !api.factoryProven() && api.create(record.key,record)==rb::Creation::absent;});other.join();
        check(refused && queries==count,"wrong thread is rejected before any provider access");return;
    }
    if(test=="closed"){
        unsigned queries=0;
        auto queried=[](void* value) noexcept {++*static_cast<unsigned*>(value);return true;};
        fp::Api api({&queries,queried,queried,nullptr,queried},std::this_thread::get_id(),1);
        rb::Record record{{42,1},{},0};fp::LocalIdentity local{};
        check(!fp::executionEnabled && !api.context(rb::Phase::pre_world) && !api.factoryProven(),"physical foundation is closed");
        check(api.create(record.key,record)==rb::Creation::absent && !api.authenticate(record) &&
            api.remove(record)==rb::Removal::indeterminate && !api.present({}),"closed gate blocks all native operations");
        check(!api.localIdentity(local) && !queries && !record.entity.ped && !record.reference,"no provider, allocation, or memory query when closed");
        return;
    }
    if(test=="private_table"){
        alignas(4) volatile LONG cell=17;
        check(!fp::publishPrivateTable(&cell,17,29,false) && cell==17,"denied context cannot publish");
        check(!fp::publishPrivateTable(&cell,18,29,true) && cell==17,"unexpected identity never overwritten");
        check(!fp::publishPrivateTable(nullptr,17,29,true) && !fp::publishPrivateTable(&cell,17,17,true),"invalid publication refused");
        check(fp::publishPrivateTable(&cell,17,29,true) && cell==29,"one private data cell exchanged");
        check(!fp::publishPrivateTable(&cell,17,31,true) && cell==29,"no overwrite after publication");return;
    }
    if(test=="private_race"){
        alignas(4) volatile LONG cell=17;bool a=false,b=false;
        std::thread first([&]{a=fp::publishPrivateTable(&cell,17,29,true);});
        std::thread second([&]{b=fp::publishPrivateTable(&cell,17,31,true);});first.join();second.join();
        check(a!=b && cell==(a ? 29:31),"competing private publications cannot both succeed; not proof of GTA thread exclusion");return;
    }
    if(test=="step"){
        check(fp::validPoseStep({100,200,30},{102,199,30}) && fp::validPoseStep({100,200,30},{104,200,30.75f}),"bounded world-space displacement");
        check(!fp::validPoseStep({100,200,30},{104.01f,200,30}) && !fp::validPoseStep({100,200,30},{100,200,30.76f}),"large/vertical correction refused");
        check(!fp::validPoseStep({NAN,20,30},{102,199,30}) && !fp::validPoseStep({100,200,30},{102,INFINITY,30}),"nonfinite pose never reaches native setters");return;
    }
    if(test=="controller"){
        using namespace grandmp::core;
        RemotePlayerController controller;RemoteRoster roster;RemotePlayerFrames frames{};
        const auto now=RemoteRoster::Time{};controller.beginSession(101);
        OnFootState state{};state.x=100;state.y=200;state.z=30;state.controls={0,1,PlayerControls::walk};
        for(std::uint32_t seq=1;seq<=3;++seq){
            if(seq==2){state.x+=1;state.controls={1,0,PlayerControls::walk};state.heading=1.f;}
            if(seq==3){state.y+=1;state.controls={};}
            check(roster.position({202,seq,state.x,state.y,state.z},now)!=RemoteRoster::PositionResult::ignored,"real roster accepted");
            check(controller.receive({101,202,seq,state},roster,now),"existing Controller receives actual protocol values");
            controller.synchronize(roster,now);
            check(controller.tryRead(now,frames) && frames.count==1 && frames.players[0].key.player==202 &&
                frames.players[0].sequence==seq && frames.players[0].reference.controls==state.controls,"walk/turn/stop share one remote identity");
        }
        const auto key=frames.players[0].key;
        check(!controller.receive({101,202,2,state},roster,now),"stale input rejected before any native adapter");
        controller.beginSession(303);
        check(controller.tryRead(now,frames) && !frames.count && frames.endedCount && frames.ended[0].key==key,"session boundary invalidates native generation");
        return; // This proves only the real portable pipeline, NOT native creation.
    }
    throw std::runtime_error("unknown test");
}
int main(int argc,char** argv){try{if(argc!=2)return 2;run(argv[1]);return 0;}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
