#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "grandmp/windows/hook_lifecycle.hpp"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <cstdio>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace {

using namespace grandmp::windows;

constexpr CallBytes original{0xe8, 0x5a, 0xd5, 0xff, 0xff};
constexpr CallBytes replacement{0xe8, 0x01, 0x02, 0x03, 0x04};
constexpr CallBytes foreign{0xe8, 0x05, 0x06, 0x07, 0x08};

constexpr ExecutableInspection hoodlumInspection{PeStatus::pe32_i386, 0, 14'383'616};
constexpr std::array<std::byte, 32> hoodlumDigest{
    std::byte{0xA5}, std::byte{0x59}, std::byte{0xAA}, std::byte{0x77},
    std::byte{0x2F}, std::byte{0xD1}, std::byte{0x36}, std::byte{0x37},
    std::byte{0x91}, std::byte{0x55}, std::byte{0xEF}, std::byte{0xA7},
    std::byte{0x1F}, std::byte{0x00}, std::byte{0xC4}, std::byte{0x7A},
    std::byte{0xAD}, std::byte{0x34}, std::byte{0xBB}, std::byte{0xFE},
    std::byte{0xAE}, std::byte{0x61}, std::byte{0x96}, std::byte{0xB0},
    std::byte{0xFE}, std::byte{0x10}, std::byte{0x47}, std::byte{0xD0},
    std::byte{0x64}, std::byte{0x5C}, std::byte{0xBD}, std::byte{0x26},
};

class ControlledSite final : public ICallSite {
public:
    explicit ControlledSite(CallBytes bytes) : bytes_(bytes) {}

    PatchAttempt exchangeIfEqual(const CallBytes& expected,
                                 const CallBytes& next) noexcept override {
        std::lock_guard lock(mutex_);
        if (bytes_ != expected) return PatchAttempt::mismatch;
        if (nextResult_ == PatchAttempt::failed_unchanged) return nextResult_;
        if (nextResult_ == PatchAttempt::partial_or_unknown) {
            bytes_[0] = next[0];
            bytes_[1] = next[1];
            bytes_[2] = next[2];
            return nextResult_;
        }
        bytes_ = next;
        return PatchAttempt::applied;
    }

    void setBytes(CallBytes bytes) noexcept { std::lock_guard lock(mutex_); bytes_ = bytes; }
    void setNextResult(PatchAttempt result) noexcept { std::lock_guard lock(mutex_); nextResult_ = result; }
    [[nodiscard]] CallBytes bytes() const noexcept { std::lock_guard lock(mutex_); return bytes_; }

private:
    mutable std::mutex mutex_;
    CallBytes bytes_;
    PatchAttempt nextResult_ = PatchAttempt::applied;
};

constexpr auto resident = ResidenceEvidence::externally_established;
constexpr auto quiet = QuiescenceEvidence::externally_established;

HookResult install(HookManager& manager, const std::shared_ptr<ControlledSite>& site,
                   const std::shared_ptr<CallbackGate>& callback) {
    return manager.install(hoodlumInspection, hoodlumDigest, resident, quiet,
                           site, callback, original, replacement);
}

bool successAndDuplicate() {
    HookManager manager;
    auto site = std::make_shared<ControlledSite>(original);
    auto callback = std::make_shared<CallbackGate>();
    const bool installed = install(manager, site, callback) == HookResult::installed &&
        manager.state() == HookState::installed && manager.ownsPatch() && callback->diagnosticsEnabled() &&
        site->bytes() == replacement &&
        install(manager, site, callback) == HookResult::duplicate && site->bytes() == replacement;
    return manager.disableDiagnostics() == HookResult::disabled &&
           manager.restore(quiet) == HookResult::restored && installed;
}

bool rejectsUnknownAndUnverified() {
    HookManager manager;
    auto site = std::make_shared<ControlledSite>(original);
    auto callback = std::make_shared<CallbackGate>();
    auto changedDigest = hoodlumDigest;
    changedDigest[0] = std::byte{0x00};
    auto wrongSize = hoodlumInspection;
    wrongSize.fileSize--;
    return manager.install(hoodlumInspection, changedDigest, resident, quiet, site, callback, original, replacement) == HookResult::unsupported_game &&
           manager.install(wrongSize, hoodlumDigest, resident, quiet, site, callback, original, replacement) == HookResult::unsupported_game &&
           manager.install(hoodlumInspection, hoodlumDigest, ResidenceEvidence::unverified, quiet, site, callback, original, replacement) == HookResult::residence_unverified &&
           manager.install(hoodlumInspection, hoodlumDigest, resident, QuiescenceEvidence::unverified, site, callback, original, replacement) == HookResult::quiescence_unverified &&
           manager.state() == HookState::idle && site->bytes() == original;
}

bool rejectsUnexpectedBytes() {
    HookManager manager;
    auto site = std::make_shared<ControlledSite>(foreign);
    auto callback = std::make_shared<CallbackGate>();
    return install(manager, site, callback) == HookResult::unexpected_bytes &&
           manager.state() == HookState::idle && !manager.mustRemainResident() && site->bytes() == foreign;
}

