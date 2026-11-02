#pragma once
#include <windows.h>
#include <filesystem>

namespace grandmp::windows::cleo_bootstrap {
inline constexpr wchar_t installationA[]=L"D:/Jogos/GrandMP-Smoke";
inline constexpr wchar_t installationB[]=L"D:/Jogos/GrandMP-Smoke-B";
inline const wchar_t* installationRoot(const std::filesystem::path& executable) {
    // Raw absolute spelling first: never canonicalize an arbitrary alias into
    // an allowed installation. No environment/CWD/substring-based admission.
    if(executable==std::filesystem::path(installationA)/L"gta_sa.exe")return installationA;
    if(executable==std::filesystem::path(installationB)/L"gta_sa.exe")return installationB;
    return nullptr;
}
template<class Attributes>
bool plainPath(const std::filesystem::path& path,Attributes attributes) {
    if(!path.is_absolute())return false;
    for(auto item=path;!item.empty();){
        const DWORD value=attributes(item.c_str());
        if(value==INVALID_FILE_ATTRIBUTES || (value&FILE_ATTRIBUTE_REPARSE_POINT))return false;
        const auto parent=item.parent_path();
        if(parent==item)break;
        item=parent;
    }return true;
}
inline bool plainPath(const std::filesystem::path& path) {
    return plainPath(path,[](const wchar_t* item){return GetFileAttributesW(item);});
}
template<class Attributes>
const wchar_t* installationLogPrefix(const std::filesystem::path& executable,Attributes attributes) {
    const auto* selected=installationRoot(executable);
    if(!selected || !plainPath(executable,attributes))return nullptr;
    return selected==installationA ? L"D:/Jogos/GrandMP-Smoke/GrandMPPlayerBootstrap.Diagnostic" :
        L"D:/Jogos/GrandMP-Smoke-B/GrandMPPlayerBootstrap.Diagnostic";
}
inline const wchar_t* installationLogPrefix(const std::filesystem::path& executable) {
    return installationLogPrefix(executable,[](const wchar_t* item){return GetFileAttributesW(item);});
}
}
