#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../client/windows/tools/background_gameplay.hpp"
#include "../client/windows/tools/native_player_replay.hpp"
#include <cstdio>
#include <stdexcept>
#include <string_view>

namespace bg = grandmp::windows::background_gameplay;
namespace ni = grandmp::windows::remote_player_binding::replay;
void check(bool yes, const char* why) { if (!yes) throw std::runtime_error(why); }
unsigned calls = 0;
using TestThunk = void (__cdecl*)();
TestThunk loopThunk = nullptr;
unsigned thunkNotZero = 0, thunkPreserved = 0;
void __cdecl called() { ++calls; }
__declspec(naked) void invokeLoopThunk() { __asm {
    push ebx
    xor ebx, ebx
    mov eax, 123
    mov ecx, 456
    mov edx, 789
    call dword ptr [loopThunk]
    setnz byte ptr [thunkNotZero]
    cmp eax, 123
    jne done
    cmp ecx, 456
    jne done
    cmp edx, 789
    jne done
    cmp ebx, 0
    jne done
    mov dword ptr [thunkPreserved], 1
done:
    pop ebx
    ret
} }
#pragma code_seg(push, ".gmpatch")
__declspec(naked) void siteA() { __asm {
    nop
    nop
    nop
    nop
    nop
    nop
    ret
} }
__declspec(naked) void siteB() { __asm {
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    ret
} }
__declspec(naked) void siteC() { __asm {
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    ret
} }
#pragma code_seg(pop)
std::array<std::byte, 0x134 * 2> pads{}; // Own IMAGE only; never GTA memory.
std::uint32_t address(auto function) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(function)); }
struct TestCowPolicy { std::array<std::uint32_t, 3> sites{}; };
bool authorizeTestCow(void* context, std::uint32_t at, std::size_t size, DWORD& error) noexcept {
    const auto& policy = *static_cast<const TestCowPolicy*>(context);
    const bool permitted = size == 6 && std::find(policy.sites.begin(), policy.sites.end(), at) != policy.sites.end();
    error = permitted ? ERROR_SUCCESS : ERROR_ACCESS_DENIED;
    return permitted;
}
auto sites() {
    return std::array{
        bg::Patches::call(address(&siteA), {0x90,0x90,0x90,0x90,0x90,0x90}, address(&called)),
        bg::Patches::call(address(&siteB), {0x90,0x90,0x90,0x90,0x90,0x90}, address(&called)),
        bg::Patches::call(address(&siteC), {0x90,0x90,0x90,0x90,0x90,0x90}, address(&called))};
}

