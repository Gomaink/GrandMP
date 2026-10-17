#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>

#include "native_counter_admission.hpp"
#include "native_counter_ready.hpp"
#include "native_counter_preflight.hpp"
#include "native_counter_supervision.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

static_assert(sizeof(void*) == 4, "The lab debugger is Win32 only");
namespace {
using grandmp::windows::NativeCounterCall;
using grandmp::windows::NativeCounterReadyV1;
using grandmp::windows::encodeNativeCounterCall;
using grandmp::windows::decodeNativeCounterCall;

class Handle final {
public:
    explicit Handle(HANDLE value = nullptr) noexcept : value_(value) {}
    ~Handle() { if (valid()) CloseHandle(value_); }
    Handle(Handle&& other) noexcept : value_(other.value_) { other.value_ = nullptr; }
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            if (valid()) CloseHandle(value_);
            value_ = other.value_;
            other.value_ = nullptr;
        }
        return *this;
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    bool valid() const noexcept { return value_ && value_ != INVALID_HANDLE_VALUE; }
    HANDLE get() const noexcept { return value_; }
private:
    HANDLE value_;
};

[[noreturn]] void throwWin32(const char* operation) {
    const DWORD error = GetLastError();
    throw std::runtime_error(std::string(operation) + " win32=" + std::to_string(error));
}

Handle duplicate(HANDLE source) {
    HANDLE copy = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(), &copy,
                         0, FALSE, DUPLICATE_SAME_ACCESS)) throwWin32("DuplicateHandle");
    return Handle(copy);
}

struct ModuleIdentity {
    std::filesystem::path path;
    std::array<std::byte, 32> digest;
    DWORD volume, indexHigh, indexLow;
    std::uint32_t size, timestamp;
    bool operator==(const ModuleIdentity& other) const {
        return CompareStringOrdinal(path.c_str(), -1, other.path.c_str(), -1, TRUE) == CSTR_EQUAL &&
            digest == other.digest && volume == other.volume && indexHigh == other.indexHigh &&
            indexLow == other.indexLow && size == other.size && timestamp == other.timestamp;
    }
};

struct ModuleEvent {
    ModuleIdentity identity;
    Handle backing; // Read-sharing lock retained until the matching unload.
};

std::vector<std::uint8_t> readFile(HANDLE file,
    std::uint32_t maximumBytes = grandmp::windows::nativeCounterAdmissionFileLimit) {
    LARGE_INTEGER size{}, zero{};
    if (!maximumBytes || maximumBytes > grandmp::windows::nativeCounterInventoryFileLimit || !file)
        throw std::runtime_error("file size/seek");
    if (!GetFileSizeEx(file, &size)) throwWin32("GetFileSizeEx");
    if (size.QuadPart <= 0 || size.QuadPart > maximumBytes)
        throw std::runtime_error("file size/seek");
    if (!SetFilePointerEx(file, zero, nullptr, FILE_BEGIN)) throwWin32("SetFilePointerEx");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    if (!ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr)) throwWin32("ReadFile");
    if (read != bytes.size()) throw std::runtime_error("file partial read");
    return bytes;
}

// Strict local PE parser for admitted files and inventory images. No target
// address is accepted from a CLI, descriptor, or filename alone.
class Image final {
public:
    explicit Image(const std::filesystem::path& path,
        std::uint32_t maximumBytes = grandmp::windows::nativeCounterAdmissionFileLimit)
        : path_(std::filesystem::canonical(path)),
          file_(CreateFileW(path_.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr)), maximumBytes_(maximumBytes) {
        if (!file_.valid()) throwWin32("open expected image");
        bytes_ = readFile(file_.get(), maximumBytes_);
        const auto dos = at<IMAGE_DOS_HEADER>(0);
        if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0)
            throw std::runtime_error("DOS identity");
        nt_ = at<IMAGE_NT_HEADERS32>(static_cast<std::size_t>(dos.e_lfanew));
        if (nt_.Signature != IMAGE_NT_SIGNATURE || nt_.FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
            nt_.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
            nt_.FileHeader.SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER32) ||
            nt_.FileHeader.NumberOfSections == 0 || nt_.FileHeader.NumberOfSections > 32)
            throw std::runtime_error("PE32 identity");
        const std::size_t sections = dos.e_lfanew + 24 + nt_.FileHeader.SizeOfOptionalHeader;
        for (unsigned index = 0; index < nt_.FileHeader.NumberOfSections; ++index)
            sections_.push_back(at<IMAGE_SECTION_HEADER>(sections + index * sizeof(IMAGE_SECTION_HEADER)));
        if (!GetFileInformationByHandle(file_.get(), &identity_)) throwWin32("GetFileInformationByHandle image");
        const auto evidence = grandmp::windows::readNativeCounterFile(path_, maximumBytes_);
        if (!evidence || evidence->bytes.size() != bytes_.size() ||
            std::memcmp(evidence->bytes.data(), bytes_.data(), bytes_.size()) != 0)
            throw std::runtime_error("file fingerprint");
        evidence_ = *evidence;
    }
    const grandmp::windows::NativeCounterFileEvidence& evidence() const noexcept { return evidence_; }
    const std::filesystem::path& path() const noexcept { return path_; }
    std::uint32_t size() const noexcept { return nt_.OptionalHeader.SizeOfImage; }
    std::uint32_t timestamp() const noexcept { return nt_.FileHeader.TimeDateStamp; }
    ModuleIdentity identity() const {
        return {path_, evidence_.digest, identity_.dwVolumeSerialNumber,
            identity_.nFileIndexHigh, identity_.nFileIndexLow, size(), timestamp()};
    }
    // Transfer the supervisor's own GENERIC_READ/FILE_SHARE_READ open, rather
    // than inheriting the sharing mode of a debugger-supplied file handle.
    Handle takeBackingFile() noexcept { return std::move(file_); }
    bool sameBackingFile(HANDLE loaded) const {
        BY_HANDLE_FILE_INFORMATION observed{};
        if (!loaded) return false;
        if (!GetFileInformationByHandle(loaded, &observed)) throwWin32("GetFileInformationByHandle backing");
        if (observed.dwVolumeSerialNumber != identity_.dwVolumeSerialNumber ||
            observed.nFileIndexHigh != identity_.nFileIndexHigh ||
            observed.nFileIndexLow != identity_.nFileIndexLow) return false;
        return readFile(loaded, maximumBytes_) == bytes_;
    }
    bool executable(std::uint32_t rva, std::size_t length = 1) const noexcept {
        for (const auto& section : sections_) {
            const auto end = std::uint64_t{section.VirtualAddress} + section.Misc.VirtualSize;
            if (rva >= section.VirtualAddress && std::uint64_t{rva} + length <= end)
                return (section.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
        }
        return false;
    }
    std::uint32_t exported(std::string_view requested) const {
        const auto directory = nt_.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (!directory.VirtualAddress || !directory.Size) throw std::runtime_error("missing exports");
        const auto exports = at<IMAGE_EXPORT_DIRECTORY>(offset(directory.VirtualAddress));
        if (exports.NumberOfNames > 65536 || exports.NumberOfFunctions > 65536)
            throw std::runtime_error("export bounds");
        for (std::uint32_t index = 0; index < exports.NumberOfNames; ++index) {
            const auto nameRva = at<std::uint32_t>(offset(exports.AddressOfNames) + index * 4ULL);
            const auto nameOffset = offset(nameRva);
            std::string name;
            for (std::size_t n = 0; n < 128; ++n) {
                const auto character = at<char>(nameOffset + n);
                if (!character) break;
                name += character;
            }
            if (name != requested) continue;
            const auto ordinal = at<std::uint16_t>(offset(exports.AddressOfNameOrdinals) + index * 2ULL);
            if (ordinal >= exports.NumberOfFunctions) throw std::runtime_error("export ordinal");
            const auto rva = at<std::uint32_t>(offset(exports.AddressOfFunctions) + ordinal * 4ULL);
            if (!rva || rva >= size() || (rva >= directory.VirtualAddress &&
                std::uint64_t{rva} < std::uint64_t{directory.VirtualAddress} + directory.Size))
                throw std::runtime_error("forwarded/invalid export");
            return rva;
        }
        throw std::runtime_error("required export absent");
    }
private:
    template<class T> T at(std::size_t position) const {
        if (position > bytes_.size() || sizeof(T) > bytes_.size() - position)
            throw std::runtime_error("PE file bounds");
        T value{};
        std::memcpy(&value, bytes_.data() + position, sizeof(T));
        return value;
    }
    std::size_t offset(std::uint32_t rva) const {
        if (rva < nt_.OptionalHeader.SizeOfHeaders) return rva;
        for (const auto& section : sections_) {
            if (rva >= section.VirtualAddress &&
                std::uint64_t{rva} < std::uint64_t{section.VirtualAddress} + section.SizeOfRawData)
                return std::size_t{section.PointerToRawData} + rva - section.VirtualAddress;
        }
        throw std::runtime_error("unmapped PE RVA");
    }
    grandmp::windows::NativeCounterFileEvidence evidence_;
    std::filesystem::path path_;
    Handle file_;
    std::vector<std::uint8_t> bytes_;
    IMAGE_NT_HEADERS32 nt_{};
    std::vector<IMAGE_SECTION_HEADER> sections_;
    BY_HANDLE_FILE_INFORMATION identity_{};
    const std::uint32_t maximumBytes_;
};

bool readExact(HANDLE process, std::uintptr_t address, void* data, std::size_t length) noexcept {
    SIZE_T read = 0;
    if (!ReadProcessMemory(process, reinterpret_cast<LPCVOID>(address), data, length, &read)) {
        const DWORD error = GetLastError();
        std::fprintf(stderr, "read_refusal operation=ReadProcessMemory address=%08lX requested=%zu returned=%zu win32=%lu\n",
            address, length, read, error);
        return false;
    }
    if (read != length) {
        std::fprintf(stderr, "read_refusal operation=ReadProcessMemory address=%08lX requested=%zu returned=%zu reason=incomplete win32=0\n",
            address, length, read);
        return false;
    }
    return true;
}

