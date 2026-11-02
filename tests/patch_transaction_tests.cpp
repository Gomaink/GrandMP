#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../client/windows/tools/ped_smoke_patch_transaction.hpp"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace tx = grandmp::windows::ped_smoke::patch_transaction;
namespace ps = grandmp::windows::ped_smoke;
namespace {
unsigned assertions = 0;
void check(bool value, const char* reason) {
    ++assertions;
    if (!value) throw std::runtime_error(reason);
}

struct Fake {
    std::uint32_t address = ps::phaseCall;
    std::array<std::byte, 8> bytes{};
    std::array<std::byte, 5> original{std::byte{0xE8},std::byte{0x11},std::byte{0x22},std::byte{0x33},std::byte{0x44}};
    std::array<std::byte, 5> replacement{std::byte{0xE8},std::byte{0xA1},std::byte{0xB2},std::byte{0xC3},std::byte{0xD4}};
    DWORD protection = PAGE_EXECUTE_READ;
    DWORD allocationProtection = PAGE_EXECUTE_READ;
    DWORD state = MEM_COMMIT, type = MEM_IMAGE;
    std::uintptr_t allocation = ps::imageBase;
    bool shared = true, valid = true, authorize = true;
    bool failProtect = false, mismatchOldProtect = false;
    bool partialWrite = false, failWrite = false, failRollbackWrite = false;
    bool failFlush = false, failRestoreOnce = false, failRestoreAlways = false;
    bool mutateAfterWritable = false, mutateAfterRestore = false;
    unsigned queries = 0, reads = 0, writes = 0, protects = 0, flushes = 0;

