#include "../client/windows/tools/alpha09_hoodlum_memory_reader_core.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace mr = grandmp::windows::alpha09_memory_reader;

namespace {
unsigned assertions = 0;
void check(bool condition, const char* message) {
    ++assertions;
    if (!condition) throw std::runtime_error(message);
}

void put16(std::vector<std::byte>& bytes, std::size_t offset, std::uint16_t value) {
    for (unsigned i = 0; i < 2; ++i) bytes[offset + i] = std::byte((value >> (i * 8)) & 0xFF);
}
void put32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = std::byte((value >> (i * 8)) & 0xFF);
}

std::vector<std::byte> peFixture() {
    constexpr std::size_t pe = 0x80;
    constexpr std::size_t optional = pe + 24;
    constexpr std::size_t sections = optional + 0xE0;
    constexpr std::size_t rawOffset = 0x400;
    std::vector<std::byte> bytes(rawOffset + 0x2000);
    put16(bytes, 0, 0x5A4D);
    put32(bytes, 0x3C, static_cast<std::uint32_t>(pe));
    put32(bytes, pe, 0x00004550);
    put16(bytes, pe + 4, 0x014C);
    put16(bytes, pe + 6, 1);
    put16(bytes, pe + 20, 0xE0);
    put16(bytes, optional, 0x010B);
    put32(bytes, optional + 28, 0x00400000);
    constexpr char sectionName[] = ".text";
    for (std::size_t i = 0; i < sizeof(sectionName) - 1; ++i) bytes[sections + i] = std::byte(sectionName[i]);
    put32(bytes, sections + 8, 0x2000);
    put32(bytes, sections + 12, 0x0013B000);
    put32(bytes, sections + 16, 0x2000);
    put32(bytes, sections + 20, static_cast<std::uint32_t>(rawOffset));
    put32(bytes, sections + 36, 0x60000020);
    return bytes;
}

void testPeMapping() {
    auto fixture = peFixture();
    mr::PeRange range{};
    check(mr::mapPe32Range(fixture, range), "valid PE32 text region maps");
    check(range.rva == 0x0013BEE0 && range.rawOffset == 0x12E0, "RVA maps to raw file offset");
    check(range.sectionName[0] == '.' && range.sectionName[1] == 't' && range.sectionName[4] == 't', "section name retained");
    check(range.sectionCharacteristics == 0x60000020, "text characteristics retained");

    auto bad = fixture;
    put16(bad, 0, 0);
    check(!mr::mapPe32Range(bad, range), "non-MZ file refused");
    bad = fixture;
    put16(bad, 0x84, 0x8664);
    check(!mr::mapPe32Range(bad, range), "PE32+ machine refused");
    bad = fixture;
    put32(bad, 0x80 + 24 + 28, 0x10000000);
    check(!mr::mapPe32Range(bad, range), "unexpected image base refused");
    bad = fixture;
    put32(bad, 0x80 + 24 + 0xE0 + 36, 0xE0000020);
    check(!mr::mapPe32Range(bad, range), "writable code section refused");
    bad = fixture;
    put32(bad, 0x80 + 24 + 0xE0 + 36, 0x40000040);
    check(!mr::mapPe32Range(bad, range), "non-executable section refused");
    bad = fixture;
    put32(bad, 0x80 + 24 + 0xE0 + 20, 0xFFFFFFF0);
    check(!mr::mapPe32Range(bad, range), "raw file range overflow refused");
    bad = fixture;
    put32(bad, 0x80 + 24 + 0xE0 + 16, 0x1000);
    check(!mr::mapPe32Range(bad, range), "cross-section truncated raw range refused");
}

void testComparison() {
    std::array<std::byte, mr::rangeSize> expected{};
    auto observed = expected;
    auto result = mr::compareBytes(expected, observed, expected.size(), true);
    check(result.complete && result.equal && result.firstMismatch == 0, "complete equal bytes accepted as equal");
    observed[217] = std::byte{0xA5};
    result = mr::compareBytes(expected, observed, observed.size(), true);
    check(result.complete && !result.equal && result.firstMismatch == 217, "first differing byte reported");
    result = mr::compareBytes(expected, observed, 219, true);
    check(!result.complete && !result.equal && result.transferred == 219, "partial successful read is inconclusive");
    result = mr::compareBytes(expected, observed, expected.size(), false);
    check(!result.complete, "failed read is never a complete comparison");
    result = mr::compareBytes(std::span<const std::byte>(expected).first(10), observed, expected.size(), true);
    check(!result.complete, "wrong expected length is refused");
    result = mr::compareBytes(expected, std::span<const std::byte>(observed).first(200), expected.size(), true);
    check(!result.complete, "short output buffer is refused");
}

