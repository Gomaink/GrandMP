#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../client/windows/tools/native_player_bootstrap_diagnostic.hpp"
#include <cstdio>
#include <string_view>
namespace bd=grandmp::windows::bootstrap_diagnostic;
int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 2;
    const bool preload=std::wstring_view(argv[1])==L"preloaded";
    constexpr auto asi=L"GrandMPNativePlayerSyncExperimental.asi";
    if(preload && !LoadLibraryW(asi))return 3;
    if(!LoadLibraryW(L".\\dinput8.dll"))return 4;
    STARTUPINFOW startup{};startup.cb=sizeof(startup);GetStartupInfoW(&startup);
    const auto m=GetModuleHandleW(asi);if(!m)return 5;
    auto* d=reinterpret_cast<bd::Data*>(GetProcAddress(m,"GrandMPNativePlayerBootstrapDiagnostic"));
    auto status=reinterpret_cast<unsigned(__cdecl*)()>(GetProcAddress(m,"GrandMPNativePlayerStatus"));
    if(!d || !status || d->magic!=0x39424D47 || d->bytes!=184 || d->attached!=1)return 6;
    if(preload){
        if(d->initializeCalls!=0 || status()!=0 || GetFileAttributesW(L"GrandMPNativePlayerSyncExperimental.log")!=INVALID_FILE_ATTRIBUTES)return 7;
        std::puts("UAL preload: DllMain=seen InitializeASI=not_seen log=absent; no inference about GTA");
    }else{
        const auto deadline=GetTickCount64()+5000;
        auto& exit=d->events[static_cast<unsigned>(bd::Event::prepare_exit)];
        while(exit.state!=bd::succeeded && GetTickCount64()<deadline)Sleep(1);
        if(exit.state!=bd::succeeded || d->initializeCalls!=1 || status()!=2 ||
           d->events[static_cast<unsigned>(bd::Event::write_file)].state!=bd::succeeded)return 8;
        std::puts("UAL normal: DllMain -> InitializeASI -> reference -> thread -> prepare -> first log; physical gate CLOSED");
    }
    return 0; // Normal process exit; never patch or call GTA.
}
