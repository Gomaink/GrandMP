#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "grandmp/windows/module_residency.hpp"

#include <cstdio>
#include <filesystem>

int main(int argc, char* argv[]) {
    if (argc != 2) return 2;
    const std::wstring path = std::filesystem::absolute(argv[1]).wstring();
    HMODULE module = LoadLibraryW(path.c_str());
    if (!module) return 1;
    auto* entry = GetProcAddress(module, "InitializeASI");
    if (!entry) {
        FreeLibrary(module);
        return 1;
    }

    // Disposable test process only: identities model a successful preflight.
    // InitializeASI is deliberately not called and the GTA is not involved.
    grandmp::windows::ModuleResidency residency;
    grandmp::windows::Win32ModulePinApi api;
    const auto result = residency.pinHoodlum(
        grandmp::windows::GameVersion::gta_sa_10us_hoodlum,
        grandmp::windows::LoaderIdentity::smoke_ual_win32_770,
        module, reinterpret_cast<const void*>(entry), api);
    if (result != grandmp::windows::ResidenceResult::pinned) {
        FreeLibrary(module);
        return 1;
    }
    FreeLibrary(module);
    return GetModuleHandleW(path.c_str()) == module ? 0 : 1;
}