std::string digestText(const std::array<std::byte, 32>& digest) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string value;
    for (const auto byte : digest) {
        const auto n = std::to_integer<unsigned>(byte);
        value += digits[n >> 4]; value += digits[n & 15];
    }
    return value;
}
std::optional<std::array<std::byte, 32>> parseDigest(std::wstring_view text) {
    if (text.size() != 64) return {};
    std::array<std::byte, 32> digest{};
    for (std::size_t n = 0; n < text.size(); ++n) {
        const auto c = text[n];
        const int digit = c >= L'0' && c <= L'9' ? c - L'0' :
            c >= L'A' && c <= L'F' ? c - L'A' + 10 : c >= L'a' && c <= L'f' ? c - L'a' + 10 : -1;
        if (digit < 0) return {};
        digest[n / 2] |= std::byte(digit << (n % 2 ? 0 : 4));
    }
    return digest;
}

class DebugSession;
// No public constructor, no fabricated allStopped flag. Issued only after a
// successful WaitForDebugEvent, revoked only by its owning session's continue.
class DebugStop final {
    friend class DebugSession;
    DebugStop(DebugSession& owner, DWORD pid, DWORD tid) noexcept : owner_(&owner),
        pid_(pid), tid_(tid), ownerThread_(GetCurrentThreadId()) {}
public:
    DebugStop(const DebugStop&) = delete;
    DebugStop& operator=(const DebugStop&) = delete;
    bool active() const noexcept { return pending_ && ownerThread_ == GetCurrentThreadId(); }
    DWORD pid() const noexcept { return pid_; }
    DWORD tid() const noexcept { return tid_; }
private:
    DebugSession* owner_;
    DWORD pid_, tid_, ownerThread_;
    bool pending_ = true;
};

constexpr char restoreMarker[] = "GRANDMP_NATIVE_COUNTER_RESTORE_V1";
bool restorationCase(std::wstring_view scenario) noexcept {
    return scenario != L"restore_failure" &&
        (scenario.starts_with(L"restore_") || scenario.starts_with(L"fatal_restore_"));
}
std::wstring_view restorationFault(std::wstring_view scenario) noexcept {
    if (scenario == L"fatal_restore_write") return L"fatal_write";
    if (scenario == L"fatal_restore_flush") return L"fatal_flush";
    if (scenario == L"fatal_restore_protection") return L"fatal_protection";
    if (scenario == L"fatal_restore_commit") return L"fatal_commit";
    return scenario.substr(8);  // restore_ prefix, already admitted by the CLI.
}
enum class Outcome { installed, restored, inspected, rejected, recovered, fatal };
Outcome expectedOutcome(std::wstring_view scenario) noexcept {
    if (scenario.starts_with(L"fatal_")) return Outcome::fatal;
    if (scenario == L"restore_success" || scenario == L"restore_repeat") return Outcome::restored;
    if (scenario == L"inspect") return Outcome::inspected;
    if (restorationCase(scenario) && scenario.starts_with(L"restore_")) return expectedOutcome(restorationFault(scenario));
    if (scenario == L"success" || scenario == L"repeat") return Outcome::installed;
    if (scenario == L"short_write" || scenario == L"flush_failure" ||
        scenario == L"readback_failure" || scenario == L"restore_failure" || scenario == L"commit_failure")
        return Outcome::recovered;
    return Outcome::rejected;
}
const char* label(Outcome value) {
    switch (value) {
    case Outcome::installed: return "INSTALLED";
    case Outcome::restored: return "RESTORED";
    case Outcome::inspected: return "INSPECTED";
    case Outcome::rejected: return "REJECTED";
    case Outcome::recovered: return "ROLLED_BACK";
    default: return "FATAL";
    }
}

// Faults act on real lab memory/APIs. They are explicit test injection; they do
// not turn real OS failures into success or make WriteProcessMemory atomic.
class Memory final {
public:
    Memory(HANDLE process, std::wstring_view scenario) : process_(process), scenario_(scenario) {}
    bool noWrites() const noexcept { return mutations == 0 && flushes_ == 0 && publications_ == 0; }
    bool untouched() const noexcept { return noWrites() && protections_ == 0; }
    bool recovery = false;
    unsigned mutations = 0;
    unsigned faultsConsumed = 0;
    bool protect(std::uint32_t address, DWORD desired, DWORD& old) {
        ++protections_;
        if ((scenario_ == L"protect_failure" && protections_ == 1) ||
            (scenario_ == L"restore_failure" && protections_ == 2) ||
            (scenario_ == L"fatal_protection" && (protections_ == 2 || (recovery && desired != PAGE_EXECUTE_READWRITE))))
            { ++faultsConsumed; return false; }
        return VirtualProtectEx(process_, reinterpret_cast<LPVOID>(address), 5, desired, &old) != FALSE;
    }
    bool write(std::uint32_t address, const NativeCounterCall& bytes) {
        ++mutations;
        if (scenario_ == L"fatal_write" && recovery) { ++faultsConsumed; return false; }
        SIZE_T written = 0;
        const bool partial = !recovery && (scenario_ == L"short_write" || scenario_ == L"fatal_write");
        if (partial) ++faultsConsumed;
        const auto length = partial ? 2U : 5U;
        const bool ok = WriteProcessMemory(process_, reinterpret_cast<LPVOID>(address),
            bytes.data(), length, &written) != FALSE;
        return ok && written == 5;
    }
    bool read(std::uint32_t address, NativeCounterCall& bytes) {
        ++reads_;
        if (!recovery && scenario_ == L"readback_failure" && reads_ == 2) {
            ++faultsConsumed;
            return readExact(process_, address, bytes.data(), 4) && false;
        }
        const bool ok = readExact(process_, address, bytes.data(), bytes.size());
        if (ok && !recovery && scenario_ == L"unexpected_bytes" && reads_ == 1) {
            ++faultsConsumed;
            bytes[0] = 0x90;
        }
        return ok;
    }
    bool flush(std::uint32_t address) {
        ++flushes_;
        if ((scenario_ == L"flush_failure" && flushes_ == 1) || scenario_ == L"fatal_flush") {
            ++faultsConsumed;
            return false;
        }
        return FlushInstructionCache(process_, reinterpret_cast<LPCVOID>(address), 5) != FALSE;
    }
    bool publish(std::uint32_t descriptor, std::uint32_t value) {
        ++publications_;
        if (scenario_ == L"fatal_commit" && recovery) { ++faultsConsumed; return false; }
        const bool partial = !recovery && (scenario_ == L"commit_failure" || scenario_ == L"fatal_commit");
        if (partial) ++faultsConsumed;
        const auto address = descriptor + offsetof(NativeCounterReadyV1, installed);
        const auto length = partial ? 2U : 4U;
        SIZE_T written = 0;
        std::uint32_t read = UINT32_MAX;
        return WriteProcessMemory(process_, reinterpret_cast<LPVOID>(address), &value, length, &written) &&
            written == sizeof(value) && readExact(process_, address, &read, sizeof(read)) && read == value;
    }
private:
    HANDLE process_;
    std::wstring_view scenario_;
    unsigned protections_ = 0, reads_ = 0, flushes_ = 0, publications_ = 0;
};

