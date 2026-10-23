#include "../client/windows/tools/native_diagnostic_binding.hpp"
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace bind = grandmp::windows::native_diagnostic_binding;
namespace diag = bind::diag;
namespace ps = bind::ps;
namespace native = bind::native;
thread_local bool trackAllocations = false;
std::atomic<unsigned> allocations{0};
void* operator new(std::size_t size) {
    if (trackAllocations) ++allocations;
    if (auto* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void check(bool value,const char* why) { if(!value)throw std::runtime_error(why); }

struct Engine {
    struct Key { std::uint64_t generation = 2, player = 42; } key;
    void* owned = nullptr;
    int reference = -1;
    const std::thread::id owner = std::this_thread::get_id();
    bool phase = true, ready = true, ownership = true;
    unsigned calls = 0, reads = 0, assignments = 0;
    std::array<std::uint32_t,4> slots{};
    ps::Position actual{1000,2000,30};
    void touch() { check(phase && owner==std::this_thread::get_id(),"unauthorized engine call");++calls; }
    bool owns(std::uintptr_t ped) { touch();return ownership && owned && reference==0x101 && ped==reinterpret_cast<std::uintptr_t>(owned); }
    bool requestModel(){touch();return true;}
    bool modelLoaded(){touch();return true;}
    bool revalidateModel(){touch();return true;}
    bool spawnPosition(ps::Position& p){touch();p=actual;return true;}
    std::uintptr_t create(const ps::Position&){touch();owned=reinterpret_cast<void*>(std::uintptr_t{0x1000});reference=0x101;return 0x1000;}
    bool visible(std::uintptr_t){touch();return true;}
    bool remove(std::uintptr_t){throw std::runtime_error("binding cannot remove");}
    void log(ps::State,const char*){touch();}
    void moveLog(const char*,const char*){touch();} // No I/O/allocations in the fake baseline.
    const char* movePosition(std::uintptr_t,ps::Position& p){touch();p=actual;return nullptr;}
    const char* moveDestination(std::uintptr_t,ps::Position p,ps::Position& out){touch();out={p.x+4,p.y,p.z};return nullptr;}
    const char* assignMove(std::uintptr_t,const ps::Position&){
        touch();check(assignments==0,"one task only");++assignments;slots[3]=0xDEADBEEF;return nullptr;
    }
    native::ReadResult read(std::uint32_t address,void* output,std::size_t size) {
        touch();++reads;check(size==4,"Win32 exact DWORD");std::uint32_t value=0;
        if(address==0x1000+ps::task_layout::intelligence)value=0x2000;
        else if(address==0x2000 || address==0x2004+ps::task_layout::managerOwner)value=0x1000;
        else if(address>=0x2004 && address<0x2014 && (address-0x2004)%4==0)value=slots[(address-0x2004)/4];
        else throw std::runtime_error("unexpected read/task dereference");
        std::memcpy(output,&value,size);return {true,size};
    }
};
// Public surface matches rm::Controller. States come from ORIGINAL objects.
struct Controller {
    ps::Experiment entity;
    ps::Movement movement;
    native::Session session{2,42};
    native::Session key()const{return session;}
    ps::State state()const{return entity.state();}
    std::uintptr_t ped()const{return entity.ped();}
    ps::MoveState moveState()const{return movement.state();}
};
template<bool Enabled=true>
struct Fixture {
    Engine engine;
    Controller controller;
    bind::Binding<Enabled> binding;
    std::uint64_t now=0,callback=0;
    Fixture(){for(int n=0;n<3;++n)controller.entity.tick(engine,true,true,false,now++);}
    auto view(){
        // Providers are members of this stack frame's caller, not stored by Binding.
        return bind::RuntimeView(engine,context,ready,reader);
    }
    struct Context {Engine& e;bool operator()()const{return e.phase && e.owner==std::this_thread::get_id();}} context{engine};
    struct Ready {Engine& e;bool operator()(){e.touch();return e.ready;}} ready{engine};
    struct Reader {Engine& e;native::ReadResult operator()(std::uint32_t a,void* p,std::size_t n){return e.read(a,p,n);}} reader{engine};
    void frame(bool request=false){
        auto access=view();
        if(request)binding.beforeAttempt(access,controller,1,callback,32);
        controller.movement.tick(engine,controller.entity,true,true,request,false,now);
        binding.afterTick(access,controller,1,callback++);
    }
    std::vector<bind::Record> drain(){
        check(!trackAllocations,"format/collection outside producer");
        std::vector<bind::Record> events;bind::Record record;
        while(binding.pop(record)){
            char line[bind::messageCapacity]{};
            check(bind::format(line,sizeof(line),record),"complete deferred format");
            check(!std::strstr(line,"0x") && !std::strstr(line,"DEADBEEF") && !std::strstr(line,"ped="),"value-only event");
            events.push_back(record);
        }
        return events;
    }
};
void run(std::string_view which){
    static_assert(!ps::physicalExecutionApproved && !ps::movementExecutionApproved);
    static_assert(sizeof(bind::Binding<true>)<2048,"bounded per-attempt storage");
    if(which=="mapping"){
        struct NoRuntime {} noRuntime;bind::Binding<> disabled;
        disabled.beforeAttempt(noRuntime,noRuntime,1,0);disabled.afterTick(noRuntime,noRuntime,1,0);
        check(!disabled.used(),"default disabled instantiation needs no runtime API at all");
        check(bind::walkState(ps::MoveState::idle)==diag::Walk::before_assignment,"idle");
        check(bind::walkState(ps::MoveState::assigned)==diag::Walk::assigned,"assigned");
        check(bind::walkState(ps::MoveState::moving)==diag::Walk::started,"started");
        check(bind::walkState(ps::MoveState::arrived)==diag::Walk::arrived,"arrived");
        check(!bind::walkState(ps::MoveState::failed),"failure is never arrival");return;
    }
    if(which=="disabled"){
        Fixture<false> disabled;Fixture<> enabled;
        disabled.frame(true);enabled.frame(true);
        check(!disabled.binding.used() && disabled.drain().empty() && !disabled.engine.reads,"disabled makes no diagnostic calls");
        check(disabled.controller.moveState()==enabled.controller.moveState() && disabled.engine.assignments==1,"original outcome unchanged");
        for(int i=0;i<8;++i){disabled.engine.actual.x+=0.5f;disabled.now+=100;disabled.frame();}
        disabled.now+=300;disabled.frame();
        check(disabled.controller.moveState()==ps::MoveState::arrived && disabled.engine.assignments==1,"disabled original walk arrives");return;
    }
    Fixture<> f;
    if(which=="initial" || which=="instance"){
        auto access=f.view();f.binding.beforeAttempt(access,f.controller,1,0,32);
        check(f.engine.reads==0 && f.engine.assignments==0,"instance and notice before attribution, no GTA probe");
        f.binding.beforeAttempt(access,f.controller,2,99,32);
        const auto initial=f.drain();check(initial.size()==1 && initial[0].event.attempt==1 && !initial[0].event.sampled,"single instance, no rearm");
        f.frame(true);check(f.engine.assignments==1 && f.drain().size()==1,"first probe after original assignment in same callback");return;
    }
    if(which=="context"){
        auto access=f.view();const auto calls=f.engine.calls;
        f.engine.phase=false;f.binding.beforeAttempt(access,f.controller,1,0);
        f.binding.afterTick(access,f.controller,1,0);std::uint32_t dummy=0;
        check(!access.readMemory(0x1000,&dummy,4).success && !access.engineReady() && !access.owns(0x1000),"view refuses off-phase primitives");
        f.engine.phase=true;
        std::thread worker([&]{f.binding.beforeAttempt(access,f.controller,1,0);f.binding.afterTick(access,f.controller,1,0);});worker.join();
        check(f.engine.calls==calls && !f.binding.used(),"no reads or binding mutation off-thread");return;
    }
    if(which=="allocation"){
        allocations=0;trackAllocations=true;f.frame(true);f.frame();trackAllocations=false;
        check(!allocations && f.engine.assignments==1,"producer has no allocations, no format/file sink");return;
    }
    f.frame(true);
    if(which=="generation" || which=="attempt"){
        const auto reads=f.engine.reads;auto access=f.view();
        if(which=="generation")++f.engine.key.generation;
        f.binding.afterTick(access,f.controller,which=="attempt"?2:1,1);
        auto events=f.drain();check(f.binding.terminal() && f.engine.reads==reads,"changed identity rejected before native reads");
        check(events.back().event.reason==(which=="generation"?diag::Reason::session_changed:diag::Reason::attempt_changed),"specific change reason");
        if(which=="generation"){
            Fixture<> controllerOnly;controllerOnly.frame(true);++controllerOnly.controller.session.generation;
            const auto prior=controllerOnly.engine.reads;auto access2=controllerOnly.view();
            controllerOnly.binding.afterTick(access2,controllerOnly.controller,1,1);
            check(controllerOnly.binding.terminal() && controllerOnly.engine.reads==prior,"controller/registry generation disagreement refuses");
            Fixture<> registry;registry.frame(true);++registry.engine.reference;
            const auto priorReference=registry.engine.reads;auto access3=registry.view();
            registry.binding.afterTick(access3,registry.controller,1,1);
            check(registry.binding.terminal() && registry.engine.reads==priorReference &&
                registry.drain().back().event.authentication==native::Reason::identity,"changed registry handle refuses before reads");
        }
    }else if(which=="ownership"){
        f.engine.ownership=false;const auto reads=f.engine.reads;auto access=f.view();
        f.controller.entity.tick(f.engine,true,true,false,f.now);
        f.binding.afterTick(access,f.controller,1,1);
        check(f.binding.terminal() && f.engine.reads==reads && f.drain().back().event.authentication==native::Reason::ownership,"ownership terminal");
    }else if(which=="authentication"){
        f.engine.ready=false;const auto reads=f.engine.reads;auto access=f.view();f.binding.afterTick(access,f.controller,1,1);
        check(f.binding.terminal() && f.engine.reads==reads && f.drain().back().event.authentication==native::Reason::engine,"runtime ready proof forwarded");
    }else if(which=="timeout"){
        const auto reads=f.engine.reads;auto access=f.view();f.binding.afterTick(access,f.controller,1,32);
        check(f.binding.terminal() && f.engine.reads==reads && f.drain().back().event.reason==diag::Reason::callback_limit,"bounded observation, no task cancellation");
        Fixture<> delayed;auto delayedAccess=delayed.view();
        delayed.binding.beforeAttempt(delayedAccess,delayed.controller,1,0,8);
        delayed.controller.movement.tick(delayed.engine,delayed.controller.entity,true,true,true,false,0);
        delayed.binding.afterTick(delayedAccess,delayed.controller,1,8);
        check(delayed.binding.terminal() && !delayed.engine.reads,"budget starts before task, even when first post-tick observation is delayed");
        Fixture<> offset;auto offsetAccess=offset.view();offset.binding.beforeAttempt(offsetAccess,offset.controller,1,0,8);
        offset.controller.movement.tick(offset.engine,offset.controller.entity,true,true,true,false,0);
        offset.binding.afterTick(offsetAccess,offset.controller,1,4);
        check(offset.drain().back().event.elapsedCallbacks==4,"elapsed is relative to attempt start, not first slot sample");
    }else if(which=="failure"){
        f.now+=20001;f.frame();const auto record=f.drain().back();
        check(f.binding.terminal() && record.bindingFailure==bind::Failure::movement_failed && record.event.authentication==native::Reason::none,
            "movement timeout is not falsely labelled ownership loss");
    }else if(which=="copy"){
        f.engine.owned=nullptr;const auto reads=f.engine.reads;std::vector<bind::Record> copy;
        std::thread consumer([&]{copy=f.drain();});consumer.join();
        check(copy.size()==2 && copy.back().event.occupiedMask==8 && f.engine.reads==reads,"deferred consumer uses Event copies, never stale native pointers");
    }else if(which=="overflow"){
        for(unsigned n=1;n<25;++n){f.engine.slots[0]=n%2?0xDEADBEEF:0;f.frame();}
        check(f.binding.terminal() && f.binding.overflowed() && f.binding.failure()==bind::Failure::outbox_full,"overflow is explicit terminal, never blocking");
        const auto reads=f.engine.reads;auto access=f.view();f.binding.afterTick(access,f.controller,1,100);
        check(f.engine.reads==reads && f.drain().size()==bind::EventOutbox::capacity,"no polling after overflow; bounded backlog");
    }else if(which=="walk" || which=="coalescing"){
        if(which=="coalescing"){
            for(int n=0;n<10;++n){f.now+=10;f.frame();}
            check(f.drain().size()==2,"metadata plus assigned snapshot only, not one log per callback");
        }else{
            for(int n=0;n<8;++n){f.engine.actual.x+=0.5f;f.now+=100;f.frame();}
            f.now+=300;f.frame();check(f.controller.moveState()==ps::MoveState::arrived && !f.binding.terminal(),"arrived/busy keeps observation");
            f.engine.slots[3]=0;f.frame();check(f.binding.terminal(),"arrived/empty ends diagnostic only");
            auto events=f.drain();check(events.size()==5 && events.front().event.walk==diag::Walk::before_assignment &&
                events[1].event.walk==diag::Walk::assigned && events[2].event.walk==diag::Walk::started &&
                events[3].event.walk==diag::Walk::arrived && events[3].event.slots==diag::Slots::busy &&
                events[4].event.slots==diag::Slots::empty,"real Movement states, authenticated mask, deferred copies");
            for(const auto& record:events){char line[bind::messageCapacity]{};bind::format(line,sizeof(line),record);std::puts(line);}
        }
    }else throw std::runtime_error("unknown case");
    check(f.engine.assignments==1,"binding never attributes second task");
}
int main(int argc,char** argv){
    try{check(argc==2,"case required");run(argv[1]);std::puts("native_binding PASS");return 0;}
    catch(const std::exception& e){trackAllocations=false;std::fprintf(stderr,"native_binding FAIL: %s\n",e.what());return 1;}
}
