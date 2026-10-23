#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include "grandmp/windows/observer_network.hpp"
#endif
#include "grandmp/core/remote_player_controller.hpp"
#include "grandmp/server/sessions.hpp"
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <atomic>
#include <type_traits>

using namespace grandmp;
using namespace std::chrono_literals;
using core::RemoteRoster;
using core::RemotePlayerController;
using core::RemotePlayerFrames;
void check(bool okay,const char* why) { if (!okay) throw std::runtime_error(why); }
core::OnFootState sample(float right=0,float forward=1) {
    core::OnFootState s{};s.x=100;s.y=200;s.z=30;s.controls={right,forward,core::PlayerControls::walk};return s;
}
void run(std::string_view which) {
    const auto t=RemoteRoster::Time{};
#ifdef _WIN32
    if (which=="sender") {
        WSADATA data{};check(WSAStartup(MAKEWORD(2,2),&data)==0,"winsock");
        struct Runtime { ~Runtime(){WSACleanup();} } runtime;
        const SOCKET socket=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
        check(socket!=INVALID_SOCKET,"socket");
        struct Socket { SOCKET value;~Socket(){closesocket(value);} } owner{socket};
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        check(bind(socket,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0,"bind ephemeral loopback");
        int size=sizeof(address);check(getsockname(socket,reinterpret_cast<sockaddr*>(&address),&size)==0,"port");
        DWORD timeout=1000;check(setsockopt(socket,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char*>(&timeout),sizeof(timeout))==0,"bounded receive");
        core::RemotePlayerController controller;windows::ObserverNetwork network;
        check(!network.sendOnFoot(sample()),"no sender without explicit controller/session");
        network.setRemotePlayerController(&controller);
        check(network.start(ntohs(address.sin_port)) && network.beginRemoteScenePreview(),"sender starts");
        network.tick(std::nullopt);
        sockaddr_in peer{};int peerSize=sizeof(peer);std::array<std::uint8_t,81> buffer{};
        int n=recvfrom(socket,reinterpret_cast<char*>(buffer.data()),81,0,reinterpret_cast<sockaddr*>(&peer),&peerSize);
        check(n>0,"hello sent");auto decoded=protocol::decode(std::span<const std::uint8_t>(buffer.data(),n));
        check(decoded.message && std::holds_alternative<protocol::Hello>(*decoded.message),"real hello");
        auto bytes=*protocol::encode(protocol::Welcome{std::get<protocol::Hello>(*decoded.message).nonce,42});
        check(sendto(socket,reinterpret_cast<char*>(bytes.data()),static_cast<int>(bytes.size()),0,reinterpret_cast<sockaddr*>(&peer),sizeof(peer))>0,"welcome");
        network.tick(std::nullopt);
        check(network.connected() && network.sendOnFoot(sample(1,0)),"supplied captured values sent by ObserverNetwork");
        bool found=false;
        for (unsigned i=0;i<4 && !found;++i) {
            peerSize=sizeof(peer);n=recvfrom(socket,reinterpret_cast<char*>(buffer.data()),81,0,reinterpret_cast<sockaddr*>(&peer),&peerSize);
            check(n>0,"bounded packet receive");decoded=protocol::decode(std::span<const std::uint8_t>(buffer.data(),n));
            if (decoded.message) if (const auto* foot=std::get_if<protocol::PlayerOnFoot>(&*decoded.message))
                found=foot->session_id==42 && foot->sequence==1 && foot->state==sample(1,0);
        }
        check(found,"on-foot payload uses real sender identity/sequence");return;
    }
#endif
    if (which=="wire") {
        for (const protocol::Message& m : {protocol::Message{protocol::PlayerOnFoot{12,1,sample()}},
            protocol::Message{protocol::RemoteOnFoot{42,12,1,sample(1,0)}},protocol::Message{protocol::OnFootSubscribe{42}}}) {
            auto b=protocol::encode(m);check(b.has_value(),"encode extension");
            auto d=protocol::decode(*b);check(d.message && *d.message==m,"extension exact roundtrip");
            b->push_back(0);check(!protocol::decode(*b).message,"no trailing bytes");
        }
        auto foot=sample();
        check(protocol::encode(protocol::PlayerOnFoot{12,1,foot})->size()==62,"client fixed wire size");
        check(protocol::encode(protocol::RemoteOnFoot{42,12,1,foot})->size()==70,"recipient-bound fixed wire size");
        foot.controls.forward=1.01f;check(!protocol::encode(protocol::PlayerOnFoot{12,1,foot}),"axis range");
        foot=sample();foot.vx=std::numeric_limits<float>::infinity();check(!protocol::encode(protocol::PlayerOnFoot{12,1,foot}),"nonfinite reference");
        foot=sample();foot.controls.buttons=2;check(!protocol::encode(protocol::PlayerOnFoot{12,1,foot}),"reserved buttons");
        check(!protocol::encode(protocol::RemoteOnFoot{12,12,1,sample()}),"no self identity");
        auto b=*protocol::encode(protocol::PlayerOnFoot{12,1,sample()});b[20]=0x7f;b[21]=0xc0;
        check(protocol::decode(b).status==protocol::DecodeStatus::invalid_field,"NaN wire rejected");
        return;
    }
    if (which=="server") {
        server::SessionTable table(12);server::Endpoint a{{127,0,0,1},40001},b{{127,0,0,1},40002};
        (void)table.handle(protocol::Hello{1},a,t);(void)table.handle(protocol::Hello{2},b,t);
        check(!table.onFootSubscribed(13),"extension not sent to legacy peer");
        check(table.handle(protocol::OnFootSubscribe{13},a,t).kind==server::EventKind::rejected_endpoint,"subscribe endpoint");
        check(table.handle(protocol::OnFootSubscribe{13},b,t).kind==server::EventKind::on_foot_subscribed && table.onFootSubscribed(13),"explicit opt-in");
        check(table.handle(protocol::PlayerOnFoot{12,1,sample()},b,t).kind==server::EventKind::rejected_endpoint,"input spoof rejected");
        auto e=table.handle(protocol::PlayerOnFoot{12,1,sample()},a,t);
        check(e.kind==server::EventKind::on_foot_accepted && e.position && e.onFoot,"same accepted state also yields legacy XYZ");
        check(table.peersExcept(13).front().onFoot.has_value(),"snapshot retains kinematics/controls");
        check(table.recentOnFoot(12,t+499ms).has_value() && !table.recentOnFoot(12,t+500ms) &&
            !table.recentOnFoot(12,t-1ms),"snapshot cannot revive expired controls for a newly subscribed observer");
        check(table.handle(protocol::PlayerOnFoot{12,1,sample()},a,t+200ms).kind==server::EventKind::rejected_sequence,"duplicate input rejected");
        check(table.handle(protocol::PlayerPosition{12,1,1,2,3},a,t+200ms).kind==server::EventKind::rejected_sequence,"shared legacy/input ordering");
        check(table.handle(protocol::PlayerOnFoot{12,2,sample()},a,t+50ms).kind==server::EventKind::rejected_rate,"existing rate guard reused");
        (void)table.handle(protocol::PlayerPosition{12,2,1,2,3},a,t+200ms);
        check(!table.peersExcept(13).front().onFoot,"legacy snapshot cannot prolong old input");
        (void)table.handle(protocol::Disconnect{12},a,t+300ms);
        check(table.handle(protocol::PlayerOnFoot{12,3,sample()},a,t+400ms).kind==server::EventKind::rejected_unknown_session,"no input after leave");return;
    }
    RemoteRoster roster;RemotePlayerController c;c.beginSession(42);
    auto receive=[&](std::uint32_t seq,core::OnFootState state,RemoteRoster::Time now) {
        (void)roster.position({12,seq,state.x,state.y,state.z},now);
        const bool accepted=c.receive({42,12,seq,state},roster,now);c.synchronize(roster,now);return accepted;
    };
    RemotePlayerFrames out;
    if (which=="end_reasons") {
        check(receive(1,sample(),t),"initial input");check(c.tryRead(t,out),"snapshot");const auto key=out.players[0].key;
        check(c.tryRead(t+500ms,out) && out.count==1 && !out.endedCount &&
            out.players[0].freshness==core::ControlFreshness::input_timeout,"input expires without leaving");
        check(roster.left({12},t+600ms),"server leave accepted");
        c.endRemote(12,core::RemoteEndReason::server_leave);c.synchronize(roster,t+600ms);
        check(c.tryRead(t+600ms,out) && !out.count && out.endedCount==1 && out.ended[0].key==key &&
            out.ended[0].reason==core::RemoteEndReason::server_leave,"exact receiver evidence preserved across Scene despawn");
        c.beginSession(43);roster.clear();(void)roster.position({13,1,100,200,30},t+1s);
        check(c.receive({43,13,1,sample()},roster,t+1s),"new session");c.synchronize(roster,t+1s);
        c.beginSession(0,core::RemoteEndReason::session_timeout);
        check(c.tryRead(t+2s,out) && !out.count && out.endedCount==2 && out.ended[1].key.generation>key.generation &&
            out.ended[1].reason==core::RemoteEndReason::session_timeout,"session timeout distinct, correlated by generation");return;
    }
    if (which=="continuous") {
        check(receive(1,sample(),t),"walk input");check(c.tryRead(t,out) && out.count==1,"scene admission");
        const auto key=out.players[0].key;
        check(receive(2,sample(1,0),t+200ms),"direction changes with same reference XYZ");
        check(c.tryRead(t+200ms,out) && out.players[0].reference.controls.right==1 && out.players[0].key==key,"same remote/generation, no recreation");
        auto stop=sample(0,0);stop.controls.buttons=0;check(receive(3,stop,t+400ms),"stop input accepted");
        check(c.tryRead(t+400ms,out) && out.players[0].reference.controls==core::PlayerControls{} && out.players[0].key==key,"walk/turn/stop stable identity");
        auto moved=stop;moved.x+=2;check(receive(4,moved,t+600ms),"reference snapshot retained");
        c.synchronize(roster,t+650ms);
        check(c.tryRead(t+650ms,out) && out.players[0].reference.x==102 && out.players[0].presentation.x<102,"Scene interpolates pose, not controls");
        return;
    }
    check(receive(1,sample(),t),"initial state");
    if (which=="stale") {
        check(!receive(1,sample(1,0),t+450ms),"duplicate cannot refresh input lifetime");
        check(c.tryRead(t+500ms,out) && out.players[0].freshness==core::ControlFreshness::input_timeout && out.players[0].reference.controls==core::PlayerControls{},"loss stops controls without pose/velocity write");
        check(!c.receive({99,12,1,sample()},roster,t+500ms),"previous receiver session rejected");
        check(!receive(0,sample(),t+500ms),"old sequence");
        check(receive(2,sample(1,0),t+600ms),"new input resumes same entity");
        check(c.tryRead(t+600ms,out) && out.players[0].freshness==core::ControlFreshness::fresh,"fresh again");return;
    }
    if (which=="lifecycle") {
        check(c.tryRead(t,out),"snapshot");const auto key=out.players[0].key;
        check(roster.left({12},t+1ms),"leave");c.synchronize(roster,t+1ms);
        check(c.tryRead(t+1ms,out) && !out.count,"leave removes pending controls");
        check(!receive(2,sample(),t+2ms),"retired ID cannot command");
        c.beginSession(43);roster.clear();
        (void)roster.position({12,1,100,200,30},t+3ms);
        check(!c.receive({42,12,1,sample()},roster,t+3ms),"stale receiver bound packet");
        check(c.receive({43,12,1,sample()},roster,t+3ms),"new session");c.synchronize(roster,t+3ms);
        check(c.tryRead(t+3ms,out) && out.players[0].key.generation>key.generation,"new lifetime generation never reset");return;
    }
    check(which=="thread","known case");
    static_assert(std::is_trivially_copyable_v<RemotePlayerFrames>);
    std::atomic<bool> done=false;std::atomic<bool> valid=true;
    std::thread consumer([&] {
        RemotePlayerFrames copy;
        while (!done.load()) if (c.tryRead(t,copy)) for (std::size_t i=0;i<copy.count;++i)
            if (copy.players[i].key.player!=12 || !copy.players[i].key.generation) valid=false;
    });
    for (unsigned i=2;i<100;++i) check(receive(i,sample(i%2 ? 1.f : 0.f,0),t),"coalesced input");
    done=true;consumer.join();check(valid && c.tryRead(t,out) && out.count==1 && out.players[0].sequence==99,"bounded latest copied state; no native operations");
}
int main(int argc,char** argv) {
    try { if (argc!=2) return 2;run(argv[1]);return 0; }
    catch (const std::exception& e) { std::fprintf(stderr,"%s\n",e.what());return 1; }
}
