#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <tlhelp32.h>
#include <psapi.h>

#include "alpha09_hoodlum_memory_reader_core.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mr = grandmp::windows::alpha09_memory_reader;
namespace {

constexpr wchar_t gamePath[] = L"D:\\Jogos\\GrandMP-Smoke\\gta_sa.exe";
constexpr char gameSha256[] = "A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26";
constexpr std::uint64_t gameFileBytes = 14383616;
constexpr DWORD readOnlyProcessRights = static_cast<DWORD>(mr::readOnlyProcessRights);
constexpr DWORD forbiddenProcessRights = PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_CREATE_THREAD |
    PROCESS_TERMINATE | PROCESS_SUSPEND_RESUME;
static_assert(readOnlyProcessRights == (PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE));
static_assert((readOnlyProcessRights & forbiddenProcessRights) == 0);

class Handle final {
public:
    explicit Handle(HANDLE value = nullptr) noexcept : value_(value) {}
    ~Handle() { if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value_(other.value_) { other.value_ = nullptr; }
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_);
            value_ = other.value_;
            other.value_ = nullptr;
        }
        return *this;
    }
    HANDLE get() const noexcept { return value_; }
    explicit operator bool() const noexcept { return value_ && value_ != INVALID_HANDLE_VALUE; }
private:
    HANDLE value_;
};

std::string narrow(std::wstring_view value) {
    if (value.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) throw std::runtime_error("utf8_conversion_failed");
    std::string result(static_cast<std::size_t>(needed), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
            result.data(), needed, nullptr, nullptr) != needed) throw std::runtime_error("utf8_conversion_failed");
    return result;
}

std::string jsonString(std::string_view value) {
    std::ostringstream out;
    out << '"';
    for (const unsigned char ch : value) {
        switch (ch) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (ch < 0x20) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(ch);
            else out << static_cast<char>(ch);
        }
    }
    out << '"';
    return out.str();
}

std::string hex(std::uint64_t value, unsigned digits = 0) {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex;
    if (digits) out << std::setw(static_cast<int>(digits)) << std::setfill('0');
    out << value;
    return out.str();
}

std::wstring fullPath(std::wstring_view input) {
    std::wstring terminated(input);
    const DWORD needed = GetFullPathNameW(terminated.c_str(), 0, nullptr, nullptr);
    if (!needed || needed > 32767) throw std::runtime_error("full_path_query_failed");
    std::wstring result(needed, L'\0');
    const DWORD written = GetFullPathNameW(terminated.c_str(), needed, result.data(), nullptr);
    if (!written || written >= needed) throw std::runtime_error("full_path_failed");
    result.resize(written);
    return result;
}

std::wstring finalPathFromHandle(HANDLE file) {
    const DWORD needed = GetFinalPathNameByHandleW(file, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!needed || needed > 32767) throw std::runtime_error("final_path_query_failed");
    std::wstring result(needed, L'\0');
    const DWORD written = GetFinalPathNameByHandleW(file, result.data(), needed, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!written || written >= needed) throw std::runtime_error("final_path_failed");
    result.resize(written);
    if (result.starts_with(L"\\\\?\\")) result.erase(0, 4);
    return result;
}

