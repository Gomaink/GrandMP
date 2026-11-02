// Alpha 0.9 only. Never included by factory/replay/production variants.
// The original UpdatePads runs once, before any neutralization. No entity,
// task, timer, focus, camera or pad1 write is introduced by this adapter.
bool backgroundActive = false, backgroundTerminal = false;
enum class BackgroundNotice { none, installed, foreground, neutral_background, failed };
BackgroundNotice backgroundNotice = BackgroundNotice::none;
const char* backgroundReason = "none";
ps::CodeIdentityFailure backgroundIdentityFailure{};

void backgroundStop(const char* reason) noexcept {
    bg::close(); backgroundTerminal = true;
    runtimeDisabled.store(true);
    if (backgroundNotice != BackgroundNotice::failed) backgroundReason = reason;
    backgroundNotice = BackgroundNotice::failed;
}
bool backgroundCodeMatches(ps::CodeIdentityFailure* detail = nullptr) noexcept {
    ps::CodeIdentityFailure local{};
    auto& failure = detail ? *detail : local;
    failure = {}; failure.function = "backgroundCodeMatches";
    for (const auto region : bg::windows) {
        const auto* disk = diskBytes(region.address, region.size);
        std::array<std::byte, 0x600> expected{}, actual{};
        if (!disk) return ps::identityRefuse(failure, "disk_mapping_failed", region.address, region.size);
        if (region.size > expected.size()) return ps::identityRefuse(failure, "buffer_capacity", region.address, region.size);
        std::memcpy(expected.data(), disk, region.size);
        if (!backgroundPatches.overlay(region.address, expected.data(), region.size))
            return ps::identityRefuse(failure, "disk_bytes_mismatch", region.address, region.size);
        if (!identityCodePage(failure, region.address, region.size) ||
            !ps::identityRead(failure, region.address, actual.data(), region.size) ||
            !ps::identityBytes(failure, region.address, actual.data(), expected.data(), region.size)) return false;
    }
    return true;
}
struct BackgroundInvocation { bool complete = false; };
void backgroundPadsBody(void* value) {
    auto& invocation = *static_cast<BackgroundInvocation*>(value);
    // Original cdecl(), RET0. No early/second call to ProcessControl or CWorld.
    reinterpret_cast<void(__cdecl*)()>(bg::updatePads)();
    if (backgroundTerminal || preparation.load() != 3 || runtimeDisabled.load()) {
        invocation.complete = true; return;
    }
    if (!backgroundPatches.installed() || !codeMatches(&backgroundIdentityFailure) ||
            !backgroundCodeMatches(&backgroundIdentityFailure)) {
        backgroundStop("code_identity"); invocation.complete = true; return;
    }
    std::uint32_t foreground = 0;
    if (!read(0x8D621C, foreground) || foreground > 1) {
        backgroundStop("foreground_unavailable"); invocation.complete = true; return;
    }
    DWORD foregroundPid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
    if (!foreground || foregroundPid != GetCurrentProcessId()) {
        // The same authenticated IMAGE writer used by replay, now explicitly
        // limited to pad0 NewState/OldState. Tail/disabled flags and pad1 stay intact.
        if (!ni::writePadStates(nf::pad0, reinterpret_cast<void*>(ps::imageBase), true, {})) {
            backgroundStop("pad0_neutralization"); invocation.complete = true; return;
        }
        backgroundNotice = BackgroundNotice::neutral_background;
    } else backgroundNotice = BackgroundNotice::foreground;
    bg::allow(); // Only after the first successfully authenticated original input frame.
    invocation.complete = true;
}
void backgroundPadsFinish(void* value) noexcept {
    backgroundActive = false;
    if (!static_cast<BackgroundInvocation*>(value)->complete) backgroundStop("original_unwind");
}
__declspec(noinline) void __cdecl backgroundPads() {
    bg::close(); // Never carry an authorization through an incomplete input frame.
    const auto caller = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
    if (!gameThread || GetCurrentThreadId() != gameThread || caller != bg::padsCall + 5) {
        runtimeDisabled.store(true); // No non-atomic diagnostic state on a foreign thread.
        reinterpret_cast<void(__cdecl*)()>(bg::updatePads)(); return;
    }
    if (backgroundActive || phaseDispatchActive || inputActive) {
        backgroundStop("pad_dispatch_context");
        reinterpret_cast<void(__cdecl*)()>(bg::updatePads)(); // Vanilla call once, without GrandMP writes.
        return;
    }
    backgroundActive = true;
    BackgroundInvocation invocation{};
    np::withFinally(backgroundPadsBody, backgroundPadsFinish, &invocation);
}
bool installBackground() noexcept {
    bg::close();
    if (!bg::executionApproved) { backgroundStop("physical_gate_closed"); return false; }
    if (GetCurrentThreadId() != gameThread || phaseDispatchActive || inputActive) {
        backgroundStop("installation_context"); return false;
    }
    if (!backgroundCodeMatches(&backgroundIdentityFailure) || !codeMatches(&backgroundIdentityFailure)) {
        backgroundStop("installation_code_identity"); return false;
    }
    const auto address = [](auto function) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(function)); };
    // Publish input first; partial publication cannot enable background gameplay.
    const std::array sites{
        bg::Patches::call(bg::padsCall, {0xE8,0xE5,0x5E,0x00,0x00}, address(&backgroundPads)),
        bg::Patches::call(bg::focusCall, {0xE8,0xF8,0x3B,0xDF,0xFF}, address(&bg::onSetFocus)),
        bg::Patches::call(bg::loopCompare, {0x39,0x1D,0x1C,0x62,0x8D,0x00}, address(&bg::loopForeground))
    };
    const auto patchBackend = ps::patch_transaction::win32Backend(nullptr, authorizeCowPatch);
    if (!backgroundPatches.publish(sites, reinterpret_cast<void*>(ps::imageBase), true,
            hoodlumIdentityAuthenticated.load(std::memory_order_acquire), patchBackend) ||
            !codeMatches(&backgroundIdentityFailure) || !backgroundCodeMatches(&backgroundIdentityFailure)) {
        backgroundStop("publication_or_verification"); return false;
    }
    backgroundNotice = BackgroundNotice::installed;
    return true;
}
void backgroundReport() noexcept {
    static auto last = BackgroundNotice::none;
    if (last == backgroundNotice) return;
    last = backgroundNotice;
    if (last == BackgroundNotice::failed) {
        const auto failure = backgroundPatches.failure();
        char detail[256]{};
        std::snprintf(detail, sizeof(detail), "background_terminal reason=%s site=%u category=%s win32=%lu cleanup_win32=%lu writer=%s rollback_complete=%u no_retry",
            backgroundReason, failure.site, failure.category, failure.error, failure.cleanupError,
            ps::patch_transaction::failureName(failure.transactionFailure), unsigned(failure.rollbackComplete));
        writeLog("failed", detail);
        if (std::strcmp(backgroundIdentityFailure.category, "none")) {
            char identity[320]{};
            if (ps::formatCodeIdentityFailure(identity, sizeof(identity), backgroundIdentityFailure))
                writeLog("failed", identity);
        }
    } else writeLog("waiting", last == BackgroundNotice::installed ? "background_installed_waiting_authenticated_input_frame" :
        last == BackgroundNotice::foreground ? "background_armed_foreground_original_input" : "background_armed_pad0_neutral_world_replay_enabled");
}
