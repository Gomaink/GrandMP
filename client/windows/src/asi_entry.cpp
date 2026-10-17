#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

#include "grandmp/core/client_adapter.hpp"
#include "grandmp/core/version.hpp"
#include "grandmp/windows/executable_detection.hpp"
#include "grandmp/windows/hook_lifecycle.hpp"
#include "grandmp/windows/module_residency.hpp"
#include "grandmp/windows/player_position_diagnostics.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {

HMODULE moduleHandle = nullptr;
std::atomic_flag initializationStarted = ATOMIC_FLAG_INIT;


class WindowsLogSink final : public grandmp::core::ILogSink {
public:
    explicit WindowsLogSink(const HMODULE module) noexcept : module_(module) {}

    void write(grandmp::core::LogLevel level, std::string_view message) override {
        const char* label = "INFO";
        if (level == grandmp::core::LogLevel::debug) label = "DEBUG";
        if (level == grandmp::core::LogLevel::warning) label = "WARN";
        if (level == grandmp::core::LogLevel::error) label = "ERROR";
        std::string line = "[GrandMP][";
        line += label;
        line += "] ";
        line.append(message);
        line += "\r\n";
        OutputDebugStringA(line.c_str());

        std::array<wchar_t, 32768> path{};
        const DWORD length = GetModuleFileNameW(module_, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0 || length >= path.size()) return;
        std::wstring file(path.data(), length);
        const auto slash = file.find_last_of(L"\\/");
        if (slash == std::wstring::npos) return;
        file.resize(slash + 1);
        file += L"GrandMP.log";
        const HANDLE output = CreateFileW(file.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                          nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (output == INVALID_HANDLE_VALUE) return;
        DWORD written = 0;
        WriteFile(output, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
        CloseHandle(output);
    }

private:
    HMODULE module_;
};

[[nodiscard]] bool readFile(const std::wstring& path, std::vector<std::byte>& bytes) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size{};
    constexpr LONGLONG maxFileSize = 64LL * 1024 * 1024;
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > maxFileSize) {
        CloseHandle(file);
        return false;
    }
    bytes.resize(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    const BOOL success = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
    CloseHandle(file);
    return success && read == bytes.size();
}

[[nodiscard]] bool readExecutable(std::vector<std::byte>& bytes, std::wstring& path) {
    std::array<wchar_t, 32768> buffer{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return false;
    path.assign(buffer.data(), length);
    return readFile(path, bytes);
}

[[nodiscard]] bool sha256(std::span<const std::byte> bytes, std::array<std::byte, 32>& digest) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) return false;
    const NTSTATUS status = BCryptHash(algorithm, nullptr, 0,
        reinterpret_cast<PUCHAR>(const_cast<std::byte*>(bytes.data())), static_cast<ULONG>(bytes.size()),
        reinterpret_cast<PUCHAR>(digest.data()), static_cast<ULONG>(digest.size()));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return BCRYPT_SUCCESS(status);
}

[[nodiscard]] std::string hashText(const std::array<std::byte, 32>& digest) {
    constexpr char digits[] = "0123456789abcdef";
    std::string value;
    value.reserve(digest.size() * 2);
    for (const auto byte : digest) {
        const auto octet = std::to_integer<unsigned char>(byte);
        value += digits[octet >> 4];
        value += digits[octet & 0x0f];
    }
    return value;
}

[[nodiscard]] grandmp::windows::LoaderIdentity recognizeSmokeLoader(const std::wstring& executablePath,
                                                                      grandmp::core::Logger& logger) {
    const auto slash = executablePath.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return grandmp::windows::LoaderIdentity::unknown;
    const std::wstring loaderPath = executablePath.substr(0, slash + 1) + L"dinput8.dll";
    if (!GetModuleHandleW(loaderPath.c_str())) {
        logger.log(grandmp::core::LogLevel::warning, "UAL 7.7.0 dinput8.dll nao carregado da pasta do jogo; sem PIN");
        return grandmp::windows::LoaderIdentity::unknown;
    }
    std::vector<std::byte> bytes;
    std::array<std::byte, 32> digest{};
    if (!readFile(loaderPath, bytes) || !sha256(bytes, digest)) {
        logger.log(grandmp::core::LogLevel::warning, "Falha ao identificar dinput8.dll; sem PIN");
        return grandmp::windows::LoaderIdentity::unknown;
    }
    logger.log(grandmp::core::LogLevel::info, "ASI Loader SHA-256=" + hashText(digest));
    constexpr std::array<std::byte, 32> smokeLoaderSha256{
        std::byte{0x29}, std::byte{0xFE}, std::byte{0xE0}, std::byte{0xAA},
        std::byte{0x46}, std::byte{0x41}, std::byte{0x48}, std::byte{0x64},
        std::byte{0xAD}, std::byte{0x1F}, std::byte{0x65}, std::byte{0xB2},
        std::byte{0x68}, std::byte{0x9B}, std::byte{0x47}, std::byte{0x0B},
        std::byte{0xFB}, std::byte{0x2C}, std::byte{0xF2}, std::byte{0xF0},
        std::byte{0x9A}, std::byte{0x94}, std::byte{0xE2}, std::byte{0xAF},
        std::byte{0xD2}, std::byte{0x71}, std::byte{0xE6}, std::byte{0x8F},
        std::byte{0x2E}, std::byte{0x16}, std::byte{0x74}, std::byte{0x3B},
    };
    return std::equal(digest.begin(), digest.end(), smokeLoaderSha256.begin())
        ? grandmp::windows::LoaderIdentity::smoke_ual_win32_770
        : grandmp::windows::LoaderIdentity::unknown;
}