bool samePath(std::wstring_view left, std::wstring_view right) noexcept {
    return CompareStringOrdinal(left.data(), static_cast<int>(left.size()),
        right.data(), static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

std::wstring processImagePath(HANDLE process) {
    std::wstring value(32768, L'\0');
    DWORD length = static_cast<DWORD>(value.size());
    if (!QueryFullProcessImageNameW(process, 0, value.data(), &length) || !length || length >= value.size())
        throw std::runtime_error("process_image_path_failed:" + std::to_string(GetLastError()));
    value.resize(length);
    return value;
}

std::uint64_t creationFileTime(HANDLE process) {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(process, &creation, &exit, &kernel, &user))
        throw std::runtime_error("process_creation_time_failed:" + std::to_string(GetLastError()));
    ULARGE_INTEGER value{};
    value.LowPart = creation.dwLowDateTime;
    value.HighPart = creation.dwHighDateTime;
    return value.QuadPart;
}

bool isX86Process(HANDLE process) {
    using IsWow64Process2Fn = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
    const auto kernel = GetModuleHandleW(L"kernel32.dll");
    const auto fn = reinterpret_cast<IsWow64Process2Fn>(GetProcAddress(kernel, "IsWow64Process2"));
    if (!fn) throw std::runtime_error("iswow64process2_unavailable:" + std::to_string(GetLastError()));
    USHORT processMachine = 0, nativeMachine = 0;
    if (!fn(process, &processMachine, &nativeMachine))
        throw std::runtime_error("iswow64process2_failed:" + std::to_string(GetLastError()));
    return processMachine == IMAGE_FILE_MACHINE_I386 ||
        (processMachine == IMAGE_FILE_MACHINE_UNKNOWN && nativeMachine == IMAGE_FILE_MACHINE_I386);
}

bool processIsStillRunning(HANDLE process) {
    const DWORD state = WaitForSingleObject(process, 0);
    if (state == WAIT_TIMEOUT) return true;
    if (state == WAIT_OBJECT_0) return false;
    throw std::runtime_error("process_liveness_query_failed:" + std::to_string(GetLastError()));
}

std::uint64_t parseUnsignedFileTime(std::wstring_view text) {
    if (text.empty()) throw std::runtime_error("invalid_creation_filetime");
    std::wstring owned(text);
    wchar_t* end = nullptr;
    errno = 0;
    const auto value = std::wcstoull(owned.c_str(), &end, 10);
    if (errno == ERANGE || end != owned.c_str() + owned.size() || value == 0)
        throw std::runtime_error("invalid_creation_filetime");
    return static_cast<std::uint64_t>(value);
}

DWORD parsePid(std::wstring_view text) {
    if (text.empty()) throw std::runtime_error("invalid_pid");
    std::wstring owned(text);
    wchar_t* end = nullptr;
    errno = 0;
    const auto value = std::wcstoul(owned.c_str(), &end, 10);
    if (errno == ERANGE || end != owned.c_str() + owned.size() || value == 0 || value > MAXDWORD)
        throw std::runtime_error("invalid_pid");
    return static_cast<DWORD>(value);
}

std::uint64_t currentFileTime() {
    FILETIME value{};
    GetSystemTimeAsFileTime(&value);
    ULARGE_INTEGER ticks{};
    ticks.LowPart = value.dwLowDateTime;
    ticks.HighPart = value.dwHighDateTime;
    return ticks.QuadPart;
}

struct ModuleIdentity {
    bool found = false;
    std::uint32_t base = 0;
    std::wstring path;
};

ModuleIdentity findGameModule(DWORD pid) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
    if (!snapshot) throw std::runtime_error("module_snapshot_failed:" + std::to_string(GetLastError()));
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Module32FirstW(snapshot.get(), &entry))
        throw std::runtime_error("module_first_failed:" + std::to_string(GetLastError()));
    ModuleIdentity result{};
    do {
        if (_wcsicmp(entry.szModule, L"gta_sa.exe") == 0) {
            if (result.found) throw std::runtime_error("duplicate_game_module");
            result.found = true;
            result.base = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(entry.modBaseAddr));
            result.path = entry.szExePath;
        }
    } while (Module32NextW(snapshot.get(), &entry));
    if (GetLastError() != ERROR_NO_MORE_FILES) throw std::runtime_error("module_iteration_failed:" + std::to_string(GetLastError()));
    if (!result.found) throw std::runtime_error("game_module_not_found");
    return result;
}

std::vector<std::byte> readDiskImage(HANDLE file) {
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size)) throw std::runtime_error("game_file_size_failed:" + std::to_string(GetLastError()));
    if (size.QuadPart != static_cast<LONGLONG>(gameFileBytes)) throw std::runtime_error("game_file_size_mismatch");
    std::vector<std::byte> bytes(static_cast<std::size_t>(size.QuadPart));
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(file, zero, nullptr, FILE_BEGIN)) throw std::runtime_error("game_file_seek_failed:" + std::to_string(GetLastError()));
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto request = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, MAXDWORD));
        DWORD read = 0;
        if (!ReadFile(file, bytes.data() + offset, request, &read, nullptr))
            throw std::runtime_error("game_file_read_failed:" + std::to_string(GetLastError()));
        if (!read) throw std::runtime_error("game_file_read_incomplete");
        offset += read;
    }
    return bytes;
}

