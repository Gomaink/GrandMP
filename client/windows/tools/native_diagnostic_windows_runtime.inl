// Included after the reviewed primitives by the exclusive diagnostic gate or a closed host.
// Keep all authentication in the reviewed runtime, not in a parallel PE/owner
// implementation. Caller/callback belong to a stack-local Context, not logging.
struct DiagnosticResidence final {
    bool operator()() const noexcept {
        return preparation.load() == 3 && hookPublished.load() && !runtimeDisabled.load() &&
            module && executableFile != INVALID_HANDLE_VALUE && !executable.empty();
    }
};
struct DiagnosticRuntimePrimitives final {
    bool context(std::uintptr_t caller, unsigned callback) const noexcept {
        // Acquire admission before reading published metadata. Then thread first:
        // phaseDispatchActive is native-thread state, never read by a wrong thread.
        // preparation==3 is published only after exact Hoodlum admission,
        // opt-in, successful PIN of this module, publication and bootstrap cleanup.
        return DiagnosticResidence{}() && phaseThreadMatches(caller) && phaseDispatchActive &&
            callbacks.load(std::memory_order_relaxed) == callback;
    }
    bool codeReady() const noexcept { return codeMatches() && taskCodeMatches(); }
    bool gameplayReady() const noexcept {
        void* cj = nullptr;
        return playerReady(cj) && streamingReady(); // No diagnostic logs/extra engine functions.
    }
};
