#include "../client/windows/tools/native_second_player.hpp"
#include "../client/windows/tools/native_create_player.hpp"
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string_view>

using namespace grandmp;
namespace rb=windows::remote_player_binding;
namespace sp=rb::second_player;
using namespace std::chrono_literals;
void check(bool okay,const char* message) { if (!okay) throw std::runtime_error(message); }

// Sparse simulated process memory, NOT a GTA host or proof of native construction.
struct Memory final : sp::Access {
    std::map<std::uint32_t,std::byte> bytes;
    bool admitted=true,readyFlag=true,enabled=true,construction=true,setupReturn=true;
    bool finishReturn=true,controlOnly=false;
    unsigned finishes=0,finishRemovals=0;
    bool finishRemoval(const rb::Record&) override { ++finishRemovals;return true; }
    bool finishPlayerOne(const rb::Record&) override { ++finishes;return finishReturn; }
    bool emptyResult=false,badType=false,localMutation=false,registerFailure=false,cleanFailure=false;
    bool keepSlot=false,keepPool=false,destroyed=false,removeReturn=true;
    bool occupyDuringPreflight=false,replaceDuringCleanup=false;
    unsigned slotReads=0;
    std::uint32_t partialAt=0;
    unsigned reads=0,setups=0,registers=0,cleans=0,removes=0,deadReads=0;
    static constexpr std::uint32_t objects=0x100000,flags=0x200000,pool=0x210000;
    static constexpr std::uint32_t cj=objects,ped=objects+sp::poolStride,node=0x220000,referenceNode=0x230000;
    template<class T> void put(std::uint32_t at,const T& value) {
        const auto* input=reinterpret_cast<const std::byte*>(&value);
        for (std::size_t n=0;n<sizeof(T);++n) bytes[at+static_cast<unsigned>(n)]=input[n];
    }
    Memory() {
        put(sp::slot0,std::array<std::byte,sp::infoStride>{});
        put(sp::pad0,std::array<std::byte,sp::padStride*2>{});
        put(sp::ps::playerIndex,std::uint8_t{0});
        put(sp::ps::streamingTable+0x10,std::uint8_t{1});
        put(sp::slot0,cj);put(sp::slot1,std::uint32_t{0});
        put(sp::ps::pedPool,pool);put(pool,sp::Pool{objects,flags,4,0});
        put(flags,std::array<std::uint8_t,4>{1,0x82,0x80,0x80});
        put(cj,sp::ps::playerVtable);put(cj+sp::playerData,sp::slot0+4);put(cj+sp::pedType,std::uint32_t{0});
    }
    bool context() const noexcept override { return admitted; }
    bool controlContext() const noexcept override { return controlOnly; }
    bool ready() const noexcept override { return readyFlag; }
    bool executorEnabled() const noexcept override { return enabled; }
    bool constructionProven() const noexcept override { return construction; }
    sp::ps::MemoryRead read(std::uint32_t at,void* output,std::size_t size) noexcept override {
        ++reads;
        if (destroyed && at>=ped && at<ped+sp::poolStride) { ++deadReads;return {false,0,ERROR_NOACCESS}; }
        auto* destination=static_cast<std::byte*>(output);
        for (std::size_t n=0;n<size;++n) {
            auto i=bytes.find(at+static_cast<unsigned>(n));
            if (i==bytes.end()) return {false,n,ERROR_NOACCESS};
            destination[n]=i->second;
        }
        if (at==sp::slot1 && ++slotReads==2 && occupyDuringPreflight) put(sp::slot1,std::uint32_t{0x777777});
        return {true,at==partialAt ? size-1 : size,ERROR_SUCCESS};
    }
    bool createPlayerOne() override {
        ++setups;destroyed=false;
        if (emptyResult) return setupReturn;
        const auto tag=static_cast<std::uint8_t>((std::to_integer<unsigned>(bytes[flags+1])+1)&0x7f);
        put(sp::slot1,ped);put(flags+1,tag);
        put(pool,sp::Pool{objects,flags,4,1});
        put(ped,sp::ps::playerVtable);put(ped+sp::playerData,sp::data1);
        put(ped+sp::pedType,std::uint32_t{badType ? 0u : 1u});put(ped+sp::vehicle,std::uint32_t{0});
        put(ped+sp::movingLink,node);put(node,ped);put(ped+0x24,std::uint32_t{0});
        if (localMutation) put(sp::slot0+0x120,std::uint32_t{9});
        return setupReturn;
    }
    bool registerCell(std::uint32_t entity,std::uint32_t* cell) override {
        ++registers;check(entity==ped && *cell==ped,"register only correlated remote");
        if (registerFailure) return false;
        put(ped+0x24,referenceNode);put(referenceNode,std::uint32_t{0});
        put(referenceNode+4,static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(cell)));return true;
    }
    bool cleanCell(std::uint32_t entity,std::uint32_t* cell) override {
        ++cleans;check(entity==ped && *cell==ped,"cleanup only owned remote");
        if (cleanFailure) return false;
        put(ped+0x24,std::uint32_t{0});
        if (replaceDuringCleanup) put(sp::slot1,std::uint32_t{0x777777});
        return true;
    }
    bool removeOne() override {
        ++removes;
        if (!removeReturn) return false;
        destroyed=true;
        if (!keepSlot) put(sp::slot1,std::uint32_t{0});
        if (!keepPool) put(flags+1,static_cast<std::uint8_t>(std::to_integer<unsigned>(bytes[flags+1])|0x80));
        return true;
    }
};