std::string sha256(std::span<const std::byte> data) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (status < 0) throw std::runtime_error("bcrypt_open_failed:" + hex(static_cast<std::uint32_t>(status), 8));
    BCRYPT_HASH_HANDLE hash = nullptr;
    status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
    if (status < 0) { BCryptCloseAlgorithmProvider(algorithm, 0); throw std::runtime_error("bcrypt_create_failed:" + hex(static_cast<std::uint32_t>(status), 8)); }
    std::array<UCHAR, 32> digest{};
    status = BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<std::byte*>(data.data())),
        static_cast<ULONG>(data.size()), 0);
    if (status >= 0) status = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) throw std::runtime_error("bcrypt_hash_failed:" + hex(static_cast<std::uint32_t>(status), 8));
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string output;
    output.reserve(64);
    for (const auto byte : digest) { output.push_back(digits[byte >> 4]); output.push_back(digits[byte & 0xF]); }
    return output;
}

struct PageRecord {
    std::uint32_t queryAddress = 0;
    bool querySucceeded = false;
    DWORD queryError = ERROR_SUCCESS;
    std::uint32_t baseAddress = 0;
    std::uint32_t allocationBase = 0;
    std::uint32_t allocationProtect = 0;
    std::uint64_t regionSize = 0;
    std::uint32_t state = 0, protect = 0, type = 0;
    bool coversQuery = false;
    bool workingSetQuerySucceeded = false;
    DWORD workingSetError = ERROR_SUCCESS;
    std::uint32_t workingSetRaw = 0;
    mr::WorkingSetBits workingSet{};
};

PageRecord inspectPage(HANDLE process, std::uint32_t address) {
    PageRecord result{};
    result.queryAddress = address;
    MEMORY_BASIC_INFORMATION info{};
    const SIZE_T queried = VirtualQueryEx(process, reinterpret_cast<LPCVOID>(address), &info, sizeof(info));
    result.querySucceeded = queried == sizeof(info);
    result.queryError = result.querySucceeded ? ERROR_SUCCESS : GetLastError();
    if (result.querySucceeded) {
        result.baseAddress = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(info.BaseAddress));
        result.allocationBase = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(info.AllocationBase));
        result.allocationProtect = info.AllocationProtect;
        result.regionSize = info.RegionSize;
        result.state = info.State;
        result.protect = info.Protect;
        result.type = info.Type;
        const auto start = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        result.coversQuery = address >= start && std::uint64_t{address} < std::uint64_t{start} + info.RegionSize;
    }
    return result;
}

void inspectWorkingSet(HANDLE process, std::array<PageRecord, 2>& pages) {
    std::array<PSAPI_WORKING_SET_EX_INFORMATION, 2> info{};
    for (std::size_t i = 0; i < pages.size(); ++i)
        info[i].VirtualAddress = reinterpret_cast<PVOID>(pages[i].queryAddress);
    if (!QueryWorkingSetEx(process, info.data(), static_cast<DWORD>(sizeof(info)))) {
        const auto error = GetLastError();
        for (auto& page : pages) { page.workingSetError = error; }
        return;
    }
    for (std::size_t i = 0; i < pages.size(); ++i) {
        pages[i].workingSetQuerySucceeded = true;
        pages[i].workingSetRaw = static_cast<std::uint32_t>(info[i].VirtualAttributes.Flags);
        pages[i].workingSet = mr::decodeWorkingSet(pages[i].workingSetRaw);
    }
}

