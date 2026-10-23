#include "../client/windows/tools/native_task_release_diagnostics.hpp"
#include "../client/windows/tools/ped_smoke_move.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

namespace diag = grandmp::windows::task_release_diagnostics;
namespace native = grandmp::windows::continuous_remote_native;
namespace ps = grandmp::windows::ped_smoke;
void check(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }

// Only synthetic addresses. Unknown reads fail the test, including any attempt
// to dereference a task. None of these bytes come from a GTA executable/process.
struct FakeEngine {
    const std::thread::id consumer = std::this_thread::get_id();
    native::Entity entity{{2,42},0x1000,0x101};
    std::array<std::uint32_t,4> slots{};
    bool phase = true, admitted = true, owned = true;
    unsigned reads = 0, calls = 0, assignments = 0;
    std::uint32_t partialAddress = 0;
    ps::Position actual{1000,2000,30}, destination{1004,2000,30};
    bool nativeContext() const { return phase && consumer == std::this_thread::get_id(); }
    void touch() { check(nativeContext(),"no engine call outside native phase/thread"); ++calls; }
    bool matches(const native::Entity& candidate) const { return candidate == entity; }
    bool engineReady() { touch(); return admitted; }
    bool owns(std::uintptr_t ped) { touch(); return owned && ped == entity.ped; }
    native::ReadResult readMemory(std::uint32_t address, void* output, std::size_t size) {
        touch(); ++reads; check(size==4,"Win32 DWORD read");
        std::uint32_t value = 0;
        if (address==entity.ped + ps::task_layout::intelligence) value=0x2000;
        else if (address==0x2000 || address==0x2004 + ps::task_layout::managerOwner) value=entity.ped;
        else if (address>=0x2004 && address<0x2014 && (address-0x2004)%4==0) value=slots[(address-0x2004)/4];
        else throw std::runtime_error("task dereference, slot 4 or unexpected native read");
        const auto count=address==partialAddress?size-1:size;
        std::memcpy(output,&value,count); return {true,count};
    }
    bool requestModel() { touch(); return true; }
    bool modelLoaded() { touch(); return true; }
    bool revalidateModel() { touch(); return true; }
    bool spawnPosition(ps::Position& out) { touch(); out=actual; return true; }
    std::uintptr_t create(const ps::Position&) { touch(); return entity.ped; }
    bool visible(std::uintptr_t) { touch(); return true; }
    bool remove(std::uintptr_t) { throw std::runtime_error("diagnostic must not remove entities"); }
    void log(ps::State,const char*) { touch(); }
    void moveLog(const char*,const char*) { touch(); }
    const char* movePosition(std::uintptr_t ped,ps::Position& out) { check(owns(ped),"walk ownership"); out=actual; return nullptr; }
    const char* moveDestination(std::uintptr_t,ps::Position,ps::Position& out) { touch(); out=destination; return nullptr; }
    const char* assignMove(std::uintptr_t,const ps::Position&) {
        touch();check(assignments==0,"one Alpha 0.7 task only");
        check(std::all_of(slots.begin(),slots.end(),[](auto slot){return slot==0;}),"never replace a task");
        ++assignments;slots[3]=0xDEADBEEF;return nullptr;
    }
};
struct RecordingSink {
    std::vector<diag::Event> events;
    std::vector<std::string> lines;
    void operator()(const diag::Event& event) {
        char line[diag::messageCapacity]{};
        check(diag::format(line,sizeof(line),event),"complete diagnostic formatting");
        check(!std::strstr(line,"DEADBEEF") && !std::strstr(line,"0x") && !std::strstr(line,"ped="),"no addresses in diagnostics");
        events.push_back(event);lines.emplace_back(line);
    }
};
struct Fixture {
    FakeEngine engine;
    diag::Observer observer{engine.entity.session,1,8};
    RecordingSink sink;
    void sample(unsigned callback,diag::Walk walk=diag::Walk::assigned) {
        const auto slots=engine.slots;const auto assignments=engine.assignments;
        observer.sample(engine,engine.entity,1,walk,callback,sink);
        check(slots==engine.slots && assignments==engine.assignments,"observer cannot mutate slots or assign tasks");
    }
    void terminal(diag::Reason reason) {
        check(observer.terminal() && observer.reason()==reason,"expected terminal diagnostic");
        const auto reads=engine.reads,calls=engine.calls;
        const auto events=sink.events.size();
        for (unsigned n=20;n<100;++n) sample(n,diag::Walk::arrived);
        check(engine.reads==reads && engine.calls==calls && sink.events.size()==events,"no polling/events after terminal");
        check(!engine.assignments,"observation never assigns a task");
    }
};
void run(std::string_view which) {
    static_assert(!ps::physicalExecutionApproved && !ps::movementExecutionApproved);
    static_assert(std::is_trivially_copyable_v<diag::Event> && sizeof(diag::Observer)<192,"fixed value-only state");
    Fixture f;
    if (which=="before_task") {
        f.engine.slots[0]=0xDEADBEEF;f.sample(100,diag::Walk::before_assignment);
        check(f.sink.events.size()==1 && f.sink.events.back().slots==diag::Slots::busy && f.sink.events.back().occupiedMask==1,
            "pre-task occupied slot, even with slot 3 null");
    } else if (which=="during_walk" || which=="arrived_busy") {
        f.engine.slots[3]=0xDEADBEEF;f.sample(10);f.sample(11,diag::Walk::started);
        f.sample(12,diag::Walk::started);check(f.sink.events.size()==2,"unchanged running state is silent");
        f.engine.slots[0]=0xDEADBEEF;f.sample(13,diag::Walk::started);
        check(f.sink.events.size()==3 && f.sink.events.back().occupiedMask==9,"mask transition is logged even when state stays busy");
        if (which=="arrived_busy") {
            f.sample(14,diag::Walk::arrived);
            check(!f.observer.terminal() && f.sink.events.back().slots==diag::Slots::busy && f.sink.events.back().afterArrival,
                "arrival is not structural release");
        }
    } else if (which=="release_after" || which=="release_before") {
        f.engine.slots[3]=0xDEADBEEF;f.sample(100);f.sample(101,diag::Walk::started);
        if (which=="release_after") {
            f.sample(102,diag::Walk::arrived);check(!f.observer.terminal(),"busy after arrival keeps observing");
            f.engine.slots[3]=0;f.sample(103,diag::Walk::arrived);
        } else {
            f.engine.slots[3]=0;f.sample(102,diag::Walk::started);
            check(!f.observer.terminal() && !f.sink.events.back().afterArrival,"release alone never becomes arrival");
            f.sample(103,diag::Walk::arrived);
        }
        check(f.sink.events.back().slots==diag::Slots::empty && f.sink.events.back().occupiedMask==0,"authenticated empty snapshot");
        for (const auto& line:f.sink.lines) std::puts(line.c_str());
        f.terminal(diag::Reason::empty_after_arrival);
    } else if (which=="timeout") {
        f.engine.slots[3]=0xDEADBEEF;
        for(unsigned n=0;n<8;++n)f.sample(n,n==0?diag::Walk::assigned:n==1?diag::Walk::started:diag::Walk::arrived);
        check(f.engine.reads==80,"ten authenticated reads per sample within budget");
        f.sample(1000,diag::Walk::arrived);f.terminal(diag::Reason::callback_limit);
        check(f.engine.reads==80,"callback limit performs no extra native query");
        check(f.sink.events.back().elapsedCallbacks==8 && !f.sink.events.back().sampled &&
            f.sink.lines.back().find("mask=unknown")!=std::string::npos,"limit clamps count and never reuses a stale mask");
        check(f.engine.slots[3]==0xDEADBEEF,"timeout never frees task");
    } else if (which=="partial") {
        f.engine.slots[0]=0xDEADBEEF;f.sample(0);f.engine.partialAddress=0x2010;f.sample(1);
        check(f.sink.events.back().authentication==native::Reason::incomplete_read &&
            f.sink.events.back().slots==diag::Slots::unknown && f.sink.lines.back().find("mask=unknown")!=std::string::npos,
            "partial read is unknown, not busy/empty inferred from previous slots");
        f.terminal(diag::Reason::authentication_failed);
    } else if (which=="ownership") {
        f.engine.slots[3]=0xDEADBEEF;f.sample(1);const auto reads=f.engine.reads;
        f.engine.owned=false;f.sample(2);
        check(f.engine.reads==reads && f.sink.events.back().authentication==native::Reason::ownership,"no entity reads after ownership loss");
        f.terminal(diag::Reason::authentication_failed);
    } else if (which=="generation" || which=="attempt") {
        f.sample(1);const auto calls=f.engine.calls;
        if (which=="generation")++f.engine.entity.session.generation;
        f.observer.sample(f.engine,f.engine.entity,which=="attempt"?2:1,diag::Walk::assigned,2,f.sink);
        check(f.engine.calls==calls && !f.sink.events.back().sampled,"stale session/attempt terminates before native authentication");
        check(f.sink.events.back().elapsedCallbacks==1,"refusal reports current elapsed callbacks, not stale count");
        f.terminal(which=="generation"?diag::Reason::session_changed:diag::Reason::attempt_changed);
        if (which=="generation") {
            Fixture other;other.sample(1);++other.engine.entity.session.player;
            const auto reads=other.engine.reads;other.sample(2);other.terminal(diag::Reason::session_changed);
            check(other.engine.reads==reads,"different player in same generation is also refused before reads");
        }
    } else if (which=="authentication") {
        f.engine.admitted=false;f.sample(1);
        check(!f.engine.reads && f.sink.events.back().authentication==native::Reason::engine,"failed engine authentication");
        f.terminal(diag::Reason::authentication_failed);
    } else if (which=="thread") {
        f.engine.phase=false;f.sample(1);f.engine.phase=true;
        std::thread worker([&] {f.sample(2);});worker.join();
        check(!f.engine.calls && !f.engine.reads && f.sink.events.empty(),"no engine calls, publication or state changes outside context");
        f.sample(50);check(f.sink.events.size()==1 && f.sink.events.back().elapsedCallbacks==0,"only native consumer arms callback count");
    } else if (which=="coalescing") {
        f.engine.slots[3]=0xDEADBEEF;
        for(unsigned n=0;n<8;++n)f.sample(100+n);
        check(f.sink.events.size()==1,"unchanged callbacks do not log");
        f.sample(108);check(f.sink.events.size()==2,"only initial and terminal budget events");
        f.terminal(diag::Reason::callback_limit);
        FakeEngine engine;diag::Observer nested(engine.entity.session,1,8);RecordingSink sink;
        auto reenter=[&](const diag::Event& event) {
            sink(event);nested.sample(engine,engine.entity,1,diag::Walk::assigned,0,sink);
        };
        nested.sample(engine,engine.entity,1,diag::Walk::assigned,0,reenter);
        check(engine.reads==10 && sink.events.size()==1 && !nested.terminal(),"publication reentry cannot query or emit again");
    } else if (which=="monotonic") {
        f.sample(10);const auto reads=f.engine.reads;f.sample(10);
        check(f.engine.reads==reads,"duplicate callback does not query again");f.terminal(diag::Reason::callback_regressed);
        Fixture regressed;regressed.sample(20,diag::Walk::started);regressed.sample(21,diag::Walk::assigned);
        check(regressed.sink.events.back().elapsedCallbacks==1,"walk refusal also reports the current callback");
        regressed.terminal(diag::Reason::walk_regressed);
        Fixture overflow;overflow.observer.sample(overflow.engine,overflow.engine.entity,1,diag::Walk::assigned,
            std::numeric_limits<std::uint64_t>::max(),overflow.sink);
        overflow.sample(0);overflow.terminal(diag::Reason::callback_regressed);
    } else if (which=="configuration") {
        for (const auto limit:{0u,diag::Observer::maximumLimit+1}) {
            diag::Observer observer(f.engine.entity.session,1,limit);
            observer.sample(f.engine,f.engine.entity,1,diag::Walk::assigned,0,f.sink);
            check(observer.terminal() && observer.reason()==diag::Reason::invalid_configuration,"invalid limit is terminal");
        }
        check(!f.engine.calls && f.sink.events.empty(),"invalid configuration never polls");
    } else if (which=="format") {
        diag::Event event{};event.generation=event.attempt=std::numeric_limits<std::uint64_t>::max();
        event.elapsedCallbacks=event.limit=diag::Observer::maximumLimit;
        event.authentication=native::Reason::removal_not_completed;event.reason=diag::Reason::authentication_failed;
        event.walk=diag::Walk::before_assignment;event.sampled=true;event.terminal=true;event.occupiedMask=15;
        for (const auto slots:{diag::Slots::unknown,diag::Slots::busy,diag::Slots::empty}) {
            event.slots=slots;char line[diag::messageCapacity]{};
            check(diag::format(line,sizeof(line),event) && std::strstr(line,"terminal=1"),"maximum complete event formatting");
            char small[16]{};check(!diag::format(small,sizeof(small),event) && !small[0],"truncation explicit, partial line discarded");
            check(!diag::format(nullptr,0,event),"null buffer rejected");
            char finalLine[512]{};
            const int count=std::snprintf(finalLine,sizeof(finalLine),"[GrandMPPedSmoke] state=remote_task_release_diagnostic reason=%s pid=4294967295 tid=4294967295 callback=4294967295",line);
            check(count>0 && static_cast<std::size_t>(count)<sizeof(finalLine),"complete event fits existing final logger capacity");
        }
    } else if (which=="single_walk") {
        ps::Experiment entity;ps::Movement walk;std::uint64_t now=0,callback=0;
        diag::Observer observer(f.engine.entity.session,1,64);
        for(int n=0;n<3;++n)entity.tick(f.engine,true,true,false,now++);
        check(entity.state()==ps::State::ped_created,"one fake PED");
        observer.sample(f.engine,f.engine.entity,1,diag::Walk::before_assignment,callback++,f.sink);
        walk.tick(f.engine,entity,true,true,true,false,now);
        check(walk.state()==ps::MoveState::assigned && f.engine.assignments==1,"one original Alpha 0.7 task");
        observer.sample(f.engine,f.engine.entity,1,diag::Walk::assigned,callback++,f.sink);
        for(int n=0;n<8;++n) {
            f.engine.actual.x+=0.5f;now+=100;walk.tick(f.engine,entity,true,true,false,false,now);
            observer.sample(f.engine,f.engine.entity,1,diag::Walk::started,callback++,f.sink);
        }
        now+=300;walk.tick(f.engine,entity,true,true,false,false,now);
        check(walk.state()==ps::MoveState::arrived,"observed displacement and real settle policy");
        observer.sample(f.engine,f.engine.entity,1,diag::Walk::arrived,callback++,f.sink);
        check(!observer.terminal() && f.engine.slots[3]==0xDEADBEEF,"observed arrival preserves busy root");
        f.engine.slots[3]=0; // Explicit ENGINE FAKE action, never observer action.
        observer.sample(f.engine,f.engine.entity,1,diag::Walk::arrived,callback++,f.sink);
        check(observer.terminal() && observer.reason()==diag::Reason::empty_after_arrival,"release observed after actual fake arrival");
        for(int n=0;n<10;++n)walk.tick(f.engine,entity,true,true,true,false,++now);
        check(f.engine.assignments==1,"diagnostic never rearms original Alpha 0.7 one-shot");
        for(const auto& line:f.sink.lines)std::puts(line.c_str());
    } else throw std::runtime_error("unknown diagnostic case");
}
int main(int argc,char** argv) {
    try {check(argc==2,"case required");run(argv[1]);std::puts("task_release_diag PASS");return 0;}
    catch(const std::exception& error) {std::fprintf(stderr,"task_release_diag FAIL: %s\n",error.what());return 1;}
}
