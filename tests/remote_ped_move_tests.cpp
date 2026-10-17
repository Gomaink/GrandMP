#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define GRANDMP_REMOTE_PED_MOVE_VARIANT 1
#include <windows.h>
#include "../client/windows/tools/remote_ped_move.hpp"
#include "grandmp/windows/observer_network.hpp"
#include <atomic>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
namespace rm=grandmp::windows::remote_ped_move;
namespace rp=grandmp::windows::remote_ped;
namespace ps=grandmp::windows::ped_smoke;
void require(bool okay,const char* reason) { if(!okay) throw std::runtime_error(reason); }
struct Engine {
    std::thread::id owner=std::this_thread::get_id();
    ps::Position initial{1000,2000,30},position=initial,candidate{},destination{};
    rp::Key key{};
    unsigned calls=0,creates=0,removes=0,tasks=0,probes=0,destructorCancels=0;
    bool live=false,ownership=true,groundOkay=true,obstacle=false,active=false,reenter=false,verbose=false;
    float groundHeight=29;
    rm::Controller* controller=nullptr; rp::Intent intent{};
    std::vector<std::string> events;
    void touch() { require(owner==std::this_thread::get_id(),"engine call outside native consumer thread"); ++calls; }
    bool beginRemote(rp::Key k) { touch();require(!live,"registry still occupied");key=k;return true; }
    void log(ps::State s,const char*) { touch();if(verbose)std::fwprintf(stdout,L"native %hs\n",ps::name(s)); }
    bool requestModel() {touch();return true;} bool modelLoaded(){touch();return true;} bool revalidateModel(){touch();return true;}
    bool spawnPosition(ps::Position& p){touch();p=initial;return true;}
    std::uintptr_t create(const ps::Position& p){touch();++creates;position=p;live=true;return 0x1000;}
    bool owns(std::uintptr_t p){touch();return live && ownership && p==0x1000;}
    bool visible(std::uintptr_t){touch();return true;}
    bool remove(std::uintptr_t p){require(owns(p),"unknown removal");if(active)++destructorCancels;active=false;live=false;++removes;return true;}
    bool initialPosition(ps::Position& p){touch();p=initial;return live && ownership;}
    const char* movePosition(std::uintptr_t p,ps::Position& out){if(!owns(p))return "ownership_unproven";out=position;return nullptr;}
    void recordRemoteSample(const grandmp::core::RemotePose&,std::uint64_t){touch();}
    void prepareRemoteMove(ps::Position p){touch();candidate=p;}
    float ground(float,float,float,bool& found){touch();++probes;found=groundOkay;return groundHeight;}
    std::uintptr_t sphere(ps::Position){touch();return obstacle?1:0;}
    const char* moveDestination(std::uintptr_t p,ps::Position origin,ps::Position& out){if(!owns(p))return "ownership_unproven";return rm::findDestination(*this,origin,candidate,out);}
    const char* assignMove(std::uintptr_t p,const ps::Position& target){
        require(owns(p),"task identity");if(active)return "primary_task_slot_busy";
        ++tasks;active=true;destination=target;
        if(reenter)controller->tick(*this,intent,true,true,5);
        return nullptr;
    }
    void moveLog(const char* state,const char*){touch();std::string s=state;if(!s.starts_with("remote_"))s="remote_"+s;events.push_back(s);if(verbose)std::fwprintf(stdout,L"%hs remote=%llu generation=%llu\n",s.c_str(),key.player,key.generation);}
    unsigned count(std::string_view event) const {unsigned n=0;for(const auto& s:events)n+=s==event;return n;}
};
grandmp::core::RemotePose pose(unsigned seq=1,float x=100){return {42,seq,x,200,30};}
struct Fixture {
    rp::IntentSink sink; rm::Controller c; Engine e;
    Fixture(){e.controller=&c;sink.resetSession();sink.spawn(pose());for(unsigned i=0;i<4;++i)tick(i);}
    void tick(std::uint64_t now){c.tick(e,sink.snapshot(),true,true,now);}
    void move(float delta=2.5f){sink.move(pose(2,100+delta));e.intent=sink.snapshot();tick(5);}
};
void unit(std::wstring_view which){
    if(which==L"identity_publication"){
        // Identical sequence/XYZ must not suppress the FIRST pose of another ID.
        for(bool reset : {false,true}) {
            rp::IntentSink sink;auto a=pose();sink.spawn(a);const auto first=sink.snapshot();
            if(reset)sink.resetSession();else sink.despawn(a.id);
            auto b=a;b.id=43;sink.spawn(b);auto current=sink.snapshot();
            std::fwprintf(stdout,L"publication reset=%u key=%llu initial=%llu pose=%llu\n",unsigned(reset),
                current.key.player,current.initialPose.id,current.pose.id);
            require(current.present && current.key.player==43 && current.initialPose.id==43 && current.pose.id==43,
                "new admission must publish key/initialPose/pose with B identity");
            require(current.key.generation>first.key.generation && current.sampleRevision>first.sampleRevision,
                "new identity publishes a new generation and sample despite equal sequence/XYZ");
            const auto revision=current.sampleRevision, lifecycle=current.revision;
            sink.spawn(b);sink.move(b);require(sink.snapshot().sampleRevision==revision,"same identity/sequence/XYZ still coalesces");
            b.x+=0.25f;sink.move(b);current=sink.snapshot();
            require(current.pose.x==b.x && current.sampleRevision==revision+1 && current.initialPose.x==a.x,
                "same ID/sequence with interpolated XYZ advances sample and preserves initial anchor");
            b.y+=0.25f;sink.spawn(b);current=sink.snapshot();
            require(current.pose.y==b.y && current.sampleRevision==revision+2 && current.revision==lifecycle,"duplicate join also publishes changed XYZ without lifecycle change");
            sink.move(a);sink.despawn(a.id);sink.spawn(a);const auto stable=sink.snapshot();
            require(stable.present && stable.key==current.key && stable.pose.id==43 && stable.initialPose.id==43 &&
                stable.sampleRevision==current.sampleRevision,"old A pose/leave/join cannot corrupt B");
            b.sequence=2;sink.move(b);auto stale=b;stale.sequence=1;stale.x+=3;sink.move(stale);
            require(sink.snapshot().pose.sequence==2 && sink.snapshot().pose.x==b.x,"stale B sample ignored");
            sink.despawn(43);sink.resetSession();sink.spawn(a);sink.spawn(b);
            require(!sink.snapshot().present,"retired IDs stay refused across session reset");
        }
        return;
    }
    if(which==L"thread"){
        rp::IntentSink sink;rm::Controller c;Engine e;
        std::thread worker([&]{sink.spawn(pose());sink.move(pose(2,103));c.tick(e,sink.snapshot(),false,true,0);});worker.join();
        c.tick(e,sink.snapshot(),true,false,0);require(!e.calls,"worker/loading must not call engine");return;
    }
    if(which==L"cancel_pending"){
        rp::IntentSink sink;rm::Controller c;Engine e;sink.spawn(pose());sink.move(pose(2,103));sink.despawn(42);
        c.tick(e,sink.snapshot(),true,true,1);require(!e.calls,"leave before spawn cancels all native operations");return;
    }
    Fixture f;
    require(f.e.creates==1 && !f.e.tasks && f.c.state()==ps::State::ped_created,"first pose spawns but never walks");
    if(which==L"anchor"){
        require(f.e.count("remote_move_anchor_set")==1,"one anchor");
        // First remote sample must survive model-loading coalescing.
        rp::IntentSink sink;rm::Controller c;Engine e;sink.spawn(pose());sink.move(pose(2,103));
        for(unsigned i=0;i<4;++i)c.tick(e,sink.snapshot(),true,true,i);
        require(e.tasks==1 && std::abs(e.destination.x-1003)<0.01f,"immutable initial remote + safe local spawn anchors");
    }else if(which==L"small"){
        f.move(0.5f);f.tick(6);require(!f.e.tasks && f.e.count("remote_move_rejected")==1,"small delta does not consume valid request or spam");
        f.e.position=f.e.initial;f.sink.move(pose(3,102.5f));f.tick(7);require(f.e.tasks==1,"later sufficient delta moves once");
    }else if(which==L"valid" || which==L"maximum" || which==L"reentry"){
        f.e.reenter=which==L"reentry";f.move(which==L"maximum"?4.f:2.5f);
        require(f.e.tasks==1 && ps::distance2D(f.e.initial,f.e.destination)<=4.f,"one bounded relative native task");
        require(f.e.count("remote_move_task_assigned")==1 && !f.e.count("remote_move_arrived"),"assignment is not arrival");
    }else if(which==L"oversize"){
        f.move(4.1f);require(!f.e.tasks,"oversized delta rejected, not clamped");
        f.sink.move(pose(3,102.5f));f.tick(8);require(!f.e.tasks,"rejected attempt never retried");
    }else if(which==L"nonfinite"){
        auto p=pose(2);p.x=std::numeric_limits<float>::quiet_NaN();f.sink.move(p);p.x=std::numeric_limits<float>::infinity();f.sink.move(p);
        f.tick(5);require(!f.e.tasks && f.sink.snapshot().pose.sequence==1,"invalid UDP values never published");
        auto bad=f.sink.snapshot();bad.pose=pose(2);bad.pose.x=std::numeric_limits<float>::infinity();++bad.sampleRevision;
        f.c.tick(f.e,bad,true,true,6);require(!f.e.tasks,"consumer also refuses nonfinite sample");
    }else if(which==L"height" || which==L"ground" || which==L"obstacle"){
        if(which==L"height"){auto p=pose(2,103);p.z=32;f.sink.move(p);f.tick(5);}
        else{f.e.groundOkay=which!=L"ground";f.e.obstacle=which==L"obstacle";f.move();}
        require(!f.e.tasks,"height/ground/obstacle guard before task");
    }else if(which==L"slope" || which==L"ground_nan" || which==L"busy_task" || which==L"drift"){
        if(which==L"slope")f.e.groundHeight=30;
        if(which==L"ground_nan")f.e.groundHeight=std::numeric_limits<float>::quiet_NaN();
        if(which==L"busy_task")f.e.active=true;
        if(which==L"drift")f.e.position.x+=0.4f;
        f.move();require(!f.e.tasks,"ground slope/NaN, active task or anchor drift rejected before assignment");
    }else if(which==L"duplicates"){
        f.move();for(unsigned i=0;i<100;++i){f.sink.spawn(pose());f.sink.move(pose(2,102.5f));f.tick(6+i);}
        require(f.e.tasks==1,"one task despite duplicate joins/poses/frames");
    }else if(which==L"stale" || which==L"generation" || which==L"player"){
        auto old=f.sink.snapshot();f.sink.move(pose(2,100.5f));f.tick(5);
        if(which==L"stale"){old.pose=pose(1,103);old.sampleRevision=100;}
        if(which==L"generation"){old.key.generation=0;old.pose=pose(2,103);old.revision=100;}
        if(which==L"player"){old.key.player=99;old.pose={99,2,103,200,30};old.sampleRevision=100;}
        f.c.tick(f.e,old,true,true,6);require(!f.e.tasks,"old sample/generation/other player cannot command");
    }else if(which==L"stale_leave"){
        auto stale=f.sink.snapshot();stale.present=false;f.move();f.c.tick(f.e,stale,true,true,6);
        require(f.e.tasks==1 && !f.e.removes && !f.e.count("remote_move_cancelled"),"obsolete leave cannot cancel current intent");
    }else if(which==L"leave_before"){
        f.sink.move(pose(2,103));f.sink.despawn(42);f.tick(5);require(f.e.removes==1 && !f.e.tasks,"leave priority over pending move");
    }else if(which==L"leave_active"){
        f.move();f.sink.despawn(42);f.tick(6);
        require(f.e.removes==1 && f.e.destructorCancels==1 && f.e.count("remote_move_cancelled")==1 && !f.e.count("remote_move_failed"),"native PED cleanup owns active task; expected cancellation");
        for(unsigned i=0;i<100;++i)f.tick(7+i);require(f.e.count("remote_move_cancelled")==1,"no repeated cancel");
    }else if(which==L"ownership"){
        f.move();f.e.ownership=false;f.tick(6);const auto calls=f.e.calls;
        f.sink.despawn(42);for(unsigned i=0;i<100;++i)f.tick(7+i);
        require(f.c.poisoned() && !f.e.removes && f.e.calls==calls,"ownership loss terminal without unknown removal/queries");
    }else if(which==L"identity"){
        f.move();f.c.disable();const auto calls=f.e.calls;f.sink.despawn(42);f.tick(6);
        require(f.c.poisoned() && f.e.calls==calls && !f.e.removes,"code identity terminal without cleanup");
    }else if(which==L"timeout"){
        f.move();f.tick(20006);require(f.e.count("remote_move_failed")==1 && !f.e.count("remote_move_arrived"),"timeout not arrival");
        f.tick(21000);require(f.e.count("remote_move_failed")==1 && f.e.tasks==1,"no timeout retry");
    }else if(which==L"arrival"){
        f.move();f.e.position.x+=0.5f;f.tick(100);require(f.e.count("remote_move_started")==1,"actual displacement");
        f.e.position.x+=1.f;f.tick(200);f.e.position=f.e.destination;f.tick(300);f.tick(601);
        require(f.c.moveState()==ps::MoveState::arrived && f.e.count("remote_move_arrived")==1,"near target and stationary observation");
        f.sink.despawn(42);f.tick(700);require(f.e.removes==1 && !f.e.count("remote_move_failed"),"leave after arrival safe");
    }else if(which==L"reconnect"){
        f.move();f.sink.despawn(42);f.tick(6);f.sink.resetSession();auto p=pose();p.id=43;f.sink.spawn(p);
        for(unsigned i=0;i<4;++i)f.tick(7+i);p.sequence=2;p.x+=3;f.sink.move(p);f.tick(12);
        require(f.e.creates==2 && f.e.tasks==1 && f.e.removes==1,"one task per slice across generations");
    }else throw std::runtime_error("unknown case");
}
int loopback(unsigned short port){
    rp::IntentSink sink;rm::Controller c;Engine e;e.verbose=true;
    std::atomic<bool> stop{false},ready{false};
    std::thread worker([&]{grandmp::windows::ObserverNetwork net;net.setRemoteSceneSink(&sink);if(net.start(port)&&net.beginRemoteScenePreview()){
        ready=true;while(!stop){net.tick(std::nullopt);Sleep(10);}}net.stop();});
    const auto start=GetTickCount64();
    while(GetTickCount64()-start<11000){
        if(e.active){const float dx=e.destination.x-e.position.x,dy=e.destination.y-e.position.y;const float d=std::hypot(dx,dy);
            if(d>0.001f){const float step=(std::min)(d,0.015f);e.position.x+=step*dx/d;e.position.y+=step*dy/d;}}
        c.tick(e,sink.snapshot(),true,true,GetTickCount64());Sleep(10);
    }
    stop=true;worker.join();c.tick(e,sink.snapshot(),true,true,GetTickCount64());
    require(ready && e.creates==1 && e.tasks==1 && e.removes==1 && e.count("remote_move_started")==1 && e.count("remote_move_arrived")==1,"UDP to single native-task fake, observed arrival, leave");
    std::fwprintf(stdout,L"remote_move_loopback creates=1 tasks=1 arrived=1 removes=1 native_thread_only=1\n");return 0;
}
void loader(const wchar_t* path,bool enabled=false){
    static_assert(!rm::physicalExecutionApproved);
    SetEnvironmentVariableW(L"GRANDMP_PED_SMOKE_ENABLE",L"1");SetEnvironmentVariableW(L"GRANDMP_REMOTE_PED_MOVE_PHYSICAL_BUILD",L"1");
    HMODULE m=LoadLibraryW(path);require(m!=nullptr,"load Alpha07 ASI in disposable non-GTA host");using Query=unsigned(__cdecl*)();using Entry=void(__cdecl*)();
    const auto init=reinterpret_cast<Entry>(GetProcAddress(m,"InitializeASI"));const auto state=reinterpret_cast<Query>(GetProcAddress(m,"GrandMPPedSmokeStatus"));const auto count=reinterpret_cast<Query>(GetProcAddress(m,"GrandMPPedSmokeCallbackCount"));
    require(init && state && count && state()==0 && count()==0,"loader inert");init();const auto until=GetTickCount64()+5000;while(state()==1 && GetTickCount64()<until)Sleep(1);
    const unsigned expected=enabled ? 4 : 2;
    require(state()==expected && count()==0,"closed gate or enabled strict identity refusal before dispatch");
    init();init();require(state()==expected && count()==0,"no retry after refusal");
    std::ifstream log(std::filesystem::path(path).parent_path()/"GrandMPRemotePedMoveExperimental.log");
    const auto pid=std::string(" pid=")+std::to_string(GetCurrentProcessId())+" ";
    bool diagnosed=false;
    for(std::string line;std::getline(log,line);){
        if(line.find(pid)==std::string::npos)continue;
        if(line.find(enabled ? "reason=hoodlum_identity_path_or_live_signature" :
            "reason=installation_blocked_physical_execution_review_missing")!=std::string::npos)diagnosed=true;
        require(line.find("callback_executed")==std::string::npos && line.find("remote_network_thread_")==std::string::npos &&
            line.find("model_requested")==std::string::npos && line.find("post_population_phase_installed")==std::string::npos,
            "refusal before hook/engine/receiver");
    }
    require(diagnosed,"this PID logged the expected artifact refusal");
    std::fwprintf(stdout,L"loader enabled=%u status=%u callbacks=%u identity_refusal=%u no_hook_engine_receiver=1\n",
        unsigned(enabled),state(),count(),unsigned(diagnosed));
    require(FreeLibrary(m)!=FALSE,"release disposable host reference");
}
int wmain(int argc,wchar_t** argv){try{require(argc>=2,"case required");const std::wstring_view which=argv[1];
    if(which==L"loopback" && argc==3)return loopback(static_cast<unsigned short>(std::wcstoul(argv[2],nullptr,10)));
    if((which==L"loader" || which==L"loader_enabled") && argc==3)loader(argv[2],which==L"loader_enabled");else unit(which);std::fwprintf(stdout,L"remote_move PASS\n");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"remote_move FAIL %s\n",e.what());return 1;}}
