#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../client/windows/tools/ped_smoke_move.hpp"
#include <cstdio>

namespace ps = grandmp::windows::ped_smoke;
constexpr bool expectedSpawn = GRANDMP_EXPECT_SPAWN_GATE == 1;
constexpr bool expectedMove = expectedSpawn && GRANDMP_EXPECT_MOVE_GATE == 1;
static_assert(ps::physicalExecutionApproved == expectedSpawn);
static_assert(ps::movementExecutionApproved == expectedMove);

int main() {
    // Setting either compile-define's name as an environment variable cannot
    // turn a closed build gate on. This host never loads any ASI or GTA code.
    if (!SetEnvironmentVariableW(L"GRANDMP_PED_SMOKE_ENABLE", L"1") ||
        !SetEnvironmentVariableW(L"GRANDMP_PED_SMOKE_PHYSICAL_BUILD", L"1") ||
        !SetEnvironmentVariableW(L"GRANDMP_PED_MOVE_PHYSICAL_BUILD", L"1")) return 2;
    if (ps::physicalExecutionApproved != expectedSpawn || ps::movementExecutionApproved != expectedMove) return 1;
    std::printf("spawn=%u movement=%u environment_cannot_override=pass\n", unsigned(expectedSpawn), unsigned(expectedMove));
    return 0;
}
