#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <new>

// Faults are scoped to the calling test thread. Ordinary allocations and the
// production API are unchanged; no actual memory/resource exhaustion is used.
thread_local bool failNextTicket = false, failNextThread = false;
thread_local unsigned ticketFailures = 0, threadCreates = 0;
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    if (failNextTicket) { failNextTicket = false; ++ticketFailures; return nullptr; }
    try { return ::operator new(size); } catch (...) { return nullptr; }
}
void operator delete(void* memory, const std::nothrow_t&) noexcept { ::operator delete(memory); }
HANDLE WINAPI diagnosticCreateThread(LPSECURITY_ATTRIBUTES attributes, SIZE_T stack, LPTHREAD_START_ROUTINE entry,
    LPVOID argument, DWORD flags, LPDWORD id) noexcept {
    ++threadCreates;
    if (failNextThread) { failNextThread = false; SetLastError(ERROR_NOT_ENOUGH_QUOTA); return nullptr; }
    return ::CreateThread(attributes,stack,entry,argument,flags,id);
}
// Intercept only the worker's call in this disposable host, not std::thread.
#define CreateThread diagnosticCreateThread
#include "../client/windows/tools/native_diagnostic_worker.hpp"
#undef CreateThread
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

namespace deferred = grandmp::windows::native_diagnostic_deferred;
namespace bind = deferred::binding;
namespace native = bind::native;
namespace ps = bind::ps;
void check(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
struct Backend {
    DWORD thread=GetCurrentThreadId();bool phase=true,ownership=true;
    unsigned reads=0,assignments=1; // One existing Alpha 0.7 task; no task API in delivery.
    native::Entity entity{{2,42},0x1000,0x101};
    std::array<std::uint32_t,4> slots{0,0,0,0xDEADBEEF};
    bool nativeContext() const { return phase && GetCurrentThreadId()==thread; }
    native::Entity identity() const { return entity; }
    bool matches(const native::Entity& value) const { return value==entity; }
    bool engineReady() const { return true; }
    bool owns(std::uintptr_t) const { return ownership; }
    native::ReadResult readMemory(std::uint32_t address,void* output,std::size_t size) {
        check(nativeContext(),"off-thread read");++reads;check(size==4,"DWORD only");std::uint32_t value=0;
        if (address==0x1000+ps::task_layout::intelligence)value=0x2000;
        else if (address==0x2000 || address==0x2004+ps::task_layout::managerOwner)value=0x1000;
        else if (address>=0x2004 && address<0x2014 && (address-0x2004)%4==0)value=slots[(address-0x2004)/4];
        else throw std::runtime_error("task dereference/unexpected memory");
        std::memcpy(output,&value,4);return {true,4};
    }
};
struct Controller {
    ps::MoveState walk=ps::MoveState::idle;
    native::Session session{2,42};
    auto key()const{return session;}
    auto state()const{return ps::State::ped_created;}
    auto moveState()const{return walk;}
    std::uintptr_t ped()const{return 0x1000;}
};
struct Lines {
    std::vector<std::string> values;
    bool operator()(const char* line) {
        check(!std::strstr(line,"0x") && !std::strstr(line,"DEADBEEF") && !std::strstr(line,"3735928559") && !std::strstr(line,"ped="),"no addresses in logs, hexadecimal or decimal");
        values.emplace_back(line);return true;
    }
};
struct Fixture {
    Backend backend;Controller controller;
    std::shared_ptr<deferred::Session<true>> source=std::make_shared<deferred::Session<true>>();
    unsigned callback=0;
    void begin(){source->beforeAttempt(backend,controller,1,0,0,64);controller.walk=ps::MoveState::assigned;tick();}
    void tick(){source->afterTick(backend,controller,1,callback,callback);++callback;}
    void finish(){controller.walk=ps::MoveState::moving;tick();controller.walk=ps::MoveState::arrived;tick();backend.slots[3]=0;tick();}
};
void run(std::string_view which) {
    static_assert(!ps::physicalExecutionApproved && !ps::movementExecutionApproved);
    if (which=="disabled") {
        struct Empty{} empty;deferred::Session<> disabled;
        disabled.beforeAttempt(empty,empty,1,0,0);disabled.afterTick(empty,empty,1,0,0);
        bind::Record record;check(!disabled.pop(record),"default session has no runtime dependency");return;
    }
    Fixture f;Lines lines;
    if (which=="concurrent") {
        f.source->beforeAttempt(f.backend,f.controller,1,0,GetTickCount64(),64);
        f.controller.walk=ps::MoveState::assigned;
        f.source->afterTick(f.backend,f.controller,1,0,GetTickCount64());
        std::atomic<bool> entered{false};deferred::Outcome outcome=deferred::Outcome::observing;
        std::thread external([&]{
            deferred::Consumer consumer(GetTickCount64(),{1000,2000});entered.store(true,std::memory_order_release);
            do {outcome=consumer.poll(*f.source,GetTickCount64(),lines);if(outcome==deferred::Outcome::observing)Sleep(1);}while(outcome==deferred::Outcome::observing);
        });
        while(!entered.load(std::memory_order_acquire))std::this_thread::yield();
        f.controller.walk=ps::MoveState::moving;f.source->afterTick(f.backend,f.controller,1,1,GetTickCount64());
        f.controller.walk=ps::MoveState::arrived;f.source->afterTick(f.backend,f.controller,1,2,GetTickCount64());
        f.backend.slots[3]=0;f.source->afterTick(f.backend,f.controller,1,3,GetTickCount64());external.join();
        check(outcome==deferred::Outcome::terminal_observed && lines.values.size()==6 && f.backend.assignments==1,"real SPSC producer/consumer, ordered complete drain");return;
    }
    if (which.starts_with("worker")) {
        const auto path=std::filesystem::current_path()/("diagnostic-"+std::string(which)+"-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+".log");
        struct Residence {bool allowed=true;bool operator()()const noexcept{return allowed;}} residence;
        if(which=="worker_allocation_failure" || which=="worker_thread_failure") {
            const bool allocation=which=="worker_allocation_failure";
            f.begin();const auto reads=f.backend.reads;
            deferred::WindowsWorker<true> worker(f.source,f.backend.thread,path.wstring(),{50,100});
            unsigned residenceChecks=0;
            auto admitted=[&]()noexcept{++residenceChecks;return true;};
            std::thread external([&]{
                failNextTicket=allocation;failNextThread=!allocation;
                const DWORD expected=allocation?ERROR_NOT_ENOUGH_MEMORY:ERROR_NOT_ENOUGH_QUOTA;
                check(!worker.start(admitted) && f.source->stopped(),"startup failure stops session BEFORE worker destruction");
                check(worker.error()==expected && !worker.join(0),"startup error retained, no worker handle");
                check(ticketFailures==(allocation?1u:0u) && threadCreates==(allocation?0u:1u),"exact injected failure branch, no unexpected thread");
                failNextTicket=true;failNextThread=true;
                check(!worker.start(admitted) && residenceChecks==1 && failNextTicket && failNextThread,
                    "latched start refuses retry before admission/allocation/CreateThread");
                check(worker.error()==expected,"retry refusal cannot overwrite original error");
                failNextTicket=false;failNextThread=false;
            });external.join();
            f.tick();
            check(f.backend.reads==reads && f.backend.assignments==1,"stopped session cannot resume observation or add task");
            check(!std::filesystem::exists(path),"no sink/thread initialized on startup failure");return;
        }
        if(which=="worker_lifetime") {
            std::weak_ptr<deferred::Session<true>> lifetime=f.source;
            {
                deferred::WindowsWorker<true> worker(f.source,f.backend.thread,path.wstring(),{50,100});
                bool launched=false;
                std::thread external([&]{launched=worker.start(residence);});external.join();
                check(launched,"worker started before owner destruction");
                f.source.reset(); // Worker/ticket retain state; no producer remains in flight.
            } // Requests stop, never waits, never destroys a running worker's state.
            const auto deadline=GetTickCount64()+1000;
            while(!lifetime.expired() && GetTickCount64()<deadline)Sleep(1);
            check(lifetime.expired(),"shared state survives owner then releases after deferred termination");return;
        }
        const bool absent=which=="worker_absence", denied=which=="worker_residence", badSink=which=="worker_sink";
        if (!absent && !denied) {f.begin();f.finish();}
        deferred::WindowsWorker<true> worker(f.source,f.backend.thread,badSink?(path/L"missing"/L"file.log").wstring():path.wstring(),{50,100});
        check(!worker.start(residence),"worker start forbidden on producer thread");
        if (denied) residence.allowed=false;
        bool launched=false,joined=false;
        std::thread external([&] {launched=worker.start(residence);if(launched){check(!worker.join(INFINITE),"external infinite wait refused");joined=worker.join(3000);}});external.join();
        if (denied) check(!launched && f.source->stopped() && worker.error()==ERROR_ACCESS_DENIED,"residence refused, no worker/retry");
        else {
            check(launched && joined,"worker terminates within finite host deadline");
            const auto expected=absent?deferred::Outcome::callbacks_absent:badSink?deferred::Outcome::sink_failed:deferred::Outcome::terminal_observed;
            check(worker.outcome()==expected,"Windows worker result");
            if (!badSink) {
                std::ifstream input(path);std::string text((std::istreambuf_iterator<char>(input)),{});
                check(text.find("diagnostic_history=")!=std::string::npos && text.find("0x")==std::string::npos && text.find("DEADBEEF")==std::string::npos,"actual deferred file contains values and summary");
            } else check(worker.error()!=ERROR_SUCCESS,"Win32 file failure retained");
        }
        check(!worker.start(residence) && !worker.join(INFINITE),"no restart or unbounded wait");return;
    }
    f.begin();
    deferred::Consumer consumer(0,{50,200});
    if (which=="ordered" || which=="slow" || which=="closure") {
        f.finish();const auto reads=f.backend.reads;
        std::thread external([&]{check(consumer.poll(*f.source,10,lines)==deferred::Outcome::terminal_observed,"drain after producer closure");});external.join();
        check(lines.values.size()==6 && consumer.records()==5,"five ordered events plus summary");
        check(lines.values[0].find("walk=before_assignment")!=std::string::npos && lines.values[1].find("walk=assigned")!=std::string::npos &&
            lines.values[2].find("walk=started")!=std::string::npos && lines.values[3].find("walk=arrived")!=std::string::npos &&
            lines.values[4].find("slots=empty")!=std::string::npos,"publication order preserved");
        check(f.backend.reads==reads,"deferred thread performs no native reads");
    } else if (which=="overflow" || which=="overflow_sink") {
        for(unsigned n=0;n<25;++n){f.backend.slots[0]=n%2?0:1;f.tick();}
        check(f.source->closed() && f.source->overflowed(),"full queue closes producer, terminal may be lost");
        if(which=="overflow_sink") {
            auto failed=[](const char*)noexcept{return false;};
            check(consumer.poll(*f.source,30,failed)==deferred::Outcome::overflow && consumer.loggingFailed(),"logging failure cannot erase overflow");
        }else{
            check(consumer.poll(*f.source,30,lines)==deferred::Outcome::overflow && consumer.records()==16,"overflow overrides absent terminal");
            check(lines.values.back().find("inconclusive")!=std::string::npos,"incomplete history never proves release");
        }
    } else if (which=="absence" || which=="cancel" || which=="producer_stop") {
        consumer.poll(*f.source,1,lines);const auto reads=f.backend.reads;
        if (which=="producer_stop") f.source->producerStopped(f.backend);
        else if (which=="cancel") f.source->requestStop();
        const auto expected=which=="absence"?deferred::Outcome::callbacks_absent:which=="cancel"?deferred::Outcome::cancelled:deferred::Outcome::producer_ended_without_terminal;
        check(consumer.poll(*f.source,51,lines)==expected,"external end condition is explicit");
        f.tick();check(f.backend.reads==reads,"late callback after external stop does not resume reads");
        check(lines.values.back().find("inconclusive")!=std::string::npos,"no guessed slots on missing callbacks");
    } else if (which=="context") {
        const auto reads=f.backend.reads;f.backend.phase=false;f.tick();
        f.backend.phase=true;std::thread external([&]{f.tick();});external.join();
        check(f.backend.reads==reads,"no off-phase/thread reads");
        check(consumer.poll(*f.source,51,lines)==deferred::Outcome::callbacks_absent,"denied phase does not fake heartbeat");
    } else if (which=="generation" || which=="identity") {
        ++f.backend.entity.session.generation;f.tick();
        check(consumer.poll(*f.source,2,lines)==deferred::Outcome::terminal_observed,"authenticated failure closes history");
        check(lines.values[2].find("slots=unknown")!=std::string::npos && lines.values[2].find("session_changed")!=std::string::npos,"identity failure never empty");
    } else if (which=="ownership") {
        f.backend.ownership=false;f.tick();
        check(consumer.poll(*f.source,2,lines)==deferred::Outcome::terminal_observed && lines.values[2].find("auth=ownership")!=std::string::npos,"ownership failure copied, not pointer");
    } else if (which=="consumer_conflict") {
        consumer.poll(*f.source,1,lines);const auto records=consumer.records();
        deferred::Consumer second(1,{50,200});Lines other;
        check(second.poll(*f.source,2,other)==deferred::Outcome::consumer_conflict && !second.records(),"second consumer refused before pop");
        check(consumer.poll(*f.source,2,lines)==deferred::Outcome::cancelled && consumer.records()==records,"conflict stops diagnostic without duplicate consumption");
    } else if (which=="heartbeat_ahead") {
        f.source->afterTick(f.backend,f.controller,1,1,2);f.callback=2;
        check(consumer.poll(*f.source,1,lines)==deferred::Outcome::observing,"new heartbeat after sampled now is fresh, not clock failure");
        f.finish();check(consumer.poll(*f.source,10,lines)==deferred::Outcome::terminal_observed,"later closure still drains");
    } else if (which=="deadline" || which=="clock") {
        const auto now=which=="clock"?0u:200u;
        consumer.poll(*f.source,1,lines);
        if(which=="deadline") {f.source->afterTick(f.backend,f.controller,1,1,199);}
        check(consumer.poll(*f.source,now,lines)==(which=="clock"?deferred::Outcome::clock_regressed:deferred::Outcome::lifetime_limit),"finite consumer bound even with heartbeat");
    } else throw std::runtime_error("unknown case");
    check(f.backend.assignments==1,"delivery cannot assign second task");
    const auto count=lines.values.size();consumer.poll(*f.source,999,lines);check(lines.values.size()==count,"no terminal log spam/restart");
}
int main(int argc,char** argv) {
    try {check(argc==2,"case required");run(argv[1]);std::puts("diagnostic_deferred PASS");return 0;}
    catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