std::string pageJson(const PageRecord& page) {
    std::ostringstream out;
    out << "{\"query_address\":" << jsonString(hex(page.queryAddress, 8))
        << ",\"query_succeeded\":" << (page.querySucceeded ? "true" : "false")
        << ",\"query_win32\":" << page.queryError
        << ",\"base_address\":" << jsonString(page.querySucceeded ? hex(page.baseAddress, 8) : "unknown")
        << ",\"region_size\":" << (page.querySucceeded ? std::to_string(page.regionSize) : "null")
        << ",\"region_end_exclusive\":" << jsonString(page.querySucceeded ? hex(std::uint64_t{page.baseAddress} + page.regionSize, 8) : "unknown")
        << ",\"state\":" << jsonString(page.querySucceeded ? hex(page.state, 8) : "unknown")
        << ",\"type\":" << jsonString(page.querySucceeded ? hex(page.type, 8) : "unknown")
        << ",\"protect\":" << jsonString(page.querySucceeded ? hex(page.protect, 8) : "unknown")
        << ",\"allocation_protect\":" << jsonString(page.querySucceeded ? hex(page.allocationProtect, 8) : "unknown")
        << ",\"allocation_base\":" << jsonString(page.querySucceeded ? hex(page.allocationBase, 8) : "unknown")
        << ",\"covers_query_address\":" << (page.coversQuery ? "true" : "false")
        << ",\"working_set_query_succeeded\":" << (page.workingSetQuerySucceeded ? "true" : "false")
        << ",\"working_set_win32\":" << page.workingSetError
        << ",\"working_set_raw\":" << jsonString(page.workingSetQuerySucceeded ? hex(page.workingSetRaw, 8) : "unknown")
        << ",\"working_set_valid\":";
    if (page.workingSetQuerySucceeded) out << (page.workingSet.valid ? "true" : "false"); else out << "null";
    out << ",\"working_set_shared\":";
    if (page.workingSetQuerySucceeded && page.workingSet.valid) out << (page.workingSet.shared ? "true" : "false"); else out << "null";
    out << ",\"working_set_share_count\":";
    if (page.workingSetQuerySucceeded && page.workingSet.valid) out << unsigned(page.workingSet.shareCount); else out << "null";
    out << ",\"working_set_protection\":";
    if (page.workingSetQuerySucceeded && page.workingSet.valid) out << jsonString(hex(page.workingSet.win32Protection, 3)); else out << "null";
    out << '}';
    return out.str();
}

std::wstring finalPathForFile(std::wstring_view path, Handle& file) {
    const std::wstring owned(path);
    file = Handle(CreateFileW(owned.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) throw std::runtime_error("executable_open_failed:" + std::to_string(GetLastError()));
    return finalPathFromHandle(file.get());
}

bool isX86ReaderTargetPath(const std::wstring& path) {
    return samePath(path, gamePath);
}

void ensureSafeOutput(const std::wstring& output) {
    const auto absolute = fullPath(output);
    const auto root = fullPath(L"D:\\Jogos\\GrandMP-Smoke");
    if (absolute.size() >= root.size() && samePath(std::wstring_view(absolute).substr(0, root.size()), root) &&
        (absolute.size() == root.size() || absolute[root.size()] == L'\\'))
        throw std::runtime_error("output_must_be_outside_game_tree");
    std::filesystem::path current = std::filesystem::path(absolute).parent_path();
    while (!current.empty()) {
        const auto attributes = GetFileAttributesW(current.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) throw std::runtime_error("output_parent_missing:" + std::to_string(GetLastError()));
        if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) throw std::runtime_error("output_reparse_parent_refused");
        const auto parent = current.parent_path();
        if (parent == current) break;
        current = parent;
    }
    if (GetFileAttributesW(absolute.c_str()) != INVALID_FILE_ATTRIBUTES)
        throw std::runtime_error("output_already_exists_no_overwrite");
}

void writeNewEvidence(const std::wstring& path, std::string_view text) {
    Handle file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!file) throw std::runtime_error("evidence_create_new_failed:" + std::to_string(GetLastError()));
    std::size_t offset = 0;
    while (offset < text.size()) {
        const auto request = static_cast<DWORD>(std::min<std::size_t>(text.size() - offset, MAXDWORD));
        DWORD written = 0;
        if (!WriteFile(file.get(), text.data() + offset, request, &written, nullptr) || !written)
            throw std::runtime_error("evidence_write_failed:" + std::to_string(GetLastError()));
        offset += written;
    }
    if (!FlushFileBuffers(file.get())) throw std::runtime_error("evidence_flush_failed:" + std::to_string(GetLastError()));
}

