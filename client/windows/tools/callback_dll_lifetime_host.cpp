#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "callback_dll_lifetime_api.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using namespace std::chrono_literals;
using namespace grandmp::windows::lifetime_lab;

namespace {

class HostGate final {
public:
    void notify(std::uint32_t event) {
        std::unique_lock lock(mutex_);
        if (event == notify_callback_enter) {
            entered_ = true;
            changed_.notify_all();
            changed_.wait(lock, [this] { return released_; });
        }
    }
    bool waitEntered() {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, 3s, [this] { return entered_; });
    }
    void release() {
        { std::lock_guard lock(mutex_); released_ = true; }
        changed_.notify_all();
    }
private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool entered_ = false;
    bool released_ = false;
};

void __cdecl notifyHost(std::uint32_t event, void* context) {
    static_cast<HostGate*>(context)->notify(event);
}

class DynamicModule final {
public:
    ~DynamicModule() {
        // Never force FreeLibrary from a destructor. A failed lifecycle leaves
        // the reference held until process exit rather than risking stale code.
    }

    bool load(const wchar_t* path) {
        HMODULE loadedModule = LoadLibraryW(path);
        if (!loadedModule) return false;
        std::wstring loadedName;
        try {
            const auto slash = std::wstring_view(path).find_last_of(L"\\/");
            loadedName = std::wstring(path + (slash == std::wstring_view::npos ? 0 : slash + 1));
        } catch (...) {
            FreeLibrary(loadedModule);
            return false;
        }
        using GetApi = const ApiV1*(__cdecl*)(std::uint32_t);
        const auto getApi = reinterpret_cast<GetApi>(GetProcAddress(loadedModule, "GmpLabGetApi"));
        if (!getApi) {
            FreeLibrary(loadedModule);
            return false;
        }
        const ApiV1* loadedApi = getApi(api_version);
        if (!loadedApi || loadedApi->version != api_version) {
            FreeLibrary(loadedModule);
            return false;
        }
        {
            std::lock_guard lock(gateMutex_);
            module_ = loadedModule;
            api_ = loadedApi;
            moduleName_ = std::move(loadedName);
            acceptingCalls_ = true;
            sourceJoined_ = true;
            unloadClaimed_ = false;
        }
        return modulePresent();
    }

    template <typename Fn, typename... Args>
    auto invoke(Fn ApiV1::* member, Args&&... args) {
        using Result = decltype((std::declval<const ApiV1*>()->*member)(std::forward<Args>(args)...));
        const ApiV1* api = nullptr;
        {
            std::lock_guard lock(gateMutex_);
            if (!acceptingCalls_ || !module_ || !api_) return Result{};
            ++activeCalls_;
            api = api_;
        }
        Result result{};
        try { result = (api->*member)(std::forward<Args>(args)...); }
        catch (...) { result = Result{}; }
        api = nullptr;
        {
            std::lock_guard lock(gateMutex_);
            --activeCalls_;
            gateChanged_.notify_all();
        }
        return result;
    }

    bool start(std::uint32_t mode, HostGate* gate = nullptr) {
        if (invoke(&ApiV1::initialize, mode, gate ? &notifyHost : nullptr, gate) != 1) return false;
        return true;
    }

    bool startDispatcher(unsigned count = 1) {
        std::lock_guard sourceLock(sourceManagementMutex_);
        dispatcherStop_.store(false, std::memory_order_release);
        {
            std::lock_guard lock(gateMutex_);
            if (!module_ || !acceptingCalls_ || unloadClaimed_ || !sourceJoined_) return false;
            sourceJoined_ = false;
        }
        try {
            for (unsigned i = 0; i < count; ++i) {
                dispatchers_.emplace_back([this] {
                    while (!dispatcherStop_.load(std::memory_order_acquire)) {
                        (void)invoke(&ApiV1::tick);
                        std::this_thread::sleep_for(1ms);
                    }
                });
            }
        } catch (...) {
            dispatcherStop_.store(true, std::memory_order_release);
            for (auto& dispatcher : dispatchers_) if (dispatcher.joinable()) dispatcher.join();
            dispatchers_.clear();
            std::lock_guard lock(gateMutex_);
            sourceJoined_ = true;
            return false;
        }
        return true;
    }

    void stopDispatcher() {
        std::lock_guard sourceLock(sourceManagementMutex_);
        dispatcherStop_.store(true, std::memory_order_release);
        for (auto& dispatcher : dispatchers_) if (dispatcher.joinable()) dispatcher.join();
        dispatchers_.clear();
        std::lock_guard lock(gateMutex_);
        sourceJoined_ = true;
    }

