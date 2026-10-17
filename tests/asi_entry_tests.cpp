#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) return 1;

    const auto asi = std::filesystem::absolute(argv[1]);
    const auto log = asi.parent_path() / "GrandMP.log";
    std::filesystem::remove(log);

    const HMODULE module = LoadLibraryW(asi.c_str());
    if (!module) {
        std::cerr << "LoadLibraryW failed: " << GetLastError() << '\n';
        return 1;
    }
    const auto initialize = reinterpret_cast<void(__cdecl*)()>(GetProcAddress(module, "InitializeASI"));
    if (!initialize) {
        std::cerr << "InitializeASI export missing\n";
        FreeLibrary(module);
        return 1;
    }
    initialize();
    initialize();
    FreeLibrary(module);
    if (GetModuleHandleW(asi.c_str()) != nullptr) {
        std::cerr << "Unknown host must not pin GrandMP.asi\n";
        return 1;
    }

    std::ifstream input(log, std::ios::binary);
    if (!input) {
        std::cerr << "GrandMP.log missing\n";
        return 1;
    }
    const std::string contents(std::istreambuf_iterator<char>{input}, {});
    const std::string marker = "platform=Windows x86";
    const auto first = contents.find(marker);
    if (first == std::string::npos || contents.find(marker, first + marker.size()) != std::string::npos) {
        std::cerr << "InitializeASI must log exactly once\n";
        return 1;
    }
    const std::string context = "InitializeASI pid=" + std::to_string(GetCurrentProcessId()) +
                                " tid=" + std::to_string(GetCurrentThreadId());
    const auto contextFirst = contents.find(context);
    if (contextFirst == std::string::npos ||
        contents.find(context, contextFirst + context.size()) != std::string::npos) {
        std::cerr << "InitializeASI must log actual PID and thread ID exactly once\n";
        return 1;
    }
    if (contents.find("Variante GTA SA desconhecida ou arquivo modificado") == std::string::npos ||
        contents.find("Nenhum hook ou acesso ao jogador foi ativado") == std::string::npos) {
        std::cerr << "ASI must reject unsupported host without player integration\n";
        return 1;
    }
    return 0;
}
