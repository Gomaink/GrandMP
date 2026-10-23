// Both executables compile the ACTUAL runtime; physical defines remain absent.
#define GRANDMP_REMOTE_PED_VARIANT 1
#define GRANDMP_REMOTE_PED_MOVE_VARIANT 1
#include "../client/windows/tools/ped_smoke.cpp"
#include "../client/windows/tools/native_diagnostic_integration.hpp"
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace integration = grandmp::windows::native_diagnostic_integration;
namespace delivery = grandmp::windows::native_diagnostic_deferred;
namespace binding = grandmp::windows::native_diagnostic_binding;
namespace slots = grandmp::windows::continuous_remote_native;
inline constexpr bool enabled = integration::buildEnabled;
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
struct FakeEngine {
    rp::Key key{};
    void* owned=nullptr;int reference=-1;
    DWORD thread=GetCurrentThreadId();
    ps::Position actual{1000,2000,30}, candidate{},destination{};
    unsigned tasks=0,creates=0,removes=0,reads=0,queries=0;
    bool ownership=true,hit=false,phase=false,code=true,gameplay=true,beforeSeen=false;
    std::atomic<bool> stopDuringRead{false},readEntered{false},stopPublished{false};
    std::array<std::uint32_t,4> occupancy{};
    std::vector<std::string> movementEvents;
    void touch(){check(GetCurrentThreadId()==thread,"engine call off-thread");}
    bool beginRemote(rp::Key k){touch();check(!owned && reference==-1,"registry empty");key=k;return true;}
    void log(ps::State,const char*){touch();}
    bool requestModel(){touch();return true;}
    bool modelLoaded(){touch();return true;}
    bool revalidateModel(){touch();return true;}
    bool spawnPosition(ps::Position& p){touch();p={1000,2000,30};return true;}
    std::uintptr_t create(ps::Position p){touch();actual=p;owned=reinterpret_cast<void*>(0x1000);reference=0x101;++creates;return 0x1000;}
    bool owns(std::uintptr_t p){touch();return ownership && owned && reference==0x101 && p==0x1000;}
    bool visible(std::uintptr_t){touch();return true;}
    bool remove(std::uintptr_t p){check(owns(p),"unknown removal");owned=nullptr;reference=-1;++removes;return true;}
    bool initialPosition(ps::Position& p){touch();p={1000,2000,30};return true;}
    const char* movePosition(std::uintptr_t p,ps::Position& out){if(!owns(p))return "ownership_unproven";out=actual;return nullptr;}
    void recordRemoteSample(const grandmp::core::RemotePose&,std::uint64_t){touch();}
    void prepareRemoteMove(ps::Position p){touch();candidate=p;}
    float ground(float,float,float,bool& found){touch();found=true;return 29.f;}
    rm::SphereProbeResult sphere(ps::Position){touch();++queries;return {hit,hit?rm::CollisionIdentity::cj_hit:rm::CollisionIdentity::no_hit};}
    const char* moveDestination(std::uintptr_t p,ps::Position from,ps::Position& out){
        if(!owns(p))return "ownership_unproven";return rm::findDestination(*this,from,candidate,out);
    }
    const char* assignMove(std::uintptr_t p,const ps::Position& target){
        check(owns(p),"assignment identity");check(!enabled || beforeSeen,"diagnostic instance before assignment");
        for(auto value:occupancy)if(value)return "primary_task_slot_busy";
        ++tasks;check(tasks==1,"no second task");destination=target;occupancy[3]=0xDEADBEEF;return nullptr;
    }
    void moveLog(const char* event,const char*){touch();movementEvents.emplace_back(event);}
};
struct FakeContext {
    FakeEngine& e;
    bool operator()()const noexcept{return GetCurrentThreadId()==e.thread && e.phase;}
};
struct FakeReady {FakeEngine& e;bool operator()()const noexcept{return e.code && e.gameplay;}};
struct FakeReader {
    FakeEngine& e;
    slots::ReadResult operator()(std::uint32_t address,void* output,std::size_t size){
        e.touch();check(e.phase && e.owned,"no read of destroyed/unadmitted entity");++e.reads;
        if(e.stopDuringRead.exchange(false)){
            e.readEntered.store(true,std::memory_order_release);
            while(!e.stopPublished.load(std::memory_order_acquire))std::this_thread::yield(); // Fake-only in-flight probe.
        }
        std::uint32_t value=0;check(size==4,"DWORD only");
        if(address==0x1000+ps::task_layout::intelligence)value=0x2000;
        else if(address==0x2000 || address==0x2004+ps::task_layout::managerOwner)value=0x1000;
        else if(address>=0x2004 && address<0x2014 && (address-0x2004)%4==0)value=e.occupancy[(address-0x2004)/4];
        else throw std::runtime_error("task dereference/unexpected read");
        std::memcpy(output,&value,4);return {true,4};
    }
};
struct Lines {
    std::vector<std::string> values;
    bool operator()(const char* line){
        check(!std::strstr(line,"0x") && !std::strstr(line,"DEADBEEF") && !std::strstr(line,"3735928559") && !std::strstr(line,"ped="),"no addresses in diagnostic records");
        values.emplace_back(line);return true;
    }
};
struct Fixture {
    FakeEngine e;rm::Controller controller;rp::IntentSink sink;
    std::shared_ptr<delivery::Session<enabled>> source=std::make_shared<delivery::Session<enabled>>();
    integration::Producer<enabled> producer{*source};
    std::uint64_t callback=0,now=0;
    Fixture(){sink.resetSession();sink.spawn({42,1,100,200,30});for(unsigned n=0;n<4;++n)tick(n);}
    template<class View>
    struct Frame {
        Fixture& f;View& view;
        void beforeAttempt(const rm::Controller& c){
            check(c.moveState()==ps::MoveState::idle && !f.e.tasks,"real pre-attribution state");
            f.producer.beforeAttempt(view,c,f.callback,f.now);
            check(f.producer.requested() || f.source->stopped(),"instance signalled or diagnostic already stopped");
            f.e.beforeSeen=true; // The seam still precedes assignment after diagnostic expiry.
        }
    };
    void tick(std::uint64_t time){
        now=time;e.phase=true;
        FakeContext context{e};FakeReady ready{e};FakeReader reader{e};binding::RuntimeView view(e,context,ready,reader);
#if defined(GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD) && GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD == 1
        Frame<decltype(view)> frame{*this,view};
        controller.tick(e,sink.snapshot(),GetCurrentThreadId()==e.thread,e.gameplay,time,frame);
#else
        controller.tick(e,sink.snapshot(),GetCurrentThreadId()==e.thread,e.gameplay,time);
#endif
        producer.afterTick(view,controller,callback++,time);e.phase=false;
    }
    void move(){sink.move({42,2,102.5f,200,30});tick(5);}
    void arrive(){for(unsigned n=1;n<=5;++n){e.actual.x+=0.5f;tick(100*n);}tick(800);}
};
void run(std::string_view which){
    static_assert(!ps::physicalExecutionApproved && !ps::movementExecutionApproved && !rm::physicalExecutionApproved);
    if(which=="runtime_closed"){
#if defined(GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD) && GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD == 1
        static_assert(!taskDiagnosticPhysicalApproved);
        prepareTaskDiagnostic();check(!taskDiagnosticRoot.load() && !taskDiagnosticSetupAttempted.load(),"real setup blocked by physical gates");
        TaskDiagnosticFrame frame(ps::phaseCall+5,0);frame.beforeAttempt(remoteController);frame.afterTick(remoteController);
#endif
        check(!preparation.load() && !callbacks.load() && !hookPublished.load() && !remoteEngine.owned,"no callback/bootstrap/engine in host");return;
    }
    Fixture f;
    if(which=="physical_release_trace"){
        check(enabled,"trace uses diagnostic host only, with physical gates closed");
        f.callback=148;f.move();
        f.callback=194;f.e.actual.x+=0.5f;f.tick(100);
        f.callback=250;f.e.actual.x+=1.f;f.tick(500); // Unchanged mask/walk: no extra record.
        f.callback=313;f.e.occupancy={};f.e.actual=f.e.destination;f.tick(1000);
        check(f.controller.moveState()==ps::MoveState::moving && !f.source->closed(),"empty before settling is not arrival or terminal history");
        f.callback=329;f.tick(1300);
        delivery::Consumer consumer(0);Lines lines;
        check(consumer.poll(*f.source,1300,lines)==delivery::Outcome::terminal_observed && consumer.records()==5,"complete five-record history, including unsampled metadata");
        const char* expected[]{"walk=assigned","walk=started","walk=started","walk=arrived"};
        const unsigned elapsed[]{0,46,165,181},mask[]{8,8,0,0};
        for(unsigned i=0;i<4;++i){
            const auto& line=lines.values[i+1];
            check(line.find(expected[i])!=std::string::npos && line.find("elapsed_callbacks="+std::to_string(elapsed[i])+" ")!=std::string::npos &&
                line.find("mask="+std::to_string(mask[i])+" ")!=std::string::npos && line.find("binding=none ")!=std::string::npos &&
                line.find("auth=none sampled=1")!=std::string::npos,"physical trace semantics reproduced by actual binding/observer/consumer");
        }
        check(lines.values[0].find("sampled=0")!=std::string::npos && lines.values[0].find("mask=unknown")!=std::string::npos,
            "auth=none in initial metadata does not claim a memory observation");
        f.sink.move({42,3,105,200,30});f.tick(1400);
        check(f.e.tasks==1 && f.source->closed(),"complete/empty never rearms the physical Alpha 0.7 one-shot");
        for(const auto& line:lines.values)std::puts(line.c_str());
        return;
    }
    if(which=="complete_not_empty"){
        check(enabled,"closed diagnostic host");
        f.move();f.arrive();
        check(!f.source->closed() && f.e.occupancy[3],"arrived with busy slots keeps observing");
        f.callback+=600;f.tick(1000);
        delivery::Consumer consumer(0);Lines lines;
        check(consumer.poll(*f.source,1000,lines)==delivery::Outcome::terminal_observed && f.e.occupancy[3],
            "complete history can report bounded diagnostic termination while a protected slot stays occupied");
        const auto& terminal=lines.values.at(consumer.records()-1);
        check(terminal.find("reason=callback_limit")!=std::string::npos &&
            terminal.find("slots=unknown")!=std::string::npos && terminal.find("sampled=0")!=std::string::npos &&
            lines.values.back().find("diagnostic_history=complete")!=std::string::npos && f.e.tasks==1,
            "history completeness is not successful slot release or authority for another native task");
        return;
    }
    if(which.starts_with("preparation_")){
        check(enabled,"preparation tests require diagnostic host gate");
        if(which=="preparation_format"){
            const integration::PreparationResult results[]{
                {false,false,false,true,UINT64_MAX},{true,true,true,true,UINT64_MAX},
                {true,false,true,true,UINT64_MAX},{true,false,false,true,UINT64_MAX}};
            for(const auto& result:results){
                char detail[288]{},small[8]{};
                check(integration::formatPreparation(detail,sizeof(detail),result) &&
                    !integration::formatPreparation(small,sizeof(small),result),"max elapsed fits; truncation detected");
                check(std::strstr(detail,"18446744073709551615") && std::strstr(detail,"consumer_start_attempted=0") &&
                    std::strstr(detail,"win32_source=not_called"),"full value-only evidence, Win32 not queried");
#if defined(GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD) && GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD == 1
                const auto path=std::filesystem::current_path()/("integration-preparation-"+std::to_string(GetCurrentProcessId())+"-"+result.reason()+".log");
                ResidentTaskDiagnostic root(path.wstring());
                root.inconclusive("preparation_wait_ended_inconclusive",ERROR_SUCCESS,detail);
                std::ifstream file(path);std::string log((std::istreambuf_iterator<char>(file)),{});
                check(root.source->stopped() && log.find(detail)!=std::string::npos &&
                    log.find("win32=0 attempt=1")!=std::string::npos && log.find("0x")==std::string::npos,
                    "actual preparation FileSink preserves full failure evidence");
#endif
            }
            return;
        }
        std::atomic<bool> disabled{false};std::uint64_t clock=0;unsigned pauses=0;
        if(which=="preparation_early" || which=="preparation_closed_queue")f.move();
        if(which=="preparation_closed_queue"){f.arrive();f.e.occupancy[3]=0;f.tick(900);}
        auto pause=[&]{
            ++pauses;clock+=10000;
            if(which=="preparation_delayed" && pauses==3)f.move();
            else if(which=="preparation_deadline_race" && clock==120000)f.move();
            else if(which=="preparation_stop")f.source->requestStop();
            else if(which=="preparation_disabled")disabled.store(true);
            else f.tick(clock); // Active callbacks alone do not signal an attempt.
        };
        const auto result=integration::awaitPreparation(f.producer,*f.source,disabled,[&]{return clock;},pause,120000);
        char detail[288]{};check(integration::formatPreparation(detail,sizeof(detail),result),"complete preparation evidence");
        check(!std::strstr(detail,"0x") && !std::strstr(detail," ped="),"no address field in preparation evidence");
        if(which=="preparation_early" || which=="preparation_delayed" || which=="preparation_closed_queue"){
            check(result.ready() && !result.expired && f.e.tasks==1,"timely request starts only one consumer");
            check(pauses==(which=="preparation_delayed"?3u:0u),"signal before/during preparation wait");
            if(which!="preparation_closed_queue"){f.arrive();f.e.occupancy[3]=0;f.tick(900);}
            delivery::Consumer consumer(0);Lines lines;
            check(consumer.poll(*f.source,900,lines)==delivery::Outcome::terminal_observed && consumer.records()==5,
                "late consumer drains pending assigned/started/arrived/released in order, even after producer closes");
            check(lines.values[1].find("walk=assigned")!=std::string::npos && lines.values[3].find("walk=arrived")!=std::string::npos,"ordered queued records");
        }else{
            check(!result.ready(),"failed preparation cannot start consumer");
            if(which=="preparation_stop")check(result.stopped && !result.expired && !std::strcmp(result.reason(),"session_stopped"),"startup stop distinguished");
            else if(which=="preparation_disabled")check(result.runtimeDisabled && !result.expired && !std::strcmp(result.reason(),"runtime_disabled"),"runtime stop distinguished");
            else check(result.expired && result.elapsedMs==120000 && !std::strcmp(result.reason(),"deadline_expired"),"original preparation deadline preserved");
            f.source->requestStop(); // Same terminal action as the real preparation service.
            if(which=="preparation_timeout"){
                check(!result.requested && !f.e.tasks && !f.e.reads,"callbacks without attempt do not publish or read slots");
                f.move();f.arrive();
                binding::Record record;
                check(f.e.tasks==1 && !f.e.reads && !f.producer.requested() && !f.source->pop(record),
                    "physical reproduction: late one-shot walk survives; diagnostic stopped, no events/slot conclusions/retry");
            }else if(which=="preparation_deadline_race"){
                check(result.requested && f.e.tasks==1,"signal at deadline does not extend window");
                binding::Record record;unsigned pending=0;while(f.source->pop(record))++pending;
                check(pending==2,"queued initial/assigned records exist but do not imply consumer startup or completion");
            }else check(!result.requested && !f.e.tasks && !f.e.reads,"stopped before any native attempt");
        }
        return;
    }
    if(which=="service_failure" || which=="service_native_denied"){
#if defined(GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD) && GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD == 1
        gameThread=GetCurrentThreadId();
        const auto path=std::filesystem::current_path()/("integration-service-"+std::to_string(GetCurrentProcessId())+".log");
        auto* root=new ResidentTaskDiagnostic(path.wstring());taskDiagnosticRoot.store(root);
        f.e.phase=true;FakeContext context{f.e};FakeReady ready{f.e};FakeReader reader{f.e};binding::RuntimeView view(f.e,context,ready,reader);
        root->producer.beforeAttempt(view,f.controller,f.callback,f.now);f.e.phase=false;
        check(root->producer.requested() && !root->source->stopped(),"resident graph initialized before task");
        if(which=="service_native_denied"){
            serviceTaskDiagnostic();check(root->source->stopped() && !std::filesystem::exists(path),"service on native thread: atomic stop, no wait/format/file");
        }else{
            std::thread external(serviceTaskDiagnostic);external.join();
            check(root->source->stopped() && root->worker.error()==ERROR_ACCESS_DENIED,"real service refuses unadmitted host residence");
            const auto bytes=std::filesystem::file_size(path);
            std::thread duplicate(serviceTaskDiagnostic);duplicate.join();
            check(std::filesystem::file_size(path)==bytes,"no second worker start or duplicate startup report");
            std::ifstream file(path);std::string log((std::istreambuf_iterator<char>(file)),{});
            check(log.find("inconclusive")!=std::string::npos && log.find("consumer_start_failed")!=std::string::npos && log.find("0x")==std::string::npos,"startup failure is explicit, no engine addresses");
        }
        check(!f.e.tasks && !f.e.reads && !callbacks.load() && !hookPublished.load(),"no native task/read/hook during service failure");return;
#else
        throw std::runtime_error("gate ON only");
#endif
    }
    if(which=="equivalence"){
        f.move();f.arrive();check(f.controller.moveState()==ps::MoveState::arrived && f.e.tasks==1 && f.e.queries==5,"original one-walk and geometry");
        f.sink.despawn(42);f.tick(900);
        check(f.e.removes==1 && !f.e.owned,"original leave/removal");
        if constexpr(!enabled)check(!f.e.reads && !f.producer.requested(),"gate OFF has zero diagnostic reads/signals");
        return;
    }
    check(enabled,"diagnostic case requires host gate ON");
    if(which=="collision")f.e.hit=true;
    f.move();check(f.producer.requested(),"one instance signalled by actual Controller pre-tick seam");
    delivery::Consumer consumer(0);Lines lines;
    if(which=="walk" || which=="arrived_busy" || which=="arrived_empty"){
        f.arrive();check(!f.source->closed(),"arrival is not release");
        if(which=="arrived_busy")for(unsigned n=0;n<20;++n)f.tick(810+n);
        f.e.occupancy[3]=0;f.tick(900);
        check(consumer.poll(*f.source,900,lines)==delivery::Outcome::terminal_observed && consumer.records()==5,"assigned/start/arrival/busy/release copies");
        check(lines.values[3].find("walk=arrived")!=std::string::npos && lines.values[3].find("slots=busy")!=std::string::npos &&
            lines.values[4].find("slots=empty")!=std::string::npos,"observed release after arrival");
    }else if(which=="timeout"){
        const auto reads=f.e.reads;f.callback+=600;f.tick(1000);
        consumer.poll(*f.source,1000,lines);check(f.source->closed() && f.e.reads==reads && lines.values[2].find("callback_limit")!=std::string::npos,"bounded terminal with no final read/task cancellation");
    }else if(which=="ownership" || which=="leave"){
        const auto reads=f.e.reads;
        if(which=="ownership")f.e.ownership=false;else f.sink.despawn(42);
        f.tick(20);consumer.poll(*f.source,20,lines);
        check(f.e.reads==reads && f.source->closed(),"lifecycle terminal without probing destroyed entity");
        check(which!="leave" || f.e.removes==1,"leave still native lifecycle owner");
    }else if(which=="generation"){
        const auto reads=f.e.reads;++f.e.key.generation;f.tick(20);consumer.poll(*f.source,20,lines);
        check(f.source->closed() && f.e.reads==reads && lines.values[2].find("session_changed")!=std::string::npos,"changed generation refuses before memory");
    }else if(which=="context"){
        const auto reads=f.e.reads;std::thread external([&]{f.tick(20);});external.join();
        check(f.e.reads==reads,"worker cannot probe or advance native controller");
        f.source->requestStop();consumer.poll(*f.source,20,lines);
    }else if(which=="stop"){
        const auto reads=f.e.reads;
        std::thread external([&]{f.source->requestStop();});external.join();f.tick(20);
        check(f.e.reads==reads && consumer.poll(*f.source,20,lines)==delivery::Outcome::cancelled,"external shutdown never frees producer or resumes reads");
    }else if(which=="inflight"){
        f.e.occupancy[0]=1;f.e.stopDuringRead=true;
        std::thread stopper([&]{
            while(!f.e.readEntered.load(std::memory_order_acquire))std::this_thread::yield();
            f.source->requestStop();f.e.stopPublished.store(true,std::memory_order_release);
        });
        f.tick(20);stopper.join();const auto reads=f.e.reads;f.tick(21);
        check(f.e.reads==reads && consumer.poll(*f.source,21,lines)==delivery::Outcome::cancelled,"in-flight sample may finish; next cannot read; history inconclusive");
    }else if(which=="overflow"){
        for(unsigned n=0;n<25;++n){f.e.occupancy[0]=n%2?0:1;f.tick(20+n);}
        check(consumer.poll(*f.source,50,lines)==delivery::Outcome::overflow && consumer.records()==16,"slow consumer bounded; terminal loss inconclusive");
    }else if(which=="absence"){
        const auto reads=f.e.reads;check(consumer.poll(*f.source,6000,lines)==delivery::Outcome::callbacks_absent,"external timer no guessed slots");
        f.tick(6001);check(f.e.reads==reads,"late callback cannot restart observer");
    }else if(which=="unknown"){
        f.e.code=false;f.tick(20);consumer.poll(*f.source,20,lines);
        check(f.source->closed() && lines.values[2].find("slots=unknown")!=std::string::npos,"unknown never empty");
    }else if(which=="collision"){
        consumer.poll(*f.source,20,lines);check(!f.e.tasks && !f.e.reads && f.source->closed(),"FU6 refusal unchanged; metadata only, no task/probe after failure");
        return;
    }else if(which=="reconnect"){
        f.arrive();f.e.occupancy[3]=0;f.tick(900);
        f.sink.despawn(42);f.tick(901);f.sink.resetSession();f.sink.spawn({43,1,100,200,30});
        for(unsigned n=0;n<4;++n)f.tick(902+n);f.sink.move({43,2,102.5f,200,30});f.tick(910);
        check(f.e.tasks==1 && f.e.creates==2,"no second task after reconnect/release");
        consumer.poll(*f.source,910,lines);
    }else throw std::runtime_error("unknown case");
    check(f.e.tasks==1,"diagnostic never assigns another task");
    check(!lines.values.empty(),"deferred evidence copied");
}
int main(int argc,char** argv){try{check(argc==2,"case required");run(argv[1]);std::puts("integration PASS");return 0;}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
