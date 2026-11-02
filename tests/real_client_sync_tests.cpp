#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "grandmp/core/protocol.hpp"
#include "../client/windows/tools/local_player_capture.hpp"
#include "grandmp/windows/observer_network.hpp"
#include <cstdio>
#include <stdexcept>
#include <string_view>
#include <string>
#include <thread>
#include <limits>

using namespace grandmp;
namespace np=windows::remote_player_binding;
void check(bool okay,const char* reason) { if (!okay) throw std::runtime_error(reason); }
core::OnFootState state(float x=100,float forward=1,float right=0) {
    core::OnFootState s{};s.x=x;s.y=200;s.z=30;s.controls={right,forward,1};return s;
}
struct TestSocket {
    SOCKET value=INVALID_SOCKET;
    ~TestSocket(){if(value!=INVALID_SOCKET)closesocket(value);}
    sockaddr_in address{};
    void bindAt(const char* ip,unsigned short port=0) {
        value=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);check(value!=INVALID_SOCKET,"fixture socket");
        address.sin_family=AF_INET;address.sin_port=htons(port);
        check(inet_pton(AF_INET,ip,&address.sin_addr)==1,"fixture IP");
        check(bind(value,reinterpret_cast<const sockaddr*>(&address),sizeof(address))==0,"fixture bind");
        int size=sizeof(address);check(getsockname(value,reinterpret_cast<sockaddr*>(&address),&size)==0,"fixture port");
        DWORD timeout=1000;check(setsockopt(value,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout))==0,"fixture timeout");
    }
};
void run(std::string_view which,unsigned port) {
    if(which=="endpoint") {
        core::Ipv4Endpoint endpoint{};std::uint16_t legacy=7777;
        check(windows::parseLanEndpoint(L"192.168.1.10:7777",endpoint) && endpoint.address==core::Ipv4Address{192,168,1,10},"explicit LAN endpoint");
        check(!windows::parseLoopbackEndpoint(L"192.168.1.10:7777",legacy) && legacy==7777,"Alpha 0.8 parser stays loopback");
        check(!windows::parseLanEndpoint(L"192.168.1.10:\uFF17",endpoint),"non-ASCII refused");
        windows::ObserverNetwork network;check(!network.start(core::Ipv4Endpoint{{8,8,8,8},7777}),"public server refused before Winsock");return;
    }
    if(which=="source") {
        WSADATA data{};check(WSAStartup(MAKEWORD(2,2),&data)==0,"fixture Winsock");
        struct Cleanup {~Cleanup(){WSACleanup();}} cleanup;
        TestSocket server,wrongIp,wrongPort;server.bindAt("127.0.0.2");
        wrongIp.bindAt("127.0.0.1",ntohs(server.address.sin_port));wrongPort.bindAt("127.0.0.2");
        windows::ObserverNetwork network;
        check(network.start(core::Ipv4Endpoint{{127,0,0,2},ntohs(server.address.sin_port)}) && network.beginRemoteScenePreview(),"configured receiver");
        network.tick(std::nullopt);
        std::array<std::uint8_t,protocol::kMaxPacketSize> bytes{};sockaddr_in client{};int size=sizeof(client);
        const int received=recvfrom(server.value,reinterpret_cast<char*>(bytes.data()),static_cast<int>(bytes.size()),0,reinterpret_cast<sockaddr*>(&client),&size);
        check(received>0,"real Hello to configured address");
        const auto decoded=protocol::decode(std::span<const std::uint8_t>(bytes.data(),received));
        check(decoded.message && std::holds_alternative<protocol::Hello>(*decoded.message),"Hello format");
        const auto packet=protocol::encode(protocol::Welcome{std::get<protocol::Hello>(*decoded.message).nonce,123});
        check(packet.has_value(),"Welcome format");
        const auto send=[&](SOCKET socket) {check(sendto(socket,reinterpret_cast<const char*>(packet->data()),static_cast<int>(packet->size()),0,reinterpret_cast<const sockaddr*>(&client),sizeof(client))==static_cast<int>(packet->size()),"fixture send");};
        send(wrongIp.value);send(wrongPort.value);
        for(unsigned i=0;i<10;++i){network.tick(std::nullopt);Sleep(5);check(!network.connected(),"wrong IP/port never admitted");}
        send(server.value);
        for(unsigned i=0;i<100 && !network.connected();++i){network.tick(std::nullopt);Sleep(5);}
        check(network.connected() && network.sessionId()==123,"exact endpoint admitted");return;
    }
    if (which=="pad") {
        np::OnFootPadStates pad{};core::PlayerControls collected{};
        for (const auto input : {core::PlayerControls{0,1,1},core::PlayerControls{1,0,1},core::PlayerControls{}}) {
            check(np::prepareOnFootPad({},input,pad),"existing replay conversion");const auto before=pad;
            check(np::collectOnFootPad(pad.current,collected) && collected==input,"capture/replay walk/turn/stop roundtrip");
            check(std::memcmp(&pad,&before,sizeof(pad))==0,"collector never changes pad bytes");
        }
        std::int16_t axis=128,walk=255;
        std::memcpy(pad.current.data(),&axis,2);std::memcpy(pad.current.data()+2,&axis,2);
        std::memcpy(pad.current.data()+0x2A,&walk,2);
        check(np::collectOnFootPad(pad.current,collected) && std::abs(std::hypot(collected.right,collected.forward)-1)<0.001f,"diagonal bounded");
        walk=0;std::memcpy(pad.current.data()+0x2A,&walk,2);
        check(np::collectOnFootPad(pad.current,collected) && collected==core::PlayerControls{},"unsupported run is neutral");return;
    }
    if (which=="capture") {
        np::LocalKinematics capture;core::OnFootState s;
        check(!capture.sample({100,200,30},{0,1,0},0,1000,s),"warmup does not invent initial velocity");
        check(capture.sample({100,200.2f,30},{-1,0,0},0,1200,s),"valid measured snapshot");
        check(std::abs(s.vy-1)<0.001f && std::abs(s.heading-1.5707963f)<0.001f,"world units/second and orientation from forward basis");
        check(!capture.sample({100,200,30},{0,1,0},0,1200,s),"zero time rejected");
        check(!capture.sample({100,200,30},{0,1,0},0,2000,s),"pause/gap requires warmup");
        check(!capture.sample({NAN,200,30},{0,1,0},0,2200,s),"nonfinite values rejected");
        check(!capture.sample({100,200,30},{0,0,0},0,2400,s),"no degenerate heading");return;
    }
    if (which=="mailbox") {
        core::LocalPlayerSnapshots box;core::LocalPlayerSnapshots::Snapshot copy{};
        check(!box.publish({1,box.nextRevision(),1000,state()}),"no sample before authenticated session");
        box.session(42);check(box.publish({42,box.nextRevision(),1000,state()}),"publish");
        check(box.take(1000,copy) && copy.session==42,"consume copy");
        check(!box.take(1200,copy),"same revision never resent");
        check(box.publish({42,box.nextRevision(),1100,state(101)}),"next");
        check(!box.take(1100,copy),"5Hz bound");
        box.invalidate();check(!box.take(1200,copy),"unavailable gameplay invalidates pending sample");
        check(box.publish({42,box.nextRevision(),1300,state(102)}),"latest");
        box.session(43);check(!box.take(1300,copy),"old session cannot replay capture");
        check(!box.publish({42,box.nextRevision(),1300,state()}),"in-flight old-session producer rejected");
        check(box.publish({43,box.nextRevision(),1400,state()}),"new session sample");
        check(!box.take(1399,copy) && !box.take(1600,copy),"clock regression and stale capture rejected");
        check(box.publish({43,box.nextRevision(),1700,state()}),"fresh after stale");
        check(box.publish({43,box.nextRevision(),1710,state(103)}),"coalescing");
        check(box.take(1710,copy) && copy.state.x==103,"bounded latest only");return;
    }
    if (which=="contention") {
        core::LocalPlayerSnapshots box;box.session(42);std::atomic<bool> done=false,bad=false;
        std::thread consumer([&] { core::LocalPlayerSnapshots::Snapshot copy{};
            for (std::uint64_t now=1000;!done.load();now+=200) if (box.take(now,copy))
                if (copy.state.x!=copy.state.y || copy.session!=42) bad=true;
        });
        for (unsigned n=0;n<10000;++n) { auto s=state();s.x=s.y=static_cast<float>(n);
            (void)box.publish({42,box.nextRevision(),1000,s}); }
        done=true;consumer.join();check(!bad,"immutable coherent copies under producer/consumer contention");return;
    }
    check((which=="duplex" || which=="duplex_lan") && port>0 && port<65536,"known case/port");
    // Real server, two real ObserverNetwork instances and the ACTUAL worker seam.
    // Supplied capture values only: no GTA, native entity backend or DLL loading.
    core::RemotePlayerController ca,cb;windows::ObserverNetwork a,b;
    core::LocalPlayerSnapshots qa,qb;
    const char* stage="initial connection";
    a.setRemotePlayerController(&ca);b.setRemotePlayerController(&cb);
    const auto connect=[&](windows::ObserverNetwork& network) {
        return which=="duplex_lan" ? network.start(core::Ipv4Endpoint{{127,0,0,2},static_cast<std::uint16_t>(port)}) : network.start(static_cast<std::uint16_t>(port));
    };
    check(connect(a) && a.beginRemoteScenePreview(),"client A");
    check(connect(b) && b.beginRemoteScenePreview(),"client B");
    auto tick=[&] { check(a.tickLocal(qa) && b.tickLocal(qb),"duplex worker send"); };
    auto until=[&](auto predicate,unsigned timeout=2500) { const auto deadline=GetTickCount64()+timeout;
        do { tick();if (predicate()) return;Sleep(10); } while (GetTickCount64()<deadline);
        throw std::runtime_error(std::string("duplex deadline: ")+stage);
    };
    until([&]{return a.connected() && b.connected();});const auto aid=a.sessionId(),bid=b.sessionId();
    check(aid!=bid,"independent sessions");
    core::RemotePlayerFrames fa{},fb{};core::RemotePlayerKey ka{},kb{};
    for (unsigned phase=0;phase<3;++phase) {
        stage="walk/turn/stop delivery";
        auto sa=state(100,phase==0 ? 1.f : 0.f,phase==1 ? 1.f : 0.f);
        auto sb=state(102,phase==1 ? 1.f : 0.f,phase==0 ? -1.f : 0.f);
        if (phase==2) sa.controls=sb.controls={};
        np::OnFootPadStates capturedA{},capturedB{};
        check(np::prepareOnFootPad({},sa.controls,capturedA) && np::prepareOnFootPad({},sb.controls,capturedB),"test-owned local pad images");
        check(np::collectOnFootPad(capturedA.current,sa.controls) && np::collectOnFootPad(capturedB.current,sb.controls),"actual read-only capture codec feeds UDP mailbox");
        const auto now=GetTickCount64();
        check(qa.publish({aid,qa.nextRevision(),now,sa}) && qb.publish({bid,qb.nextRevision(),now,sb}),"publish distinct local inputs");
        until([&]{ const auto time=core::RemoteRoster::Clock::now();
            if (!ca.tryRead(time,fa) || !cb.tryRead(time,fb) || fa.count!=1 || fb.count!=1) return false;
            check(fa.players[0].key.player==bid && fb.players[0].key.player==aid,"no self echo");
            return fa.players[0].reference==sb && fb.players[0].reference==sa &&
                fa.players[0].freshness==core::ControlFreshness::fresh && fb.players[0].freshness==core::ControlFreshness::fresh;
        });
        if (!phase) { ka=fa.players[0].key;kb=fb.players[0].key; }
        check(fa.players[0].key==ka && fb.players[0].key==kb,"persistent identity/generation through walk/turn/stop");
        np::OnFootPadStates replayA{},replayB{};
        check(np::prepareOnFootPad({},fa.players[0].reference.controls,replayA) && np::prepareOnFootPad({},fb.players[0].reference.controls,replayB),"received Controller frames feed actual replay codec");
        check(replayA.current==capturedB.current && replayB.current==capturedA.current,"opposite real session input survives capture/wire/replay conversion");
        Sleep(210);
    }
    stage="input expiry";
    until([&]{return cb.tryRead(core::RemoteRoster::Clock::now(),fb) && fb.count==1 &&
        fb.players[0].freshness==core::ControlFreshness::input_timeout;});
    qa.session(0);a.stop();
    // B must observe leave without A reconnecting inside tick().
    const auto deadline=GetTickCount64()+2000;
    do { check(b.tickLocal(qb),"B receive leave");
        if (cb.tryRead(core::RemoteRoster::Clock::now(),fb) && !fb.count) break;Sleep(10);
    } while (GetTickCount64()<deadline);
    check(!fb.count,"disconnect removes remote logical representation");
    a.setRemotePlayerController(&ca);check(connect(a) && a.beginRemoteScenePreview(),"new session");
    stage="reconnect";
    until([&]{return a.connected() && a.sessionId()!=aid;});
    tick();core::LocalPlayerSnapshots::Snapshot discarded{};
    check(!qa.take(GetTickCount64(),discarded),"reconnect cannot replay an old-session capture");
    const auto now=GetTickCount64();const auto resumed=state(103,0,1);
    check(qa.publish({a.sessionId(),qa.nextRevision(),now,resumed}),"fresh new-session local frame");
    stage="fresh reconnected input";
    until([&]{return cb.tryRead(core::RemoteRoster::Clock::now(),fb) && fb.count==1 &&
        fb.players[0].key.player==a.sessionId() && fb.players[0].sequence>0 && fb.players[0].reference==resumed;});
    check(fb.players[0].key.generation>kb.generation,"reconnect gets a new lifetime, never an old native record");
    b.stop();a.stop();std::puts("duplex_input=PASS no_GTA no_native_calls");
}
int main(int argc,char** argv) {
    try { if (argc<2) return 2;run(argv[1],argc==3 ? static_cast<unsigned>(std::stoul(argv[2])) : 0);return 0; }
    catch (const std::exception& e) { std::fprintf(stderr,"%s\n",e.what());return 1; }
}
