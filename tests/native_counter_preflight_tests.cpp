#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "native_counter_preflight.hpp"
#include "native_counter_call.hpp"
#include "native_counter_supervision.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string_view>

namespace {
using namespace grandmp::windows;
template<class T> void put(std::vector<std::byte>& bytes, std::size_t position, const T& value) {
    std::memcpy(bytes.data() + position, &value, sizeof(value));
}
NativeCounterFileEvidence moduleFixture() {
    NativeCounterFileEvidence file;
    file.bytes.resize(0xA00);
    IMAGE_DOS_HEADER dos{};
    dos.e_magic = IMAGE_DOS_SIGNATURE; dos.e_lfanew = 0x80;
    put(file.bytes, 0, dos);
    IMAGE_NT_HEADERS32 nt{};
    nt.Signature = IMAGE_NT_SIGNATURE;
    nt.FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    nt.FileHeader.Characteristics = IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_DLL;
    nt.FileHeader.NumberOfSections = 2;
    nt.FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER32);
    nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    nt.OptionalHeader.ImageBase = 0x10000000;
    nt.OptionalHeader.SizeOfImage = 0x3000;
    nt.OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
    nt.OptionalHeader.DataDirectory[0] = {0x2000, 0x180};
    put(file.bytes, 0x80, nt);
    IMAGE_SECTION_HEADER code{}, data{};
    code.VirtualAddress = 0x1000; code.Misc.VirtualSize = 0x200;
    code.PointerToRawData = 0x400; code.SizeOfRawData = 0x200;
    code.Characteristics = IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;
    data.VirtualAddress = 0x2000; data.Misc.VirtualSize = 0x400;
    data.PointerToRawData = 0x600; data.SizeOfRawData = 0x400;
    data.Characteristics = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE;
    put(file.bytes, 0x80 + sizeof(nt), code);
    put(file.bytes, 0x80 + sizeof(nt) + sizeof(code), data);
    IMAGE_EXPORT_DIRECTORY exports{};
    exports.NumberOfFunctions = exports.NumberOfNames = 3;
    exports.AddressOfFunctions = 0x2040;
    exports.AddressOfNameOrdinals = 0x2050;
    exports.AddressOfNames = 0x2060;
    put(file.bytes, 0x600, exports);
    const std::uint32_t functions[]{0x2200, 0x1000, 0x1008};
    const std::uint16_t ordinals[]{0, 1, 2};
    const std::uint32_t names[]{0x2080, 0x20B0, 0x20F0};
    put(file.bytes, 0x640, functions); put(file.bytes, 0x650, ordinals); put(file.bytes, 0x660, names);
    const char first[] = "GrandMPNativeCounterReady";
    const char second[] = "GrandMPNativeCounterWrapperHost";
    const char third[] = "GrandMPNativeCounterPrepareDeferred";
    put(file.bytes, 0x680, first); put(file.bytes, 0x6B0, second); put(file.bytes, 0x6F0, third);
    file.inspection = inspectExecutable(file.bytes);
    file.imageSize = 0x3000; file.imageBase = 0x10000000;
    file.digest.fill(std::byte{1});  // Supplied synthetic identity; not a real ASI hash.
    return file;
}
struct Fixture {
    NativeCounterFileEvidence executable{}, module = moduleFixture();
    NativeCounterPhysicalSnapshot snapshot{};
    NativeCounterCall call = *encodeNativeCounterCall(0x53E981, 0x53BEE0);
    Fixture() {
        executable.inspection = {PeStatus::pe32_i386, 0, 14383616};
        constexpr unsigned char digest[]{0xA5,0x59,0xAA,0x77,0x2F,0xD1,0x36,0x37,
            0x91,0x55,0xEF,0xA7,0x1F,0x00,0xC4,0x7A,0xAD,0x34,0xBB,0xFE,
            0xAE,0x61,0x96,0xB0,0xFE,0x10,0x47,0xD0,0x64,0x5C,0xBD,0x26};
        std::memcpy(executable.digest.data(), digest, 32);
        executable.imageBase = snapshot.executableBase = 0x400000;
        snapshot.processId = 13; snapshot.moduleBase = 0x10000000;
        snapshot.loadedModuleFileDigest = module.digest;
        snapshot.first = {nativeCounterReadyMagic, 1, 80, 13, 0x10000000, 0x3000,
            2, 1, 0x10001000, 0x53BEE0, 0x53E981, 0x10002280, 0x10002200,
            0, 1, 0, 21, 0, 0xFF, 0};
        std::memcpy(&snapshot.first.originalBytesLow, call.data(), 4);
        sync(); snapshot.callBytes = call;
    }
    void sync() { snapshot.second = snapshot.first; }
    NativeCounterPreflightResult inspect() {
        return inspectNativeCounterPhysicalPreflight(executable, module, snapshot);
    }
};
bool fileCase() {
    wchar_t directory[MAX_PATH]{}, temporary[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, directory) || !GetTempFileNameW(directory, L"gmp", 0, temporary)) return false;
    { std::ofstream stream(std::filesystem::path(temporary), std::ios::binary); stream << "abc"; }
    const auto evidence = readNativeCounterFile(temporary);
    DeleteFileW(temporary);
    constexpr unsigned char expected[]{0xBA,0x78,0x16,0xBF,0x8F,0x01,0xCF,0xEA,
        0x41,0x41,0x40,0xDE,0x5D,0xAE,0x22,0x23,0xB0,0x03,0x61,0xA3,
        0x96,0x17,0x7A,0x9C,0xB4,0x10,0xFF,0x61,0xF2,0x00,0x15,0xAD};
    return evidence && evidence->bytes.size() == 3 && evidence->inspection.status == PeStatus::invalid &&
        std::memcmp(evidence->digest.data(), expected, 32) == 0 && !readNativeCounterFile(temporary);
}
bool actualAsiCase(const std::filesystem::path& path, bool async = false, bool hoodlum = false) {
    const auto admitted = readNativeCounterFile(path);
    if (!admitted || admitted->inspection.status != PeStatus::pe32_i386) return false;
    HMODULE module = LoadLibraryW(path.c_str());
    if (!module) return false;
    const auto entry = reinterpret_cast<void(__cdecl*)()>(GetProcAddress(module, "InitializeASI"));
    const auto prepare = reinterpret_cast<int(__cdecl*)()>(GetProcAddress(module, "GrandMPNativeCounterPrepareDeferred"));
    auto* ready = reinterpret_cast<NativeCounterReadyV1*>(GetProcAddress(module, "GrandMPNativeCounterReady"));
    bool ok = entry && prepare && ready;
    if (ok) {
        if (!SetEnvironmentVariableW(L"GRANDMP_NATIVE_COUNTER_PREPARE_HOODLUM", hoodlum ? L"1" : nullptr)) return false;
        if (async && !SetEnvironmentVariableW(L"GRANDMP_NATIVE_COUNTER_ASYNC_READY", L"1")) return false;
        entry();
        if (async) {
            const auto deadline = GetTickCount64() + 5000;
            while (std::atomic_ref(ready->state).load(std::memory_order_acquire) != 2 &&
                   GetTickCount64() < deadline) Sleep(1);
        }
        // This test EXE is deliberately not the cooperative counter host.
        ok = prepare() == 0 && ready->state == (async ? 2U : 1U) &&
            ready->residencyConfirmed == (async ? 1U : 0U);
        const auto before = *ready;
        Fixture facts;
        facts.snapshot.processId = GetCurrentProcessId();
        facts.snapshot.moduleBase = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(module));
        facts.snapshot.loadedModuleFileDigest = admitted->digest;
        facts.snapshot.first = facts.snapshot.second = *ready;
        ok &= inspectNativeCounterPhysicalPreflight(facts.executable, *admitted, facts.snapshot) ==
              NativeCounterPreflightResult::not_ready;
        const auto admission = dryRunNativeCounterAdmission(facts.executable, *admitted, facts.snapshot);
        ok &= admission.evidence == NativeCounterPreflightResult::not_ready && !admission.plan &&
            std::memcmp(&before, ready, sizeof(before)) == 0 && ready->wrapperCapable == 0 &&
            ready->installed == 0 && ready->originalTarget == 0 && ready->callsite == 0;
    }
    ok &= FreeLibrary(module) != FALSE;
    ok &= (GetModuleHandleW(path.c_str()) != nullptr) == async;
    return ok;
}
void initializingFixture(Fixture& f) {
    auto& r = f.snapshot.first;
    r.state = static_cast<std::uint32_t>(NativeCounterReadyState::initializing);
    r.wrapperCapable = r.callsite = r.originalTarget = r.originalBytesLow = r.originalBytesHigh = 0;
    f.sync();
}
bool hoodlumPreparationCase() {
    Fixture f;
    const auto expected = f.snapshot.first;
    initializingFixture(f);
    const auto before = f.snapshot;
    const auto call = f.call;
    const auto candidate = prepareNativeCounterHoodlumDescriptor(f.executable, f.module, f.snapshot);
    if (!candidate || std::memcmp(&expected, &*candidate, sizeof(expected)) != 0 || candidate->installed != 0 ||
        std::memcmp(&before.first, &f.snapshot.first, sizeof(before.first)) != 0 ||
        std::memcmp(&before.second, &f.snapshot.second, sizeof(before.second)) != 0 || f.call != call) return false;
    f.snapshot.first = f.snapshot.second = *candidate;
    return dryRunNativeCounterAdmission(f.executable, f.module, f.snapshot).plan.has_value();
}
bool hoodlumPreparationRejectionsCase() {
    const auto rejected = [](Fixture& f) {
        const auto before = f.snapshot;
        return !prepareNativeCounterHoodlumDescriptor(f.executable, f.module, f.snapshot) &&
            std::memcmp(&before.first, &f.snapshot.first, sizeof(before.first)) == 0 &&
            std::memcmp(&before.second, &f.snapshot.second, sizeof(before.second)) == 0;
    };
    for (std::uint32_t state : {0U, 1U, 2U, 3U, 4U, 999U}) {
        Fixture f; initializingFixture(f); f.snapshot.first.state = state; f.sync();
        if (!rejected(f)) return false;
    }
    for (auto field : {&NativeCounterReadyV1::magic, &NativeCounterReadyV1::interfaceVersion,
        &NativeCounterReadyV1::size, &NativeCounterReadyV1::residencyConfirmed,
        &NativeCounterReadyV1::wrapperCapable, &NativeCounterReadyV1::installed,
        &NativeCounterReadyV1::shutdownLogical, &NativeCounterReadyV1::reserved,
        &NativeCounterReadyV1::callsite, &NativeCounterReadyV1::originalTarget,
        &NativeCounterReadyV1::originalBytesLow, &NativeCounterReadyV1::originalBytesHigh,
        &NativeCounterReadyV1::wrapper, &NativeCounterReadyV1::processId}) {
        Fixture f; initializingFixture(f); ++(f.snapshot.first.*field); f.sync();
        if (!rejected(f)) return false;
    }
    { Fixture f; initializingFixture(f); f.executable.digest[0] ^= std::byte{1};
      if (!rejected(f)) return false; }
    { Fixture f; initializingFixture(f); f.executable.inspection.status = PeStatus::unsupported_architecture;
      if (!rejected(f)) return false; }
    { Fixture f; initializingFixture(f); f.snapshot.executableBase++;
      if (!rejected(f)) return false; }
    { Fixture f; initializingFixture(f); f.snapshot.second.threadId++;
      if (!rejected(f)) return false; }
    { Fixture f; initializingFixture(f); f.module.bytes.resize(0x81);
      if (!rejected(f)) return false; }
    for (unsigned i = 0; i < 5; ++i) {
        Fixture f; initializingFixture(f); f.call[i] ^= 1;
        if (!rejected(f)) return false;
    }
    { Fixture f; initializingFixture(f); f.snapshot.callBytes = std::span(f.call).first(4);
      if (!rejected(f)) return false; }
    { Fixture f; initializingFixture(f); f.snapshot.moduleBase = 0xF0000000;
      auto& r = f.snapshot.first; r.moduleBase = f.snapshot.moduleBase;
      r.wrapper = r.moduleBase + 0x1000; r.counterAddress = r.moduleBase + 0x2280;
      r.descriptorAddress = r.moduleBase + 0x2200; f.sync();
      if (!rejected(f)) return false; }
    return true;
}
bool admissionCase() {
    Fixture fixture;
    const auto before = fixture.snapshot;
    const auto moduleBytes = fixture.module.bytes;
    const auto call = fixture.call;
    const auto result = dryRunNativeCounterAdmission(fixture.executable, fixture.module, fixture.snapshot);
    if (result.evidence != NativeCounterPreflightResult::evidence_consistent_read_only || !result.plan)
        return false;
    const auto& plan = *result.plan;
    return plan.callsite == 0x53E981 && plan.originalTarget == 0x53BEE0 && plan.wrapper == 0x10001000 &&
        plan.original == call && plan.replacement == *encodeNativeCounterCall(plan.callsite, plan.wrapper) &&
        decodeNativeCounterCall(plan.callsite, plan.replacement) == plan.wrapper &&
        std::memcmp(&before.first, &fixture.snapshot.first, sizeof(before.first)) == 0 &&
        std::memcmp(&before.second, &fixture.snapshot.second, sizeof(before.second)) == 0 &&
        fixture.module.bytes == moduleBytes && fixture.call == call && !nativeCounterGtaWriteEnabled;
}
bool admissionRejectionsCase() {
    using Result = NativeCounterPreflightResult;
    const auto rejected = [](Fixture& fixture, Result reason) {
        const auto result = dryRunNativeCounterAdmission(fixture.executable, fixture.module, fixture.snapshot);
        return result.evidence == reason && !result.plan;
    };
    { Fixture f; f.executable.digest[0] ^= std::byte{1};
      if (!rejected(f, Result::unknown_executable)) return false; }
    { Fixture f; f.snapshot.executableBase++;
      if (!rejected(f, Result::incompatible_base)) return false; }
    { Fixture f; f.snapshot.loadedModuleFileDigest[0] ^= std::byte{1};
      if (!rejected(f, Result::incompatible_module)) return false; }
    { Fixture f; f.snapshot.second.threadId++;
      if (!rejected(f, Result::unstable_snapshot)) return false; }
    for (std::uint32_t state : {0U, 1U, 3U, 4U, 5U, 999U}) {
        Fixture f; f.snapshot.first.state = state; f.sync();
        if (!rejected(f, Result::not_ready) || planNativeCounterCall(f.snapshot.second, f.call)) return false;
    }
    for (auto field : {&NativeCounterReadyV1::residencyConfirmed, &NativeCounterReadyV1::wrapperCapable,
                       &NativeCounterReadyV1::installed, &NativeCounterReadyV1::shutdownLogical}) {
        Fixture f; f.snapshot.first.*field ^= 1; f.sync();
        if (!rejected(f, Result::not_ready) || planNativeCounterCall(f.snapshot.second, f.call)) return false;
    }
    for (auto field : {&NativeCounterReadyV1::processId, &NativeCounterReadyV1::moduleBase,
                       &NativeCounterReadyV1::wrapper, &NativeCounterReadyV1::originalTarget,
                       &NativeCounterReadyV1::callsite, &NativeCounterReadyV1::interfaceVersion}) {
        Fixture f; ++(f.snapshot.first.*field); f.sync();
        if (!rejected(f, Result::incompatible_identity)) return false;
    }
    for (unsigned index = 0; index < 5; ++index) {
        Fixture f; f.call[index] ^= 1;
        if (!rejected(f, Result::incompatible_call) || planNativeCounterCall(f.snapshot.second, f.call)) return false;
    }
    { Fixture f; f.snapshot.callBytes = std::span(f.call).first(4);
      if (!rejected(f, Result::incompatible_call)) return false; }
    { Fixture f; ++f.snapshot.first.originalBytesLow; f.sync();
      if (!rejected(f, Result::incompatible_call)) return false; }
    // Valid image bindings and original bytes, but wrapper outside conservative rel32 range.
    { Fixture f; f.snapshot.moduleBase = 0xF0000000;
      auto& r = f.snapshot.first;
      r.moduleBase = f.snapshot.moduleBase; r.wrapper = r.moduleBase + 0x1000;
      r.counterAddress = r.moduleBase + 0x2280; r.descriptorAddress = r.moduleBase + 0x2200; f.sync();
      if (!rejected(f, Result::incompatible_call)) return false; }
    return true;
}
bool focusObservationCase(std::string_view scenario) {
    using Observation = NativeCounterFocusObservation;
    using State = Observation::State;
    constexpr std::uint32_t pid = 123;
    constexpr std::uint64_t start = 500;
    Observation observation(pid, start);
    if (scenario == "focus_success") {
        if (observation.sample(start, 0) != State::waiting || observation.finished() ||
            observation.sample(start + 6000, pid) != State::observing ||
            observation.sample(start + 7499, pid) != State::observing || observation.finished() ||
            observation.callbackObserved(10, 11) ||
            observation.sample(start + 7500, pid) != State::complete || !observation.finished() ||
            !observation.callbackObserved(10, 11) || observation.callbackObserved(10, 10) ||
            observation.callbackObserved(10, 9)) return false;
        std::puts("focus_exact_pid=1 observation_ms=1500 restore_due=1");
        return true;
    }
    if (scenario == "focus_timeout") {
        if (observation.sample(start + 9999, pid + 1) != State::waiting || observation.finished() ||
            observation.sample(start + 10000, pid) != State::no_focus_timeout || !observation.finished() ||
            observation.callbackObserved(0, 100) ||
            observation.sample(start + 12000, pid) != State::no_focus_timeout) return false;
        Observation invalid(0, start);
        if (invalid.sample(start, 0) != State::waiting ||
            invalid.sample(start + 10000, 0) != State::no_focus_timeout) return false;
        std::puts("foreign_missing_or_zero_pid_not_focus=1 budget_ms=10000 restore_due=1 callback_observed=0");
        return true;
    }
    if (scenario == "focus_late") {
        if (observation.sample(start + 9000, pid) != State::observing ||
            observation.sample(start + 9999, pid) != State::observing ||
            observation.sample(start + 10000, pid) != State::focus_timeout || !observation.finished() ||
            observation.callbackObserved(0, 100) ||
            observation.sample(start + 10500, pid) != State::focus_timeout) return false;
        std::puts("late_focus_does_not_extend_budget=1 restore_due=1 callback_observed=0");
        return true;
    }
    if (scenario == "focus_lost") {
        for (const auto foreignPid : {0U, pid + 1}) {
            Observation lost(pid, start);
            if (lost.sample(start, pid) != State::observing ||
                lost.sample(start + 500, foreignPid) != State::focus_lost || !lost.finished() ||
                lost.callbackObserved(0, 100) || lost.sample(start + 2000, pid) != State::focus_lost)
                return false;
        }
        std::puts("focus_loss_sticky=1 regain_does_not_restart=1 restore_due=1 callback_observed=0");
        return true;
    }
    if (scenario == "focus_boundary") {
        if (observation.sample(start + 8500, pid) != State::observing ||
            observation.sample(start + 10000, pid) != State::complete || !observation.finished()) return false;
        Observation overdue(pid, start);
        if (overdue.sample(start + 8500, pid) != State::observing ||
            overdue.sample(start + 10001, pid) != State::focus_timeout ||
            overdue.callbackObserved(0, 100)) return false;
        std::puts("deadline_inclusive_no_extension=1 restore_due=1");
        return true;
    }
    return false;
}
}  // namespace
int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "actual_hoodlum_rejected") return actualAsiCase(argv[2], true, true) ? 0 : 25;
    if (argc == 3 && std::string_view(argv[1]) == "actual_ready") return actualAsiCase(argv[2], true) ? 0 : 20;
    if (argc == 3 && std::string_view(argv[1]) == "actual_asi") return actualAsiCase(argv[2]) ? 0 : 19;
    if (argc != 2) return 2;
    const std::string_view scenario(argv[1]);
    if (scenario.starts_with("focus_")) return focusObservationCase(scenario) ? 0 : 27;
    if (scenario == "hoodlum_preparation") return hoodlumPreparationCase() ? 0 : 23;
    if (scenario == "hoodlum_preparation_rejections") return hoodlumPreparationRejectionsCase() ? 0 : 24;
    if (scenario == "admission") return admissionCase() ? 0 : 21;
    if (scenario == "admission_rejections") return admissionRejectionsCase() ? 0 : 22;
    using Result = NativeCounterPreflightResult;
    if (scenario == "smoke_scope") {
        Fixture f;
        constexpr auto exe = L"D:/Jogos/GrandMP-Smoke/gta_sa.exe";
        constexpr auto asi = L"D:/Jogos/GrandMP-Smoke/scripts/GrandMPNativeCounterExperimental.asi";
        // Independent test vector: exact SHA-256 explicitly approved by the user.
        constexpr std::array<std::byte, 32> expected{
            std::byte{0xAC}, std::byte{0x0B}, std::byte{0x83}, std::byte{0x9D},
            std::byte{0x5E}, std::byte{0x52}, std::byte{0x4D}, std::byte{0xDA},
            std::byte{0xD6}, std::byte{0x59}, std::byte{0x58}, std::byte{0x8E},
            std::byte{0xC9}, std::byte{0xB9}, std::byte{0x89}, std::byte{0x4C},
            std::byte{0x2B}, std::byte{0x0E}, std::byte{0xDC}, std::byte{0x1A},
            std::byte{0x07}, std::byte{0xFE}, std::byte{0xA8}, std::byte{0xEB},
            std::byte{0x57}, std::byte{0xDB}, std::byte{0x23}, std::byte{0xE2},
            std::byte{0xFD}, std::byte{0xCC}, std::byte{0x84}, std::byte{0x6C}};
        const bool approved = nativeCounterSmokeDigestApproved(expected);
        if (!approved || nativeCounterSmokeApprovedExperimentalDigest != expected ||
            nativeCounterSmokeDigestApproved({}) ||
            nativeCounterSmokeScope(exe, asi, f.executable, expected, {}) ||
            nativeCounterSmokeScope(exe, asi, f.executable, {}, expected)) return 26;
        // Matching CLI and measured copies cannot approve any other digest.
        for (std::size_t index = 0; index < expected.size(); ++index) {
            auto unapproved = expected;
            unapproved[index] ^= std::byte{1};
            if (nativeCounterSmokeDigestApproved(unapproved) ||
                nativeCounterSmokeScope(exe, asi, f.executable, unapproved, unapproved)) return 26;
        }
        f.module.digest = expected;
        if (nativeCounterSmokeScope(exe, asi, f.executable, f.module.digest, expected) != approved ||
            nativeCounterSmokeScope(L"D:/Jogos/Other/gta_sa.exe", asi, f.executable, f.module.digest, expected) ||
            nativeCounterSmokeScope(exe, L"D:/Jogos/GrandMP-Smoke/GrandMPNativeCounterExperimental.asi", f.executable, f.module.digest, expected) ||
            nativeCounterSmokeScope(exe, L"D:/Jogos/GrandMP-Smoke/GrandMP.asi", f.executable, f.module.digest, expected)) return 26;
        f.module.digest[0] ^= std::byte{1};
        if (nativeCounterSmokeScope(exe, asi, f.executable, f.module.digest, expected)) return 26;
        f.module.digest = expected;
        f.executable.digest[0] ^= std::byte{1};
        if (nativeCounterSmokeScope(exe, asi, f.executable, f.module.digest, expected)) return 26;
        f = Fixture{};
        std::printf("smoke_approved_reference_present=%u\n", approved ? 1U : 0U);
        return !nativeCounterSmokeScope(exe, asi, f.executable, {}, {}) ? 0 : 26;
    }
    Fixture fixture;
    if (scenario == "file") return fileCase() ? 0 : 3;
    if (fixture.inspect() != Result::evidence_consistent_read_only || nativeCounterGtaWriteEnabled) return 4;
    if (scenario == "identity") {
        fixture.executable.digest[0] ^= std::byte{1};
        if (fixture.inspect() != Result::unknown_executable) return 5;
        fixture = Fixture{}; fixture.snapshot.callBytes = fixture.call;
        fixture.executable.inspection.status = PeStatus::unsupported_architecture;
        if (fixture.inspect() != Result::unknown_executable) return 6;
        fixture = Fixture{}; fixture.snapshot.callBytes = fixture.call;
        fixture.snapshot.executableBase++;
        return fixture.inspect() == Result::incompatible_base ? 0 : 7;
    }
    if (scenario == "ready") {
        for (std::uint32_t state : {0U, 1U, 3U, 4U, 5U, 999U}) {
            fixture.snapshot.first.state = state; fixture.sync();
            if (fixture.inspect() != Result::not_ready) return 8;
        }
        fixture.snapshot.first.state = 2; fixture.sync();
        fixture.snapshot.second.threadId++;
        if (fixture.inspect() != Result::unstable_snapshot) return 9;
        fixture.sync(); fixture.snapshot.first.residencyConfirmed = 0; fixture.sync();
        return fixture.inspect() == Result::not_ready ? 0 : 10;
    }
    if (scenario == "module") {
        fixture.snapshot.loadedModuleFileDigest[0] ^= std::byte{1};
        if (fixture.inspect() != Result::incompatible_module) return 11;
        fixture.snapshot.loadedModuleFileDigest = fixture.module.digest;
        put(fixture.module.bytes, 0x644, std::uint32_t{0x2080});  // Forwarded wrapper rejected.
        if (fixture.inspect() != Result::incompatible_module) return 12;
        fixture.module.bytes.resize(0x81);
        return fixture.inspect() == Result::incompatible_module ? 0 : 13;
    }
    if (scenario == "call") {
        for (unsigned index = 0; index < 5; ++index) {
            fixture.call[index] ^= 1;
            if (fixture.inspect() != Result::incompatible_call) return 14;
            fixture.call[index] ^= 1;
        }
        fixture.snapshot.callBytes = std::span(fixture.call).first(4);
        return fixture.inspect() == Result::incompatible_call ? 0 : 15;
    }
    if (scenario == "policy") {
        auto good = fixture.snapshot.first;
        const unsigned offsets[]{0, 1, 2, 3, 4, 5, 8, 9, 10, 12, 19};
        for (unsigned offset : offsets) {
            fixture.snapshot.first = good;
            std::uint32_t value = 0;
            auto* field = reinterpret_cast<unsigned char*>(&fixture.snapshot.first) + offset * 4;
            std::memcpy(&value, field, 4);
            ++value;
            std::memcpy(field, &value, 4);
            fixture.sync();
            if (fixture.inspect() != Result::incompatible_identity) return 18;
        }
        fixture.snapshot.first = good;
        fixture.snapshot.first.wrapper++; fixture.sync();
        if (fixture.inspect() != Result::incompatible_identity) return 16;
        fixture.snapshot.first.wrapper--; fixture.snapshot.first.counterAddress = 0x10001000; fixture.sync();
        return fixture.inspect() == Result::incompatible_identity && !nativeCounterGtaWriteEnabled ? 0 : 17;
    }
    return 2;
}