struct Arguments { DWORD pid = 0; std::uint64_t creation = 0; std::wstring output; };
Arguments parseArguments(int argc, wchar_t** argv) {
    Arguments result{};
    bool havePid = false, haveCreation = false, haveOutput = false;
    for (int i = 1; i < argc; ++i) {
        const std::wstring_view key(argv[i]);
        if (i + 1 >= argc) throw std::runtime_error("argument_value_missing");
        const std::wstring_view value(argv[++i]);
        if (key == L"--pid" && !havePid) { result.pid = parsePid(value); havePid = true; }
        else if (key == L"--creation-filetime-utc" && !haveCreation) { result.creation = parseUnsignedFileTime(value); haveCreation = true; }
        else if (key == L"--output" && !haveOutput) { result.output = value; haveOutput = !result.output.empty(); }
        else throw std::runtime_error("invalid_or_duplicate_argument");
    }
    if (!havePid || !haveCreation || !haveOutput) throw std::runtime_error("required_args:--pid --creation-filetime-utc --output");
    return result;
}

std::string runCapture(const Arguments& args) {
    ensureSafeOutput(args.output);
    Handle process(OpenProcess(readOnlyProcessRights, FALSE, args.pid));
    if (!process) throw std::runtime_error("open_process_read_only_failed:" + std::to_string(GetLastError()));
    if (GetProcessId(process.get()) != args.pid) throw std::runtime_error("pid_handle_mismatch");
    if (!processIsStillRunning(process.get())) throw std::runtime_error("process_already_exited");
    const auto creationBefore = creationFileTime(process.get());
    if (creationBefore != args.creation) throw std::runtime_error("creation_time_argument_mismatch");
    if (!isX86Process(process.get())) throw std::runtime_error("target_is_not_x86");

    const auto reportedImage = processImagePath(process.get());
    Handle reportedFile;
    const auto reportedFinal = finalPathForFile(reportedImage, reportedFile);
    if (!isX86ReaderTargetPath(reportedFinal)) throw std::runtime_error("process_image_path_not_authorized");

    Handle expectedFile;
    const auto expectedFinal = finalPathForFile(gamePath, expectedFile);
    if (!samePath(reportedFinal, expectedFinal)) throw std::runtime_error("process_image_file_identity_mismatch");
    const auto disk = readDiskImage(expectedFile.get());
    const auto diskHash = sha256(disk);
    if (diskHash != gameSha256) throw std::runtime_error("hoodlum_sha256_mismatch");
    mr::PeRange mapped{};
    if (!mr::mapPe32Range(disk, mapped)) throw std::runtime_error("hoodlum_pe32_or_range_identity_mismatch");

    const auto moduleBefore = findGameModule(args.pid);
    Handle moduleFile;
    const auto modulePathFull = finalPathForFile(moduleBefore.path, moduleFile);
    if (!samePath(modulePathFull, reportedFinal)) throw std::runtime_error("module_image_path_mismatch");
    if (moduleBefore.base != mr::imageBase) throw std::runtime_error("module_base_not_400000");
    if (creationFileTime(process.get()) != creationBefore) throw std::runtime_error("creation_time_changed_before_capture");
    if (!processIsStillRunning(process.get())) throw std::runtime_error("process_exited_before_capture");

    const auto captureStarted = currentFileTime();
    std::array<PageRecord, 2> pages{
        inspectPage(process.get(), mr::firstPage), inspectPage(process.get(), mr::secondPage)};
    inspectWorkingSet(process.get(), pages);

    std::array<std::byte, mr::rangeSize> observed{};
    SIZE_T transferred = 0;
    const BOOL readSucceeded = ReadProcessMemory(process.get(), reinterpret_cast<LPCVOID>(mr::rangeAddress),
        observed.data(), observed.size(), &transferred);
    const DWORD readError = readSucceeded ? ERROR_SUCCESS : GetLastError();
    const auto diskRange = std::span<const std::byte>(disk).subspan(mapped.rawOffset, mr::rangeSize);
    const auto comparison = mr::compareBytes(diskRange, observed, transferred, readSucceeded != FALSE);

    const auto moduleAfter = findGameModule(args.pid);
    Handle moduleAfterFile;
    const auto moduleAfterPath = finalPathForFile(moduleAfter.path, moduleAfterFile);
    const bool moduleStable = moduleAfter.base == moduleBefore.base && samePath(moduleAfterPath, modulePathFull);
    const auto creationAfter = creationFileTime(process.get());
    const bool processStable = creationAfter == creationBefore && GetProcessId(process.get()) == args.pid;
    if (!moduleStable || !processStable) throw std::runtime_error("target_identity_changed_during_capture");
    if (!processIsStillRunning(process.get())) throw std::runtime_error("process_exited_during_capture");

    const auto captureCompleted = currentFileTime();
    const auto observedHash = comparison.complete ? sha256(observed) : std::string{};
    const auto expectedHash = sha256(diskRange);
    std::ostringstream out;
    out << "{\n  \"schema\": \"grandmp-alpha09-hoodlum-memory-capture-v1\",\n"
        << "  \"capture_id\": " << jsonString(std::to_string(args.pid) + "-" + std::to_string(captureCompleted)) << ",\n"
        << "  \"capture_started_utc_filetime\": " << captureStarted << ",\n"
        << "  \"capture_completed_utc_filetime\": " << captureCompleted << ",\n"
        << "  \"authorization\": {\"authenticated\": true, \"engine_code_executed\": false, \"process_write_rights_requested\": false},\n"
        << "  \"process\": {\"pid\": " << args.pid << ", \"creation_filetime_utc\": " << creationBefore
        << ", \"image_path\": " << jsonString(narrow(reportedFinal))
        << ", \"image_sha256\": " << jsonString(diskHash)
        << ", \"architecture\": \"x86\", \"module_path\": " << jsonString(narrow(fullPath(moduleBefore.path)))
        << ", \"module_base\": " << jsonString(hex(moduleBefore.base, 8)) << "},\n"
        << "  \"reference\": {\"image_base\": " << jsonString(hex(mr::imageBase, 8))
        << ", \"pe_machine\": " << jsonString(hex(mr::expectedMachine, 4))
        << ", \"optional_magic\": " << jsonString(hex(mr::expectedOptionalMagic, 4))
        << ", \"section\": " << jsonString(mapped.sectionName.data())
        << ", \"section_characteristics\": " << jsonString(hex(mapped.sectionCharacteristics, 8))
        << ", \"range_address\": " << jsonString(hex(mr::rangeAddress, 8))
        << ", \"range_rva\": " << jsonString(hex(mapped.rva, 8))
        << ", \"file_offset\": " << jsonString(hex(mapped.rawOffset, 8))
        << ", \"range_size\": " << mr::rangeSize << ", \"sha256\": " << jsonString(expectedHash) << "},\n"
        << "  \"pages\": [" << pageJson(pages[0]) << ',' << pageJson(pages[1]) << "],\n"
        << "  \"working_set_note\": \"For valid resident pages Shared is a momentary sharing bit; Shared=false is consistent with a private/COW page but does not prove its cause. Invalid/nonresident pages are unknown. This does not identify which component changed page protection.\",\n"
        << "  \"sampling_note\": \"Queries are sequential and the live target is not suspended; this capture is not an atomic process snapshot.\",\n"
        << "  \"memory_read\": {\"api\": \"ReadProcessMemory\", \"success\": " << (readSucceeded ? "true" : "false")
        << ", \"win32\": " << readError << ", \"requested\": " << mr::rangeSize
        << ", \"transferred\": " << transferred << ", \"complete\": " << (comparison.complete ? "true" : "false")
        << ", \"equal_to_disk\":";
    if (comparison.complete) out << (comparison.equal ? "true" : "false"); else out << "null";
    out << ", \"memory_sha256\":";
    if (comparison.complete) out << jsonString(observedHash); else out << "null";
    out << ", \"first_mismatch_offset\":";
    if (comparison.complete && !comparison.equal) out << comparison.firstMismatch; else out << "null";
    out << "},\n  \"post_capture\": {\"process_still_running\": true, \"creation_filetime_unchanged\": true, \"module_identity_unchanged\": true},\n"
        << "  \"capture_count\": 1\n}\n";
    return out.str();
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        const auto args = parseArguments(argc, argv);
        const auto result = runCapture(args);
        ensureSafeOutput(args.output);
        writeNewEvidence(args.output, result);
        std::wcout << L"capture_written path=" << args.output << L"\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "capture_refused reason=" << error.what() << "\n";
        return 2;
    }
}
