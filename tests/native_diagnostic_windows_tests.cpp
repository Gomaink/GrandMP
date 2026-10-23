// Compile the REAL runtime/providers in a disposable non-GTA host. No entry,
// worker, hook or native engine address is invoked by including this file.
#define GRANDMP_REMOTE_PED_VARIANT 1
#define GRANDMP_REMOTE_PED_MOVE_VARIANT 1
#include "../client/windows/tools/ped_smoke.cpp"
#include "../client/windows/tools/native_diagnostic_windows.hpp"
#include "../client/windows/tools/native_diagnostic_windows_runtime.inl"
#include <stdexcept>
#include <string_view>
#include <thread>

namespace win = grandmp::windows::native_diagnostic_windows;
namespace bind = grandmp::windows::native_diagnostic_binding;
namespace native = bind::native;
void check(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
struct FakeRuntime {
    DWORD owner = GetCurrentThreadId();
    bool phase = true, code = true, gameplay = true;
    unsigned codeCalls = 0, gameplayCalls = 0;
    bool context(std::uintptr_t caller,unsigned callback) const noexcept {
        return GetCurrentThreadId() == owner && phase && caller == 7 && callback == 9;
    }
    bool codeReady() noexcept { ++codeCalls; return code; }
    bool gameplayReady() noexcept { ++gameplayCalls; return gameplay; }
};
struct FakeEngine {
    struct Key { std::uint64_t generation=2, player=42; } key;
    void* owned = reinterpret_cast<void*>(0x1000);
    int reference = 0x101;
    bool owns(std::uintptr_t ped) const noexcept { return ped == 0x1000; }
};
struct Memory {
    unsigned* reads;
    bool partial=false, fail=false;
    ps::MemoryRead operator()(std::uintptr_t address,void* output,std::size_t size) const noexcept {
        ++*reads;
        if (fail) return {false,0,ERROR_NOACCESS};
        if (partial) return {true,size-1,ERROR_SUCCESS};
        std::uint32_t value = 0;
        if (address == 0x1000+ps::task_layout::intelligence) value=0x2000;
        else if (address == 0x2000 || address == 0x2004+ps::task_layout::managerOwner) value=0x1000;
        std::memcpy(output,&value,size); return {true,size,ERROR_SUCCESS};
    }
};
void run(std::string_view which) {
    static_assert(!ps::physicalExecutionApproved && !ps::movementExecutionApproved && !rm::physicalExecutionApproved);
    if (which == "runtime") {
        DiagnosticRuntimePrimitives runtime; DiagnosticResidence residence;
        win::Context context(runtime,ps::phaseCall+5,0); win::Ready ready(runtime,context); win::Reader reader(context);
        std::uint32_t value=0;
        check(!residence() && !context() && !ready() && !reader(0x1000,&value,4).success,"real closed runtime refuses before reads");
        check(!hookPublished.load() && !remoteEngine.owned && installation.load()==0 && callbacks.load()==0,"no actual hook or gameplay");
        check(!codeMatches() && !taskCodeMatches(),"non-GTA host cannot authenticate real code");
        // Shared phase primitive is equivalent to the prior inline condition.
        for (DWORD thread : {DWORD{0},GetCurrentThreadId(),GetCurrentThreadId()+1}) {
            gameThread=thread;
            for (auto caller : {ps::phaseCall+5,ps::phaseCall})
                check(phaseThreadMatches(caller)==bool(thread && GetCurrentThreadId()==thread && caller==ps::phaseCall+5),"original phase condition preserved");
        }
        return;
    }
    FakeRuntime runtime; win::Context context(runtime,7,9); win::Ready ready(runtime,context);
    unsigned reads=0; FakeEngine engine;
    if (which == "context" || which == "thread") {
        win::Reader reader(context,Memory{&reads});
        bind::RuntimeView view(engine,context,ready,reader); std::uint32_t output=0;
        auto refused=[&] {
            check(!context() && !ready() && !view.identity().ped && !reader(0x1000,&output,4).success,"context refuses every primitive");
        };
        if (which=="thread") { std::thread worker(refused); worker.join(); }
        else { runtime.phase=false; refused(); runtime.phase=true; check(context() && ready(),"valid providers admitted"); }
        check(!reads && runtime.codeCalls==(which=="context"?1u:0u),"denied context does no native read or code query");
    } else if (which == "ready") {
        runtime.code=false;check(!ready() && runtime.codeCalls==1 && !runtime.gameplayCalls,"code failure precedes gameplay");
        runtime.code=true;runtime.gameplay=false;check(!ready() && runtime.gameplayCalls==1,"gameplay failure closed");
        runtime.gameplay=true;check(ready(),"all original ready primitives accepted");
    } else if (which == "partial" || which == "identity" || which == "complete") {
        win::Reader reader(context,Memory{&reads,which=="partial",false});
        bind::RuntimeView view(engine,context,ready,reader);const auto entity=view.identity();
        if (which=="complete") {
            check(native::observeSlots(view,entity).slots==native::Coordinator::Slots::empty && reads==10,"complete Windows provider chain, all four slots and reauthentication");
        } else if (which=="identity") {
            ++engine.key.generation;
            check(native::observeSlots(view,entity).slots==native::Coordinator::Slots::unknown && !reads,"old generation refuses before reads");
            --engine.key.generation;engine.owned=reinterpret_cast<void*>(0x1100);
            check(native::observeSlots(view,entity).reason==native::Reason::identity && !reads,"changed registry identity refuses");
        } else {
            check(native::observeSlots(view,entity).slots==native::Coordinator::Slots::unknown && reads==1,"partial read never yields empty");
            check(reader.lastRead().success && reader.lastRead().transferred==3,"partial Win32 success/count preserved");
            win::Reader failed(context,Memory{&reads,false,true}); std::uint32_t output=0;
            const auto result=failed(0x1000,&output,4);
            check(!result.success && result.transferred==0 && failed.lastRead().error==ERROR_NOACCESS,"failure error/count preserved");
        }
    } else if (which == "memory") {
        // Actual Windows memory primitive, on HOST-owned storage only.
        win::Reader reader(context);std::uint32_t input=0x12345678,output=0;
        auto result=reader(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&input)),&output,sizeof(output));
        check(result.success && result.transferred==sizeof(output) && output==input,"real RPM exact count");
        result=reader(1,&output,sizeof(output));
        check(!result.success && !result.transferred && reader.lastRead().error!=ERROR_SUCCESS,"real RPM invalid host memory fails");
    } else throw std::runtime_error("unknown case");
}
int main(int argc,char** argv) {
    try { check(argc==2,"case required");run(argv[1]);std::puts("windows_provider PASS");return 0; }
    catch (const std::exception& e) { std::fprintf(stderr,"%s\n",e.what());return 1; }
}
