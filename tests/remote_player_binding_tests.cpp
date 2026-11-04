#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "grandmp/windows/remote_player_binding.hpp"
#if defined(GRANDMP_MINIMAL_NATIVE_LIFECYCLE_BUILD) && GRANDMP_MINIMAL_NATIVE_LIFECYCLE_BUILD == 1
#include "grandmp/windows/minimal_player_lifecycle.hpp"
#endif
#include "grandmp/windows/remote_player_hoodlum.hpp"
#include "../client/windows/cleo/independent_control_proof.hpp"
#include "../client/windows/tools/semantic_pad_context.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

using namespace grandmp;
namespace rb=windows::remote_player_binding;
namespace semantic=windows::semantic_pad_context;
namespace tail=windows::remote_tail_diagnostic;
void touched(tail::Tail& value,std::uint32_t time){
    for(unsigned n=0;n<4;++n)value[0x120-0x60+n]=std::byte((time>>(8*n))&255);
}
using namespace std::chrono_literals;
void check(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
using Time=core::RemoteRoster::Time;
const Time start{};

// Contract test double ONLY. Numbers are synthetic identities, never GTA addresses.
struct TestApi final : rb::Api {
    bool authorized=true,factory=true,removal=true,replay=true,ownership=true,dispatchOkay=true;
    bool captureOkay=true,applyOkay=true,restoreOkay=true,postcondition=true;
    bool throwApply=false,throwOriginal=false,sehOriginal=false,reenter=false;
    bool originalOkay=true,loseOwnershipOnApply=false,sehCapture=false;
    bool semanticEnabled=false,semanticReturned=false,loseOwnershipOnReturn=false;
    tail::Tail engineTail{},savedTail{},nextTail{};
    tail::Boundary clockBefore{7,14552,4,0,true},clockAfter{7,14552,4,0,true};
    semantic::Decision semanticDecision=semantic::Decision::not_observed;
    rb::Creation creation=rb::Creation::complete;
    rb::Removal removalResult=rb::Removal::complete;
    unsigned probes=0,creates=0,removes=0,captures=0,applies=0,originals=0,restores=0;
    // Models an arbitrary previous focus/camera/pad/flags image; focus is NOT zero.
    std::array<std::byte,64> contextBytes{};
    const std::uint32_t* referenceCell=nullptr;
    rb::Record authenticated{},removedBefore{};
    rb::Controls controls{};
    rb::Binding<true>* binding=nullptr;
    core::RemotePlayerController* controller=nullptr;
    rb::Dispatch activeDispatch{};
    TestApi() { contextBytes.fill(std::byte{0x65}); }
    bool context(rb::Phase) const noexcept override { return authorized; }
    bool factoryProven() const noexcept override { return factory; }
    bool removalProven() const noexcept override { return removal; }
    bool replayProven() const noexcept override { return replay; }
    rb::Creation create(core::RemotePlayerKey key,rb::Record& record) override {
        ++creates;check(record.key==key,"immutable admission key");
        referenceCell=&record.reference;
        if (creation!=rb::Creation::absent) record.entity={101,202,303,17,1},record.reference=101;
        authenticated=record;return creation;
    }
    bool authenticate(const rb::Record& record) noexcept override {
        ++probes;
        return ownership && record.key==authenticated.key && record.entity==authenticated.entity &&
            record.reference==record.entity.ped && &record.reference==referenceCell;
    }
#if defined(GRANDMP_MINIMAL_NATIVE_LIFECYCLE_BUILD) && GRANDMP_MINIMAL_NATIVE_LIFECYCLE_BUILD == 1
    bool removalReentry=false;
#endif
    rb::Removal remove(rb::Record& record) override {
#if defined(GRANDMP_MINIMAL_NATIVE_LIFECYCLE_BUILD) && GRANDMP_MINIMAL_NATIVE_LIFECYCLE_BUILD == 1
        if(removalReentry)check(binding->requestRemoval(*this)==rb::Result::reentrant,"removal reentrancy blocked");
#endif
        ++removes;record.reference=0;return removalResult;
    }
    bool removed(const rb::Record& before,const rb::Record& after) noexcept override {
        removedBefore=before;
        return postcondition && before.key==authenticated.key && before.entity==authenticated.entity &&
            before.reference==101 && after.reference==0; // Synthetic slot/pool proof below is explicit.
    }
    bool dispatch(const rb::Record&,const rb::Dispatch&) noexcept override { return dispatchOkay; }
    bool capture(rb::ContextImage& image) noexcept override {
        if(sehCapture) RaiseException(0xE0421234,0,0,nullptr);
        ++captures;if (!captureOkay) return false;
        if(semanticEnabled){savedTail=engineTail;semanticReturned=false;}
        image.used=contextBytes.size();std::memcpy(image.bytes.data(),contextBytes.data(),image.used);return true;
    }
    bool apply(const rb::Record&,const rb::Controls& input) override {
        ++applies;controls=input;contextBytes.fill(std::byte{0xab});
        if (throwApply) throw std::runtime_error("partial input application");
        if (loseOwnershipOnApply) ownership=false;
        return applyOkay;
    }
    bool original(const rb::Record&) override {
        ++originals;
        if (reenter) check(binding->process(*controller,start,activeDispatch,*this)==rb::Result::reentrant,"reentry denied");
        if (throwOriginal) throw std::runtime_error("original throws");
        if (sehOriginal) RaiseException(0xE0421234,0,0,nullptr);
        if(semanticEnabled){engineTail=nextTail;semanticReturned=originalOkay;}
        if(loseOwnershipOnReturn)ownership=false;
        return originalOkay;
    }
    bool restore(const rb::ContextImage& image) noexcept override {
        ++restores;std::memcpy(contextBytes.data(),image.bytes.data(),contextBytes.size());
        if(!restoreOkay)return false;
        if(semanticEnabled){
            semanticDecision=semantic::evaluate(savedTail,engineTail,clockBefore,clockAfter,semanticReturned);
            return semantic::accepted(semanticDecision); // Shared real policy, never restore the engine-owned history.
        }
        return true;
    }
};
struct Fixture {
    core::RemoteRoster roster;
    core::RemotePlayerController controller;
    rb::Binding<true>* binding=rb::Binding<true>::prepare(std::this_thread::get_id());
    TestApi api;
    Fixture() { check(binding!=nullptr,"resident registry");controller.beginSession(42);api.binding=binding;api.controller=&controller; }
    void sample(unsigned seq,float right=0,float forward=1,Time now=start,std::uint64_t id=12) {
        core::OnFootState state{};state.x=100;state.y=200;state.z=30;
        state.controls={right,forward,forward!=0 ? core::PlayerControls::walk : std::uint16_t{0}};
        (void)roster.position({id,seq,100,200,30},now);
        check(controller.receive({42,id,seq,state},roster,now),"real controller accepts packet");
        controller.synchronize(roster,now);
    }
    rb::Result consume(Time now=start) { return binding->consume(controller,now,api); }
    rb::Result process(std::uint64_t serial,Time now=start,rb::ProcessObservation* observation=nullptr) {
        api.activeDispatch={api.authenticated.key,api.authenticated.entity.ped,serial};
        return binding->process(controller,now,api.activeDispatch,api,observation);
    }
    void admit() { sample(1);check(consume()==rb::Result::created,"single test entity created"); }
    void leave() { check(roster.left({12},start+1ms),"leave");controller.synchronize(roster,start+1ms); }
};
// Keep SEH handler in an object-free frame; do not hide faults in production code.
bool sehCall(Fixture* fixture,rb::ProcessObservation* observation=nullptr) {
    __try { (void)fixture->process(1,start,observation);return false; }
    __except(GetExceptionCode()==0xE0421234 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return true; }
}
void run(std::string_view which,const char* executable) {
    if (which=="gate") {
        static_assert(!rb::buildEnabled);
        core::RemotePlayerController c;TestApi api;
        auto* b=rb::Binding<>::prepare(std::this_thread::get_id());
        check(b->consume(c,start,api)==rb::Result::closed && b->process(c,start,{},api)==rb::Result::closed,"default gate closed");
        check(!api.probes && !api.creates && !api.captures,"no native operations");return;
    }
    if (which=="hoodlum") {
        auto invalid=rb::inspectHoodlumPlayerContract({});check(!invalid.exactExecutable,"invalid image refused");
        check(executable!=nullptr,"explicit authorized file required for offline evidence");
        std::ifstream input(executable,std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(input)),{});
        const auto evidence=rb::inspectHoodlumPlayerContract(std::as_bytes(std::span(bytes)));
        check(evidence.exactExecutable && evidence.playerConstructorWindows && evidence.slotFactoryWindows &&
            evidence.slotDestructorWindows && evidence.processControlVtable,"exact SHA and Hoodlum evidence windows");
        check(!evidence.executionApproved && !evidence.additionalSlotAvailable && !evidence.originalDispatchAbi &&
            !evidence.completePlayerLifecycle && !evidence.completePadFocusCameraContext,"evidence does not open physical gates");
        bytes.back()^=1;
        check(!rb::inspectHoodlumPlayerContract(std::as_bytes(std::span(bytes))).exactExecutable,"one changed byte refuses identity");return;
    }
    Fixture f;
#if defined(GRANDMP_MINIMAL_NATIVE_LIFECYCLE_BUILD) && GRANDMP_MINIMAL_NATIVE_LIFECYCLE_BUILD == 1
    if(which=="armed_pending" || which=="armed_complete"){
        namespace m=rb::minimal_lifecycle;
        f.admit();const auto key=f.binding->key();m::ArmedRemoval request;
        check(request.poll(true,true,key,m::Window::waiting,0)==m::Opportunity::wait,"arm during walk");
        for(unsigned i=1;i<=3;++i){
            f.sample(i+1,0,1,start+std::chrono::milliseconds(i));
            check(f.process(i,start+std::chrono::milliseconds(i))==rb::Result::processed,"original Binding continues while F8 pending");
            check(request.poll(true,true,key,m::Window::waiting,i)==m::Opportunity::wait,"held F8 still pending");
        }
        check(f.api.originals==3 && f.api.applies==3 && f.api.restores==3 && !f.api.removes,"no suppressed original and no early cleanup");
        f.sample(5,0,0,start+4ms);
        check(f.process(4,start+4ms)==rb::Result::processed && f.api.controls.current==core::PlayerControls{},"neutral original completed/restored");
        check(request.poll(false,true,key,m::Window::complete,6000)==m::Opportunity::attempt,"one scheduled removal");
        if(which=="armed_complete"){
            check(f.binding->requestRemoval(f.api)==rb::Result::removed && f.api.removes==1,"actual Binding consumes native removal once");
            check(f.process(5,start+5ms)==rb::Result::indeterminate && f.api.originals==4,"late callback cannot call original after removal");
        }
        check(request.poll(true,true,key,m::Window::complete,6001)==m::Opportunity::wait,"no second dispatch");return;
    }
    if(which.starts_with("minimal_")){
        f.admit();const auto key=f.binding->key();const auto ped=f.binding->entityAddress();
        if(which=="minimal_restore"){
            f.api.restoreOkay=false;check(f.process(1)==rb::Result::restore_failed,"restoration failure remains terminal");
        }else check(f.process(1)==rb::Result::processed,"baseline process returned and restored");
        if(which=="minimal_identity")f.api.ownership=false;
        if(which=="minimal_admission")f.api.removal=false;
        if(which=="minimal_partial")f.api.removalResult=rb::Removal::indeterminate;
        if(which=="minimal_postcondition")f.api.postcondition=false;
        if(which=="minimal_reentry")f.api.removalReentry=true;
        const auto result=f.binding->requestRemoval(f.api);
        const bool success=which=="minimal_remove" || which=="minimal_reentry";
        check((result==rb::Result::removed)==success,"only proven completion is removed");
        const auto calls=f.api.removes,originals=f.api.originals,probes=f.api.probes;
        check(f.binding->requestRemoval(f.api)==rb::Result::blocked_contract,"no second removal request");
        check(f.process(2)==rb::Result::indeterminate,"late callback cannot process old entity");
        check(f.consume()==(success ? rb::Result::blocked_contract : rb::Result::indeterminate),"no readmission");
        check(f.api.removes==calls && f.api.originals==originals && f.api.probes==probes,"no engine identity/dtor retry on late requests");
        check(f.binding->key()==key && f.binding->entityAddress()==ped,"resident tombstone identity retained");
        if(which=="minimal_restore" || which=="minimal_identity" || which=="minimal_admission")check(!calls,"refusal before irreversible operation");
        return;
    }
#endif

    if(which=="proof_source" || which=="proof_timeout"){
        namespace proof=windows::independent_control_proof;
        static_assert(!proof::enabled); // Only the isolated physical target opens it.
        proof::Source source;core::OnFootState reference{};reference.x=100;reference.y=200;reference.z=30;
        check(source.begin(reference,start,f.controller) && f.consume()==rb::Result::created,"actual Controller/Binding, one admission, no UDP");
        const auto key=f.binding->key();
        check(f.process(1)==rb::Result::processed && f.api.controls.current.forward==1 && f.api.controls.current.right==0,"walk through original Binding");
        if(which=="proof_timeout"){
            check(f.process(2,start+501ms)==rb::Result::processed && f.api.controls.current==core::PlayerControls{},"missing local update neutralizes using existing timeout");
            check(!source.publish(start-1ms,f.controller) && !source.begin(reference,start,f.controller) &&
                !source.publish(start+1s,f.controller),"clock failure terminal and source never restarts");return;
        }
        check(source.publish(start+2s,f.controller) && f.consume(start+2s)==rb::Result::updated &&
            f.process(2,start+2s)==rb::Result::processed && f.api.controls.current.right==1 && f.api.controls.current.forward==0,"turn on the SAME native binding identity");
        check(source.publish(start+4s,f.controller) && f.consume(start+4s)==rb::Result::updated &&
            f.process(3,start+4s)==rb::Result::processed && f.api.controls.current==core::PlayerControls{},"neutral stop, no pose or velocity command");
        check(source.publish(start+6s,f.controller) && source.complete(start+6s) && f.consume(start+6s)==rb::Result::updated &&
            f.process(4,start+6s)==rb::Result::processed && f.api.controls.current==core::PlayerControls{},"neutral persists, no automatic restart");
        core::RemotePlayerFrames frames{};check(f.controller.tryRead(start+6s,frames) && frames.count==1 &&
            frames.players[0].presentation.x==100 && frames.players[0].presentation.y==200 && frames.players[0].presentation.z==30 &&
            frames.players[0].reference.vx==0 && frames.players[0].reference.vy==0,"XYZ is a fixed spawn/reference, never simulated movement");
        check(f.binding->key()==key && f.api.creates==1 && f.api.originals==4 && f.api.applies==4 && f.api.restores==4 && !f.api.removes,
            "one original per admitted dispatch, no factory/task spam");
        check(f.binding->requestRemoval(f.api)==rb::Result::removed && f.api.removes==1,"existing authenticated removal, no new lifecycle");return;
    }
    if (which=="contract") {
        f.sample(1);f.api.factory=false;
        check(f.consume()==rb::Result::blocked_contract && !f.api.creates,"unproven slot cannot create");
        f.api.factory=true;check(f.consume()==rb::Result::indeterminate && !f.api.creates,"no retry after denial");return;
    }
    if (which=="partial_create" || which=="absent_create") {
        f.api.creation=which=="partial_create" ? rb::Creation::indeterminate : rb::Creation::absent;
        f.sample(1);(void)f.consume();(void)f.consume();
        check(f.api.creates==1 && !f.api.removes,"unknown allocation retained, no retry or speculative delete");return;
    }
    f.admit();
    const auto original=f.api.contextBytes;
    if(which.starts_with("semantic_")){
        f.api.semanticEnabled=true;touched(f.api.nextTail,14552);
        if(which=="semantic_sequence"){
            const auto key=f.binding->key();
            for(unsigned step=1;step<=3;++step){
                if(step>1){f.sample(step,step==2 ? 1.f:0.f,0.f);check(f.consume()==rb::Result::updated,"same logical entity updated");}
                const auto prior=tail::touched(f.api.engineTail),now=14551u+step;
                f.api.clockBefore.engineMs=f.api.clockAfter.engineMs=now;touched(f.api.nextTail,now);
                rb::ProcessObservation observation{};
                check(f.process(step,start,&observation)==rb::Result::processed && f.binding->state()==rb::State::live,"all validations complete before live");
                check(tail::touched(f.api.savedTail)==prior && tail::touched(f.api.engineTail)==now,"native timestamp persists rather than being restored");
                check(f.api.contextBytes==original && f.api.semanticDecision==semantic::Decision::native_touch &&
                    (observation.passed&(1u<<static_cast<unsigned>(rb::ProcessStep::after_original))),"local context and post-original ownership still required");
                check(step==1 ? f.api.controls.current.forward==1.f : step==2 ? f.api.controls.current.right==1.f : f.api.controls.current==core::PlayerControls{},"walk, lateral, neutral on same record");
            }
            check(f.binding->key()==key && f.api.creates==1 && f.api.originals==3 && f.api.restores==3 && !f.api.removes,"three consecutive native-call contracts, no recreation or extra original");return;
        }
        if(which=="semantic_clock")++f.api.clockAfter.engineMs;
        else if(which=="semantic_unknown")f.api.nextTail.front()=std::byte{1};
        else if(which=="semantic_combined"){f.api.nextTail.back()=std::byte{1};touched(f.api.nextTail,123);}
        else if(which=="semantic_restore")f.api.restoreOkay=false;
        else if(which=="semantic_post_identity")f.api.loseOwnershipOnReturn=true;
        else if(which=="semantic_original")f.api.originalOkay=false;
        else throw std::runtime_error("unknown semantic binding case");
        const auto expected=which=="semantic_post_identity" ? rb::Result::ownership_lost:rb::Result::restore_failed;
        check(f.process(1)==expected && f.binding->state()!=rb::State::live,"refused semantic or other guard never returns live");
        check(f.api.contextBytes==original && f.api.originals==1 && f.api.restores==1,"existing restoration still runs exactly once");
        const auto calls=f.api.originals;
        check(f.process(2)==rb::Result::indeterminate && f.api.originals==calls && f.api.restores==1,"terminal forbids retry or second original");
        if(which=="semantic_restore")check(f.api.semanticDecision==semantic::Decision::not_observed,"restore failure cannot be promoted by semantic exception");return;
    }
    if (which.starts_with("trace_")) {
        rb::ProcessObservation trace{};
        using Step=rb::ProcessStep;
        const auto entered=[&](Step step){return (trace.entered&(1u<<static_cast<unsigned>(step)))!=0;};
        const auto passed=[&](Step step){return (trace.passed&(1u<<static_cast<unsigned>(step)))!=0;};
        if(which=="trace_context") {
            f.api.authorized=false;
            check(f.process(1,start,&trace)==rb::Result::context_refused && trace.primary==Step::context &&
                !entered(Step::replay) && !f.api.captures && !f.api.originals,"phase refusal has no downstream calls");return;
        }
        if(which=="trace_capture_seh") {
            f.api.sehCapture=true;
            check(sehCall(&f,&trace) && trace.body==Step::capture && entered(Step::capture) &&
                !passed(Step::capture) && !entered(Step::apply) && !entered(Step::restore) && !f.api.originals,
                "capture entry marked before the call; no fictitious restoration without a saved image");return;
        }
        f.api.captureOkay=which!="trace_capture";
        f.api.applyOkay=which!="trace_apply" && which!="trace_combined";
        f.api.restoreOkay=which!="trace_combined";
        f.api.originalOkay=which!="trace_original";
        f.api.loseOwnershipOnApply=which=="trace_before_original";
        f.api.throwOriginal=which=="trace_exception";
        f.api.sehOriginal=which=="trace_seh";
        if(which=="trace_seh") {
            check(sehCall(&f,&trace) && trace.body==Step::original && entered(Step::original) &&
                !passed(Step::original) && passed(Step::restore) && !trace.cppException,"SEH still propagates, finally is independently observable");
        } else {
            const auto result=f.process(1,start,&trace);
            const Step first=which=="trace_capture" ? Step::capture : which=="trace_apply" || which=="trace_combined" ? Step::apply :
                which=="trace_before_original" ? Step::before_original : which=="trace_original" || which=="trace_exception" ? Step::original : Step::none;
            check(trace.primary==first,"first refused stage retained");
            const auto expected=which=="trace_capture" || which=="trace_apply" ? rb::Result::context_failed :
                which=="trace_combined" ? rb::Result::restore_failed : which=="trace_before_original" || which=="trace_original" || which=="trace_exception" ?
                rb::Result::original_failed : rb::Result::processed;
            check(result==expected,"observation preserves existing Result including restore precedence");
            if(which=="trace_success")check(passed(Step::complete) && f.api.probes==4 && f.api.captures==1 &&
                f.api.originals==1 && f.api.applies==1 && f.api.restores==1,"one original and unchanged authentication/capture counts");
            if(which=="trace_exception")check(trace.cppException && !passed(Step::original),"caught C++ original failure distinguished from normal false return");
        }
        if(which=="trace_capture")check(!entered(Step::apply) && !entered(Step::restore) && !f.api.applies && !f.api.restores,"capture refusal never applies or restores");
        else {
            check(entered(Step::restore) && passed(Step::restore)==f.api.restoreOkay && f.api.restores==1 && f.api.contextBytes==original,"restore observed once including combined failure");
            if(which=="trace_apply" || which=="trace_combined" || which=="trace_before_original")check(!entered(Step::original) && !f.api.originals,"original never invented after an earlier refusal");
        }
        if(which!="trace_success") {
            const auto calls=f.api.originals;
            check(f.process(2)==rb::Result::indeterminate && f.api.originals==calls,"trace never retries a terminal processing failure");
        }
        return;
    }
    if (which=="dispatch_identity") {
        check(f.binding->authenticatesDispatchEntity(101,f.api),"live record reauthenticated for routing");
        auto probes=f.api.probes;
        check(!f.binding->authenticatesDispatchEntity(999,f.api) && f.api.probes==probes,"foreign address never probed as owned");
        f.api.authorized=false;
        check(!f.binding->authenticatesDispatchEntity(101,f.api) && f.api.probes==probes,"phase refused before identity reads");
        f.api.authorized=true;f.api.ownership=false;
        check(!f.binding->authenticatesDispatchEntity(101,f.api),"historical pointer does not authenticate a remote");
        check(!f.api.applies && !f.api.originals && f.api.creates==1,"routing has no controls, dispatch or second construction");return;
    }
    if (which=="dispatch_identity_thread") {
        const auto probes=f.api.probes;bool accepted=true;
        std::thread other([&]{accepted=f.binding->authenticatesDispatchEntity(101,f.api);});other.join();
        check(!accepted && probes==f.api.probes,"no probes from foreign thread");return;
    }
    if (which=="dispatch_identity_retired") {
        f.leave();check(f.consume()==rb::Result::removed,"retire prior identity");
        const auto probes=f.api.probes;
        check(!f.binding->authenticatesDispatchEntity(101,f.api) && probes==f.api.probes,"retired address never authorizes routing");return;
    }
    if (which=="pipeline") {
        check(f.process(1)==rb::Result::processed && f.api.controls.current.forward==1,"walk");
        const auto identity=f.api.authenticated;
        f.sample(2,1,0,start+200ms);check(f.consume(start+200ms)==rb::Result::updated,"direction input");
        check(f.process(2,start+200ms)==rb::Result::processed && f.api.controls.current.right==1 &&
            f.api.controls.previous.forward==1,"turn with previous remote input");
        f.sample(3,0,0,start+400ms);(void)f.consume(start+400ms);
        check(f.process(3,start+400ms)==rb::Result::processed && f.api.controls.current==core::PlayerControls{},"stop");
        check(f.api.creates==1 && f.api.authenticated.key==identity.key && f.api.authenticated.entity==identity.entity &&
            f.api.contextBytes==original && f.api.originals==3 && f.api.restores==3,"same entity; exact previous focus/pad image restored");return;
    }
    if (which=="freshness") {
        check(f.process(1,start+500ms)==rb::Result::processed && f.api.controls.current==core::PlayerControls{},
            "dispatch reads current controller, expired cached pre-world input is neutral");
        check(f.api.originals==1 && f.api.restores==1 && !f.api.removes &&
            f.binding->inputDisposition()==rb::InputDisposition::neutral_timeout,"input timeout is not disconnect");return;
    }
    if (which=="pad_image") {
        check(f.process(1)==rb::Result::processed,"prepared at authenticated original dispatch");
        std::int16_t lr=0,ud=0,walk=0;
        const auto& bytes=f.api.controls.pad.current;
        std::memcpy(&lr,bytes.data(),2);std::memcpy(&ud,bytes.data()+2,2);std::memcpy(&walk,bytes.data()+0x2A,2);
        check(lr==0 && ud==-60 && walk==1,"closed axis/walk proposal, not physical direction proof");
        for (unsigned n=4;n<bytes.size();++n) if (n!=0x2A) check(bytes[n]==std::byte{},"no combat/camera channels");
        rb::OnFootPadStates invalid;
        check(!rb::prepareOnFootPad({}, {0,std::numeric_limits<float>::infinity(),0},invalid),"invalid input never encoded");
        check(invalid.current==rb::OnFootPadStates{}.current,"no partial encoded image");return;
    }
    if (which=="persistent") {
        for (unsigned n=0;n<100;++n) {
            const auto now=start+200ms*n;
            f.sample(n+2,n==0 ? 1.f : 0.f,0,now);
            check(f.consume(now)==rb::Result::updated && f.process(n+1,now)==rb::Result::processed,"same session stays alive");
        }
        check(f.api.creates==1 && f.api.removes==0 && f.api.originals==100,"no per-snapshot spawn/remove");return;
    }
    if (which=="duplicates") {
        check(f.process(1)==rb::Result::processed && f.process(1)==rb::Result::duplicate,"same original dispatch once");
        check(f.api.originals==1 && f.api.creates==1,"no repeated original or entity");return;
    }
    if (which=="reentry") {
        f.api.reenter=true;check(f.process(1)==rb::Result::processed && f.api.originals==1 && f.api.restores==1,"single scoped original");return;
    }
    if (which=="thread") {
        const auto probes=f.api.probes;
        std::thread other([&] { check(f.consume()==rb::Result::context_refused && f.process(1)==rb::Result::context_refused,"wrong thread"); });
        other.join();check(f.api.probes==probes && !f.api.captures,"no probes off owner");
        f.api.authorized=false;check(f.process(1)==rb::Result::context_refused && f.api.probes==probes,"wrong phase denied before reads");return;
    }
    if (which=="ownership") {
        f.api.ownership=false;check(f.process(1)==rb::Result::ownership_lost && !f.api.captures,"ownership proof before context writes");
        f.api.ownership=true;check(f.process(2)==rb::Result::indeterminate && !f.api.originals,"terminal loss");return;
    }
    if (which=="dispatch") {
        auto d=rb::Dispatch{f.api.authenticated.key,999,1};
        check(f.binding->process(f.controller,start,d,f.api)==rb::Result::ownership_lost && !f.api.captures,"CJ/other entity never consumes input");return;
    }
    if (which=="replay_contract") {
        f.api.replay=false;check(f.process(1)==rb::Result::blocked_contract && !f.api.captures,"ABI/context unresolved: no writes");return;
    }
    if (which=="cleanup" || which=="cleanup_unknown") {
        check(f.roster.left({12},start+1ms),"leave");f.controller.endRemote(12,core::RemoteEndReason::server_leave);
        f.controller.synchronize(f.roster,start+1ms);
        check(f.process(1)==rb::Result::processed && f.api.originals==1 && f.api.controls.current==core::PlayerControls{},"legitimate original continues neutral until pre-world removal");
        f.api.postcondition=which=="cleanup";
        check(f.consume()==(f.api.postcondition ? rb::Result::removed : rb::Result::indeterminate),"correlated pool/slot postcondition required");
        check(std::string_view(f.binding->retirementReason())=="server_remote_leave_cause_unspecified","do not guess server Disconnect versus timeout");
        check(f.api.removedBefore.key==f.api.authenticated.key && f.api.removedBefore.entity.handle==17,"old identity retained across reference cleanup");
        f.sample(1,0,1,start+200ms,13);
        check(f.consume(start+200ms)==(f.api.postcondition ? rb::Result::created : rb::Result::indeterminate),"readmission only after proven cleanup");
        check(f.api.creates==(f.api.postcondition ? 2u : 1u),"unknown cleanup never reused");return;
    }
    if (which=="generation") {
        f.controller.beginSession(43);f.roster.clear();
        (void)f.roster.position({12,1,100,200,30},start);
        core::OnFootState state{};state.x=100;state.y=200;state.z=30;
        check(f.controller.receive({43,12,1,state},f.roster,start),"new session");f.controller.synchronize(f.roster,start);
        check(f.process(1)==rb::Result::processed && f.api.originals==1 && f.api.controls.current==core::PlayerControls{},"new generation cannot drive old record; neutral original still runs");
        check(f.consume()==rb::Result::removed && f.consume()==rb::Result::created && f.api.removes==1,"remove before new generation admission");return;
    }
    if (which=="capture") {
        f.api.captureOkay=false;check(f.process(1)==rb::Result::context_failed && !f.api.applies && !f.api.originals,"complete capture before writes");return;
    }
    if (which=="apply" || which=="exception" || which=="restore" || which=="seh") {
        f.api.throwApply=which=="apply";f.api.throwOriginal=which=="exception";
        f.api.restoreOkay=which!="restore";f.api.sehOriginal=which=="seh";
        if (which=="seh") check(sehCall(&f),"SEH propagates after finally; no invented recovery");
        else check(f.process(1)==(which=="apply" ? rb::Result::context_failed :
            which=="exception" ? rb::Result::original_failed : rb::Result::restore_failed),"failure reported");
        check(f.api.restores==1 && f.api.contextBytes==original,"restore actual prior bytes even on partial apply or fault");
        check(f.process(2)==rb::Result::indeterminate && f.api.restores==1,"poisoned session has no retry");return;
    }
    throw std::runtime_error("unknown test");
}
int main(int argc,char** argv) {
    try { if (argc<2) return 2;run(argv[1],argc>2 ? argv[2] : nullptr);return 0; }
    catch (const std::exception& e) { std::fprintf(stderr,"%s\n",e.what());return 1; }
}
