#pragma once

#include <cstdint>

namespace grandmp::windows::lifetime_lab {

inline constexpr std::uint32_t api_version = 1;
inline constexpr std::uint32_t notify_callback_enter = 1;
inline constexpr std::uint32_t notify_callback_leave = 2;
inline constexpr std::uint32_t mode_normal = 0;
inline constexpr std::uint32_t mode_fail_initialization = 1;
inline constexpr std::uint32_t mode_fail_rollback = 2;
inline constexpr std::uint32_t mode_fail_shutdown_cleanup = 3;
inline constexpr std::uint32_t mode_reentrant = 4;

using HostNotify = void(__cdecl*)(std::uint32_t event, void* context);

struct Metrics {
    std::uint64_t callbacksStarted;
    std::uint64_t callbacksFinished;
    std::uint64_t reentrantDispatches;
    std::uint64_t cleanupCalls;
};

struct ApiV1 {
    std::uint32_t version;
    int(__cdecl* initialize)(std::uint32_t mode, HostNotify notify, void* context);
    int(__cdecl* tick)();
    int(__cdecl* requestShutdown)();
    int(__cdecl* confirmSourceQuiescent)();
    int(__cdecl* waitForShutdown)();
    int(__cdecl* canUnload)();
    int(__cdecl* getMetrics)(Metrics* output);
    int(__cdecl* destroy)();
};

}  // namespace grandmp::windows::lifetime_lab

#if defined(GMP_CALLBACK_LIFETIME_MODULE)
#define GMP_CALLBACK_LAB_EXPORT __declspec(dllexport)
#else
#define GMP_CALLBACK_LAB_EXPORT
#endif
extern "C" GMP_CALLBACK_LAB_EXPORT
const grandmp::windows::lifetime_lab::ApiV1* __cdecl GmpLabGetApi(std::uint32_t requestedVersion);