class WindowsClientAdapter final : public grandmp::core::IClientAdapter {
public:
    explicit WindowsClientAdapter(HMODULE module) noexcept : sink_(module) {}

    [[nodiscard]] grandmp::windows::GameVersion detectedVersion() const noexcept { return detectedVersion_; }
    [[nodiscard]] grandmp::windows::LoaderIdentity loaderIdentity() const noexcept { return loaderIdentity_; }

    bool initialize() override {
        grandmp::core::Logger logger(sink_);
        logger.log(grandmp::core::LogLevel::info,
                   "GrandMP " + grandmp::core::versionString() + " platform=Windows x86");
        char threadContext[96]{};
        std::snprintf(threadContext, sizeof(threadContext), "InitializeASI pid=%lu tid=%lu",
                      static_cast<unsigned long>(GetCurrentProcessId()),
                      static_cast<unsigned long>(GetCurrentThreadId()));
        logger.log(grandmp::core::LogLevel::info, threadContext);
        std::vector<std::byte> bytes;
        std::wstring path;
        if (!readExecutable(bytes, path)) {
            logger.log(grandmp::core::LogLevel::error, "Falha ao ler o executavel principal; jogo nao suportado");
            return false;
        }
        const auto inspection = grandmp::windows::inspectExecutable(bytes);
        std::array<std::byte, 32> digest{};
        if (!sha256(bytes, digest)) {
            logger.log(grandmp::core::LogLevel::error, "Falha ao calcular SHA-256; jogo nao suportado");
            return false;
        }
        const auto hash = hashText(digest);
        char details[256]{};
        std::snprintf(details, sizeof(details), "Executavel: bytes=%llu PE timestamp=0x%08lx SHA-256=%s",
                      static_cast<unsigned long long>(inspection.fileSize),
                      static_cast<unsigned long>(inspection.timestamp), hash.c_str());
        logger.log(grandmp::core::LogLevel::info, details);
        if (inspection.status != grandmp::windows::PeStatus::pe32_i386) {
            logger.log(grandmp::core::LogLevel::warning, "Cabecalho PE32/i386 nao reconhecido; jogo nao suportado");
            return false;
        }
        detectedVersion_ = grandmp::windows::recognizeGameVersion(inspection, digest);
        if (detectedVersion_ == grandmp::windows::GameVersion::gta_sa_10us_hoodlum) {
            logger.log(grandmp::core::LogLevel::info,
                       "Identidade binaria confirmada: GTA SA 1.0 US Hoodlum");
            loaderIdentity_ = recognizeSmokeLoader(path, logger);
            logger.log(grandmp::core::LogLevel::warning,
                       "Ciclo de vida do hook ainda nao validado. Nenhum hook ou acesso ao jogador foi ativado");
        } else {
            logger.log(grandmp::core::LogLevel::warning,
                       "Variante GTA SA desconhecida ou arquivo modificado. Nenhum hook ou acesso ao jogador foi ativado");
        }
        return false;
    }

    void shutdown() noexcept override {}
    grandmp::core::ILogSink& logSink() noexcept override { return sink_; }

private:
    WindowsLogSink sink_;
    grandmp::windows::GameVersion detectedVersion_ = grandmp::windows::GameVersion::unknown;
    grandmp::windows::LoaderIdentity loaderIdentity_ = grandmp::windows::LoaderIdentity::unknown;
};

enum class RuntimePhase { cold, detecting, unsupported, residency_blocked };

// Constructed by InitializeASI, outside DllMain. No game patch backend is connected.
class WindowsClientRuntime final {
public:
    explicit WindowsClientRuntime(HMODULE module) : module_(module), adapter_(module) {}

    [[nodiscard]] bool initialize() {
        if (phase_ != RuntimePhase::cold) return false;
        phase_ = RuntimePhase::detecting;
        (void)adapter_.initialize();
        positionGate_ = std::make_unique<grandmp::windows::PlayerPositionLogGate>(
            adapter_.detectedVersion(), false);
        if (adapter_.detectedVersion() != grandmp::windows::GameVersion::gta_sa_10us_hoodlum) {
            phase_ = RuntimePhase::unsupported;
            return false;
        }
        phase_ = RuntimePhase::residency_blocked;
        grandmp::core::Logger logger(adapter_.logSink());
        logger.log(grandmp::core::LogLevel::warning,
                   "PIN desativado: contexto de InitializeASI nao comprovado fora do loader lock");
        return false;
    }

private:
    HMODULE module_;
    WindowsClientAdapter adapter_;
    std::shared_ptr<grandmp::windows::CallbackGate> callbackGate_ =
        std::make_shared<grandmp::windows::CallbackGate>();
    std::unique_ptr<grandmp::windows::PlayerPositionLogGate> positionGate_;
    RuntimePhase phase_ = RuntimePhase::cold;
};

}  // namespace

extern "C" BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        moduleHandle = module;
    }
    return TRUE;
}

extern "C" void __cdecl InitializeASI() noexcept {
    if (initializationStarted.test_and_set(std::memory_order_acq_rel)) {
        return;
    }
    try {
        auto candidate = std::make_unique<WindowsClientRuntime>(moduleHandle);
        (void)candidate->initialize();
    } catch (...) {
        OutputDebugStringA("[GrandMP][ERROR] Falha inesperada na inicializacao ASI\r\n");
    }
}