class DebugSession final {
public:
    DebugSession(const Image& host, const Image& module, std::wstring_view scenario, bool supervised = false,
                 std::array<std::byte, 32> expectedDigest = {})
        : host_(host), module_(module), scenario_(scenario), expectedDigest_(expectedDigest), supervised_(supervised), hoodlum_(
            grandmp::windows::recognizeGameVersion(host.evidence().inspection, host.evidence().digest) ==
                grandmp::windows::GameVersion::gta_sa_10us_hoodlum) {}
    int run(DWORD externalPid = 0) {
        try {
            return runSession(externalPid);
        } catch (...) {
            // Keep the session owner alive even for exceptions outside the
            // event handler or not derived from std::exception. Unwinding the
            // local event token must never imply permission to detach.
            if (external_ && attached_) {
                if (externalStop_ == std::string_view("event_owned")) externalStop_ = "event_token_unwound";
                return holdExternal("active session exception");
            }
            throw; // Preserve pre-attach and owned-child exception policy.
        }
    }
private:
    int runSession(DWORD externalPid) {
        external_ = externalPid != 0;
        Handle output;
        if (external_) {
            if ((!supervised_ && (scenario_ != L"inspect" || hoodlum_)) ||
                (supervised_ && hoodlum_ && !smokeScope())) return 2;
            pid_ = externalPid;
            process_ = Handle(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE,
                                          FALSE, pid_));
            if (!process_.valid()) { inventoryWin32("OpenProcess before attach"); return 2; }
            if (!prepareExternalInventory()) return 2;
            if (externalFault(L"before_attach_exception"))
                throw std::runtime_error("lab exception before attach");
            const auto processState = WaitForSingleObject(process_.get(), 0);
            if (processState == WAIT_FAILED) { inventoryWin32("WaitForSingleObject process before attach"); return 2; }
            if (processState != WAIT_TIMEOUT) { inventoryRefusal("process no longer live before attach"); return 2; }
            if (!DebugActiveProcess(pid_)) { inventoryWin32("DebugActiveProcess"); return 2; }
            attached_ = true;
            // Existing lab attach faults remain unavailable for Hoodlum.
            if (externalFault(L"before_policy_exception")) throw std::uint32_t{0xEE04};
            // Do not inherit the owned child's destructive failure policy.
            if (externalFault(L"policy_failure")) return holdExternal("injected kill-on-exit setup failure");
            if (!DebugSetProcessKillOnExit(FALSE)) return holdExternal("kill-on-exit setup");
            killOnExitDisabled_ = true;
            std::puts("external_policy=disabled");
            if (externalFault(L"after_policy_exception")) throw std::uint32_t{0xEE05};
            std::printf("external_attached pid=%lu pre_threads=%zu pre_modules=%zu writeEnabled=0\n",
                        pid_, beforeThreads_.size(), beforeModules_.size());
        } else {
            SECURITY_ATTRIBUTES inheritable{sizeof(inheritable), nullptr, TRUE};
            HANDLE pipeRead = nullptr, pipeWrite = nullptr;
            if (!CreatePipe(&pipeRead, &pipeWrite, &inheritable, 0)) return 2;
            output = Handle(pipeRead);
            Handle childOutput(pipeWrite);
            if (!SetHandleInformation(output.get(), HANDLE_FLAG_INHERIT, 0)) return 2;
            STARTUPINFOW startup{sizeof(startup)};
            startup.dwFlags = STARTF_USESTDHANDLES;
            startup.hStdOutput = startup.hStdError = childOutput.get();
            std::wstring command = L"\"" + host_.path().wstring() + L"\" \"" +
                                   module_.path().wstring() + (restorationCase(scenario_) ?
                                       L"\" debug_target_restore" : L"\" debug_target");
            PROCESS_INFORMATION child{};
            if (!CreateProcessW(host_.path().c_str(), command.data(), nullptr, nullptr, TRUE,
                DEBUG_ONLY_THIS_PROCESS | CREATE_NO_WINDOW, nullptr, host_.path().parent_path().c_str(),
                &startup, &child)) return 2;
            process_ = Handle(child.hProcess);
            Handle initialThread(child.hThread);
            childOutput = Handle();
            pid_ = child.dwProcessId;
            initialTid_ = child.dwThreadId;
            if (!DebugSetProcessKillOnExit(TRUE)) return killFailure("kill-on-exit setup");
        }
        auto deadline = GetTickCount64() + (externalFault(L"timeout") ? 0 : 12000);
        while (GetTickCount64() < deadline) {
            bool restoreDue = supervised_ && installation_ && observeUntil_ && GetTickCount64() >= observeUntil_;
            if (supervised_ && installation_ && focusObservation_ && !restoreBreakRequested_) {
                DWORD foregroundPid = 0;
                const HWND foreground = GetForegroundWindow();
                if (!foreground || !GetWindowThreadProcessId(foreground, &foregroundPid)) foregroundPid = 0;
                const auto state = focusObservation_->sample(GetTickCount64(), foregroundPid);
                if (state == grandmp::windows::NativeCounterFocusObservation::State::observing && !counterAtFocus_) {
                    std::uint64_t count = 0;
                    if (readCounter(installation_->ready, count)) counterAtFocus_ = count;
                    else focusCounterReadFailed_ = true;
                    std::printf("supervised_focus_acquired pid=%lu counter_read=%u\n", pid_, counterAtFocus_ ? 1U : 0U);
                }
                restoreDue = focusObservation_->finished() || focusCounterReadFailed_;
                if (restoreDue) std::printf("supervised_focus_result=%s counter_read_failed=%u\n",
                    focusObservation_->result(), focusCounterReadFailed_ ? 1U : 0U);
            }
            if (restoreDue) {
                observeUntil_ = 0;
                for (const auto& [id, thread] : threads_) breakBaseline_.insert(id);
                if (!DebugBreakProcess(process_.get())) return killFailure("restore break request failed", GetLastError());
                restoreBreakRequested_ = true;
                std::puts("supervised_restore_break_requested=1");
            }
            DEBUG_EVENT event{};
            if (!WaitForDebugEvent(&event, supervised_ && installation_ ? 50 : 1000)) {
                const DWORD error = GetLastError();
                if (error == ERROR_SEM_TIMEOUT) continue;
                return killFailure("WaitForDebugEvent", error);
            }
            if (external_) externalStop_ = "event_owned";
            DebugStop stop(*this, event.dwProcessId, event.dwThreadId);
            if (event.dwProcessId != pid_) return killFailure("unexpected process event");
            DWORD continuation = DBG_CONTINUE;
            try {
                switch (event.dwDebugEventCode) {
                case CREATE_PROCESS_DEBUG_EVENT: {
                    Handle image(event.u.CreateProcessInfo.hFile);
                    if (!trackModule(event.u.CreateProcessInfo.lpBaseOfImage, image.get()))
                        return killFailure("process module event invalid");
                    if (!host_.sameBackingFile(image.get())) return killFailure("host backing identity");
                    hostBase_ = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(event.u.CreateProcessInfo.lpBaseOfImage));
                    threads_.emplace(event.dwThreadId, duplicate(event.u.CreateProcessInfo.hThread));
                    if (!verifyImage(host_, hostBase_)) return killFailure("host PE identity");
                    break;
                }
                case CREATE_THREAD_DEBUG_EVENT:
                    if (installation_ && !restoreBreakRequested_ && externalFault(L"restore_thread_event_missing"))
                        break; // Lab: omit an actual new live thread from the ledger.
                    if (!threads_.emplace(event.dwThreadId, duplicate(event.u.CreateThread.hThread)).second)
                        return killFailure("duplicate thread event");
                    if (external_ && installation_) std::printf("inventory_thread_event=created tid=%lu\n", event.dwThreadId);
                    break;
                case EXIT_THREAD_DEBUG_EVENT:
                    if (external_ && beforeThreads_.contains(event.dwThreadId) && threads_.contains(event.dwThreadId) &&
                        !(installation_ && externalFault(L"restore_exit_event_missing"))) {
                        if (sameThread(beforeThreads_.at(event.dwThreadId).get(), threads_.at(event.dwThreadId).get())) {
                            exitedBeforeThreads_.insert(event.dwThreadId);
                            std::printf("inventory_pre_thread_exit tid=%lu\n", event.dwThreadId);
                        }
                    }
                    if (!threads_.erase(event.dwThreadId)) return killFailure("untracked thread exit");
                    if (external_ && installation_) std::printf("inventory_thread_event=exited tid=%lu\n", event.dwThreadId);
                    break;
                case LOAD_DLL_DEBUG_EVENT: {
                    Handle image(event.u.LoadDll.hFile);
                    if (!trackModule(event.u.LoadDll.lpBaseOfDll, image.get()))
                        return killFailure("DLL load event invalid");
                    if (image.valid() && module_.sameBackingFile(image.get())) {
                        if (moduleBase_) return killFailure("duplicate lab image");
                        moduleBase_ = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(event.u.LoadDll.lpBaseOfDll));
                        if (!verifyImage(module_, moduleBase_)) return killFailure("lab PE identity");
                    }
                    // The debugger's only handled software breakpoint is the
                    // system break-in points, verified against ntdll's image.
                    if (image.valid()) {
                        wchar_t name[32768]{};
                        if (GetFinalPathNameByHandleW(image.get(), name, 32768, FILE_NAME_NORMALIZED)) {
                            if (_wcsicmp(std::filesystem::path(name).filename().c_str(), L"ntdll.dll") == 0)
                                ntdllBase_ = reinterpret_cast<std::uintptr_t>(event.u.LoadDll.lpBaseOfDll);
                        }
                    }
                    break;
                }
                case UNLOAD_DLL_DEBUG_EVENT: {
                    const auto base = reinterpret_cast<std::uintptr_t>(event.u.UnloadDll.lpBaseOfDll);
                    if (!eventModules_.erase(base)) return killFailure("untracked module unload");
                    if (base == moduleBase_ || base == hostBase_ || base == ntdllBase_)
                        return killFailure("protected module unloaded");
                    moduleEvents_.erase(base);
                    if (external_ && installation_) std::printf("inventory_module_event=removed base=%08lX\n", base);
                    break;
                }
                case EXCEPTION_DEBUG_EVENT: {
                    const auto& exception = event.u.Exception;
                    MEMORY_BASIC_INFORMATION location{};
                    const bool restoreBreak = supervised_ && restoreBreakRequested_ &&
                        !breakBaseline_.contains(event.dwThreadId);
                    const bool systemInitial = ((!initialBreakpoint_ && !readySeen_) || restoreBreak) &&
                        (restoreBreak || (external_ ? !beforeThreads_.contains(event.dwThreadId) : event.dwThreadId == initialTid_)) &&
                        exception.dwFirstChance &&
                        exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT && ntdllBase_ &&
                        VirtualQueryEx(process_.get(), exception.ExceptionRecord.ExceptionAddress,
                            &location, sizeof(location)) == sizeof(location) &&
                        reinterpret_cast<std::uintptr_t>(location.AllocationBase) == ntdllBase_ &&
                        location.State == MEM_COMMIT && location.Type == MEM_IMAGE &&
                        (location.Protect == PAGE_EXECUTE_READ || location.Protect == PAGE_EXECUTE);
                    if (systemInitial) {
                        initialBreakpoint_ = true;
                        if (external_) {
                            if (externalFault(L"lost_stop")) {
                                stop.pending_ = false; // Lab fault: revoke our token without continuing OS event.
                                externalStop_ = "event_token_revoked";
                                return holdExternal("stop ownership lost");
                            }
                            if (restoreBreak && externalFault(L"restore_stop_lost")) {
                                stop.pending_ = false; // Lab: retain OS stop but lose our token.
                                externalStop_ = "event_token_revoked";
                            }
                            if (!(restoreBreak ? restorationInventory(stop) : admissionInventory(stop)))
                                return holdExternal("external inventory refused (see inventory_refusal)");
                            if (!contexts(stop, hoodlum_ ? 0x53E981 : hostBase_ + host_.exported("GrandMPNativeCounterHostCallsite")))
                                return holdExternal("external contexts invalid");
                            if (supervised_) {
                                if (restoreBreak) return supervisedRestore(stop);
                                if (installation_ || !supervisedInstall(stop)) return holdExternal("supervised installation did not commit");
                                if (!resume(stop, DBG_CONTINUE)) return holdExternal("supervised install continue failed");
                                const auto resumedAt = GetTickCount64();
                                observeUntil_ = resumedAt + 1000;
                                if (hoodlum_) {
                                    focusObservation_.emplace(pid_, resumedAt);
                                    observeUntil_ = resumedAt + grandmp::windows::NativeCounterFocusObservation::budgetMs;
                                    std::puts("supervised_focus_wait budget_ms=10000 observation_ms=1500 return_focus_to_target=1");
                                }
                                deadline = observeUntil_ + 12000;
                                continue;
                            }
                            const auto outcome = externalFault(L"inspection_failure") ?
                                Outcome::rejected : readyTransaction(stop);
                            std::printf("external_inspect=%s pid=%lu threads=%zu modules=%zu writeEnabled=0\n",
                                        label(outcome), pid_, threads_.size(), eventModules_.size());
                            if (outcome != Outcome::inspected && outcome != Outcome::rejected)
                                return holdExternal("indeterminate inspection");
                            if (externalFault(L"disconnect")) return holdExternal("verified clean stop for disconnect test");
                            if (!resume(stop, DBG_CONTINUE)) return holdExternal("continue ownership lost");
                            if (externalFault(L"detach_failure") || !DebugActiveProcessStop(pid_))
                                return holdExternal("detach failed");
                            attached_ = false;
                            std::puts("external_detached=1 terminated=0");
                            return outcome == Outcome::inspected ? 0 : 3;
                        }
                    } else if (external_) return holdExternal("unclassified external exception");
                    else continuation = DBG_EXCEPTION_NOT_HANDLED;
                    break;
                }
                case OUTPUT_DEBUG_STRING_EVENT:
                    if (external_) break; // External transactions use authenticated system break-ins only.
                    if (isMarker(event.u.DebugString, grandmp::windows::nativeCounterReadyMarker) ||
                        isMarker(event.u.DebugString, restoreMarker)) {
                        const bool restoring = isMarker(event.u.DebugString, restoreMarker);
                        if (restoring) {
                            if (!restorationCase(scenario_) || !readySeen_ || restoreSeen_)
                                return killFailure("unexpected restore marker");
                            restoreSeen_ = true;
                        } else {
                            if (readySeen_) return killFailure("duplicate READY marker");
                            readySeen_ = true;
                        }
                        const auto outcome = restoring ? restoreTransaction(stop) : readyTransaction(stop);
                        std::printf("transaction=%s pid=%lu threads=%zu scenario=%ls\n",
                            label(outcome), pid_, threads_.size(), scenario_.data());
                        if (outcome == Outcome::fatal) {
                            if (external_) return holdExternal("external FATAL");
                            const bool expected = scenario_.starts_with(L"fatal_") && fatalRecoveryProved_;
                            // Never continue a living process with indeterminate
                            // bytes. Only this child is terminable by this tool.
                            if (!TerminateProcess(process_.get(), 0xEE01)) return 7;
                            // Windows can defer final teardown while this debug
                            // event is retained. Exit this debugger with kill-on-
                            // exit enabled; the independent test supervisor checks
                            // the child handle/exit. Never continue the pending stop.
                            std::printf("fatal_termination_requested=1 pid=%lu continue_after_fatal=0\n", pid_);
                            return expected ? 0 : 7;
                        }
                        const auto expected = !restoring && restorationCase(scenario_) ?
                            Outcome::installed : expectedOutcome(scenario_);
                        if (outcome != expected)
                            return killFailure("unexpected transaction outcome");
                    }
                    break;
                case EXIT_PROCESS_DEBUG_EVENT: {
                    const DWORD code = event.u.ExitProcess.dwExitCode;
                    if (!resume(stop, continuation)) return killFailure("exit event ownership");
                    if (external_) { attached_ = false; return 3; }
                    if (WaitForSingleObject(process_.get(), 3000) != WAIT_OBJECT_0) return 7;
                    char text[8192]{};
                    DWORD got = 0;
                    if (ReadFile(output.get(), text, sizeof(text) - 1, &got, nullptr)) std::fputs(text, stdout);
                    if (!readySeen_ || (restorationCase(scenario_) && !restoreSeen_) || code != 0) return 8;
                    std::puts("debug_transaction_host_exit=0");
                    return 0;
                }
                default: break;
                }
                if (stop.active() && !resume(stop, continuation)) return killFailure("ContinueDebugEvent");
            } catch (const std::exception& exception) {
                std::fprintf(stderr, "debug gate failed: %s\n", exception.what());
                if (external_) inventoryRefusal(exception.what());
                return killFailure("validation exception");
            }
        }
        return killFailure("bounded debugger timeout");
    }