    explicit Fake(bool cow = false, std::uint32_t at = ps::phaseCall) : address(at) {
        std::copy(original.begin(), original.end(), bytes.begin());
        if (cow) { protection = PAGE_EXECUTE_WRITECOPY; allocationProtection = PAGE_EXECUTE_WRITECOPY; }
    }
};

bool query(void* context, std::uint32_t at, tx::Page& page, DWORD& error) noexcept {
    auto& f = *static_cast<Fake*>(context); ++f.queries; error = ERROR_SUCCESS;
    const auto base = static_cast<std::uintptr_t>(at & ~std::uint32_t{0xFFF});
    page = {base, 0x1000, f.allocation, f.state, f.type, f.protection, f.allocationProtection};
    return at >= f.address - (f.address & 0xFFF) && std::uint64_t{at} < std::uint64_t{f.address - (f.address & 0xFFF)} + 0x1000;
}

bool protect(void* context, std::uint32_t, std::size_t, DWORD requested, DWORD& previous, DWORD& error) noexcept {
    auto& f = *static_cast<Fake*>(context); ++f.protects; error = ERROR_SUCCESS;
    const bool restoring = requested == PAGE_EXECUTE_READ || requested == PAGE_EXECUTE_WRITECOPY;
    if (f.failProtect) { error = ERROR_ACCESS_DENIED; return false; }
    if (restoring && (f.failRestoreAlways || (f.failRestoreOnce &&
        f.protects == (f.allocationProtection == PAGE_EXECUTE_WRITECOPY ? 1u : 2u)))) {
        error = ERROR_ACCESS_DENIED; return false;
    }
    previous = f.mismatchOldProtect && requested == PAGE_EXECUTE_READWRITE ? PAGE_READWRITE : f.protection;
    f.protection = requested;
    if (f.protection == PAGE_EXECUTE_READWRITE && f.mutateAfterWritable && f.reads >= 1) f.bytes[1] ^= std::byte{0x40};
    if (restoring && f.mutateAfterRestore) f.bytes[2] ^= std::byte{0x20};
    return true;
}

tx::Io read(void* context, std::uint32_t, std::span<std::byte> output) noexcept {
    auto& f = *static_cast<Fake*>(context); ++f.reads;
    std::copy_n(f.bytes.begin(), output.size(), output.begin());
    return {true, output.size(), ERROR_SUCCESS};
}

tx::Io write(void* context, std::uint32_t, std::span<const std::byte> input) noexcept {
    auto& f = *static_cast<Fake*>(context); ++f.writes;
    if (f.failWrite && f.writes == 1) return {false, 0, ERROR_WRITE_FAULT};
    if (f.failRollbackWrite && f.writes > 1) return {false, 0, ERROR_WRITE_FAULT};
    auto count = input.size();
    if (f.partialWrite && f.writes == 1) count = 2;
    std::copy_n(input.begin(), count, f.bytes.begin());
    if (f.protection == PAGE_EXECUTE_WRITECOPY && count) {
        f.protection = PAGE_EXECUTE_READWRITE;
        f.shared = false;
    }
    return {!(f.partialWrite && f.writes == 1), count,
        f.partialWrite && f.writes == 1 ? static_cast<DWORD>(ERROR_WRITE_FAULT) : static_cast<DWORD>(ERROR_SUCCESS)};
}

bool flush(void* context, std::uint32_t, std::size_t, DWORD& error) noexcept {
    auto& f = *static_cast<Fake*>(context); ++f.flushes;
    if (f.failFlush && f.flushes == 1) { error = ERROR_INVALID_FUNCTION; return false; }
    error = ERROR_SUCCESS; return true;
}

bool authorize(void* context, std::uint32_t at, std::size_t size, DWORD& error) noexcept {
    auto& f = *static_cast<Fake*>(context);
    error = f.authorize ? ERROR_SUCCESS : ERROR_ACCESS_DENIED;
    return f.authorize && tx::exactCowSite(at, size);
}

bool workingSet(void* context, std::uint32_t, bool& valid, bool& shared, DWORD& error) noexcept {
    auto& f = *static_cast<Fake*>(context); valid = f.valid; shared = f.shared;
    error = ERROR_SUCCESS; return true;
}

tx::Backend backend(Fake& fake) noexcept {
    return {&fake, query, protect, read, write, flush, authorize, workingSet};
}

tx::Result apply(Fake& fake, bool authenticated = true) {
    return tx::apply(backend(fake), fake.address,
        std::span<const std::byte>(fake.original), std::span<const std::byte>(fake.replacement), authenticated);
}

bool isOriginal(const Fake& f) { return std::equal(f.original.begin(), f.original.end(), f.bytes.begin()); }
bool isReplacement(const Fake& f) { return std::equal(f.replacement.begin(), f.replacement.end(), f.bytes.begin()); }

void rxSuccess() {
    Fake f; const auto result = apply(f);
    check(bool(result) && isReplacement(f), "RX writes only exact replacement bytes");
    check(f.protection == PAGE_EXECUTE_READ && f.flushes == 1, "RX protection restored and instruction cache flushed");
}
void cowHookAndPadsSuccess() {
    for (const auto at : {ps::phaseCall, tx::backgroundPadsCall}) {
        Fake f(true, at); const auto result = apply(f);
        check(bool(result) && result.cow && isReplacement(f), "exact Hoodlum COW site writes replacement");
        check(f.protection == PAGE_EXECUTE_WRITECOPY && f.allocationProtection == PAGE_EXECUTE_WRITECOPY,
            "COW writer restores exact original protection flags");
        check(!f.shared && f.valid, "COW write creates a resident private copy");
        check(f.protects == 1, "COW path does not pre-convert the shared page to RWX");
    }
}
void refusesUnexpectedProtectionAndIdentity() {
    Fake unexpected; unexpected.protection = PAGE_EXECUTE_READWRITE;
    auto result = apply(unexpected);
    check(result.failure == tx::Failure::unexpected_protection && unexpected.writes == 0,
        "unexpected initial protection refuses before write");
    Fake wrongSite(true, tx::backgroundPadsCall + 1); result = apply(wrongSite);
    check(result.failure == tx::Failure::unexpected_protection && wrongSite.writes == 0,
        "adjacent COW address is not whitelisted");
    Fake unauthenticated(true); result = apply(unauthenticated, false);
    check(result.failure == tx::Failure::unexpected_protection && unauthenticated.writes == 0,
        "COW is denied without authenticated Hoodlum identity");
}
void refusesPreimageAndPageMismatch() {
    Fake bytes; bytes.bytes[2] ^= std::byte{0x01}; auto result = apply(bytes);
    check(result.failure == tx::Failure::preimage_mismatch && bytes.protects == 0 && bytes.writes == 0,
        "divergent preimage refuses before protection transition");
    Fake mapping; mapping.allocation = 0x500000; result = apply(mapping);
    check(result.failure == tx::Failure::page_identity && mapping.writes == 0,
        "wrong image allocation base refuses");
    Fake raced; raced.mismatchOldProtect = true; result = apply(raced);
    check(result.failure == tx::Failure::unexpected_protection && raced.writes == 0 &&
        raced.protection == PAGE_READWRITE,
        "unexpected VirtualProtect old protection is restored exactly before refusal");
}
void rollsBackPartialWrite() {
    for (const bool cow : {false, true}) {
        Fake f(cow); f.partialWrite = true; const auto result = apply(f);
        check(result.failure == tx::Failure::write_partial && result.rollbackAttempted &&
            result.rollbackComplete && isOriginal(f), "short write is rolled back to exact preimage");
        check(f.protection == static_cast<DWORD>(cow ? PAGE_EXECUTE_WRITECOPY : PAGE_EXECUTE_READ),
            "partial-write rollback restores original protection");
    }
}
void rollsBackFlushAndRestoreFailures() {
    for (const bool cow : {false, true}) {
        Fake flushFake(cow); flushFake.failFlush = true; auto result = apply(flushFake);
        check(result.failure == tx::Failure::flush_failed && result.rollbackComplete && isOriginal(flushFake),
            "flush failure rolls bytes back");
        Fake restoreFake(cow); restoreFake.failRestoreOnce = true; result = apply(restoreFake);
        check(result.failure == tx::Failure::protection_restore_failed && result.rollbackComplete && isOriginal(restoreFake),
            "one restoration failure is reported after successful exact rollback");
        check(restoreFake.protection == static_cast<DWORD>(cow ? PAGE_EXECUTE_WRITECOPY : PAGE_EXECUTE_READ),
            "rollback verifies original protection after restoration failure");
    }
}
void refusesUnknownRollback() {
    Fake f; f.partialWrite = true; f.failRollbackWrite = true; const auto result = apply(f);
    check(result.failure == tx::Failure::write_partial && result.rollbackAttempted && !result.rollbackComplete,
        "failed rollback is never reported as successful");
    check(f.protection == PAGE_EXECUTE_READWRITE,
        "unknown incomplete rollback leaves terminal failure visible instead of claiming recovery");
}
void reportsPersistentRestoreFailure() {
    Fake f(true); f.failRestoreAlways = true; const auto result = apply(f);
    check(result.failure == tx::Failure::protection_restore_failed && !result.rollbackComplete &&
        result.cleanupError != ERROR_SUCCESS,
        "persistent protection restore failure remains explicit and fail-closed");
}
void refusesUnknownPostRestoreBytes() {
    for (const bool cow : {false, true}) {
        Fake f(cow); f.mutateAfterRestore = true; const auto result = apply(f);
        check(result.failure == tx::Failure::post_restore_readback && !result.rollbackAttempted &&
            result.bytesChanged && !result.rollbackComplete,
            "unrecognized post-restore bytes are not overwritten during speculative rollback");
    }
}

// Counterexamples to execution safety, not a quiescence implementation. Only
// inert fake bytes are observed; the test never executes an incomplete CALL.
struct Interleaving {
    Fake* fake = nullptr;
    std::mutex mutex;
    std::condition_variable changed;
    std::array<std::byte, 8> observed{};
    std::size_t size = 0;
    unsigned selectedWrite = 1;
    bool prefixReady = false, sampled = false, timedOut = false;
    std::thread::id readerThread{};
};
Interleaving* activeInterleaving = nullptr; // One selected case per test process.

tx::Io interleavedWrite(void* context, std::uint32_t at,
                        std::span<const std::byte> input) noexcept {
    auto& f = *static_cast<Fake*>(context);
    auto& run = *activeInterleaving;
    if (f.writes + 1 != run.selectedWrite) return write(context, at, input);
    ++f.writes;
    std::unique_lock lock(run.mutex);
    std::copy_n(input.begin(), 2, f.bytes.begin());
    run.prefixReady = true;
    run.changed.notify_one();
    if (!run.changed.wait_for(lock, std::chrono::seconds(2), [&run] { return run.sampled; })) {
        run.timedOut = true;
        return {false, 2, ERROR_TIMEOUT};
    }
    std::copy(input.begin() + 2, input.end(), f.bytes.begin() + 2);
    return {true, input.size(), ERROR_SUCCESS};
}

void witnessesNonAtomicWindow(std::size_t size, bool duringRollback) {
    Fake f(false, size == 5 ? ps::phaseCall : 0x748A87);
    const std::array<std::byte, 6> original = size == 5
        ? std::array{std::byte{0xE8},std::byte{0x11},std::byte{0x22},std::byte{0x33},std::byte{0x44},std::byte{0x90}}
        : std::array{std::byte{0x39},std::byte{0x1D},std::byte{0x1C},std::byte{0x62},std::byte{0x8D},std::byte{0x00}};
    const std::array replacement{std::byte{0xE8},std::byte{0xA1},std::byte{0xB2},
        std::byte{0xC3},std::byte{0xD4},std::byte{0x90}};
    std::copy(original.begin(), original.end(), f.bytes.begin());
    f.failFlush = duringRollback;
    Interleaving run{};
    run.fake = &f; run.size = size; run.selectedWrite = duringRollback ? 2u : 1u;
    activeInterleaving = &run;
    const auto publisher = std::this_thread::get_id();
    std::thread reader([&run] {
        std::unique_lock lock(run.mutex);
        run.readerThread = std::this_thread::get_id();
        if (!run.changed.wait_for(lock, std::chrono::seconds(2), [&run] { return run.prefixReady; })) {
            run.timedOut = true;
            return;
        }
        std::copy_n(run.fake->bytes.begin(), run.size, run.observed.begin());
        run.sampled = true;
        run.changed.notify_one();
    });
    auto api = backend(f); api.write = interleavedWrite;
    const auto result = tx::apply(api, f.address, std::span(original).first(size),
        std::span(replacement).first(size), true);
    reader.join();
    activeInterleaving = nullptr;
    check(!run.timedOut && run.sampled && publisher != run.readerThread,
        "a second thread can observe the bounded write interval despite same-thread publication");
    check(!std::equal(original.begin(), original.begin() + size, run.observed.begin()) &&
        !std::equal(replacement.begin(), replacement.begin() + size, run.observed.begin()),
        "the observer sees a mixed instruction, not either authenticated complete image");
    if (duringRollback) {
        check(result.failure == tx::Failure::flush_failed && result.rollbackComplete &&
            std::equal(original.begin(), original.begin() + size, f.bytes.begin()),
            "verified final rollback does not exclude a mixed instruction during compensation");
    } else {
        check(bool(result) && std::equal(replacement.begin(), replacement.begin() + size, f.bytes.begin()),
            "successful final readback does not prove execution exclusion during publication");
    }
    std::printf("safety_gap stage=%s bytes=%zu observed=mixed final=%s no_instruction_executed\n",
        duringRollback ? "rollback" : "publish", size, duringRollback ? "original" : "replacement");
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const auto name = std::string_view(argv[1]);
    try {
        if (name == "rx_success") rxSuccess();
        else if (name == "cow_success") cowHookAndPadsSuccess();
        else if (name == "reject") refusesUnexpectedProtectionAndIdentity();
        else if (name == "preimage") refusesPreimageAndPageMismatch();
        else if (name == "partial") rollsBackPartialWrite();
        else if (name == "flush_restore") rollsBackFlushAndRestoreFailures();
        else if (name == "rollback_failure") refusesUnknownRollback();
        else if (name == "restore_failure") reportsPersistentRestoreFailure();
        else if (name == "unknown_post") refusesUnknownPostRestoreBytes();
        else if (name == "safety_gap_publish5") witnessesNonAtomicWindow(5, false);
        else if (name == "safety_gap_publish6") witnessesNonAtomicWindow(6, false);
        else if (name == "safety_gap_rollback5") witnessesNonAtomicWindow(5, true);
        else if (name == "safety_gap_rollback6") witnessesNonAtomicWindow(6, true);
        else return 2;
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s: %s\n", argv[1], e.what());
        return 1;
    }
}
