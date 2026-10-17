#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

#include "native_counter_preflight.hpp"
#include "native_counter_call.hpp"

#include <algorithm>
#include <cstring>
#include <string_view>

namespace grandmp::windows {
namespace {
struct File {
    HANDLE value;
    ~File() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct Algorithm {
    BCRYPT_ALG_HANDLE value = nullptr;
    ~Algorithm() { if (value) BCryptCloseAlgorithmProvider(value, 0); }
};

// Bounded PE export inspection of the exact experimental file admitted by the
// caller. This is file evidence only; it cannot grant remote execution rights.
class Exports {
public:
    explicit Exports(const NativeCounterFileEvidence& file) : bytes_(file.bytes) {
        const auto dos = at<IMAGE_DOS_HEADER>(0);
        if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return;
        const auto nt = at<IMAGE_NT_HEADERS32>(dos->e_lfanew);
        if (!nt || nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
            nt->FileHeader.SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER32) ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
            nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXPORT ||
            !(nt->FileHeader.Characteristics & IMAGE_FILE_DLL) ||
            nt->FileHeader.NumberOfSections == 0 || nt->FileHeader.NumberOfSections > 32) return;
        nt_ = *nt;
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
            const auto section = at<IMAGE_SECTION_HEADER>(std::size_t(dos->e_lfanew) +
                sizeof(IMAGE_NT_HEADERS32) + i * sizeof(IMAGE_SECTION_HEADER));
            if (!section) { sections_.clear(); return; }
            sections_.push_back(*section);
        }
        valid_ = true;
    }
    bool section(std::uint32_t rva, std::size_t length, bool code) const {
        for (const auto& part : sections_) {
            if (rva >= part.VirtualAddress && std::uint64_t(rva) + length <=
                std::uint64_t(part.VirtualAddress) + part.Misc.VirtualSize) {
                const bool executable = (part.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
                return code ? executable : !executable && (part.Characteristics & IMAGE_SCN_MEM_WRITE);
            }
        }
        return false;
    }
    std::optional<std::uint32_t> find(std::string_view name) const {
        if (!valid_) return {};
        const auto directory = nt_.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        const auto exports = fromRva<IMAGE_EXPORT_DIRECTORY>(directory.VirtualAddress);
        if (!directory.VirtualAddress || !directory.Size || !exports ||
            exports->NumberOfNames > 4096 || exports->NumberOfFunctions > 4096) return {};
        for (std::uint32_t i = 0; i < exports->NumberOfNames; ++i) {
            const auto pointer = fromRva<std::uint32_t>(std::uint64_t(exports->AddressOfNames) + i * 4);
            if (!pointer) return {};
            bool same = true, terminated = false;
            for (std::size_t n = 0; n < 128; ++n) {
                const auto character = fromRva<char>(std::uint64_t(*pointer) + n);
                if (!character) return {};
                if (*character == 0) { terminated = true; same &= n == name.size(); break; }
                if (n >= name.size() || *character != name[n]) same = false;
            }
            if (!terminated) return {};
            if (!same) continue;
            const auto ordinal = fromRva<std::uint16_t>(std::uint64_t(exports->AddressOfNameOrdinals) + i * 2);
            if (!ordinal || *ordinal >= exports->NumberOfFunctions) return {};
            const auto function = fromRva<std::uint32_t>(std::uint64_t(exports->AddressOfFunctions) + *ordinal * 4);
            if (!function || !*function || *function >= nt_.OptionalHeader.SizeOfImage ||
                (*function >= directory.VirtualAddress && std::uint64_t(*function) <
                 std::uint64_t(directory.VirtualAddress) + directory.Size)) return {};
            return function;
        }
        return {};
    }
private:
    template<class T> std::optional<T> at(std::size_t position) const {
        if (position > bytes_.size() || sizeof(T) > bytes_.size() - position) return {};
        T value{};
        std::memcpy(&value, bytes_.data() + position, sizeof(T));
        return value;
    }
    template<class T> std::optional<T> fromRva(std::uint64_t rva) const {
        if (rva > UINT32_MAX) return {};
        for (const auto& part : sections_) {
            if (rva >= part.VirtualAddress && rva + sizeof(T) <=
                std::uint64_t(part.VirtualAddress) + part.SizeOfRawData)
                return at<T>(std::size_t(part.PointerToRawData) + rva - part.VirtualAddress);
        }
        return {};
    }
    std::span<const std::byte> bytes_;
    IMAGE_NT_HEADERS32 nt_{};
    std::vector<IMAGE_SECTION_HEADER> sections_;
    bool valid_ = false;
};
}  // namespace

std::optional<NativeCounterFileEvidence> readNativeCounterFile(
    const std::filesystem::path& path, std::uint32_t maximumBytes) {
    if (!maximumBytes || maximumBytes > nativeCounterInventoryFileLimit) return {};
    File file{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    LARGE_INTEGER size{};
    if (file.value == INVALID_HANDLE_VALUE || !GetFileSizeEx(file.value, &size) ||
        size.QuadPart <= 0 || size.QuadPart > maximumBytes) return {};
    NativeCounterFileEvidence result;
    result.bytes.resize(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    if (!ReadFile(file.value, result.bytes.data(), static_cast<DWORD>(result.bytes.size()), &read, nullptr) ||
        read != result.bytes.size()) return {};
    Algorithm algorithm;
    if (BCryptOpenAlgorithmProvider(&algorithm.value, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptHash(algorithm.value, nullptr, 0, reinterpret_cast<PUCHAR>(result.bytes.data()),
            static_cast<ULONG>(result.bytes.size()), reinterpret_cast<PUCHAR>(result.digest.data()), 32) < 0)
        return {};
    result.inspection = inspectExecutable(result.bytes);
    if (result.inspection.status == PeStatus::pe32_i386 && result.bytes.size() >= sizeof(IMAGE_DOS_HEADER)) {
        IMAGE_DOS_HEADER dos{};
        std::memcpy(&dos, result.bytes.data(), sizeof(dos));
        if (dos.e_lfanew > 0 && std::size_t(dos.e_lfanew) <= result.bytes.size() &&
            sizeof(IMAGE_NT_HEADERS32) <= result.bytes.size() - dos.e_lfanew) {
            IMAGE_NT_HEADERS32 nt{};
            std::memcpy(&nt, result.bytes.data() + dos.e_lfanew, sizeof(nt));
            result.imageBase = nt.OptionalHeader.ImageBase;
            result.imageSize = nt.OptionalHeader.SizeOfImage;
        }
    }
    return result;
}

NativeCounterPreflightResult inspectNativeCounterPhysicalPreflight(
    const NativeCounterFileEvidence& executable, const NativeCounterFileEvidence& admitted,
    const NativeCounterPhysicalSnapshot& snapshot) noexcept {
    using Result = NativeCounterPreflightResult;
    if (recognizeGameVersion(executable.inspection, executable.digest) != GameVersion::gta_sa_10us_hoodlum)
        return Result::unknown_executable;
    if (executable.imageBase != 0x400000 || snapshot.executableBase != 0x400000)
        return Result::incompatible_base;
    // Bad/missing evidence and allocation failure must remain closed.
    try {
        const Exports exports(admitted);
        const auto data = exports.find("GrandMPNativeCounterReady");
        const auto wrapper = exports.find("GrandMPNativeCounterWrapperHost");
        const auto prepare = exports.find("GrandMPNativeCounterPrepareDeferred");
        if (admitted.inspection.status != PeStatus::pe32_i386 || admitted.imageSize == 0 ||
            !data || !wrapper || !prepare || !exports.section(*data, sizeof(NativeCounterReadyV1), false) ||
            !exports.section(*wrapper, 1, true) || !exports.section(*prepare, 1, true) ||
            std::all_of(admitted.digest.begin(), admitted.digest.end(), [](auto value) { return value == std::byte{}; }) ||
            admitted.digest != snapshot.loadedModuleFileDigest || snapshot.moduleBase == 0 ||
            std::uint64_t(snapshot.moduleBase) + admitted.imageSize > UINT32_MAX)
            return Result::incompatible_module;
        if (std::memcmp(&snapshot.first, &snapshot.second, sizeof(snapshot.first)) != 0)
            return Result::unstable_snapshot;
        const auto& ready = snapshot.second;
        if (ready.state != static_cast<std::uint32_t>(NativeCounterReadyState::ready) ||
            ready.residencyConfirmed != 1 || ready.wrapperCapable != 1 ||
            ready.shutdownLogical != 0 || ready.installed != 0) return Result::not_ready;
        if (ready.magic != nativeCounterReadyMagic || ready.interfaceVersion != nativeCounterReadyVersion ||
            ready.size != sizeof(ready) || !snapshot.processId || ready.processId != snapshot.processId ||
            ready.moduleBase != snapshot.moduleBase || ready.moduleSize != admitted.imageSize ||
            ready.descriptorAddress != snapshot.moduleBase + *data || ready.wrapper != snapshot.moduleBase + *wrapper ||
            ready.reserved != 0 || ready.threadId == 0 || ready.callsite != 0x53E981 ||
            ready.originalTarget != 0x53BEE0 || ready.counterAddress < snapshot.moduleBase ||
            ready.counterAddress % 8 || !exports.section(ready.counterAddress - snapshot.moduleBase, 8, false))
            return Result::incompatible_identity;
        const auto expected = encodeNativeCounterCall(0x53E981, 0x53BEE0);
        std::uint32_t low = 0;
        std::memcpy(&low, expected->data(), sizeof(low));
        if (ready.originalBytesLow != low || ready.originalBytesHigh != (*expected)[4] ||
            snapshot.callBytes.size() != 5 || !std::equal(snapshot.callBytes.begin(), snapshot.callBytes.end(), expected->begin()) ||
            decodeNativeCounterCall(0x53E981, snapshot.callBytes) != 0x53BEE0)
            return Result::incompatible_call;
        return Result::evidence_consistent_read_only;
    } catch (...) {
        // This is bounded offline evidence parsing, not a GTA exception handler.
        return Result::incompatible_module;
    }
}

NativeCounterAdmissionDryRun dryRunNativeCounterAdmission(
    const NativeCounterFileEvidence& executable, const NativeCounterFileEvidence& admitted,
    const NativeCounterPhysicalSnapshot& snapshot) noexcept {
    const auto evidence = inspectNativeCounterPhysicalPreflight(executable, admitted, snapshot);
    if (evidence != NativeCounterPreflightResult::evidence_consistent_read_only) return {evidence, {}};
    const auto plan = planNativeCounterCall(snapshot.second, snapshot.callBytes);
    if (!plan) return {NativeCounterPreflightResult::incompatible_call, {}};
    return {evidence, plan};
}

std::optional<NativeCounterReadyV1> prepareNativeCounterHoodlumDescriptor(
    const NativeCounterFileEvidence& executable, const NativeCounterFileEvidence& admitted,
    const NativeCounterPhysicalSnapshot& initializing) noexcept {
    const auto& initial = initializing.second;
    if (std::memcmp(&initializing.first, &initial, sizeof(initial)) != 0 ||
        initial.state != static_cast<std::uint32_t>(NativeCounterReadyState::initializing) ||
        initial.residencyConfirmed != 1 || initial.wrapperCapable != 0 || initial.installed != 0 ||
        initial.shutdownLogical != 0 || initial.callsite != 0 || initial.originalTarget != 0 ||
        initial.originalBytesLow != 0 || initial.originalBytesHigh != 0) return {};
    auto facts = initializing;
    auto& candidate = facts.second;
    candidate.state = static_cast<std::uint32_t>(NativeCounterReadyState::ready);
    candidate.callsite = 0x53E981;
    candidate.originalTarget = 0x53BEE0;
    candidate.wrapperCapable = 1;
    const auto original = encodeNativeCounterCall(candidate.callsite, candidate.originalTarget);
    std::memcpy(&candidate.originalBytesLow, original->data(), sizeof(candidate.originalBytesLow));
    candidate.originalBytesHigh = (*original)[4];
    facts.first = candidate;
    if (!dryRunNativeCounterAdmission(executable, admitted, facts).plan) return {};
    return candidate;
}

std::optional<std::uint32_t> nativeCounterReadyRva(const NativeCounterFileEvidence& file) noexcept {
    try {
        if (file.inspection.status != PeStatus::pe32_i386) return {};
        const Exports exports(file);
        const auto rva = exports.find("GrandMPNativeCounterReady");
        if (!rva || std::uint64_t(*rva) + sizeof(NativeCounterReadyV1) > file.imageSize ||
            !exports.section(*rva, sizeof(NativeCounterReadyV1), false)) return {};
        return rva;
    } catch (...) { return {}; }
}
std::optional<std::uint32_t> nativeCounterWrapperRva(const NativeCounterFileEvidence& file) noexcept {
    try {
        if (file.inspection.status != PeStatus::pe32_i386) return {};
        const Exports exports(file);
        const auto rva = exports.find("GrandMPNativeCounterWrapperHost");
        if (!rva || !exports.section(*rva, 1, true)) return {};
        return rva;
    } catch (...) { return {}; }
}
}  // namespace grandmp::windows