private:
    bool smokeScope() const {
        return grandmp::windows::nativeCounterSmokeScope(host_.path().generic_wstring(),
            module_.path().generic_wstring(), host_.evidence(), module_.evidence().digest, expectedDigest_);
    }
    bool confirmSupervised(bool install) const {
        if (!preview_) return false;
        if (hoodlum_) {
            DWORD mode = 0;
            if (!GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode)) return false;
        }
        const auto command = std::string(install ? "INSTALL_AND_RESTORE " : "RESTORE ") +
            std::to_string(pid_) + " " + std::to_string(nonce_) + " " + digestText(module_.evidence().digest);
        std::printf("supervised_scope pid=%lu exe=%ls asi=%ls callsite=%08X original=%08X wrapper=%08X descriptor=%08X\n",
            pid_, host_.path().c_str(), module_.path().c_str(), preview_->callsite,
            preview_->originalTarget, preview_->wrapper, preview_->descriptorAddress);
        std::puts("RISK: debugger death can kill or resume target; ABI/in-flight callbacks remain experimental.");
        std::printf("supervised_confirm=%s\n", command.c_str());
        char input[256]{};
        if (!std::fgets(input, sizeof(input), stdin)) return false;
        std::string answer(input);
        if (answer.empty() || answer.back() != '\n') return false;
        while (!answer.empty() && (answer.back() == '\n' || answer.back() == '\r')) answer.pop_back();
        return answer == command;
    }
    bool readCounter(const NativeCounterReadyV1& ready, std::uint64_t& count) const {
        std::uint64_t second = 0;
        return page(ready.counterAddress, 8, moduleBase_, PAGE_READWRITE) &&
            readExact(process_.get(), ready.counterAddress, &count, sizeof(count)) &&
            readExact(process_.get(), ready.counterAddress, &second, sizeof(second)) && count == second;
    }
    bool supervisedInstall(DebugStop& stop) {
        inspectionOnly_ = true;
        supervisedOutcome_ = readyTransaction(stop);
        inspectionOnly_ = false;
        if (supervisedOutcome_ != Outcome::inspected) return false;
        NativeCounterReadyV1 ready{};
        if (!readExact(process_.get(), moduleBase_ + module_.exported("GrandMPNativeCounterReady"), &ready, sizeof(ready)))
            return false;
        preview_ = ready;
        nonce_ = GetTickCount64() ^ (std::uint64_t{pid_} << 32);
        if (!confirmSupervised(true) || !stop.active() || stop.owner_ != this ||
            !killOnExitDisabled_ || (hoodlum_ && !smokeScope())) return false;
        // Rights are acquired only after independent confirmation, for this
        // already-bound process. CREATE_THREAD is needed by DebugBreakProcess
        // for the OS debugger break-in thread; no engine routine is invoked.
        // TERMINATE is never acquired for an external target.
        Handle writable(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE |
            PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_CREATE_THREAD, FALSE, pid_));
        FILETIME before{}, after{}, exit{}, kernel{}, user{};
        if (!writable.valid() || GetProcessId(writable.get()) != pid_ ||
            !GetProcessTimes(process_.get(), &before, &exit, &kernel, &user) ||
            !GetProcessTimes(writable.get(), &after, &exit, &kernel, &user) ||
            CompareFileTime(&before, &after) != 0) return false;
        process_ = std::move(writable);
        authorized_ = true;
        supervisedOutcome_ = readyTransaction(stop); // Revalidates again, under the same owned stop.
        std::printf("supervised_install=%s\n", label(supervisedOutcome_));
        return supervisedOutcome_ == Outcome::installed && installation_ && readCounter(installation_->ready, counterBefore_);
    }
    int supervisedRestore(DebugStop& stop) {
        restoreBreakRequested_ = false;
        if (!installation_ || !authorized_) return holdExternal("missing supervised receipt");
        std::uint64_t after = 0;
        if (!readCounter(installation_->ready, after)) return holdExternal("counter observation failed");
        std::printf("supervised_counter before=%llu after=%llu increased=%u\n",
            static_cast<unsigned long long>(counterBefore_), static_cast<unsigned long long>(after),
            after > counterBefore_ ? 1U : 0U);
        const bool callbackObserved = focusObservation_ ?
            counterAtFocus_ && !focusCounterReadFailed_ && focusObservation_->callbackObserved(*counterAtFocus_, after) :
            after > counterBefore_;
        if (focusObservation_) std::printf("supervised_focus_counter before=%llu after=%llu baseline_valid=%u callback_observed=%u\n",
            static_cast<unsigned long long>(counterAtFocus_.value_or(0)), static_cast<unsigned long long>(after),
            counterAtFocus_ ? 1U : 0U, callbackObserved ? 1U : 0U);
        if (!confirmSupervised(false)) return holdExternal("restore confirmation refused");
        supervisedOutcome_ = restoreTransaction(stop);
        std::printf("supervised_restore=%s\n", label(supervisedOutcome_));
        if (supervisedOutcome_ != Outcome::restored) return holdExternal("supervised restoration did not commit");
        authorized_ = false;
        if (!resume(stop, DBG_CONTINUE)) return holdExternal("restore continue failed");
        if (!DebugActiveProcessStop(pid_)) return holdExternal("restore detach failed");
        attached_ = false;
        std::puts("supervised_detached=1 original_call_restored=1 wrapper_remains_resident=1");
        return callbackObserved ? 0 : 4;
    }
    bool externalFault(std::wstring_view fault) const noexcept {
        wchar_t value[64]{};
        const auto length = GetEnvironmentVariableW(L"GRANDMP_LAB_ATTACH_FAULT", value, 64);
        return external_ && !hoodlum_ && length && length < 64 && std::wstring_view(value) == fault;
    }
    int holdExternal(const char* reason) const noexcept {
        std::fprintf(stderr, "external_HELD reason=%s pid=%lu continue_after_failure=0 detach_after_failure=0 terminate=0 policy=%s stop=%s\n",
                     reason, pid_, killOnExitDisabled_ ? "disabled" : "unconfirmed", externalStop_);
        if (supervised_) {
            std::fprintf(stderr, "supervised_HELD outcome=%s receipt=%u authorization=%u manual_decision_required=1\n",
                label(supervisedOutcome_), installation_ ? 1U : 0U, authorized_ ? 1U : 0U);
            if (preview_) {
                NativeCounterCall bytes{};
                NativeCounterReadyV1 current{};
                const bool descriptorRead = readExact(process_.get(), preview_->descriptorAddress, &current, sizeof(current));
                std::fprintf(stderr, "supervised_descriptor read=%u state=%u installed=%u residence=%u capable=%u\n",
                    descriptorRead ? 1U : 0U, current.state, current.installed, current.residencyConfirmed, current.wrapperCapable);
                MEMORY_BASIC_INFORMATION region{};
                const bool read = readExact(process_.get(), preview_->callsite, bytes.data(), bytes.size());
                const bool queried = VirtualQueryEx(process_.get(), reinterpret_cast<LPCVOID>(preview_->callsite),
                    &region, sizeof(region)) == sizeof(region);
                std::fprintf(stderr, "supervised_evidence callsite=%08X wrapper=%08X descriptor=%08X read=%u bytes=%02X%02X%02X%02X%02X protection=%08lX\n",
                    preview_->callsite, preview_->wrapper, preview_->descriptorAddress, read ? 1U : 0U,
                    bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], queried ? region.Protect : 0);
            }
        }
        // No destructor/timeout may implicitly detach a pending, unowned event.
        // Resolution belongs to the independent supervisor/operator; this loop
        // never treats consent to install as consent to terminate.
        // Forced debugger termination remains an OS-level limitation, not a safe detach.
        for (;;) Sleep(1000);
    }
    enum class InventoryPhase { beforeAttach, admission, restoration };
    bool inventoryRefusal(const char* reason, std::uintptr_t subject = 0, DWORD error = ERROR_SUCCESS) const {
        std::fprintf(stderr, "inventory_refusal phase=%s reason=%s subject=%08lX win32=%lu\n",
            inventoryPhase_ == InventoryPhase::beforeAttach ? "before_attach" :
            inventoryPhase_ == InventoryPhase::admission ? "admission" : "restoration", reason, subject, error);
        return false;
    }
    bool inventoryWin32(const char* operation) const {
        const DWORD error = GetLastError(); // Capture before logging/cleanup/another API.
        return inventoryRefusal(operation, 0, error);
    }
    bool sameThread(HANDLE first, HANDLE second) const {
        FILETIME a{}, b{}, exit{}, kernel{}, user{};
        if (!GetThreadTimes(first, &a, &exit, &kernel, &user)) return inventoryWin32("GetThreadTimes before");
        if (!GetThreadTimes(second, &b, &exit, &kernel, &user)) return inventoryWin32("GetThreadTimes event");
        return CompareFileTime(&a, &b) == 0;
    }
    bool trackModule(void* address, HANDLE file) {
        const auto base = reinterpret_cast<std::uintptr_t>(address);
        if (installation_ && externalFault(L"restore_module_event_missing"))
            return true; // Lab: omit a real LOAD_DLL event from the ledger.
        if (!eventModules_.insert(base).second) return inventoryRefusal("duplicate module load", base);
        if (!external_) return true;
        if (!file || file == INVALID_HANDLE_VALUE) return inventoryRefusal("module event backing unavailable", base);
        wchar_t path[32768]{};
        const auto length = GetFinalPathNameByHandleW(file, path, 32768, FILE_NAME_NORMALIZED);
        if (!length) return inventoryWin32("GetFinalPathNameByHandleW module event");
        if (length >= 32768) return inventoryRefusal("module event path truncated", base);
        // Retain a read-only file lock for every live module, including ones
        // added during observation. Match the debugger's actual backing file.
        std::wstring normalized(path);
        if (normalized.starts_with(L"\\\\?\\UNC\\")) normalized = L"\\\\" + normalized.substr(8);
        else if (normalized.starts_with(L"\\\\?\\")) normalized.erase(0, 4);
        Image image(normalized, grandmp::windows::nativeCounterInventoryFileLimit);
        if (!image.sameBackingFile(file) || !verifyImage(image, static_cast<std::uint32_t>(base)))
            return inventoryRefusal("module event identity", base);
        moduleEvents_.emplace(base, ModuleEvent{image.identity(), image.takeBackingFile()});
        if (installation_) std::printf("inventory_module_event=added base=%08lX\n", base);
        return true;
    }
    bool prepareExternalInventory() { return externalInventory(InventoryPhase::beforeAttach, nullptr); }
    bool admissionInventory(const DebugStop& stop) { return externalInventory(InventoryPhase::admission, &stop); }
    bool restorationInventory(const DebugStop& stop) {
        if (!installation_ || !authorized_ || !restoreBreakRequested_)
            return inventoryRefusal("missing restoration receipt/authorization/request");
        return externalInventory(InventoryPhase::restoration, &stop);
    }
    bool externalInventory(InventoryPhase phase, const DebugStop* stop) {
        inventoryPhase_ = phase;
        const bool stopped = phase != InventoryPhase::beforeAttach;
        const bool restoration = phase == InventoryPhase::restoration;
        if (stopped && (!stop || !stop->active() || stop->owner_ != this || stop->pid() != pid_ ||
            !threads_.contains(stop->tid()))) return inventoryRefusal("unauthenticated stop");
        const auto processId = GetProcessId(process_.get());
        if (!processId) return inventoryWin32("GetProcessId");
        if (processId != pid_) return inventoryRefusal("process PID identity");
        FILETIME created{}, exit{}, kernel{}, user{};
        if (!GetProcessTimes(process_.get(), &created, &exit, &kernel, &user)) return inventoryWin32("GetProcessTimes");
        if (!stopped) processCreated_ = created;
        else if (CompareFileTime(&created, &processCreated_) != 0) return inventoryRefusal("process creation identity");
        wchar_t path[32768]{};
        DWORD length = 32768;
        if (!QueryFullProcessImageNameW(process_.get(), 0, path, &length)) return inventoryWin32("QueryFullProcessImageNameW");
        if (std::filesystem::canonical(path) != host_.path()) return inventoryRefusal("executable path identity");
        Handle file(CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
        if (!file.valid()) return inventoryWin32("CreateFileW executable");
        if (!host_.sameBackingFile(file.get())) return inventoryRefusal("executable backing identity");
        BOOL debugged = FALSE;
        if (!stopped) {
            if (!CheckRemoteDebuggerPresent(process_.get(), &debugged)) return inventoryWin32("CheckRemoteDebuggerPresent");
            if (debugged) return inventoryRefusal("already debugged");
        }
        Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid_));
        MODULEENTRY32W module{sizeof(module)};
        if (!snapshot.valid()) return inventoryWin32("CreateToolhelp32Snapshot modules");
        if (!Module32FirstW(snapshot.get(), &module)) return inventoryWin32("Module32FirstW");
        decltype(beforeModules_) modules;
        bool hostSeen = false, labSeen = false;
        do {
            if (stopped && externalFault(L"inspection_exception")) {
                std::puts("external_lab_exception=inspection");
                // Non-std C++ exception deliberately exercises the outer
                // session boundary, rather than the existing event catch.
                throw std::uint32_t{0xEE03};
            }
            Image image(module.szExePath, grandmp::windows::nativeCounterInventoryFileLimit);
            const auto base = reinterpret_cast<std::uintptr_t>(module.modBaseAddr);
            MEMORY_BASIC_INFORMATION region{};
            if (!verifyImage(image, static_cast<std::uint32_t>(base)) || image.size() != module.modBaseSize)
                return inventoryRefusal("module changed PE/size", base);
            if (VirtualQueryEx(process_.get(), module.modBaseAddr, &region, sizeof(region)) != sizeof(region))
                return inventoryWin32("VirtualQueryEx module");
            if (region.State != MEM_COMMIT || region.Type != MEM_IMAGE || region.AllocationBase != module.modBaseAddr)
                return inventoryRefusal("module changed mapping", base);
            Handle backing(CreateFileW(module.szExePath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
            if (!backing.valid()) return inventoryWin32("CreateFileW module snapshot");
            const bool admittedHost = host_.sameBackingFile(backing.get());
            hostSeen |= admittedHost;
            const bool admittedModule = module_.sameBackingFile(backing.get());
            if (admittedModule && image.path() != module_.path()) return inventoryRefusal("approved module path identity", base);
            if (stopped && ((admittedHost && base != hostBase_) || (admittedModule && base != moduleBase_)))
                return inventoryRefusal("protected module base identity", base);
            labSeen |= admittedModule;
            if (!modules.emplace(base, image.identity()).second) return inventoryRefusal("duplicate snapshot module", base);
        } while (Module32NextW(snapshot.get(), &module));
        const DWORD moduleError = GetLastError();
        if (moduleError != ERROR_NO_MORE_FILES) return inventoryRefusal("Module32NextW", 0, moduleError);
        if (!hostSeen || !labSeen) return inventoryRefusal("protected module removed/identity");
        Handle threadSnapshot(CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0));
        THREADENTRY32 thread{sizeof(thread)};
        if (!threadSnapshot.valid()) return inventoryWin32("CreateToolhelp32Snapshot threads");
        if (!Thread32First(threadSnapshot.get(), &thread)) return inventoryWin32("Thread32First");
        std::set<DWORD> ids;
        do {
            if (thread.th32OwnerProcessID != pid_) continue;
            ids.insert(thread.th32ThreadID);
            if (!stopped) {
                Handle handle(OpenThread(THREAD_QUERY_INFORMATION | THREAD_GET_CONTEXT | SYNCHRONIZE, FALSE, thread.th32ThreadID));
                if (!handle.valid()) return inventoryWin32("OpenThread before attach");
                const auto owner = GetProcessIdOfThread(handle.get());
                if (!owner) return inventoryWin32("GetProcessIdOfThread before attach");
                if (owner != pid_) return inventoryRefusal("pre-thread identity", thread.th32ThreadID);
                beforeThreads_.emplace(thread.th32ThreadID, std::move(handle));
            }
        } while (Thread32Next(threadSnapshot.get(), &thread));
        const DWORD threadError = GetLastError();
        if (threadError != ERROR_NO_MORE_FILES) return inventoryRefusal("Thread32Next", 0, threadError);
        if (ids.empty()) return inventoryRefusal("empty thread snapshot");
        if (!stopped) { beforeModules_ = std::move(modules); return true; }
        if (restoration && externalFault(L"restore_module_changed") && !modules.empty())
            modules.begin()->second.digest[0] ^= std::byte{1}; // Lab identity mismatch.
        std::size_t added = 0, removed = 0, changed = 0;
        for (const auto& [base, identity] : modules) {
            const char* delta = nullptr;
            if (!beforeModules_.contains(base)) { ++added; delta = "added"; }
            else if (beforeModules_.at(base) != identity) { ++changed; delta = "changed"; }
            if (delta) std::printf("inventory_module_delta=%s base=%08lX path=%ls\n", delta, base, identity.path.c_str());
        }
        for (const auto& [base, identity] : beforeModules_) if (!modules.contains(base)) {
            ++removed;
            std::printf("inventory_module_delta=removed base=%08lX path=%ls\n", base, identity.path.c_str());
        }
        std::printf("inventory_delta phase=%s modules_added=%zu modules_removed=%zu modules_changed=%zu pre_threads_exited=%zu\n",
            restoration ? "restoration" : "admission", added, removed, changed, exitedBeforeThreads_.size());
        if (!restoration && (added || removed || changed)) return inventoryRefusal("admission modules added/removed/changed");
        if (modules.size() != moduleEvents_.size() || modules.size() != eventModules_.size())
            return inventoryRefusal("module snapshot/events count");
        for (const auto& [base, identity] : modules) {
            if (!eventModules_.contains(base) || !moduleEvents_.contains(base))
                return inventoryRefusal("module added without load event", base);
            if (moduleEvents_.at(base).identity != identity)
                return inventoryRefusal("module changed without matching load event", base);
        }
        if (ids.size() != threads_.size()) return inventoryRefusal("thread snapshot/events count");
        for (const auto& [id, handle] : beforeThreads_) {
            const auto state = WaitForSingleObject(handle.get(), 0);
            if (state == WAIT_FAILED) return inventoryWin32("WaitForSingleObject pre-thread");
            if (exitedBeforeThreads_.contains(id)) {
                if (!restoration || state != WAIT_OBJECT_0)
                    return inventoryRefusal("pre-thread exited outside restoration or unconfirmed exit", id);
            } else if (state != WAIT_TIMEOUT || !ids.contains(id) || !threads_.contains(id) ||
                !sameThread(handle.get(), threads_.at(id).get()))
                return inventoryRefusal("pre-thread missing/identity without exit event", id);
        }
        for (const auto& [id, handle] : threads_) if (!ids.contains(id)) return inventoryRefusal("thread snapshot/events ID", id);
        if (externalFault(L"inventory_failure")) return inventoryRefusal("injected inventory failure");
        std::printf("inventory_reconciled phase=%s threads=%zu modules=%zu\n",
            restoration ? "restoration" : "admission", ids.size(), modules.size());
        return true;
    }
    bool resume(DebugStop& stop, DWORD disposition) {
        if (!stop.active() || stop.owner_ != this || stop.pid_ != pid_) return false;
        if (!ContinueDebugEvent(stop.pid_, stop.tid_, disposition)) return false;
        stop.pending_ = false;
        if (external_) externalStop_ = "event_released_queue_unknown";
        return true;
    }
    int killFailure(const char* reason, DWORD error = ERROR_SUCCESS) noexcept {
        // Logical refusals have no Win32 error; never report a stale last error
        // such as ERROR_NO_MORE_FILES from a successful enumeration.
        std::fprintf(stderr, "lab NO-GO: %s win32=%lu\n", reason, error);
        if (external_ && attached_) return holdExternal(reason);
        if (!external_ && process_.valid()) {
            (void)TerminateProcess(process_.get(), 0xEE02);
            (void)WaitForSingleObject(process_.get(), 3000);
        }
        return 9;
    }
    bool verifyImage(const Image& image, std::uint32_t base) const {
        IMAGE_DOS_HEADER dos{};
        if (!readExact(process_.get(), base, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
            dos.e_lfanew <= 0 || static_cast<std::uint32_t>(dos.e_lfanew) > image.size() - sizeof(IMAGE_NT_HEADERS32)) return false;
        IMAGE_NT_HEADERS32 nt{};
        return readExact(process_.get(), base + dos.e_lfanew, &nt, sizeof(nt)) &&
            nt.Signature == IMAGE_NT_SIGNATURE && nt.FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
            nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC &&
            nt.OptionalHeader.SizeOfImage == image.size() && nt.FileHeader.TimeDateStamp == image.timestamp();
    }
    bool isMarker(const OUTPUT_DEBUG_STRING_INFO& event, std::string_view marker) const {
        if (event.fUnicode || event.nDebugStringLength == 0 || event.nDebugStringLength > 128) return false;
        char message[129]{};
        if (!readExact(process_.get(), reinterpret_cast<std::uintptr_t>(event.lpDebugStringData),
                       message, event.nDebugStringLength)) return false;
        return std::string_view(message) == marker;
    }
    bool contexts(const DebugStop& stop, std::uint32_t source) const {
        if (!stop.active() || stop.owner_ != this || stop.pid() != pid_ || !threads_.contains(stop.tid()))
            return inventoryRefusal("context unauthenticated stop");
        Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0));
        if (!snapshot.valid()) return inventoryWin32("CreateToolhelp32Snapshot contexts");
        THREADENTRY32 entry{sizeof(entry)};
        std::set<DWORD> observed;
        if (!Thread32First(snapshot.get(), &entry)) return inventoryWin32("Thread32First contexts");
        do { if (entry.th32OwnerProcessID == pid_) observed.insert(entry.th32ThreadID); }
        while (Thread32Next(snapshot.get(), &entry));
        const DWORD error = GetLastError();
        if (error != ERROR_NO_MORE_FILES) return inventoryRefusal("Thread32Next contexts", 0, error);
        if (observed.size() != threads_.size()) {
            std::fprintf(stderr, "thread_inventory_mismatch snapshot=%zu events=%zu\n", observed.size(), threads_.size());
            return inventoryRefusal("context snapshot/events count");
        }
        for (const auto& [id, thread] : threads_) {
            const auto owner = GetProcessIdOfThread(thread.get());
            if (!owner) return inventoryWin32("GetProcessIdOfThread context");
            if (owner != pid_) return inventoryRefusal("context thread identity", id);
            if (external_) {
                const auto state = WaitForSingleObject(thread.get(), 0);
                if (state == WAIT_FAILED) return inventoryWin32("WaitForSingleObject context thread");
                if (state != WAIT_TIMEOUT) return inventoryRefusal("context thread no longer live", id);
            }
            if (externalFault(L"context_failure") ||
                (installation_ && externalFault(L"restore_context_failure"))) return inventoryRefusal("injected context failure", id);
            if (!observed.contains(id) || scenario_ == L"context_failure" ||
                (restoring_ && scenario_ == L"restore_context_failure")) return inventoryRefusal("context thread missing/fault", id);
            CONTEXT context{};
            context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
            if (installation_ && externalFault(L"restore_context_win32")) {
                // Real Win32 error on a deliberately invalid lab handle.
                if (!GetThreadContext(nullptr, &context)) return inventoryWin32("GetThreadContext lab invalid handle");
                return inventoryRefusal("unexpected lab GetThreadContext success", id);
            }
            if (!GetThreadContext(thread.get(), &context)) return inventoryWin32("GetThreadContext");
            if (scenario_ == L"context_overlap" ||
                (restoring_ && scenario_ == L"restore_context_overlap") ||
                (installation_ && externalFault(L"restore_context_overlap"))) context.Eip = source + 2;
            if (context.Eip >= source && std::uint64_t{context.Eip} < std::uint64_t{source} + 5)
                return inventoryRefusal("context EIP overlaps CALL", id);
        }
        return true;
    }
    bool queryProtection(std::uint32_t address, DWORD expected) const {
        MEMORY_BASIC_INFORMATION current{};
        return VirtualQueryEx(process_.get(), reinterpret_cast<LPCVOID>(address), &current, sizeof(current)) == sizeof(current) &&
               current.Protect == expected;
    }
    bool page(std::uint32_t address, std::size_t length, std::uint32_t owner, DWORD protection) const {
        MEMORY_BASIC_INFORMATION region{};
        return VirtualQueryEx(process_.get(), reinterpret_cast<LPCVOID>(address), &region, sizeof(region)) == sizeof(region) &&
            region.State == MEM_COMMIT && region.Type == MEM_IMAGE && region.Protect == protection &&
            reinterpret_cast<std::uintptr_t>(region.AllocationBase) == owner &&
            std::uint64_t(address) + length <= reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize;
    }
    bool stoppedBinding(const DebugStop& stop, const NativeCounterReadyV1& ready,
                        const NativeCounterCall& original) const {
        if (!contexts(stop, ready.callsite)) return false;
        if (!verifyImage(host_, hostBase_) || !verifyImage(module_, moduleBase_))
            return inventoryRefusal("transaction image identity");
        NativeCounterReadyV1 first{}, second{}, expected = ready;
        expected.installed = restoring_ ? 1U : 0U;
        const auto data = moduleBase_ + module_.exported("GrandMPNativeCounterReady");
        if (!readExact(process_.get(), data, &first, sizeof(first)) ||
            !readExact(process_.get(), data, &second, sizeof(second)) ||
            std::memcmp(&first, &second, sizeof(first)) || std::memcmp(&second, &expected, sizeof(second)))
            return inventoryRefusal("receipt/descriptor identity/state");
        const auto wrapper = moduleBase_ + module_.exported("GrandMPNativeCounterWrapperHost");
        if (ready.processId != pid_ || ready.moduleBase != moduleBase_ || ready.moduleSize != module_.size() ||
            ready.descriptorAddress != data || ready.wrapper != wrapper || !ready.threadId ||
            ready.counterAddress < moduleBase_ || ready.counterAddress % 8 ||
            std::uint64_t(ready.counterAddress) + 8 > std::uint64_t(moduleBase_) + module_.size() ||
            !module_.executable(wrapper - moduleBase_) ||
            !page(wrapper, 1, moduleBase_, PAGE_EXECUTE_READ) ||
            !page(ready.originalTarget, 1, hostBase_, PAGE_EXECUTE_READ) ||
            !page(data, sizeof(ready), moduleBase_, PAGE_READWRITE) ||
            !page(ready.counterAddress, 8, moduleBase_, PAGE_READWRITE)) return false;
        if (hoodlum_) {
            // Backing files were matched by file ID and bytes at the debugger's
            // process/DLL events and remain locked against replacement/writes.
            // Restore uses the recorded original bytes only after checking the
            // installed descriptor; transact separately verifies the live CALL.
            grandmp::windows::NativeCounterPhysicalSnapshot facts{};
            facts.processId = pid_; facts.executableBase = hostBase_; facts.moduleBase = moduleBase_;
            facts.loadedModuleFileDigest = module_.evidence().digest;
            facts.first = facts.second = ready;
            facts.callBytes = original;
            if (module_.path().filename() != L"GrandMPNativeCounterExperimental.asi" ||
                !grandmp::windows::dryRunNativeCounterAdmission(host_.evidence(), module_.evidence(), facts).plan)
                return false;
        } else {
            if (host_.path().filename() != L"grandmp_native_counter_host.exe" ||
                module_.path().filename() != L"GrandMPNativeCounterLab.dll" ||
                ready.callsite != hostBase_ + host_.exported("GrandMPNativeCounterHostCallsite") ||
                ready.originalTarget != hostBase_ + host_.exported("GrandMPNativeCounterHostOriginal") ||
                !host_.executable(ready.callsite - hostBase_, 6) ||
                !host_.executable(ready.originalTarget - hostBase_)) return false;
        }
        return true;
    }
    Outcome transact(DebugStop& stop, const NativeCounterReadyV1& ready, const NativeCounterCall& original,
                     Memory& memory, std::uint64_t wrapper) {
        if (!stoppedBinding(stop, ready, original)) return Outcome::rejected;
        const auto plan = grandmp::windows::planNativeCounterCall(ready, original);
        if (!plan || wrapper != plan->wrapper) return Outcome::rejected;
        const auto& expected = restoring_ ? plan->replacement : plan->original;
        const auto& desired = restoring_ ? plan->original : plan->replacement;
        const auto desiredTarget = restoring_ ? plan->originalTarget : plan->wrapper;
        const std::uint32_t beforeInstalled = restoring_ ? 1U : 0U;
        const std::uint32_t afterInstalled = restoring_ ? 0U : 1U;
        MEMORY_BASIC_INFORMATION region{};
        SYSTEM_INFO system{};
        GetSystemInfo(&system);
        if (VirtualQueryEx(process_.get(), reinterpret_cast<LPCVOID>(ready.callsite), &region, sizeof(region)) != sizeof(region) ||
            !page(ready.callsite, 5, hostBase_, PAGE_EXECUTE_READ) ||
            ready.callsite % system.dwPageSize > system.dwPageSize - 5) return Outcome::rejected;
        NativeCounterCall observed{};
        if (!memory.read(ready.callsite, observed) || observed != expected ||
            decodeNativeCounterCall(ready.callsite, observed) != (restoring_ ? plan->wrapper : plan->originalTarget))
            return Outcome::rejected;
        // Normal modes remain read-only for external processes. Only the
        // supervised session can admit this exact plan after separate consent.
        static_assert(!grandmp::windows::nativeCounterGtaWriteEnabled);
        if (inspectionOnly_ || (scenario_ == L"inspect" && !supervised_))
            return memory.untouched() ? Outcome::inspected : Outcome::fatal;
        if (scenario_ == L"unauthorized" || ((external_ || hoodlum_) &&
            (!supervised_ || !authorized_ || !killOnExitDisabled_ || (hoodlum_ && !smokeScope()))))
            return Outcome::rejected;
        const DWORD originalProtection = region.Protect;
        DWORD previous = 0;
        if (!memory.protect(ready.callsite, PAGE_EXECUTE_READWRITE, previous)) {
            return queryProtection(ready.callsite, originalProtection) ? Outcome::rejected : Outcome::fatal;
        }
        auto rollback = [&]() {
            memory.recovery = true;
            DWORD ignored = 0;
            bool ok = memory.protect(ready.callsite, PAGE_EXECUTE_READWRITE, ignored);
            ok &= memory.publish(ready.descriptorAddress, beforeInstalled);
            ok &= memory.write(ready.callsite, expected);
            NativeCounterCall restored{};
            ok &= memory.read(ready.callsite, restored);
            ok &= restored == expected;
            ok &= decodeNativeCounterCall(ready.callsite, restored) ==
                (restoring_ ? plan->wrapper : plan->originalTarget);
            ok &= memory.flush(ready.callsite);
            ok &= memory.protect(ready.callsite, originalProtection, ignored);
            ok &= queryProtection(ready.callsite, originalProtection);
            return ok ? Outcome::recovered : Outcome::fatal;
        };
        if (previous != originalProtection || !memory.write(ready.callsite, desired) ||
            !memory.read(ready.callsite, observed) || observed != desired ||
            decodeNativeCounterCall(ready.callsite, observed) != desiredTarget ||
            !memory.flush(ready.callsite) || !memory.protect(ready.callsite, originalProtection, previous) ||
            !queryProtection(ready.callsite, originalProtection) || !memory.publish(ready.descriptorAddress, afterInstalled))
            return rollback();
        return restoring_ ? Outcome::restored : Outcome::installed;
    }
    Outcome restoreTransaction(DebugStop& stop) {
        if ((external_ && (!supervised_ || !authorized_)) || !installation_) return Outcome::rejected;
        restoring_ = true;
        const auto receipt = *installation_;
        auto expected = receipt.ready;
        if (scenario_ == L"restore_identity") ++expected.processId;
        if (scenario_ == L"restore_invalid_state") expected.state = 3;
        Memory memory(process_.get(), supervised_ && !restorationCase(scenario_) ? L"success" : restorationFault(scenario_));
        if (scenario_ == L"restore_inactive_event" && !resume(stop, DBG_CONTINUE)) return Outcome::fatal;
        const auto outcome = transact(stop, expected, receipt.original, memory, expected.wrapper);
        std::printf("restore_trace rollback=%u writes=%u faults=%u outcome=%s\n",
            memory.recovery ? 1U : 0U, memory.mutations, memory.faultsConsumed, label(outcome));
        if (outcome == Outcome::fatal)
            fatalRecoveryProved_ = memory.recovery && memory.mutations >= 1 && memory.faultsConsumed >= 2;
        if (outcome == Outcome::rejected && !memory.noWrites()) return Outcome::fatal;
        if (outcome == Outcome::recovered && (!memory.recovery || memory.mutations < 2 || !memory.faultsConsumed))
            return Outcome::fatal;
        if (outcome == Outcome::restored) {
            installation_.reset();
            if (scenario_ == L"restore_repeat" && restoreTransaction(stop) != Outcome::rejected) return Outcome::fatal;
        }
        return outcome;
    }
    Outcome readyTransaction(DebugStop& stop) {
        if (!moduleBase_ || !hostBase_ || !initialBreakpoint_) return Outcome::fatal;
        const auto data = moduleBase_ + module_.exported("GrandMPNativeCounterReady");
        NativeCounterReadyV1 ready{};
        if (!readExact(process_.get(), data, &ready, sizeof(ready))) return Outcome::fatal;
        const auto source = hoodlum_ ? 0x53E981 : hostBase_ + host_.exported("GrandMPNativeCounterHostCallsite");
        const auto originalTarget = hoodlum_ ? 0x53BEE0 : hostBase_ + host_.exported("GrandMPNativeCounterHostOriginal");
        const auto wrapper = moduleBase_ + module_.exported("GrandMPNativeCounterWrapperHost");
        if (scenario_ == L"identity") ++ready.processId;
        if (scenario_ == L"invalid_state") ready.state = 3;
        if (scenario_ == L"no_residence") ready.residencyConfirmed = 0;
        if (scenario_ == L"version") ++ready.interfaceVersion;
        if (ready.magic != grandmp::windows::nativeCounterReadyMagic ||
            ready.interfaceVersion != grandmp::windows::nativeCounterReadyVersion || ready.size != sizeof(ready) ||
            ready.processId != pid_ || ready.moduleBase != moduleBase_ || ready.moduleSize != module_.size() ||
            ready.state != 2 || ready.residencyConfirmed != 1 || ready.installed != 0 || ready.wrapperCapable != 1 ||
            ready.shutdownLogical != 0 || !ready.threadId || (!hoodlum_ && !external_ && ready.threadId != stop.tid()) ||
            (external_ && !hoodlum_ && !threads_.contains(ready.threadId)) || ready.reserved != 0 ||
            ready.wrapper != wrapper || ready.originalTarget != originalTarget || ready.callsite != source ||
            ready.descriptorAddress != data || ready.counterAddress < moduleBase_ ||
            std::uint64_t{ready.counterAddress} + 8 > std::uint64_t{moduleBase_} + module_.size() ||
            ready.counterAddress % 8 || module_.executable(data - moduleBase_, sizeof(ready)) ||
            !module_.executable(wrapper - moduleBase_) || !host_.executable(source - hostBase_, 6) ||
            !host_.executable(originalTarget - hostBase_)) return Outcome::rejected;
        const auto original = encodeNativeCounterCall(source, originalTarget);
        if (!original) return Outcome::rejected;
        std::uint32_t low = 0;
        std::memcpy(&low, original->data(), sizeof(low));
        if (ready.originalBytesLow != low || ready.originalBytesHigh != (*original)[4]) return Outcome::rejected;
        if (scenario_ == L"third_party") {
            if (external_ || hoodlum_) return Outcome::rejected;
            const auto foreign = encodeNativeCounterCall(source,
                hostBase_ + host_.exported("GrandMPNativeCounterHostForeignOriginal"));
            Memory setup(process_.get(), L"success");
            DWORD protection = 0, ignored = 0;
            if (!foreign || !setup.protect(source, PAGE_EXECUTE_READWRITE, protection) || !setup.write(source, *foreign) ||
                !setup.flush(source) || !setup.protect(source, protection, ignored)) return Outcome::fatal;
            Memory guarded(process_.get(), L"third_party");
            const auto result = transact(stop, ready, *original, guarded, wrapper);
            NativeCounterCall observed{};
            if (result != Outcome::rejected || guarded.mutations || !guarded.read(source, observed) || observed != *foreign)
                return Outcome::fatal;
            return result;
        }
        Memory memory(process_.get(), inspectionOnly_ || restorationCase(scenario_) ? L"success" : scenario_);
        if (scenario_ == L"inactive_event") {
            if (!resume(stop, DBG_CONTINUE)) return Outcome::fatal;
            const auto result = transact(stop, ready, *original, memory, wrapper);
            return result == Outcome::rejected && memory.mutations == 0 ? result : Outcome::fatal;
        }
        const auto destination = scenario_ == L"offset_range" ? 0xF0000000ULL : std::uint64_t{wrapper};
        const auto result = transact(stop, ready, *original, memory, destination);
        std::printf("transaction_trace rollback=%u writes=%u faults=%u\n",
                    memory.recovery ? 1U : 0U, memory.mutations, memory.faultsConsumed);
        if (result == Outcome::installed) installation_ = Installation{ready, *original};
        if ((inspectionOnly_ || scenario_ == L"inspect" || scenario_ == L"unauthorized") && !memory.untouched()) return Outcome::fatal;
        if (result == Outcome::fatal)
            fatalRecoveryProved_ = memory.recovery && memory.mutations >= 1 && memory.faultsConsumed >= 2;
        if (result == Outcome::recovered && (!memory.recovery || memory.mutations < 2 || memory.faultsConsumed == 0))
            return Outcome::fatal;
        if (scenario_ == L"repeat" && result == Outcome::installed) {
            Memory duplicateAttempt(process_.get(), L"success");
            if (transact(stop, ready, *original, duplicateAttempt, wrapper) != Outcome::rejected || duplicateAttempt.mutations)
                return Outcome::fatal;
        }
        return result;
    }
    struct Installation {
        NativeCounterReadyV1 ready;
        NativeCounterCall original;
    };
    std::optional<Installation> installation_;
    std::array<std::byte, 32> expectedDigest_{};
    bool supervised_ = false, authorized_ = false, inspectionOnly_ = false, restoreBreakRequested_ = false;
    std::optional<NativeCounterReadyV1> preview_;
    Outcome supervisedOutcome_ = Outcome::rejected;
    std::uint64_t nonce_ = 0, counterBefore_ = 0, observeUntil_ = 0;
    std::optional<grandmp::windows::NativeCounterFocusObservation> focusObservation_;
    std::optional<std::uint64_t> counterAtFocus_;
    bool focusCounterReadFailed_ = false;
    std::set<DWORD> breakBaseline_;
    bool external_ = false, attached_ = false, killOnExitDisabled_ = false;
    const char* externalStop_ = "no_event_owned";
    std::map<DWORD, Handle> beforeThreads_;
    std::map<std::uintptr_t, ModuleIdentity> beforeModules_;
    std::map<std::uintptr_t, ModuleEvent> moduleEvents_;
    std::set<DWORD> exitedBeforeThreads_;
    FILETIME processCreated_{};
    InventoryPhase inventoryPhase_ = InventoryPhase::beforeAttach;
    std::set<std::uintptr_t> eventModules_;
    bool hoodlum_ = false, restoring_ = false, restoreSeen_ = false;
    const Image& host_;
    const Image& module_;
    std::wstring_view scenario_;
    Handle process_;
    DWORD pid_ = 0, initialTid_ = 0;
    std::uint32_t hostBase_ = 0, moduleBase_ = 0;
    std::uintptr_t ntdllBase_ = 0;
    std::map<DWORD, Handle> threads_;
    bool initialBreakpoint_ = false, readySeen_ = false, fatalRecoveryProved_ = false;
};
}  // namespace