struct Fixture {
    Memory memory;
    sp::Api api{memory,std::this_thread::get_id()};
    rb::Record record{{12,1},{},0};
    void create() {
        check(api.factoryProven(),"simulation preflight");
        check(api.create(record.key,record)==rb::Creation::complete && api.authenticate(record),"simulation correlation");
    }
};
struct SpawnProbe {
    unsigned players=0,grounds=0,spheres=0;
    bool hit=false,found=true;
    bool player(sp::ps::Position& p,sp::ps::Position& right) {
        ++players;p={100,200,30};right={0,1,0};return true;
    }
    float ground(float,float,float,bool& okay) { ++grounds;okay=found;return 29.f; }
    bool sphere(sp::ps::Position p) {
        ++spheres;check(p.x==100 && p.y==202.5f && p.z==30,"existing NPC centre probe unchanged");return hit;
    }
    template<class... Args> void trace(const char*,Args...) {}
};
std::uint32_t commandWord(const sp::create_player::Program& p,std::size_t at) {
    return unsigned(p[at]) | (unsigned(p[at+1])<<8) | (unsigned(p[at+2])<<16) | (unsigned(p[at+3])<<24);
}
void run(std::string_view which) {
    if (which=="command_encoding") {
        sp::create_player::Program p{};
        check(sp::create_player::encodeGround({100,200,29},p),"finite native parameters");
        const sp::create_player::Program expected{
            0x53,0, 1,1,0,0,0, 6,0,0,0xC8,0x42, 6,0,0,0x48,0x43,
            6,0,0,0xE8,0x41, 3,0,0, 0x4E,0};
        check(p==expected && sp::create_player::terminateOffset==25,"exact Hoodlum operand tags and local-index output, not a handle");
        check(!sp::create_player::encodeGround({0,0,-100},p),"reject alternate ground-query branch");
        check(!sp::create_player::encodeGround({NAN,0,0},p),"reject NaN");
        check(!sp::create_player::encodeGround({0,INFINITY,0},p),"reject infinity");
        return;
    }
    if (which=="command_position" || which=="command_refused") {
        SpawnProbe probe;sp::create_player::Program p{};
        if (which=="command_refused") {
            probe.hit=true;
            check(!sp::create_player::prepare(probe,p) && probe.players==1 && probe.grounds==1 && probe.spheres==1,
                "collision refuses; no alternate candidate or retry");
            check(p==sp::create_player::Program{},"no stale command on failed preparation");return;
        }
        check(sp::create_player::prepare(probe,p) && probe.players==1 && probe.grounds==1 && probe.spheres==1,
            "single existing spawn probe, no extra ground/collision queries");
        check(std::bit_cast<float>(commandWord(p,8))==100 && std::bit_cast<float>(commandWord(p,13))==202.5f &&
            std::bit_cast<float>(commandWord(p,18))==29,"CREATE_PLAYER gets actual ground Z, not centre Z plus another native offset");return;
    }
    if (which=="windows_closed") {
        sp::WindowsAccess access({});sp::Api api(access,std::this_thread::get_id());
        check(!access.executorEnabled() && !access.constructionProven() && !api.factoryProven(),"real executor stays closed");
        check(!access.createPlayerOne() && !access.removeOne(),"no native call without admission");
        std::uint32_t value=0;check(!access.read(0,&value,4).complete(4),"no reads outside context");return;
    }
    if (which=="group_initial" || which=="group_resources" || which=="group_members") {
        sp::factory::GroupImage image{};const auto base=sp::factory::groupBase+sp::factory::groupStride;
        auto put=[&](unsigned at,std::uint32_t value) { std::memcpy(image.data()+at,&value,4); };
        put(0x30,base);
        for (unsigned bank=0;bank<4;++bank) for (unsigned n=0;n<8;++n) put(0x30+0xC+bank*0xA0+n*0x14+8,UINT32_MAX);
        check(sp::factory::initialGroup(image,base),"initial resource-free group only");
        if (which=="group_initial") { put(0x30,base+4);check(!sp::factory::initialGroup(image,base),"allocator may not target divergent group"); }
        if (which=="group_resources") {
            for (unsigned at : {0x34u,0x38u,0x40u,0x2C0u,0x2C4u}) {
                put(at,0x123);check(!sp::factory::initialGroup(image,base),"unknown allocator/task resource refused before native cleanup");put(at,0);
            }
        }
        if (which=="group_members") {
            put(0xC,Memory::ped);check(sp::factory::membersOwned(image,base,Memory::ped),"only owned remote allowed before destruction");
            put(0x28,Memory::cj);check(!sp::factory::membersOwned(image,base,Memory::ped),"CJ/third party prevents indexed group destruction");
        }
        return;
    }
    if (which=="collision_volume") {
        sp::factory::CollisionSphere sphere{{.1f,.2f,-.5f},.35f,{}};
        check(sp::factory::sphereValid(sphere),"model volume remains unscaled");
        sphere.radius=0;check(!sp::factory::sphereValid(sphere),"unknown/empty collision volume refused");
        sphere.radius=.35f;sphere.centre.z=std::numeric_limits<float>::quiet_NaN();
        check(!sp::factory::sphereValid(sphere),"nonfinite model volume refused");return;
    }
    Fixture f;
    if (which=="finish_partial") {
        f.memory.finishReturn=false;
        check(f.api.create(f.record.key,f.record)==rb::Creation::indeterminate && f.memory.registers==1 && f.memory.finishes==1,
            "placement begins only after authenticated stable reference");
        check(f.api.construction()==sp::Construction::terminal_failure && f.api.authenticate(f.record),"partial owned record remains authenticated");
        check(!f.api.factoryProven() && f.api.create(f.record.key,f.record)==rb::Creation::absent && f.memory.setups==1,
            "no retry or second setup after failed finish");
        const auto before=f.record;
        check(f.api.remove(f.record)==rb::Removal::complete && f.api.removed(before,f.record) && !f.memory.deadReads,
            "explicit cleanup of correlated partial construction only");return;
    }
    if (which=="vacancy") { check(f.api.factoryProven() && !f.memory.setups,"two-slot preflight has no mutations");return; }
    if (which=="occupied") {
        f.memory.put(sp::slot1,std::uint32_t{0x777777});
        check(!f.api.factoryProven() && f.api.reason()==sp::Reason::occupied && !f.memory.setups,"unknown occupant never dereferenced/replaced");return;
    }
    if (which=="context" || which=="thread" || which=="closed") {
        if (which=="context") f.memory.admitted=false;
        if (which=="closed") f.memory.enabled=false;
        if (which=="thread") { std::thread other([&] {check(!f.api.factoryProven(),"wrong thread");});other.join(); }
        else check(!f.api.factoryProven(),"denied before probes");
        check(!f.memory.reads && !f.memory.setups,"no memory access/calls in denied context");return;
    }
    if (which=="unknown_contract") {
        f.memory.construction=false;check(!f.api.factoryProven() && f.api.reason()==sp::Reason::construction_unproven &&
            !f.memory.setups,"vacancy never substitutes for constructor resource proof");return;
    }
    if (which=="late_occupancy") {
        f.memory.occupyDuringPreflight=true;
        check(f.api.create(f.record.key,f.record)==rb::Creation::indeterminate && !f.memory.setups,
            "final vacancy revalidation before complete CREATE_PLAYER");return;
    }
    if (which=="partial" || which=="pool_full" || which=="local_unknown" || which=="model") {
        if (which=="partial") f.memory.partialAt=sp::slot1;
        if (which=="pool_full") f.memory.put(Memory::flags,std::array<std::uint8_t,4>{1,2,3,4});
        if (which=="local_unknown") f.memory.put(Memory::cj+sp::playerData,std::uint32_t{0});
        if (which=="model") f.memory.put(sp::ps::streamingTable+0x10,std::uint8_t{0});
        check(!f.api.factoryProven() && !f.memory.setups,"unproven read/pool/CJ refuses factory");return;
    }
    if (which=="partial_published") {
        f.memory.setupReturn=false;
        check(f.api.create(f.record.key,f.record)==rb::Creation::indeterminate && f.record.entity.ped==Memory::ped &&
            f.record.reference==Memory::ped,"retain partial publication even when full command fails");
        check(!f.memory.registers && !f.memory.cleans && !f.memory.removes && !f.api.authenticate(f.record),
            "retained value never establishes ownership or permits deletion");
        check(f.api.create(f.record.key,f.record)==rb::Creation::absent && f.memory.setups==1,"partial construction terminal, no retry");return;
    }
    if (which=="partial_factory" || which=="wrong_type" || which=="local_mutation" || which=="register_failure") {
        f.memory.emptyResult=which=="partial_factory";
        f.memory.badType=which=="wrong_type";
        f.memory.localMutation=which=="local_mutation";
        f.memory.registerFailure=which=="register_failure";
        check(f.api.create(f.record.key,f.record)==rb::Creation::indeterminate,"no success on uncorrelated factory");
        check(!f.api.factoryProven() && !f.memory.removes,"terminal failure; never delete unknown entity");
        check(f.api.create(f.record.key,f.record)==rb::Creation::absent && f.memory.setups==1,"no speculative retry");return;
    }
    if (which=="binding" || which=="binding_partial") {
        core::RemotePlayerController controller;core::RemoteRoster roster;
        const auto now=core::RemoteRoster::Time{};controller.beginSession(42);
        auto* binding=rb::Binding<true>::prepare(std::this_thread::get_id());check(binding!=nullptr,"resident binding");
        core::OnFootState state{};state.x=100;state.y=200;state.z=30;state.controls.forward=1;
        (void)roster.position({12,1,100,200,30},now);
        check(controller.receive({42,12,1,state},roster,now),"real controller");controller.synchronize(roster,now);
        if (which=="binding_partial") {
            f.memory.finishReturn=false;
            check(binding->consume(controller,now,f.api)==rb::Result::indeterminate,"partial build terminal to automatic consumption");
            check(binding->consume(controller,now,f.api)==rb::Result::indeterminate && f.memory.setups==1,"no second creation");
            check(binding->requestRemoval(f.api)==rb::Result::removed && f.memory.removes==1 && !f.memory.deadReads,
                "explicit F8 removes only authenticated partial result");
            check(binding->requestRemoval(f.api)==rb::Result::blocked_contract && f.memory.removes==1,"no second removal");return;
        }
        check(binding->consume(controller,now,f.api)==rb::Result::created && f.memory.setups==1,"binding consumes specific slot API");
        check(roster.left({12},now+1ms),"leave");controller.synchronize(roster,now+1ms);
        check(binding->consume(controller,now+1ms,f.api)==rb::Result::removed && f.memory.removes==1 && !f.memory.deadReads,
            "reference cleanup plus pool postcondition before binding releases record");return;
    }
    f.create();
    if (which=="control_auth_only") {
        f.memory.admitted=false;f.memory.controlOnly=true;
        check(f.api.authenticate(f.record),"original control may reauthenticate existing record");
        check(!f.api.factoryProven() && f.api.remove(f.record)==rb::Removal::indeterminate &&
            !f.memory.cleans && !f.memory.removes,"control context never admits lifecycle mutations");return;
    }
    check(f.memory.finishes==1,"one finish, no additional native task assignment");
    if (which=="cleanup_replacement") {
        f.memory.replaceDuringCleanup=true;
        check(f.api.remove(f.record)==rb::Removal::indeterminate && f.memory.cleans==1 && !f.memory.removes,
            "do not call indexed Remove for a replacement occupant");return;
    }
    if (which=="identity") {
        check(f.record.entity.slot==1 && f.record.entity.playerData==sp::data1 && f.record.entity.pad==sp::pad1 &&
            f.record.entity.handle==0x103,"slot/player-data/pad/tagged pool correlation");
        check(!f.api.replayProven() && !f.api.dispatch(f.record,{}) && !f.api.original(f.record),"no ProcessControl replay");return;
    }
    if (which=="ownership" || which=="handle" || which=="vehicle" || which=="reference") {
        if (which=="ownership") f.memory.put(sp::slot1,std::uint32_t{0x777777});
        if (which=="handle") f.memory.put(Memory::flags+1,std::uint8_t{4});
        if (which=="vehicle") f.memory.put(Memory::ped+sp::vehicle,std::uint32_t{123});
        if (which=="reference") f.memory.put(Memory::ped+0x24,std::uint32_t{0});
        check(!f.api.authenticate(f.record) && f.api.remove(f.record)==rb::Removal::indeterminate &&
            !f.memory.cleans && !f.memory.removes,"no cleanup/remove on ambiguous identity");return;
    }
    if (which=="cleanup" || which=="cleanup_unknown" || which=="slot_not_cleared" || which=="cleanup_call_failure") {
        const auto before=f.record;
        f.memory.keepPool=which=="cleanup_unknown";f.memory.keepSlot=which=="slot_not_cleared";
        f.memory.cleanFailure=which=="cleanup_call_failure";
        const bool complete=which=="cleanup";
        check(f.api.remove(f.record)==(complete ? rb::Removal::complete : rb::Removal::indeterminate),"destruction postcondition");
        check(f.api.removed(before,f.record)==complete && !f.memory.deadReads,"no dereference after destruction");
        check(f.memory.removes==(f.memory.cleanFailure ? 0u : 1u),"cleanup failure prevents destroy");
        check(f.memory.finishRemovals==(complete ? 1u : 0u),"do not restore group seed until old slot and pool tag prove destruction");
        if (!complete) check(!f.api.factoryProven(),"unknown cleanup blocks readmission");
        else {
            check(f.record.reference==0 && f.record.entity.handle==before.entity.handle,"original identity retained for postcondition");
            f.record={core::RemotePlayerKey{13,2},{},0};
            check(f.api.create(f.record.key,f.record)==rb::Creation::complete && f.record.entity.handle==0x104,
                "new generation/new tag only after full release");
        }
        return;
    }
    throw std::runtime_error("unknown case");
}
int main(int argc,char** argv) {
    try {if (argc!=2) return 2;run(argv[1]);return 0;}
    catch (const std::exception& e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
