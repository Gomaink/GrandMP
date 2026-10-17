#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define GRANDMP_REMOTE_PED_MOVE_VARIANT 1
#include <windows.h>
#include "../client/windows/tools/remote_ped_move.hpp"
namespace rm=grandmp::windows::remote_ped_move;
constexpr bool expected=GRANDMP_EXPECT_REMOTE_MOVE_GATE==1;
static_assert(rm::physicalExecutionApproved==expected);
int main(){SetEnvironmentVariableW(L"GRANDMP_PED_SMOKE_PHYSICAL_BUILD",L"1");SetEnvironmentVariableW(L"GRANDMP_REMOTE_PED_PHYSICAL_BUILD",L"1");SetEnvironmentVariableW(L"GRANDMP_REMOTE_PED_MOVE_PHYSICAL_BUILD",L"1");return rm::physicalExecutionApproved==expected?0:1;}