    bool tryUnload() {
        const ApiV1* control = nullptr;
        HMODULE retired = nullptr;
        {
            std::lock_guard lock(gateMutex_);
            if (!module_ || !api_ || !acceptingCalls_ || unloadClaimed_ || !sourceJoined_ ||
                activeCalls_ != 0)
                return false;
            unloadClaimed_ = true;
            acceptingCalls_ = false;
            control = api_;
            retired = module_;
        }

        // Dispatcher source is stopped and joined before these module calls.
        if (control->requestShutdown() != 1 || control->confirmSourceQuiescent() != 1 ||
            control->waitForShutdown() != 1 || control->canUnload() != 1) {
            (void)control->getMetrics(&lastMetrics_);
            return false;  // retain module ref on any cleanup/lifecycle failure
        }
        if (control->getMetrics(&lastMetrics_) != 1 || control->destroy() != 1) return false;

        control = nullptr;
        {
            std::lock_guard lock(gateMutex_);
            api_ = nullptr;
        }
        if (!FreeLibrary(retired)) return false;
        {
            std::lock_guard lock(gateMutex_);
            module_ = nullptr;
        }
        return !modulePresent();
    }

    int shutdownDuringDispatch() {
        return invoke(&ApiV1::requestShutdown);
    }

    Metrics metrics() {
        Metrics result{};
        (void)invoke(&ApiV1::getMetrics, &result);
        return result;
    }

    bool modulePresent() const {
        return !moduleName_.empty() && GetModuleHandleW(moduleName_.c_str()) != nullptr;
    }
    bool loaded() const {
        std::lock_guard lock(gateMutex_);
        return module_ != nullptr;
    }
    const Metrics& lastMetrics() const { return lastMetrics_; }

private:
    std::mutex sourceManagementMutex_;
    mutable std::mutex gateMutex_;
    std::condition_variable gateChanged_;
    HMODULE module_ = nullptr;
    const ApiV1* api_ = nullptr;
    std::wstring moduleName_;
    std::size_t activeCalls_ = 0;
    bool acceptingCalls_ = false;
    bool unloadClaimed_ = false;
    bool sourceJoined_ = true;
    std::atomic<bool> dispatcherStop_{false};
    std::vector<std::thread> dispatchers_;
    Metrics lastMetrics_{};
};

bool loadAndStart(DynamicModule& module, const wchar_t* path, std::uint32_t mode,
                  HostGate* gate = nullptr) {
    return module.load(path) && module.start(mode, gate);
}

int normal(const wchar_t* path) {
    DynamicModule module;
    if (!loadAndStart(module, path, mode_normal)) return 10;
    if (!module.startDispatcher(2)) return 13;
    std::this_thread::sleep_for(80ms);
    module.stopDispatcher();
    if (!module.tryUnload() || module.loaded() || module.modulePresent()) return 11;
    const auto m = module.lastMetrics();
    if (m.callbacksStarted == 0 || m.callbacksStarted != m.callbacksFinished || m.cleanupCalls != 1) return 12;
    return 0;
}

int activeCallback(const wchar_t* path, bool shutdownDuring) {
    DynamicModule module;
    HostGate gate;
    if (!loadAndStart(module, path, mode_normal, &gate)) return 20;
    if (!module.startDispatcher(1)) return 26;
    if (!gate.waitEntered()) { gate.release(); module.stopDispatcher(); return 21; }
    if (module.tryUnload() || !module.modulePresent()) {
        gate.release();
        module.stopDispatcher();
        return 22;
    }
    if (shutdownDuring && module.shutdownDuringDispatch() != 1) {
        gate.release();
        module.stopDispatcher();
        return 23;
    }
    gate.release();
    module.stopDispatcher();
    if (!module.tryUnload() || module.modulePresent()) return 24;
    const auto m = module.lastMetrics();
    if (m.callbacksStarted == 0 || m.callbacksStarted != m.callbacksFinished) return 25;
    return 0;
}

int directApiActive(const wchar_t* path) {
    DynamicModule module;
    HostGate gate;
    if (!loadAndStart(module, path, mode_normal, &gate)) return 80;
    std::atomic<int> tickResult{-1};
    std::thread caller([&] { tickResult.store(module.invoke(&ApiV1::tick), std::memory_order_release); });
    if (!gate.waitEntered()) {
        gate.release();
        caller.join();
        return 81;
    }
    // No source dispatcher exists here: this specifically exercises the
    // activeCalls_ lease held around an in-flight export call.
    if (module.tryUnload() || !module.modulePresent() || !module.loaded()) {
        gate.release();
        caller.join();
        return 82;
    }
    gate.release();
    caller.join();
    if (tickResult.load(std::memory_order_acquire) != 0 || !module.tryUnload()) return 83;
    return module.modulePresent() ? 84 : 0;
}

