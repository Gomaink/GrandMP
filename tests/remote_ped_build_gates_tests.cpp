#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../client/windows/tools/remote_ped.hpp"
#include <cstdio>
namespace rp = grandmp::windows::remote_ped;
namespace ps = grandmp::windows::ped_smoke;
constexpr bool spawn = GRANDMP_EXPECT_SPAWN_GATE == 1;
constexpr bool remote = spawn && GRANDMP_EXPECT_REMOTE_GATE == 1;
static_assert(ps::physicalExecutionApproved == spawn);
static_assert(rp::physicalExecutionApproved == remote);
int main() {
    if (!SetEnvironmentVariableW(L"GRANDMP_PED_SMOKE_PHYSICAL_BUILD", L"1") ||
        !SetEnvironmentVariableW(L"GRANDMP_REMOTE_PED_PHYSICAL_BUILD", L"1") ||
        !SetEnvironmentVariableW(L"GRANDMP_PED_SMOKE_ENABLE", L"1")) return 2;
    std::printf("spawn=%u remote=%u no_engine_linked=1\n", unsigned(spawn), unsigned(remote));
    return ps::physicalExecutionApproved == spawn && rp::physicalExecutionApproved == remote ? 0 : 1;
}