// These tests intentionally reproduce the current batch-publication gaps.
// Only this disposable executable's never-dispatched test stubs are modified.
// A passing case is evidence for NO-GO, not a safe publication certificate.
enum class Fault { query, read, protect, write, partial, flush, restore, restoreAlways, postReadback, postMatch };
struct Injection {
    TestCowPolicy policy{};
    unsigned failedSite = 0;
    Fault fault = Fault::query;
    std::array<unsigned, 3> writes{};
    bool fired = false, restored = false, externalMutationOkay = false;
    unsigned index(std::uint32_t at) const noexcept {
        for (unsigned n = 0; n < policy.sites.size(); ++n) if (policy.sites[n] == at) return n;
        return 3;
    }
    bool target(std::uint32_t at) const noexcept { return at == policy.sites[failedSite]; }
};
bool injectedQuery(void* context, std::uint32_t at, bg::patch_transaction::Page& page,
                   DWORD& error) noexcept {
    auto& f = *static_cast<Injection*>(context);
    if (f.target(at) && f.fault == Fault::query && !f.fired) {
        f.fired = true; error = ERROR_NOACCESS; return false;
    }
    return bg::patch_transaction::win32Query(nullptr, at, page, error);
}
bool injectedProtect(void* context, std::uint32_t at, std::size_t size, DWORD requested,
                     DWORD& previous, DWORD& error) noexcept {
    auto& f = *static_cast<Injection*>(context);
    if (f.target(at)) {
        const bool failOpening = f.fault == Fault::protect && !f.fired && requested == PAGE_EXECUTE_READWRITE;
        const bool failRestore = requested == PAGE_EXECUTE_READ && f.writes[f.failedSite] &&
            ((f.fault == Fault::restore && !f.fired) || f.fault == Fault::restoreAlways);
        if (failOpening || failRestore) { f.fired = true; error = ERROR_ACCESS_DENIED; return false; }
    }
    const bool okay = bg::patch_transaction::win32Protect(nullptr, at, size, requested, previous, error);
    if (okay && f.target(at) && requested == PAGE_EXECUTE_READ && f.writes[f.failedSite]) f.restored = true;
    return okay;
}
bg::patch_transaction::Io injectedRead(void* context, std::uint32_t at,
                                      std::span<std::byte> output) noexcept {
    auto& f = *static_cast<Injection*>(context);
    if (f.target(at) && f.fault == Fault::read && !f.fired) {
        f.fired = true; return {false, 0, ERROR_PARTIAL_COPY};
    }
    auto result = bg::patch_transaction::win32Read(nullptr, at, output);
    if (result.okay && f.target(at) && f.fault == Fault::postReadback && f.restored && !f.fired) {
        // An untrusted final observation must not trigger speculative overwrite.
        // The real test-stub bytes remain the replacement for inspection below.
        f.fired = true; output[0] ^= std::byte{0x40};
    }
    if (result.okay && f.target(at) && f.fault == Fault::postMatch && f.restored && !f.fired) {
        // Return an exact snapshot, then schedule a foreign writer before the
        // batch's next validation. No target stub executes during this test.
        f.fired = true;
        std::array<std::byte, 6> foreign{};
        std::copy(output.begin(), output.end(), foreign.begin());
        foreign[0] ^= std::byte{0x40};
        DWORD previous = 0, error = 0, writable = 0;
        if (bg::patch_transaction::win32Protect(nullptr, at, output.size(), PAGE_EXECUTE_READWRITE, previous, error)) {
            const auto mutation = bg::patch_transaction::win32Write(nullptr, at, foreign);
            const bool flushed = bg::patch_transaction::win32Flush(nullptr, at, foreign.size(), error);
            const bool closed = bg::patch_transaction::win32Protect(nullptr, at, output.size(), previous, writable, error);
            f.externalMutationOkay = mutation.okay && mutation.transferred == foreign.size() && flushed && closed;
        }
    }
    return result;
}
bg::patch_transaction::Io injectedWrite(void* context, std::uint32_t at,
                                       std::span<const std::byte> input) noexcept {
    auto& f = *static_cast<Injection*>(context);
    const auto n = f.index(at);
    if (n >= f.writes.size()) return {false, 0, ERROR_INVALID_ADDRESS};
    ++f.writes[n];
    if (f.target(at) && !f.fired) {
        if (f.fault == Fault::write) { f.fired = true; return {false, 0, ERROR_WRITE_FAULT}; }
        if (f.fault == Fault::partial) {
            f.fired = true;
            const auto prefix = bg::patch_transaction::win32Write(nullptr, at, input.first(2));
            return {false, prefix.transferred, ERROR_WRITE_FAULT};
        }
    }
    return bg::patch_transaction::win32Write(nullptr, at, input);
}
bool injectedFlush(void* context, std::uint32_t at, std::size_t size, DWORD& error) noexcept {
    auto& f = *static_cast<Injection*>(context);
    if (f.target(at) && f.fault == Fault::flush && !f.fired) {
        f.fired = true; error = ERROR_INVALID_FUNCTION; return false;
    }
    return bg::patch_transaction::win32Flush(nullptr, at, size, error);
}
bool injectedAuthorize(void* context, std::uint32_t at, std::size_t size, DWORD& error) noexcept {
    return authorizeTestCow(&static_cast<Injection*>(context)->policy, at, size, error);
}
void witnessesBatchFailure(std::string_view name) {
    siteA(); siteB(); siteC(); calls = 0;
    bg::close();
    const auto separator = name.rfind('_');
    check(separator != std::string_view::npos && separator + 2 == name.size(), "failure case syntax");
    const auto failedSite = static_cast<unsigned>(name.back() - '0');
    check(failedSite < 3, "failure case site");
    const auto kind = name.substr(11, separator - 11); // safety_gap_<kind>_<site>
    Injection f{}; f.failedSite = failedSite;
    if (kind == "query") f.fault = Fault::query;
    else if (kind == "read") f.fault = Fault::read;
    else if (kind == "protect") f.fault = Fault::protect;
    else if (kind == "write") f.fault = Fault::write;
    else if (kind == "partial") f.fault = Fault::partial;
    else if (kind == "flush") f.fault = Fault::flush;
    else if (kind == "restore") f.fault = Fault::restore;
    else if (kind == "restore_always") f.fault = Fault::restoreAlways;
    else if (kind == "post_readback") f.fault = Fault::postReadback;
    else if (kind == "post_match") f.fault = Fault::postMatch;
    else throw std::runtime_error("unknown failure injection");
    const auto plan = sites();
    f.policy.sites = {plan[0].at, plan[1].at, plan[2].at};
    const bg::patch_transaction::Backend api{&f, injectedQuery, injectedProtect, injectedRead,
        injectedWrite, injectedFlush, injectedAuthorize, bg::patch_transaction::win32WorkingSet};
    bg::Patches batch;
    check(!batch.publish(plan, GetModuleHandleW(nullptr), true, true, api) && !batch.installed(),
        "a failed site must not report the background batch installed");
    const auto failure = batch.failure();
    check(f.fired && failure.site == failedSite && std::strcmp(failure.category, "none"),
        "the first injected failure remains attributed to the selected site");
    unsigned reportedMask = 0, replacementMask = 0;
    for (unsigned n = 0; n < plan.size(); ++n) {
        std::array<std::byte, 6> actual{};
        const auto read = bg::patch_transaction::win32Read(nullptr, plan[n].at, actual);
        check(read.okay && read.transferred == actual.size(), "exact disposable-stub evidence read");
        const bool reported = batch.hasPublishedPatchInRange(plan[n].at, plan[n].size);
        const bool replacement = actual == plan[n].replacement;
        if (reported) reportedMask |= 1u << n;
        if (replacement) replacementMask |= 1u << n;
        if (n < failedSite) {
            check(reported && replacement, "earlier published sites remain active after a later-site failure");
        } else if (n == failedSite && f.fault == Fault::postMatch) {
            auto unknown = plan[n].replacement; unknown[0] ^= std::byte{0x40};
            check(f.externalMutationOkay && reported && actual == unknown &&
                failure.transactionFailure == bg::patch_transaction::Failure::none,
                "a valid transaction snapshot does not prevent later unknown bytes at a reported-published site");
        } else if (n == failedSite && f.fault == Fault::postReadback) {
            check(!reported && replacement && failure.cleanupError && !failure.rollbackComplete,
                "published=false cannot describe every residual mutation after unverifiable readback");
        } else {
            check(!reported && actual == plan[n].original, "failed/remainder sites have known original bytes in this case");
        }
        if (n > failedSite) check(f.writes[n] == 0, "no later-site write after the first failure");
    }
    if (f.fault == Fault::partial || f.fault == Fault::flush || f.fault == Fault::restore)
        check(failure.rollbackComplete, "current-site compensation is verified, but is not batch compensation");
    MEMORY_BASIC_INFORMATION page{};
    check(VirtualQuery(reinterpret_cast<void*>(plan[failedSite].at), &page, sizeof(page)) == sizeof(page),
        "final disposable image-page protection is observable");
    if (f.fault == Fault::restoreAlways) {
        check(page.Protect == PAGE_EXECUTE_READWRITE && failure.cleanupError && !failure.rollbackComplete,
            "original bytes with unrestored page protection remain an explicit terminal cleanup failure");
    } else check(page.Protect == PAGE_EXECUTE_READ, "other selected faults leave the original page protection verified");
    const auto writesBeforeRetry = f.writes;
    check(!batch.publish(plan, GetModuleHandleW(nullptr), true, true, api) && f.writes == writesBeforeRetry,
        "attempted batch is terminal and does not retry or reenter the writer");
    check(bg::permit == 0, "partial publication never grants background behavior");
    std::printf("safety_gap site=%u fault=%.*s reported_mask=%u residual_replacement_mask=%u "
        "current_rollback=%u cleanup=%lu terminal_no_retry=1 no_stub_dispatched\n",
        failedSite, static_cast<int>(kind.size()), kind.data(), reportedMask, replacementMask,
        unsigned(failure.rollbackComplete), failure.cleanupError);
    // The host exits after evidence is read. No residual test instruction runs.
}
void runOverlay() {
    siteA(); siteB(); siteC(); calls = 0; // Fault the disposable code pages into the working set before COW checks.
    bg::Patches patch;auto plan=sites();const auto image=GetModuleHandleW(nullptr);
    const auto windowAt=plan[0].at-6;
    check(bg::padsCall==0x53BEE6 && bg::padsCall-0x53BEE0==6,
        "production pads patch remains exactly offset six in the authenticated Hoodlum window");
    std::array<std::byte,380> disk{};disk.fill(std::byte{0x41});
    for(const auto& site:plan) {
        for(std::size_t n=0;n<site.size;++n) {
            const auto address=std::uint64_t{site.at}+n;
            if(address>=windowAt && address<std::uint64_t{windowAt}+disk.size())
                disk[static_cast<std::size_t>(address-windowAt)]=site.original[n];
        }
    }
    const auto original=disk;
    check(patch.overlay(windowAt,disk.data(),disk.size()) && disk==original,
        "pre-publication COW candidate does not reconcile uninstalled bytes");
    check(!patch.hasPublishedPatchInRange(windowAt,0x1000),"unpublished site cannot justify private image page");
    TestCowPolicy policy{{plan[0].at, plan[1].at, plan[2].at}};
    const auto transactionBackend = bg::patch_transaction::win32Backend(&policy, authorizeTestCow);
    const bool published = patch.publish(plan,image,true,true,transactionBackend);
    check(published && patch.installed(),
        "publish exact patches before post-publication overlay");
    disk=original;
    check(patch.overlay(windowAt,disk.data(),disk.size()),"post-publication Hoodlum window overlay accepted");
    for(const auto& site:plan) {
        for(std::size_t n=0;n<site.size;++n) {
            const auto address=std::uint64_t{site.at}+n;
            if(address>=windowAt && address<std::uint64_t{windowAt}+disk.size())
                check(disk[static_cast<std::size_t>(address-windowAt)]==site.replacement[n],
                    "only exact published calls intersecting the full code window are reconciled");
        }
    }
    for(std::size_t i=0;i<disk.size();++i) {
        bool patched=false;
        for(const auto& site:plan) {
            const auto address=std::uint64_t{windowAt}+i;
            if(address>=site.at && address<std::uint64_t{site.at}+site.size)patched=true;
        }
        if(!patched)check(disk[i]==original[i],"neighboring bytes in the full code window remain unchanged");
    }
    check(patch.hasPublishedPatchInRange(windowAt,0x1000),"published site alone can justify COW-private page");
    auto unknown=original;unknown[6]=std::byte{0xCC};
    check(!patch.overlay(windowAt,unknown.data(),unknown.size()),"unknown byte at own patch site remains refused");
}
void run(std::string_view name) {
    if (name.starts_with("safety_gap_")) { witnessesBatchFailure(name); return; }
    if (name == "gate") { check(!bg::executionApproved, "closed host cannot open background execution"); return; }
    if (name == "thunk") {
        bg::allow(); thunkNotZero = thunkPreserved = 0; loopThunk = &bg::loopForeground;
        invokeLoopThunk();
        check(thunkNotZero && thunkPreserved, "real permitted CMP thunk: ZF=0, unchanged registers, balanced RET");
        bg::onSetFocus(); // Permitted path returns without touching its GTA fallback.
        bg::close(); check(bg::permit == 0, "terminal close revokes permit"); return;
    }
    if (name == "pad" || name == "context") {
        pads.fill(std::byte{0x45}); const auto before = pads;
        if (name == "context") {
            check(!ni::writePadStates(1, nullptr, false, {}), "denied context does not touch invalid address");
            check(pads == before, "no context mutation"); return;
        }
        check(ni::writePadStates(address(pads.data()), GetModuleHandleW(nullptr), true, {}), "same real IMAGE writer");
        for (unsigned n = 0; n < pads.size(); ++n)
            check(pads[n] == (n < 0x60 ? std::byte{} : before[n]), "only pad0 Old/New neutral; metadata and all pad1 unchanged");
        return;
    }
    siteA(); siteB(); siteC(); calls = 0; // Fault the disposable code pages into the working set before COW checks.
    bg::Patches patch; auto plan = sites(); const auto image = GetModuleHandleW(nullptr);
    if (name == "refusal") {
        check(!patch.publish(plan, image, false), "context gate");
        check(!patch.publish(plan, image, true), "no retry after refusal"); siteA(); check(calls == 0, "no writer entered"); return;
    }
    if (name == "mismatch") {
        plan[2].original[0] = std::byte{0xCC};
        check(!patch.publish(plan, image, true) && !patch.installed(), "all sites prevalidated before first write");
        siteA(); siteB(); check(calls == 0, "late mismatch leaves earlier sites untouched"); return;
    }
    if (name == "overlay") {
        runOverlay();return;
    }
    TestCowPolicy policy{{plan[0].at, plan[1].at, plan[2].at}};
    const auto transactionBackend = bg::patch_transaction::win32Backend(&policy, authorizeTestCow);
    const bool published = patch.publish(plan, image, true, true, transactionBackend);
    check(published && patch.installed(),
        "publish on own admitted IMAGE");
    check(!patch.publish(plan, image, true), "one installation only");
    for (unsigned n = 0; n < 100; ++n) { siteA(); siteB(); siteC(); }
    check(calls == 300, "one target invocation per original-shaped CALL, no duplicated processing");
    MEMORY_BASIC_INFORMATION page{}; VirtualQuery(reinterpret_cast<void*>(plan[0].at), &page, sizeof(page));
    check(page.Protect == PAGE_EXECUTE_READ, "code protection restored");
}
int main(int argc, char** argv) {
    try { if (argc != 2) return 2; run(argv[1]); return 0; }
    catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