bool logicalDisableAndRestore() {
    HookManager manager;
    auto site = std::make_shared<ControlledSite>(original);
    auto callback = std::make_shared<CallbackGate>();
    if (install(manager, site, callback) != HookResult::installed) return false;
    if (manager.restore(quiet) != HookResult::invalid_state) return false;
    if (manager.disableDiagnostics() != HookResult::disabled) return false;
    return !callback->diagnosticsEnabled() && site->bytes() == replacement &&
           manager.state() == HookState::logically_disabled &&
           manager.restore(QuiescenceEvidence::unverified) == HookResult::quiescence_unverified &&
           manager.restore(quiet) == HookResult::restored && site->bytes() == original &&
           manager.state() == HookState::idle && !manager.mustRemainResident();
}

bool foreignPatchIsPreserved() {
    // A conflict has no safe restoration path. Retain the synthetic manager
    // until this dedicated CTest process exits, matching mustRemainResident().
    auto manager = std::make_unique<HookManager>();
    auto site = std::make_shared<ControlledSite>(original);
    auto callback = std::make_shared<CallbackGate>();
    if (install(*manager, site, callback) != HookResult::installed) return false;
    if (manager->disableDiagnostics() != HookResult::disabled) return false;
    site->setBytes(foreign);
    const bool correct = manager->restore(quiet) == HookResult::foreign_patch && site->bytes() == foreign &&
        manager->state() == HookState::conflict && manager->mustRemainResident() && !manager->ownsPatch();
    (void)manager.release();
    return correct;
}

bool failuresKeepConservativeState() {
    auto manager = std::make_unique<HookManager>();
    auto site = std::make_shared<ControlledSite>(original);
    auto callback = std::make_shared<CallbackGate>();
    site->setNextResult(PatchAttempt::failed_unchanged);
    if (install(*manager, site, callback) != HookResult::write_failed ||
        manager->state() != HookState::idle || site->bytes() != original) return false;
    site->setNextResult(PatchAttempt::partial_or_unknown);
    const bool correct = install(*manager, site, callback) == HookResult::partial_write &&
        manager->state() == HookState::uncertain && manager->mustRemainResident() &&
        install(*manager, site, callback) == HookResult::duplicate;
    // Partial writes have no justified rollback; keep the model resident to
    // process exit instead of silently dropping the callback registration.
    (void)manager.release();
    return correct;
}

bool ownsRegisteredObjects() {
    HookManager manager;
    auto site = std::make_shared<ControlledSite>(original);
    auto callback = std::make_shared<CallbackGate>();
    std::weak_ptr<ICallSite> weakSite = site;
    std::weak_ptr<CallbackGate> weakCallback = callback;
    if (install(manager, site, callback) != HookResult::installed) return false;
    site.reset();
    callback.reset();
    if (weakSite.expired() || weakCallback.expired()) return false;
    if (manager.disableDiagnostics() != HookResult::disabled || manager.restore(quiet) != HookResult::restored) return false;
    return weakSite.expired() && weakCallback.expired();
}

bool inFlightAndReentrancy() {
    HookManager manager;
    auto site = std::make_shared<ControlledSite>(original);
    auto callback = std::make_shared<CallbackGate>();
    if (install(manager, site, callback) != HookResult::installed) return false;
    std::promise<void> entered;
    std::promise<void> release;
    auto released = release.get_future();
    std::thread worker([&] {
        auto lease = callback->tryEnter();
        entered.set_value();
        released.wait();
    });
    entered.get_future().wait();
    const bool blocked = callback->callbackInFlight() && !callback->tryEnter() &&
        manager.disableDiagnostics() == HookResult::disabled &&
        manager.restore(quiet) == HookResult::busy && !callback->tryEnter();
    release.set_value();
    worker.join();
    return blocked && manager.restore(quiet) == HookResult::restored;
}

bool destructionGuard() {
    std::array<wchar_t, 32768> path{};
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return false;
    std::wstring command = L"\"" + std::wstring(path.data(), length) + L"\" destruction_child";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(path.data(), command.data(), nullptr, nullptr, FALSE, 0,
                        nullptr, nullptr, &startup, &process)) return false;
    const DWORD waited = WaitForSingleObject(process.hProcess, 10000);
    if (waited == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, 74);
        WaitForSingleObject(process.hProcess, INFINITE);
    }
    DWORD exitCode = 0;
    const bool passed = waited == WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess, &exitCode) && exitCode == 73;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return passed;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) return 2;
    const std::string_view name(argv[1]);
    if (name == "destruction_child") {
        std::set_terminate([] { std::_Exit(73); });
        {
            HookManager manager;
            auto site = std::make_shared<ControlledSite>(original);
            auto callback = std::make_shared<CallbackGate>();
            if (install(manager, site, callback) != HookResult::installed) return 0;
        }
        return 0; // Reaching this line means the destructor guard failed.
    }
    const bool passed = (name == "destruction" && destructionGuard()) ||
                        (name == "install" && successAndDuplicate()) ||
                        (name == "identity" && rejectsUnknownAndUnverified()) ||
                        (name == "bytes" && rejectsUnexpectedBytes()) ||
                        (name == "disable" && logicalDisableAndRestore()) ||
                        (name == "foreign" && foreignPatchIsPreserved()) ||
                        (name == "failure" && failuresKeepConservativeState()) ||
                        (name == "lifetime" && ownsRegisteredObjects()) ||
                        (name == "inflight" && inFlightAndReentrancy());
    if (!passed) {
        std::fprintf(stderr, "hook lifecycle test failed: %.*s\n", static_cast<int>(name.size()), name.data());
        return 1;
    }
    return 0;
}