int initializationFailure(const wchar_t* path, std::uint32_t mode, bool shouldUnload) {
    DynamicModule module;
    if (!module.load(path)) return 30;
    if (module.start(mode)) return 31;
    if (!module.modulePresent() || !module.loaded()) return 32;
    module.stopDispatcher();
    const bool unloaded = module.tryUnload();
    if (unloaded != shouldUnload) return 33;
    if (module.modulePresent() == shouldUnload) return 34;
    if (module.lastMetrics().cleanupCalls != 1) return 35;
    return 0;
}

int shutdownCleanupFailure(const wchar_t* path) {
    DynamicModule module;
    if (!loadAndStart(module, path, mode_fail_shutdown_cleanup)) return 40;
    if (!module.startDispatcher(1)) return 42;
    std::this_thread::sleep_for(20ms);
    module.stopDispatcher();
    if (module.tryUnload() || !module.modulePresent() || !module.loaded() ||
        module.lastMetrics().cleanupCalls != 1) return 41;
    return 0;  // deliberately retained until this test process exits
}

int reentrant(const wchar_t* path) {
    DynamicModule module;
    if (!loadAndStart(module, path, mode_reentrant)) return 50;
    if (module.invoke(&ApiV1::tick) != 0) return 51; // DispatchResult::invoked is zero.
    const auto m = module.metrics();
    if (m.reentrantDispatches != 1 || !module.tryUnload()) return 52;
    return module.modulePresent() ? 53 : 0;
}

int cycles(const wchar_t* path) {
    for (unsigned cycle = 0; cycle < 40; ++cycle) {
        DynamicModule module;
        if (!loadAndStart(module, path, mode_normal)) return 60;
        for (unsigned n = 0; n < 5; ++n)
            if (module.invoke(&ApiV1::tick) != 0) return 61;
        if (!module.tryUnload() || module.modulePresent()) return 62;
    }
    return 0;
}

int concurrentUnload(const wchar_t* path) {
    DynamicModule module;
    if (!loadAndStart(module, path, mode_normal)) return 70;
    for (unsigned n = 0; n < 5; ++n)
        if (module.invoke(&ApiV1::tick) != 0) return 71;
    std::atomic<bool> go{false};
    std::atomic<unsigned> completed{0};
    std::atomic<unsigned> succeeded{0};
    auto unload = [&] {
        while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
        if (module.tryUnload()) ++succeeded;
        ++completed;
    };
    std::thread first(unload);
    std::thread second(unload);
    go.store(true, std::memory_order_release);
    first.join();
    second.join();
    if (completed != 2 || succeeded != 1 || module.loaded() || module.modulePresent()) return 72;
    return 0;
}

int concurrentInitialize(const wchar_t* path) {
    DynamicModule module;
    if (!module.load(path)) return 90;
    std::atomic<bool> go{false};
    std::atomic<unsigned> initialized{0};
    auto initialize = [&] {
        while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
        if (module.invoke(&ApiV1::initialize, mode_normal, static_cast<HostNotify>(nullptr), nullptr) == 1)
            ++initialized;
    };
    std::thread first(initialize);
    std::thread second(initialize);
    go.store(true, std::memory_order_release);
    first.join();
    second.join();
    if (initialized != 1 || module.invoke(&ApiV1::tick) != 0 || !module.tryUnload()) return 91;
    return module.modulePresent() ? 92 : 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    const std::wstring_view scenario(argv[1]);
    const wchar_t* path = argv[2];
    int result = 2;
    if (scenario == L"normal") result = normal(path);
    else if (scenario == L"active") result = activeCallback(path, false);
    else if (scenario == L"shutdown_active") result = activeCallback(path, true);
    else if (scenario == L"init_failure") result = initializationFailure(path, mode_fail_initialization, true);
    else if (scenario == L"rollback_failure") result = initializationFailure(path, mode_fail_rollback, false);
    else if (scenario == L"cleanup_failure") result = shutdownCleanupFailure(path);
    else if (scenario == L"reentrant") result = reentrant(path);
    else if (scenario == L"cycles") result = cycles(path);
    else if (scenario == L"concurrent_unload") result = concurrentUnload(path);
    else if (scenario == L"direct_active") result = directApiActive(path);
    else if (scenario == L"concurrent_initialize") result = concurrentInitialize(path);
    if (result != 0) std::fwprintf(stderr, L"DLL lifetime scenario %.*s failed: %d\n",
                                   static_cast<int>(scenario.size()), scenario.data(), result);
    else std::fwprintf(stdout, L"DLL lifetime scenario %.*s passed\n",
                       static_cast<int>(scenario.size()), scenario.data());
    return result;
}
