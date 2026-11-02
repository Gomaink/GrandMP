// Actual runtime, execution gates CLOSED; only Win32 failures are injected.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <string_view>
#include <stdexcept>
#include <thread>
#include <vector>
namespace {
std::string_view scenario;
std::atomic<unsigned> refs=0,threads=0,files=0;
HANDLE completion=nullptr;
BOOL WINAPI referenceProbe(DWORD f,LPCWSTR p,HMODULE* m) {
    ++refs;if(scenario=="reference"){SetLastError(ERROR_MOD_NOT_FOUND);return FALSE;}
    return GetModuleHandleExW(f,p,m);
}
HANDLE WINAPI threadProbe(LPSECURITY_ATTRIBUTES a,SIZE_T n,LPTHREAD_START_ROUTINE f,LPVOID p,DWORD flags,LPDWORD tid) {
    ++threads;if(scenario=="thread"){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return nullptr;}
    if(scenario=="delayed")return CreateEventW(nullptr,FALSE,FALSE,nullptr);
    const HANDLE h=CreateThread(a,n,f,p,flags,tid);
    if(h && !DuplicateHandle(GetCurrentProcess(),h,GetCurrentProcess(),&completion,SYNCHRONIZE,FALSE,0))std::terminate();
    return h;
}
DWORD WINAPI pathProbe(HMODULE m,LPWSTR p,DWORD n) {
    if(scenario=="path"){SetLastError(ERROR_BAD_PATHNAME);return 0;}
    if(scenario=="truncated"){SetLastError(ERROR_INSUFFICIENT_BUFFER);return n;}
    return GetModuleFileNameW(m,p,n);
}
HANDLE WINAPI fileProbe(LPCWSTR p,DWORD a,DWORD s,LPSECURITY_ATTRIBUTES sa,DWORD c,DWORD flags,HANDLE t) {
    ++files;if(scenario=="file"){SetLastError(ERROR_ACCESS_DENIED);return INVALID_HANDLE_VALUE;}
    return CreateFileW(p,a,s,sa,c,flags,t);
}
BOOL WINAPI writeProbe(HANDLE h,LPCVOID p,DWORD n,LPDWORD got,LPOVERLAPPED o) {
    if(scenario=="write"){SetLastError(ERROR_DISK_FULL);return FALSE;}
    if(scenario=="partial"){*got=1;return TRUE;}
    return WriteFile(h,p,n,got,o);
}
}
#define GetModuleHandleExW referenceProbe
#define CreateThread threadProbe
#define GetModuleFileNameW pathProbe
#define CreateFileW fileProbe
#define WriteFile writeProbe
#include "../client/windows/tools/ped_smoke.cpp"
#undef GetModuleHandleExW
#undef CreateThread
#undef GetModuleFileNameW
#undef CreateFileW
#undef WriteFile
#include <cstdio>
void check(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
int main(int argc,char** argv) {
    if(argc!=2)return 2;scenario=argv[1];
    try {
        static_assert(!ps::physicalExecutionApproved);
        DllMain(GetModuleHandleW(nullptr),DLL_PROCESS_ATTACH,nullptr);
#if GRANDMP_BOOTSTRAP_DIAGNOSTIC_BUILD
        auto& d=GrandMPNativePlayerBootstrapDiagnostic;
        auto rec=[&](bd::Event e)->bd::Record&{return d.events[static_cast<unsigned>(e)];};
        check(d.attached==1 && d.initializeCalls==0 && refs==0 && threads==0 && files==0,"DllMain only owns DATA");
        if(scenario=="entry")return 0;
#endif
        if(scenario=="concurrent") {
            std::vector<std::thread> callers;for(unsigned i=0;i<12;++i)callers.emplace_back(InitializeASI);
            for(auto& t:callers)t.join();
        }else InitializeASI();
        if(completion){check(WaitForSingleObject(completion,5000)==WAIT_OBJECT_0,"worker completion");CloseHandle(completion);}
        check(refs==1 && threads==(scenario=="reference" ? 0u : 1u),"single scheduling");
        check(callbacks.load()==0 && !hookPublished.load(),"no game callback/hook");
#if GRANDMP_BOOTSTRAP_DIAGNOSTIC_BUILD
        check(d.bytes==184 && d.magic==0x39424D47,"DATA ABI");
        if(scenario=="reference" || scenario=="thread") {
            const auto e=scenario=="reference" ? bd::Event::reference : bd::Event::thread;
            const DWORD expected=scenario=="reference" ? ERROR_MOD_NOT_FOUND : ERROR_NOT_ENOUGH_MEMORY;
            check(rec(e).state==bd::failed && static_cast<DWORD>(rec(e).win32)==expected,"immediate error");
            check(rec(bd::Event::prepare).state==bd::unseen && preparation.load()==4,"failed before worker");
            InitializeASI();check(refs==1 && d.initializeCalls==2 && static_cast<DWORD>(rec(e).win32)==expected,"no retry or lost error after failure");
        } else if(scenario=="delayed") {
            check(rec(bd::Event::thread).state==bd::succeeded && rec(bd::Event::prepare).state==bd::unseen,"created is not started");
        } else {
            check(rec(bd::Event::prepare).state==bd::entered && rec(bd::Event::prepare_exit).state==bd::succeeded,"actual worker entered/exited");
            check(preparation.load()==2,"closed gate blocked before engine");
            if(scenario=="path" || scenario=="truncated") {
                check(rec(bd::Event::module_path).state==bd::failed && files==0,"no file after path failure");
                check(rec(bd::Event::module_path).win32==(scenario=="path" ? ERROR_BAD_PATHNAME : ERROR_INSUFFICIENT_BUFFER),"path error");
            }else if(scenario=="file")check(rec(bd::Event::create_file).state==bd::failed && rec(bd::Event::create_file).win32==ERROR_ACCESS_DENIED,"first CreateFile failure retained");
            else if(scenario=="write" || scenario=="partial") {
                check(rec(bd::Event::write_file).state==bd::failed,"write failure retained");
                check(rec(bd::Event::write_file).win32==(scenario=="write" ? ERROR_DISK_FULL : 0),"short write does not invent Win32 failure");
            }else check(rec(bd::Event::write_file).state==bd::succeeded,"first line written");
            if(scenario=="concurrent")check(d.initializeCalls==12,"all entries counted, one worker");
        }
#else
        check(preparation.load()==2,"unchanged closed bootstrap with diagnostics OFF");
#endif
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}
}
