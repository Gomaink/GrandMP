#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../client/windows/tools/native_player_replay.hpp"
#include "grandmp/windows/remote_player_binding.hpp"
#include <cstdio>
#include <stdexcept>
#include <string_view>

namespace ni=grandmp::windows::remote_player_binding::replay;
namespace np=grandmp::windows::remote_player_binding;
void check(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
unsigned originals=0,wrappers=0;
void* observedThis=nullptr;
void __fastcall original(void* self,void*) { ++originals;observedThis=self; }
void __fastcall wrapper(void* self,void*) {
    ++wrappers;
    // ABI-shaped stand-in, not an engine fake: test actual Win32 ECX/RET0 and cell.
    reinterpret_cast<void(__thiscall*)(void*)>(&original)(self);
}
using Entry=void(__fastcall*)(void*,void*);
// A real read-only IMAGE cell in this disposable host, not GTA memory.
Entry const dispatchTable[1]{original};
std::array<std::byte,0x134> hostPad{};
struct Borrow {
    np::OnFootPadStates before{},input{};
    bool cpp=false,seh=false,restored=false;
    unsigned calls=0;
};
bool write(const np::OnFootPadStates& values) noexcept {
    return ni::writePadStates(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(hostPad.data())),
        GetModuleHandleW(nullptr),true,values);
}
void borrowBody(void* value) {
    auto& b=*static_cast<Borrow*>(value);
    check(write(b.input),"real pad writer");++b.calls;
    if (b.cpp) throw std::runtime_error("original exception");
    if (b.seh) RaiseException(0xE1470001,0,0,nullptr);
}
void borrowRestore(void* value) noexcept {
    auto& b=*static_cast<Borrow*>(value);b.restored=write(b.before);
}
bool sehBorrow(Borrow* value) {
    __try { np::withFinally(borrowBody,borrowRestore,value); }
    __except(GetExceptionCode()==0xE1470001 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return true; }
    return false;
}
__declspec(noinline) void dispatch(void* self) {
    const auto* cell=reinterpret_cast<const volatile std::uint32_t*>(dispatchTable);
    reinterpret_cast<void(__thiscall*)(void*)>(*cell)(self);
}
void run(std::string_view which) {
    if (which=="gate") { check(!ni::executionApproved,"default physical input gate closed");return; }
    if (which=="pad_scope" || which=="pad_exception" || which=="pad_seh" || which=="pad_context") {
        if (which=="pad_context") {
            check(!ni::writePadStates(1,nullptr,false,{}),"denied context returns before any invalid memory access");return;
        }
        hostPad.fill(std::byte{0x45});const auto before=hostPad;
        Borrow b{};b.cpp=which=="pad_exception";b.seh=which=="pad_seh";
        std::memcpy(b.before.current.data(),hostPad.data(),0x30);
        std::memcpy(b.before.previous.data(),hostPad.data()+0x30,0x30);
        check(np::prepareOnFootPad({},{0,1,1},b.input),"same codec as runtime");
        if (b.seh) check(sehBorrow(&b),"SEH propagated after finally");
        else { try { np::withFinally(borrowBody,borrowRestore,&b); } catch (const std::runtime_error&) { check(b.cpp,"expected original exception"); } }
        check(b.calls==1 && b.restored && hostPad==before,"one original-shaped body and exact restore, untouched tail");return;
    }
    if (which=="basis" || which=="neutral" || which=="invalid") {
        grandmp::core::PlayerControls result{};
        if (which=="invalid") {
            check(!ni::cameraRelative({0,1,1},0,NAN,result),"no invalid live camera");
            check(!ni::cameraRelative({INFINITY,0,1},0,0,result),"no invalid input");return;
        }
        if (which=="neutral") {
            check(ni::cameraRelative({},1,-2,result) && result==grandmp::core::PlayerControls{},"neutral stays neutral");return;
        }
        check(ni::cameraRelative({0,1,1},0,0,result) && std::abs(result.forward-1)<1e-5f &&
            std::abs(result.right)<1e-5f,"same basis forward");
        check(ni::cameraRelative({0,1,1},0,1.5707963f,result) && std::abs(result.right+1)<1e-5f &&
            std::abs(result.forward)<1e-5f,"camera rotation compensated without writing camera");
        np::OnFootPadStates pad{};
        check(np::prepareOnFootPad({},result,pad),"actual pad codec");
        std::int16_t lr=0;std::memcpy(&lr,pad.current.data(),2);check(lr==-60,"native axis sign");
        check(ni::cameraRelative({1,1,1},0,2.3f,result) && std::hypot(result.right,result.forward)<=1.00001f,"bounded diagonal");
        return;
    }
    ni::HookCell hook;
    const auto at=const_cast<std::uint32_t*>(reinterpret_cast<const std::uint32_t*>(dispatchTable));
    const auto from=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&original));
    const auto to=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&wrapper));
    const auto image=GetModuleHandleW(nullptr);
    if (which=="mismatch") {
        check(!hook.install(at,from+1,to,image) && !hook.installed() && *at==from,"unknown original refused");
        check(!hook.install(at,from,to,image),"no second installation after refusal");return;
    }
    check(hook.install(at,from,to,image) && hook.installed(),"exact cell publication");
    if (which=="tamper") {
        DWORD old=0,ignored=0;check(VirtualProtect(at,4,PAGE_READWRITE,&old)!=FALSE,"host permission");
        *at=from+1;check(VirtualProtect(at,4,old,&ignored)!=FALSE,"host permission restore");
        check(!hook.remove() && hook.installed() && *at==from+1,"never overwrite unknown cell during cleanup");return;
    }
    int local=0,remote=1;
    for (unsigned n=0;n<1000;++n) { dispatch(&local);check(observedThis==&local,"local ECX");dispatch(&remote);check(observedThis==&remote,"remote ECX"); }
    check(originals==2000 && wrappers==2000,"exactly one original per engine-shaped dispatch, balanced stack");
    check(!hook.install(at,from,to,image),"no duplicate installation");
    check(hook.remove() && !hook.installed() && *at==from,"original cell restored");
    dispatch(&local);check(originals==2001 && wrappers==2000,"passthrough after restoration");
}
int main(int argc,char** argv) {
    try { if (argc!=2) return 2;run(argv[1]);return 0; }
    catch (const std::exception& e) { std::fprintf(stderr,"%s\n",e.what());return 1; }
}
