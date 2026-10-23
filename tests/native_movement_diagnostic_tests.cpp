// Real Windows runtime/providers compiled in a non-GTA host, with all physical gates absent.
#if defined(GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD) && GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD == 1
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
thread_local bool failPassiveThread=false;
HANDLE WINAPI passiveCreateThread(LPSECURITY_ATTRIBUTES a,SIZE_T s,LPTHREAD_START_ROUTINE f,LPVOID v,DWORD flags,LPDWORD id) {
    if (failPassiveThread) { failPassiveThread=false; SetLastError(ERROR_NOT_ENOUGH_QUOTA); return nullptr; }
    return ::CreateThread(a,s,f,v,flags,id);
}
#define CreateThread passiveCreateThread
#endif
#define GRANDMP_REMOTE_PED_VARIANT 1
#define GRANDMP_REMOTE_PED_MOVE_VARIANT 1
#include "../client/windows/tools/ped_smoke.cpp"
#if defined(GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD) && GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD == 1
#undef CreateThread
#endif
#include "../client/windows/tools/native_diagnostic_windows.hpp"
#if !defined(GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD) || GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD != 1
#include "../client/windows/tools/native_diagnostic_windows_runtime.inl"
#endif
#include "../client/windows/tools/native_movement_diagnostic.hpp"
#include <cstdlib>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace snap = grandmp::windows::native_movement_diagnostic;
namespace bind = grandmp::windows::native_diagnostic_binding;
namespace win = grandmp::windows::native_diagnostic_windows;
namespace native = grandmp::windows::continuous_remote_native;
thread_local bool trackSnapshotAllocations=false;
std::atomic<unsigned> snapshotAllocations{0};
thread_local bool failPassiveTicket=false;
void* operator new(std::size_t n) {
    if (trackSnapshotAllocations) ++snapshotAllocations;
    if (auto* p=std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
void* operator new(std::size_t size,const std::nothrow_t&) noexcept {
    if (failPassiveTicket) { failPassiveTicket=false; return nullptr; }
    try { return ::operator new(size); } catch (...) { return nullptr; }
}
void operator delete(void* p,const std::nothrow_t&) noexcept { ::operator delete(p); }
void check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }

struct Engine {
    rp::Key key{2,42};
    void* owned=reinterpret_cast<void*>(0x1000);
    int reference=0x101;
    DWORD owner=GetCurrentThreadId();
    bool phase=true,ownership=true,code=true,gameplay=true,matrixChanged=false,identityChanged=false;
    unsigned reads=0,matrixReads=0,auths=0,tasks=0;
    std::array<std::uint32_t,4> slots{0,0,0,0xDEADBEEF};
    bool holdOnShift=false;
    std::atomic<bool> readEntered{false},releaseRead{false};
    std::uint32_t partial=0;
    std::array<std::byte,0x600> ped{};
    ps::Position actual{100,200,30};
    template<class T> void put(unsigned offset,T value) { std::memcpy(ped.data()+offset,&value,sizeof(value)); }
    Engine() {
        put(0x14,std::uint32_t{0x3000}); put(0x46C,std::uint32_t{1}); put(0x474,std::uint32_t{0x100000});
        put(0x530,std::int32_t{1}); put(0x534,std::int32_t{4}); put(0x568,std::uint32_t{0xDEADBEEF});
        put(0x47C,std::uint32_t{0x2000});
        put(0x44,snap::Pair{0.1f,0.2f}); put(0x4D8,snap::Pair{0.01f,0.02f});
        put(0x550,snap::Pair{0.03f,0.04f}); put(0x598,std::int32_t{4}); put(0x540,100.f);
    }
    void touch() { check(phase && GetCurrentThreadId()==owner,"engine read outside admitted phase/thread"); }
    bool owns(std::uintptr_t address) { touch(); ++auths; return ownership && address==reinterpret_cast<std::uintptr_t>(owned); }
    // Only the baseline Movement test can reach these; sampler has no task interface.
    const char* movePosition(std::uintptr_t,ps::Position& p) { touch(); p=actual; return nullptr; }
    const char* moveDestination(std::uintptr_t,ps::Position p,ps::Position& d) { touch(); d={p.x+4,p.y,p.z}; return nullptr; }
    const char* assignMove(std::uintptr_t,const ps::Position&) { touch(); check(!tasks,"one baseline task"); ++tasks; return nullptr; }
    void moveLog(const char*,const char*) {}
    void log(ps::State,const char*) {}
    bool requestModel() { return true; }
    bool modelLoaded() { return true; }
    bool revalidateModel() { return true; }
    bool spawnPosition(ps::Position& p) { p=actual; return true; }
    std::uintptr_t create(const ps::Position&) { return 0x1000; }
    bool visible(std::uintptr_t) { return true; }
    bool remove(std::uintptr_t) { throw std::runtime_error("diagnostic cannot remove"); }
};
struct Context {
    Engine& e;
    bool operator()() const { return e.phase && GetCurrentThreadId()==e.owner; }
};
struct Ready { Engine& e; bool operator()() const { return e.code && e.gameplay; } };
struct Memory {
    Engine& e;
    native::ReadResult operator()(std::uint32_t address,void* output,std::size_t size) {
        e.touch(); ++e.reads;
        if (e.holdOnShift && address==0x14D8) {
            e.readEntered.store(true,std::memory_order_release);
            while (!e.releaseRead.load(std::memory_order_acquire)) std::this_thread::yield(); // Fake reader ONLY.
        }
        if ((address==0x2000 || address==0x2030) && size==4) { std::uint32_t owner=0x1000; std::memcpy(output,&owner,4); return {true,4}; }
        if (address>=0x2004 && address<=0x2010 && size==4 && (address-0x2004)%4==0) {
            std::memcpy(output,&e.slots[(address-0x2004)/4],4); return {true,4};
        }
        if (address==0x3030 && size==sizeof(ps::Position)) std::memcpy(output,&e.actual,size);
        else {
            // Pointer targets outside the admitted matrix position are NEVER followed.
            check(address>=0x1000 && address+size<=0x1600,"unknown entity/task/support dereference");
            if (address==0x1014) {
                ++e.matrixReads;
                if (e.matrixChanged && e.matrixReads%2==0) { std::uint32_t v=0x4000; std::memcpy(output,&v,size); return {true,size}; }
            }
            std::memcpy(output,e.ped.data()+address-0x1000,size);
        }
        if (e.identityChanged && address==0x14D8) ++e.key.generation;
        return {true,address==e.partial ? size-1 : size};
    }
};
struct Controller {
    rp::Key k{2,42}; ps::MoveState walk=ps::MoveState::idle; ps::State life=ps::State::ped_created;
    rp::Key key() const { return k; }
    std::uintptr_t ped() const { return 0x1000; }
    ps::State state() const { return life; }
    ps::MoveState moveState() const { return walk; }
};
struct Sink {
    std::vector<std::string> lines;
    bool fail=false;
    bool operator()(const char* line) {
        check(!std::strstr(line,"0x") && !std::strstr(line,"DEADBEEF") && !std::strstr(line,"3735928559") && !std::strstr(line,"matrix="),"no native addresses in deferred log");
        if (fail) return false;
        lines.emplace_back(line); return true;
    }
};
struct Fixture {
    Engine engine; Context context{engine}; Ready ready{engine}; Memory memory{engine};
    bind::RuntimeView<Engine,Context,Ready,Memory> view{engine,context,ready,memory};
    Controller controller; snap::Batch batch; snap::Capture<true> capture{batch}; Sink sink;
    void begin() { capture.beforeAttempt(view,controller,148,0); }
    void tick(ps::MoveState state,std::uint64_t cb,std::uint64_t now) { controller.walk=state; capture.afterTick(view,controller,cb,now); }
    snap::Consumer::Outcome drain(std::uint64_t now=1000) {
        snap::Consumer::Outcome out{};
        std::thread t([&] { snap::Consumer consumer(now); out=consumer.poll(batch,now,sink); }); t.join(); return out;
    }
    bool has(std::string_view text) const { for (const auto& line:sink.lines) if (line.find(text)!=std::string::npos) return true; return false; }
};
void run(std::string_view which) {
    static_assert(!ps::physicalExecutionApproved && !ps::movementExecutionApproved && !rm::physicalExecutionApproved);
#if defined(GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD) && GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD == 1
    if (which.starts_with("integration_")) {
        // Same fixture and real resident/seam/worker code, not a second movement model.
        Fixture f; gameThread=GetCurrentThreadId();
        const auto path=std::filesystem::current_path()/("passive-integration-"+std::to_string(GetCurrentProcessId())+"-"+std::string(which)+".log");
        auto* root=new ResidentTaskDiagnostic(path.wstring()); // Runtime also retains this graph until process exit.
        taskDiagnosticRoot.store(root,std::memory_order_release);
        if (which=="integration_context") {
            TaskDiagnosticFrame denied(ps::phaseCall+5,0);
            denied.beforeAttempt(remoteController); denied.afterTick(remoteController);
            check(!root->producer.requested() && root->source->stopped() && !root->movement->sealed() && !callbacks.load() && !hookPublished.load(),"real host context: atomic stop, no diagnostic or engine dispatch");
            auto* gap=new ResidentTaskDiagnostic(path.wstring());
            gap->beforeAttempt(f.view,f.controller,148,GetTickCount64());
            auto reads=f.engine.reads; f.engine.phase=false;
            gap->afterTick(f.view,f.controller,149,GetTickCount64());
            check(f.engine.reads==reads,"context denied in integrated seam makes no reads");
            Sink sink;
            std::thread consumer([&] { snap::Consumer c(0); check(c.poll(*gap->movement,GetTickCount64(),sink)==snap::Consumer::Outcome::inconclusive,"denied context terminates inconclusively without waiting for expiry"); });
            consumer.join(); check(sink.lines.size()==1 && sink.lines[0].find("unsealed_batch_not_read")!=std::string::npos,"in-flight batch not inspected on absence");
            return;
        }
        ps::Experiment entity; ps::Movement movement;
        for (unsigned i=0;i<3;++i) entity.tick(f.engine,true,true,false,i);
        root->beforeAttempt(f.view,f.controller,148,GetTickCount64());
        if (which=="integration_failure") {
            const auto reads=f.engine.reads;
            std::thread denied(serviceTaskDiagnostic); denied.join();
            check(root->source->stopped() && root->worker.error()==ERROR_ACCESS_DENIED,"host residence startup refused");
            f.controller.walk=ps::MoveState::assigned; root->afterTick(f.view,f.controller,149,GetTickCount64());
            check(f.engine.reads==reads && root->movement->sealed(),"startup failure stops passive producer without new reads");
            const auto bytes=std::filesystem::file_size(path);
            std::thread retry(serviceTaskDiagnostic); retry.join();
            check(std::filesystem::file_size(path)==bytes,"no retry or duplicate worker/report");
            std::ifstream file(path); std::string log((std::istreambuf_iterator<char>(file)),{});
            check(log.find("movement_history=inconclusive")!=std::string::npos && log.find("consumer_start_failed")!=std::string::npos,"passive startup failure logged outside callback");
            for (unsigned fault=0;fault<2;++fault) {
                Fixture g;
                auto* failed=new ResidentTaskDiagnostic(path.wstring());
                failed->beforeAttempt(g.view,g.controller,148,GetTickCount64());
                bool started=true,retried=true; struct Residence { bool operator()()const { return true; } } allowed;
                std::thread startup([&] {
                    failPassiveTicket=fault==0; failPassiveThread=fault==1;
                    started=failed->worker.start(allowed); retried=failed->worker.start(allowed);
                }); startup.join();
                check(!started && !retried && failed->source->stopped() &&
                    failed->worker.error()==(fault==0 ? ERROR_NOT_ENOUGH_MEMORY : ERROR_NOT_ENOUGH_QUOTA),"ticket/thread failures latch original error and forbid retry");
                const auto before=g.engine.reads;
                failed->afterTick(g.view,g.controller,149,GetTickCount64());
                check(g.engine.reads==before && failed->movement->sealed(),"both startup faults stop passive capture too");
                Sink lines;
                std::thread drain([&] { snap::Consumer c(0); check(c.poll(*failed->movement,0,lines)==snap::Consumer::Outcome::inconclusive,"startup faults never yield complete history"); }); drain.join();
            }
            return;
        }
        bool started=false; struct Residence { bool operator()() const { return true; } } residence;
        std::thread starter([&] { started=root->worker.start(residence); }); starter.join();
        check(started,"existing worker starts in disposable host; no GTA gates changed");
        const auto originalFields=f.engine.ped;
        movement.tick(f.engine,entity,true,true,true,false,3);
        f.controller.walk=movement.state(); root->afterTick(f.view,f.controller,148,GetTickCount64());
        if (which=="integration_inflight") {
            f.engine.actual.x=100.5f; movement.tick(f.engine,entity,true,true,false,false,100);
            f.controller.walk=movement.state(); f.engine.holdOnShift=true;
            bool joined=false;
            std::thread waiter([&] {
                while (!f.engine.readEntered.load(std::memory_order_acquire)) std::this_thread::yield();
                joined=root->worker.join(8000); // Consumer expires while Capture still owns an in-flight native read.
                f.engine.releaseRead.store(true,std::memory_order_release);
            });
            root->afterTick(f.view,f.controller,194,GetTickCount64()); waiter.join();
            check(joined && root->worker.movementOutcome()==snap::Consumer::Outcome::inconclusive,"worker exits while resident Capture/Batch remain valid");
            const auto reads=f.engine.reads; root->afterTick(f.view,f.controller,195,GetTickCount64());
            check(f.engine.reads==reads && root->movement->sealed(),"late producer acknowledges stop, no restart/native reads");
        } else {
            check(which=="integration_walk","known integration case");
            const float positions[]{100.5f,102.f,103.f,104.f,104.f};
            const unsigned times[]{100,150,200,250,551},callbacks[]{194,240,270,300,329};
            for (unsigned i=0;i<5;++i) {
                f.engine.actual.x=positions[i]; movement.tick(f.engine,entity,true,true,false,false,times[i]);
                f.controller.walk=movement.state();
                if (movement.state()==ps::MoveState::arrived) f.engine.slots={};
                root->afterTick(f.view,f.controller,callbacks[i],GetTickCount64());
            }
            bool joined=false; std::thread waiter([&] { joined=root->worker.join(2000); }); waiter.join();
            check(joined && root->worker.movementOutcome()==snap::Consumer::Outcome::complete && movement.state()==ps::MoveState::arrived,"worker drains passive arrival and original slots history");
        }
        check(f.engine.tasks==1 && f.engine.ped==originalFields,"integration leaves native task count and field values unchanged");
        std::ifstream file(path); std::string log((std::istreambuf_iterator<char>(file)),{});
        check(log.find("0x")==std::string::npos && log.find("3735928559")==std::string::npos,"FileSink has no native addresses");
        if (which=="integration_walk") check(log.find("walk=assigned")!=std::string::npos && log.find("walk=started")!=std::string::npos && log.find("walk=arrived")!=std::string::npos && log.find("movement_sample phase=pre_world")!=std::string::npos && log.find("movement_history=complete")!=std::string::npos,"actual shared FileSink receives both value histories");
        else check(log.find("movement_history=inconclusive")!=std::string::npos && log.find("movement_sample")==std::string::npos,"timeout does not drain unsealed batch or fabricate completion");
        const auto reads=f.engine.reads; root->beforeAttempt(f.view,f.controller,400,GetTickCount64()); root->afterTick(f.view,f.controller,400,GetTickCount64());
        check(f.engine.reads==reads && f.engine.tasks==1,"no diagnostic rearm or second task"); return;
    }
#endif
    if (which=="closed") {
        Fixture f; snap::Capture<> off(f.batch);
        off.beforeAttempt(f.view,f.controller,1,0); off.afterTick(f.view,f.controller,1,0);
        check(!f.engine.reads && !f.engine.auths && !f.batch.sealed(),"default OFF has zero side effects");
        DiagnosticRuntimePrimitives runtime; win::Context context(runtime,ps::phaseCall+5,0);
        win::Ready ready(runtime,context); win::Reader reader(context);
        bind::RuntimeView view(remoteEngine,context,ready,reader);
        snap::Batch batch; snap::Capture<true> capture(batch);
        capture.beforeAttempt(view,remoteController,0,0); capture.afterTick(view,remoteController,0,0);
        check(!batch.sealed() && !preparation.load() && !hookPublished.load(),"actual runtime providers refuse host without GTA reads/hook"); return;
    }
    Fixture f;
    if (which=="walk") {
        ps::Experiment entity; ps::Movement movement;
        for (unsigned i=0;i<3;++i) entity.tick(f.engine,true,true,false,i);
        check(entity.state()==ps::State::ped_created,"real baseline entity state");
        const auto originalFields=f.engine.ped;
        f.begin();
        movement.tick(f.engine,entity,true,true,true,false,3);
        f.tick(movement.state(),148,3);
        f.engine.actual.x=100.5f; movement.tick(f.engine,entity,true,true,false,false,100);
        f.tick(movement.state(),194,100);
        f.engine.actual.x=102; movement.tick(f.engine,entity,true,true,false,false,150); f.tick(movement.state(),240,150);
        f.engine.actual.x=103; movement.tick(f.engine,entity,true,true,false,false,200); f.tick(movement.state(),270,200);
        f.engine.actual.x=104; movement.tick(f.engine,entity,true,true,false,false,250); f.tick(movement.state(),300,250);
        movement.tick(f.engine,entity,true,true,false,false,551); f.tick(movement.state(),329,551);
        check(movement.state()==ps::MoveState::arrived && f.engine.tasks==1,"one walk with observed arrival");
        const auto reads=f.engine.reads;
        f.engine.phase=false;
        check(f.drain()==snap::Consumer::Outcome::complete,"deferred bounded batch");
        check(f.engine.reads==reads && f.engine.tasks==1 && f.has("walk=assigned") && f.has("walk=started") && f.has("walk=arrived"),"consumer has no engine access/second task");
        check(f.has("fallen=1 standing=1 support_present=1") && f.has("local=(0.01,0.02)") && f.has("phase=pre_world"),"values not pointers/false UpdatePosition phase");
        check(f.engine.reads%12==0 && f.engine.ped==originalFields,"twelve payload reads per sample and no field writes");
        std::printf("Record=%zu Batch=%zu Capture=%zu payload_reads=%u tasks=%u\n",sizeof(snap::Record),sizeof(snap::Batch),sizeof(f.capture),f.engine.reads,f.engine.tasks);
        return;
    }
    if (which=="context") {
        f.engine.phase=false; f.begin(); check(!f.engine.reads && !f.engine.auths,"denied phase does not authenticate/read engine");
        f.engine.phase=true;
        std::thread t([&] { f.begin(); }); t.join(); check(!f.engine.reads,"wrong thread makes zero reads");
        f.engine.code=false; f.begin(); check(f.batch.sealed() && !f.engine.reads && !f.engine.auths,"code readiness denied before object access"); return;
    }
    if (which=="partial") {
        f.engine.partial=0x14D8; f.begin(); check(f.drain()==snap::Consumer::Outcome::inconclusive && f.has("incomplete_read") && f.sink.lines.size()==1,"partial success emits no default sample"); return;
    }
    if (which=="identity") {
        f.begin(); const auto reads=f.engine.reads;
        f.controller.k.generation++; f.tick(ps::MoveState::assigned,149,1);
        check(f.engine.reads==reads && f.drain()==snap::Consumer::Outcome::inconclusive && f.has("identity_changed"),"generation mismatch stops before reads");
        Fixture g; g.engine.identityChanged=true; g.begin(); check(g.drain()==snap::Consumer::Outcome::inconclusive && !g.has("movement_sample"),"identity changed during read does not publish"); return;
    }
    if (which=="ownership") {
        f.engine.ownership=false; f.begin(); check(!f.engine.reads && f.drain()==snap::Consumer::Outcome::inconclusive,"ownership refused before field reads");
        Fixture g; g.begin(); auto reads=g.engine.reads; g.controller.life=ps::State::removed;
        g.tick(ps::MoveState::failed,149,1); check(g.engine.reads==reads && g.drain()==snap::Consumer::Outcome::inconclusive,"no read after removal"); return;
    }
    if (which=="values") {
        f.engine.matrixChanged=true; f.begin(); check(f.drain()==snap::Consumer::Outcome::inconclusive && f.has("matrix_changed") && !f.has("movement_sample"),"matrix pointer must remain equal");
        Fixture g; g.engine.put(0x44,snap::Pair{std::numeric_limits<float>::infinity(),0}); g.begin();
        check(g.drain()==snap::Consumer::Outcome::inconclusive && g.has("invalid_values"),"nonfinite values unknown, no successful sample"); return;
    }
    if (which=="bounded") {
        trackSnapshotAllocations=true; f.begin(); f.tick(ps::MoveState::assigned,148,0);
        const auto reads=f.engine.reads;
        for (unsigned n=149;n<180;++n) f.tick(ps::MoveState::assigned,n,n);
        trackSnapshotAllocations=false;
        check(!snapshotAllocations && f.engine.reads==reads,"no producer allocations or per-frame reads");
        f.tick(ps::MoveState::assigned,660,1000); check(f.drain()==snap::Consumer::Outcome::inconclusive && f.has("observation_limit"),"bounded callbacks terminal");
        Fixture g; g.begin();
        for (unsigned n=0;n<40;++n) g.tick(n%2 ? ps::MoveState::moving : ps::MoveState::assigned,148+n,n);
        check(g.drain()==snap::Consumer::Outcome::inconclusive && g.has("buffer_full") && g.sink.lines.size()==33,"abnormal phase churn cannot overflow fixed buffer"); return;
    }
    if (which=="deferred") {
        f.begin(); snap::Consumer wrong(0); check(wrong.poll(f.batch,0,f.sink)==snap::Consumer::Outcome::not_deferred && f.sink.lines.empty(),"no formatting/writing on producer thread");
        std::thread t([&] { snap::Consumer consumer(0); check(consumer.poll(f.batch,5000,f.sink)==snap::Consumer::Outcome::inconclusive,"absent callbacks terminal"); }); t.join();
        check(f.has("unsealed_batch_not_read") && !f.has("movement_sample"),"no concurrent drain of in-flight samples");
        const auto reads=f.engine.reads; f.tick(ps::MoveState::assigned,149,5001); check(f.engine.reads==reads && f.batch.sealed(),"external stop acknowledged without more engine reads"); return;
    }
    if (which=="once") {
        f.begin(); f.tick(ps::MoveState::arrived,149,1); const auto reads=f.engine.reads;
        f.capture.beforeAttempt(f.view,f.controller,150,2); f.tick(ps::MoveState::moving,151,3);
        snap::Capture<true> second(f.batch); second.beforeAttempt(f.view,f.controller,152,4);
        check(f.engine.reads==reads && !f.engine.tasks,"no restart/second producer/task even with another capture");
        f.sink.fail=true; check(f.drain()==snap::Consumer::Outcome::inconclusive,"sink failure never complete"); return;
    }
    if (which=="format") {
        snap::Record r{}; r.callback=r.remoteId=r.generation=UINT64_MAX; r.pedState=r.moveState=INT32_MIN;
        r.speed=r.local=r.converted={-std::numeric_limits<float>::max(),std::numeric_limits<float>::max()};
        r.position={r.speed.x,r.speed.y,r.speed.x}; r.walk=ps::MoveState::arrived;
        char line[snap::messageCapacity]{},tiny[8]{};
        check(snap::format(line,sizeof(line),r) && std::strlen(line)+23<512,"maximum values fit existing final FileSink");
        check(!snap::format(tiny,sizeof(tiny),r) && !tiny[0],"truncation explicit");
        Sink sink; check(sink(line),"pointer-free formatted record"); return;
    }
    throw std::runtime_error("unknown test");
}
int main(int argc,char** argv) {
    try { check(argc==2,"one case required"); run(argv[1]); return 0; }
    catch (const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
