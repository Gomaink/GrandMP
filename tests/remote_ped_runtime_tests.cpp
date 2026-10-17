// Disposable Win32 host: include the actual closed ASI implementation to test
// failure branches. Never map GTA, install hooks, or call native engine addresses.
#define GRANDMP_REMOTE_PED_VARIANT 1
#include "../client/windows/tools/ped_smoke.cpp"
#include <stdexcept>
#include <string_view>
void check(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
int wmain(int argc, wchar_t** argv) {
    try {
        check(argc == 2, "case required");
        static_assert(!rp::physicalExecutionApproved && !ps::physicalExecutionApproved);
        const std::wstring_view which = argv[1];
        if (which == L"receiver") {
            check(SetEnvironmentVariableW(L"GRANDMP_REMOTE_PED_SERVER", L"192.0.2.1:7777") != FALSE, "set invalid endpoint");
            remoteReceiver.store(rp::ReceiverState::starting);
            HANDLE worker = CreateThread(nullptr, 0, remoteNetwork, nullptr, 0, nullptr);
            check(worker != nullptr, "actual thread created");
            check(WaitForSingleObject(worker, 5000) == WAIT_OBJECT_0, "receiver failure must terminate worker");
            CloseHandle(worker);
            check(remoteReceiver.load() == rp::ReceiverState::failed && runtimeDisabled.load(), "thread created but receiver failed and dispatch disabled");
        } else if (which == L"code") {
            check(!codeMatches(), "host cannot authenticate Hoodlum code");
            preparation.store(3); gameThread = GetCurrentThreadId();
            remoteIntents.spawn({42, 1, 10, 20, 30});
            runPhase(ps::phaseCall + 5); // Actual runPhase must fail before player/native queries.
            check(remoteController.poisoned() && remoteController.state() == ps::State::failed, "remote code failure terminal");
            check(runtimeDisabled.load() && experiment.state() == ps::State::waiting, "remote must not fail the local F6 experiment instead");
            // Even if bytes later match, the latch is never cleared/rearmed.
            runPhase(ps::phaseCall + 5);
        } else return 2;
        check(!remoteEngine.owned && remoteEngine.reference == -1 && !remoteEngine.creationResult, "no native allocation or unknown removal");
        check(!hookPublished.load() && installation.load() == 0, "no hook installed");
        std::printf("remote_runtime failure branch PASS\n");
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
