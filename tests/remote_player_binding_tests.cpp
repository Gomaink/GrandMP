#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "grandmp/windows/remote_player_binding.hpp"
#include "grandmp/windows/remote_player_hoodlum.hpp"
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
using namespace std::chrono_literals;
void check(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
using Time=core::RemoteRoster::Time;
const Time start{};

// Contract test double ONLY. Numbers are synthetic identities, never GTA addresses.
struct TestApi final : rb::Api {
    bool authorized=true,factory=true,removal=true,replay=true,ownership=true,dispatchOkay=true;
    bool captureOkay=true,applyOkay=true,restoreOkay=true,postcondition=true;
    bool throwApply=false,throwOriginal=false,sehOriginal=false,reenter=false;
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
    rb::Removal remove(rb::Record& record) override {
        ++removes;record.reference=0;return removalResult;
    }
    bool removed(const rb::Record& before,const rb::Record& after) noexcept override {
        removedBefore=before;
        return postcondition && before.key==authenticated.key && before.entity==authenticated.entity &&
            before.reference==101 && after.reference==0; // Synthetic slot/pool proof below is explicit.
    }
    bool dispatch(const rb::Record&,const rb::Dispatch&) noexcept override { return dispatchOkay; }
    bool capture(rb::ContextImage& image) noexcept override {
        ++captures;if (!captureOkay) return false;
        image.used=contextBytes.size();std::memcpy(image.bytes.data(),contextBytes.data(),image.used);return true;
    }
    bool apply(const rb::Record&,const rb::Controls& input) override {
        ++applies;controls=input;contextBytes.fill(std::byte{0xab});
        if (throwApply) throw std::runtime_error("partial input application");
        return applyOkay;
    }
    bool original(const rb::Record&) override {
        ++originals;
        if (reenter) check(binding->process(*controller,start,activeDispatch,*this)==rb::Result::reentrant,"reentry denied");
        if (throwOriginal) throw std::runtime_error("original throws");
        if (sehOriginal) RaiseException(0xE0421234,0,0,nullptr);
        return true;
    }
    bool restore(const rb::ContextImage& image) noexcept override {
        ++restores;std::memcpy(contextBytes.data(),image.bytes.data(),contextBytes.size());return restoreOkay;
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
    rb::Result process(std::uint64_t serial,Time now=start) {
        api.activeDispatch={api.authenticated.key,api.authenticated.entity.ped,serial};
        return binding->process(controller,now,api.activeDispatch,api);
    }
    void admit() { sample(1);check(consume()==rb::Result::created,"single test entity created"); }
    void leave() { check(roster.left({12},start+1ms),"leave");controller.synchronize(roster,start+1ms); }
};
// Keep SEH handler in an object-free frame; do not hide faults in production code.
bool sehCall(Fixture* fixture) {
    __try { (void)fixture->process(1);return false; }
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
