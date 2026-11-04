#include "grandmp/windows/minimal_player_lifecycle.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>
namespace m=grandmp::windows::remote_player_binding::minimal_lifecycle;
void check(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
struct Operations {
    m::Tombstone& t;int failure=-1,throws=-1,calls=0;bool destroyed=false,ready=true,stationary=true;
    m::DispatchBarrier barrier;
    bool preflight(){check(!barrier.closed(),"barrier open throughout preflight");return failure!=0 && ready && stationary;}
    void call(int n,m::Boundary boundary){
        check(t.entered==boundary,"boundary recorded before call");++calls;
        if(throws==n)throw std::runtime_error("injected irreversible uncertainty");
    }
    void cleanReference(){barrier.close();call(1,m::Boundary::clean_reference);}
    bool referenceUnlinked(){check(!destroyed,"PED still live");return failure!=1;}
    void removeWorld(){call(2,m::Boundary::world_remove);}
    bool worldDetached(){check(!destroyed,"verify before destructor");return failure!=2;}
    void destroyPed(){call(3,m::Boundary::deleting_destructor);destroyed=true;}
    bool pedDestroyed(){check(destroyed,"metadata only after destruction");return failure!=3;}
    void destroyData(){check(destroyed,"PED destroyed before private data");call(4,m::Boundary::data_destructor);}
    bool resourcesReleased(){check(destroyed,"metadata only");return failure!=4;}
    bool verified(){return failure!=5;}
};
int main(int argc,char** argv){
    try {
        check(argc==2,"case required");const std::string_view which=argv[1];
        if(which.starts_with("armed_")){
            m::ArmedRemoval request;const grandmp::core::RemotePlayerKey key{17,1};
            m::Tombstone tombstone;Operations op{tombstone};
            if(which=="armed_after_window"){
                check(request.poll(true,true,key,m::Window::complete,7000)==m::Opportunity::attempt &&
                    !op.barrier.closed() && !op.calls,"late arm still only schedules a strict preflight");
                check(request.poll(true,true,key,m::Window::complete,7001)==m::Opportunity::wait,"never another opportunity");
                std::cout<<which<<": PASS (offline contract only)\n";return 0;
            }
            const bool beforeLive=which=="armed_before_live";
            check(request.poll(true,!beforeLive,key,m::Window::waiting,100)==
                (beforeLive ? m::Opportunity::refused:m::Opportunity::wait),"F8 only arms a live entity");
            check(!op.barrier.closed() && !op.calls,"arming never closes barrier or calls cleanup");
            if(beforeLive){
                check(request.poll(false,true,key,m::Window::complete,200)==m::Opportunity::wait &&
                    request.poll(true,true,key,m::Window::complete,300)==m::Opportunity::wait,"invalid first edge cannot arm later");
            }else if(which=="armed_missing" || which=="armed_inconclusive" || which=="armed_generation" || which=="armed_clock"){
                const auto now=which=="armed_missing" ? 9101u:which=="armed_clock" ? 99u:200u;
                const auto identity=which=="armed_generation" ? grandmp::core::RemotePlayerKey{17,2}:key;
                const auto window=which=="armed_inconclusive" ? m::Window::failed:m::Window::waiting;
                check(request.poll(true,true,identity,window,now)==m::Opportunity::refused,"timeout/failure/identity/clock terminal");
                check(request.poll(false,true,key,m::Window::complete,10000)==m::Opportunity::wait && !op.calls && !op.barrier.closed(),"no postponed attempt");
            }else {
                check(request.poll(true,true,key,m::Window::waiting,3000)==m::Opportunity::wait &&
                    request.state()==m::Scheduled::pending && !op.barrier.closed(),"early held F8 waits without suppressing native processing");
                if(which=="armed_early"){
                    check(!op.calls && request.state()==m::Scheduled::pending,"movement can continue, no native cleanup");
                    std::cout<<which<<": PASS (offline contract only)\n";return 0;
                }
                check(request.poll(which=="armed_hold",true,key,m::Window::complete,6100)==m::Opportunity::attempt,"first valid completion consumes one opportunity");
                if(which=="armed_readiness")op.ready=false;
                if(which=="armed_residual")op.stationary=false;
                if(which=="armed_partial")op.failure=2;
                const bool okay=m::removeOnce(tombstone,key,op);
                const bool refused=which=="armed_readiness" || which=="armed_residual";
                check(okay==(!refused && which!="armed_partial"),"completion alone cannot bypass preflight or partial failure");
                if(refused)check(!op.calls && !op.barrier.closed() && tombstone.failure==m::Failure::admission,"strict refusal before closing barrier");
                else check(op.barrier.closed(),"barrier closed before first irreversible call");
                if(which=="armed_partial")check(op.calls==2 && !op.destroyed,"failed world proof cannot reach destructor");
                if(!okay){request.cancel();check(request.state()==m::Scheduled::terminal,"failed opportunity terminal");}
                const auto calls=op.calls;
                check(request.poll(true,true,key,m::Window::complete,6200)==m::Opportunity::wait &&
                    request.poll(false,true,key,m::Window::complete,6300)==m::Opportunity::wait &&
                    request.poll(true,true,key,m::Window::complete,6400)==m::Opportunity::wait,"held and repeated F8 cannot reschedule");
                check(!m::removeOnce(tombstone,key,op) && calls==op.calls,"no repeated irreversible call");
                if(which=="armed_late"){
                    std::thread late([&]{if(!op.barrier.closed())++op.calls;});late.join();
                    check(op.calls==calls,"late callback sees resident barrier before entity access");
                }
            }
        }else if(which=="edge"){
            m::Edge edge;check(!edge.poll(false) && edge.poll(true),"single rising edge");
            check(!edge.poll(true) && !edge.poll(false) && !edge.poll(true),"held/released/repressed never retries");
        }else if(which=="barrier"){
            m::DispatchBarrier barrier;check(!barrier.closed(),"initial open");
            barrier.close();unsigned calls=0;
            std::thread late([&]{if(!barrier.closed())++calls;});late.join();
            check(!calls && barrier.closed(),"late callback stops before native/object access");
        }else {
            m::Tombstone t;Operations op{t};
            if(which.starts_with("fail"))op.failure=which.back()-'0';
            if(which.starts_with("throw"))op.throws=which.back()-'0';
            bool result=false,caught=false;
            try {result=m::removeOnce(t,{17,1},op);}catch(...){caught=true;}
            t.finishUnknown();
            const bool success=which=="success";
            check(result==success,"only full transaction succeeds");
            check(caught==(op.throws>=0),"uncertain native exception not swallowed");
            check(t.key==grandmp::core::RemotePlayerKey{17,1} && t.requested,"logical tombstone retained");
            if(success)check(t.confirmed==m::State::removed_verified && op.calls==4 && t.failure==m::Failure::none,"four unique calls");
            else check(t.failure!=m::Failure::none,"failed transaction terminal");
            if(op.failure>=0)check(op.calls==(op.failure<4 ? op.failure:4),"first failed proof stops subsequent calls");
            if(op.throws>=0)check(op.calls==op.throws && static_cast<int>(t.entered)==op.throws,"uncertain boundary retained");
            const auto calls=op.calls;const auto failed=t.failure;const auto state=t.confirmed;
            check(!m::removeOnce(t,{17,2},op) && op.calls==calls && t.failure==failed && t.confirmed==state,"double request no mutation or retry");
        }
        std::cout<<which<<": PASS (offline contract only; no Hoodlum execution)\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