#ifndef GRANDMP_NATIVE_COUNTER_IMAGE_TEST
int wmain(int argc, wchar_t** argv) {
    (void)std::setvbuf(stdout, nullptr, _IONBF, 0);
    const bool smoke = argc == 5 && std::wstring_view(argv[1]) == L"--hoodlum-counter";
    const bool supervisedLab = argc == 5 && std::wstring_view(argv[1]) == L"--supervised-lab";
    const bool attach = argc == 5 && std::wstring_view(argv[1]) == L"--attach-lab";
    if (!attach && !smoke && !supervisedLab && argc != 3) return 2;
    std::optional<std::array<std::byte, 32>> expectedDigest;
    if (smoke) {
        if (std::wstring_view(argv[3]) != L"--expected-asi-sha256" || !(expectedDigest = parseDigest(argv[4]))) return 2;
        if (!grandmp::windows::nativeCounterSmokeDigestApproved(*expectedDigest)) return 2;
        DWORD mode = 0;
        if (!GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode)) return 2;
    }
    DWORD externalPid = 0;
    if (attach || smoke || supervisedLab) {
        if (attach && std::wstring_view(argv[4]) != L"inspect") return 2;
        const std::wstring_view value(argv[2]);
        if (value.empty()) return 2;
        for (const auto digit : value) {
            if (digit < L'0' || digit > L'9' || externalPid > (MAXDWORD - (digit - L'0')) / 10) return 2;
            externalPid = externalPid * 10 + digit - L'0';
        }
        if (!externalPid || externalPid == GetCurrentProcessId()) return 2;
    }
    const std::wstring_view scenario(smoke ? L"success" : supervisedLab ? argv[4] : attach ? L"inspect" : argv[2]);
    constexpr std::wstring_view scenarios[]{L"success", L"repeat", L"unexpected_bytes", L"third_party", L"offset_range",
        L"short_write", L"protect_failure", L"flush_failure", L"readback_failure", L"restore_failure", L"commit_failure",
        L"fatal_write", L"fatal_flush", L"fatal_protection", L"fatal_commit", L"identity", L"version", L"invalid_state", L"no_residence",
        L"context_failure", L"context_overlap", L"inactive_event", L"inspect", L"unauthorized",
        L"restore_success", L"restore_repeat", L"restore_identity", L"restore_invalid_state",
        L"restore_unexpected_bytes", L"restore_context_failure", L"restore_context_overlap", L"restore_inactive_event",
        L"restore_short_write", L"restore_protect_failure", L"restore_flush_failure", L"restore_readback_failure",
        L"restore_restore_failure", L"restore_commit_failure",
        L"fatal_restore_write", L"fatal_restore_flush", L"fatal_restore_protection", L"fatal_restore_commit"};
    if (std::find(std::begin(scenarios), std::end(scenarios), scenario) == std::end(scenarios)) return 2;
    try {
        wchar_t self[32768]{};
        const auto length = GetModuleFileNameW(nullptr, self, 32768);
        if (!length || length >= 32768) return 2;
        const auto ownDirectory = std::filesystem::canonical(self).parent_path();
        if (smoke) {
            Image module(L"D:/Jogos/GrandMP-Smoke/scripts/GrandMPNativeCounterExperimental.asi");
            Image host(L"D:/Jogos/GrandMP-Smoke/gta_sa.exe");
            Image reference(ownDirectory / L"GrandMPNativeCounterExperimental.asi");
            if (reference.evidence().digest != *expectedDigest ||
                !grandmp::windows::nativeCounterSmokeScope(host.path().generic_wstring(), module.path().generic_wstring(),
                    host.evidence(), module.evidence().digest, *expectedDigest)) return 2;
            DebugSession session(host, module, scenario, true, *expectedDigest);
            return session.run(externalPid);
        }
        const auto path = std::filesystem::canonical(argv[(attach || supervisedLab) ? 3 : 1]);
        if (path.filename() != L"GrandMPNativeCounterLab.dll" || path.parent_path() != ownDirectory) return 2;
        if (supervisedLab) {
            constexpr std::wstring_view labCases[]{L"success", L"identity", L"invalid_state", L"context_failure",
                L"unexpected_bytes", L"short_write", L"flush_failure", L"commit_failure", L"fatal_write",
                L"restore_short_write", L"restore_identity", L"fatal_restore_write"};
            if (std::find(std::begin(labCases), std::end(labCases), scenario) == std::end(labCases)) return 2;
        }
        Image module(path);
        Image host(path.parent_path() / L"grandmp_native_counter_host.exe");
        DebugSession session(host, module, scenario, supervisedLab);
        return session.run(externalPid);
    } catch (const std::exception& exception) {
        std::fprintf(stderr, "lab validation: %s\n", exception.what());
        return 2;
    }
}
#endif