void testWorkingSetAndIdentity() {
    auto bits = mr::decodeWorkingSet(1u | (5u << 1) | (0x20u << 4) | (1u << 15));
    check(bits.valid && bits.shared && bits.shareCount == 5 && bits.win32Protection == 0x20,
        "valid shared working-set fields decoded");
    bits = mr::decodeWorkingSet(0);
    check(!bits.valid && !bits.shared, "invalid/nonresident working set is not classified shared");

    mr::IdentityFacts facts{true, true, true, true, true, true, true, true};
    check(mr::authenticated(facts), "all process identity guards required and satisfied");
    facts.pid = false; check(!mr::authenticated(facts), "PID guard required"); facts.pid = true;
    facts.creationTime = false; check(!mr::authenticated(facts), "creation time guard required"); facts.creationTime = true;
    facts.architecture = false; check(!mr::authenticated(facts), "x86 guard required"); facts.architecture = true;
    facts.executablePath = false; check(!mr::authenticated(facts), "path guard required"); facts.executablePath = true;
    facts.executableHash = false; check(!mr::authenticated(facts), "hash guard required"); facts.executableHash = true;
    facts.peIdentity = false; check(!mr::authenticated(facts), "PE identity guard required"); facts.peIdentity = true;
    facts.modulePath = false; check(!mr::authenticated(facts), "module path guard required"); facts.modulePath = true;
    facts.imageBase = false; check(!mr::authenticated(facts), "module base guard required");
}

void testAddressPlan() {
    check(mr::rangeAddress == 0x0053BEE0 && mr::rangeSize == 380, "exact admitted range");
    check(mr::firstPage == 0x0053B000 && mr::secondPage == 0x0053C000, "both touched pages queried independently");
    check(mr::firstPageBytes == 288 && mr::secondPageBytes == 92, "range divided at 4 KiB boundary");
    check(std::uint64_t{mr::rangeAddress} + mr::rangeSize == 0x0053C05C, "range end exclusive verified");
}

void testProcessAccessMask() {
    check(mr::readOnlyProcessRights == (PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE),
        "external reader access mask includes synchronize and only required query/read rights");
    check((mr::readOnlyProcessRights & mr::prohibitedProcessRights) == 0,
        "external reader mask excludes modifying, thread creation, terminate and suspend rights");
    check((mr::readOnlyProcessRights & SYNCHRONIZE) != 0,
        "wait handle right is explicitly present");
}

void testWaitOnReadOnlyHandleToHelper() {
    std::array<wchar_t, 32768> executable{};
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    check(length > 0 && length < executable.size(), "test executable path acquired");
    std::wstring command = L"\"" + std::wstring(executable.data(), length) + L"\" --reader-helper";
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    check(CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &child) != FALSE, "harmless helper process created");
    CloseHandle(child.hThread);

    HANDLE queryHandle = OpenProcess(static_cast<DWORD>(mr::readOnlyProcessRights), FALSE, child.dwProcessId);
    if (!queryHandle) {
        const DWORD error = GetLastError();
        WaitForSingleObject(child.hProcess, 6000);
        CloseHandle(child.hProcess);
        throw std::runtime_error("OpenProcess with reader mask failed: " + std::to_string(error));
    }
    const DWORD immediate = WaitForSingleObject(queryHandle, 0);
    check(immediate == WAIT_TIMEOUT, "SYNCHRONIZE permits zero-time liveness wait on read-only handle");
    const DWORD finished = WaitForSingleObject(child.hProcess, 6000);
    check(finished == WAIT_OBJECT_0, "test helper exits naturally");
    CloseHandle(queryHandle);
    CloseHandle(child.hProcess);
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wstring_view(argv[1]) == L"--reader-helper") {
        Sleep(2500);
        return 0;
    }
    try {
        testPeMapping();
        testComparison();
        testWorkingSetAndIdentity();
        testAddressPlan();
        testProcessAccessMask();
        testWaitOnReadOnlyHandleToHelper();
        std::cout << "alpha09_memory_reader_offline PASS assertions=" << assertions << " gta_opened=0 helper_process_only=1\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "alpha09_memory_reader_offline FAIL reason=" << error.what() << '\n';
        return 1;
    }
}
