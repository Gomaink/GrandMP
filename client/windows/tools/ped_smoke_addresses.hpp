#pragma once
#include <array>
#include <cstdint>

namespace grandmp::windows::ped_smoke {
// SA-only reference facts; exact revisions, local Hoodlum disassembly and
// phase evidence/remaining physical contracts: docs/ped-smoke-alpha0401.md.
inline constexpr std::uint32_t imageBase = 0x400000;
inline constexpr std::uint32_t playerVtable = 0x86D168;
// Hoodlum CGame::Process: both Population::Update branches merge at this CALL,
// before CWorld::Process. No vtable hook / gameProcessEvent registration.
inline constexpr std::uint32_t phaseCall = 0x53C05C;
inline constexpr std::uint32_t updateWeapons = 0x73A360; // void __cdecl(), no args
// WinMain stores CreateWindowExA's return in [ [0xC17054] + 0 ].
inline constexpr std::uint32_t platformGlobal = 0xC17054;
inline constexpr std::uint32_t requestModel = 0x4087E0;
inline constexpr std::uint32_t loadModels = 0x40EA10;
inline constexpr std::uint32_t addPed = 0x612710;
inline constexpr std::uint32_t removePed = 0x610F20;
inline constexpr std::uint32_t findGround = 0x5696C0;
inline constexpr std::uint32_t testSphere = 0x569E20;
inline constexpr std::uint32_t pedRef = 0x54FF60, resolvePed = 0x54FF90;
inline constexpr std::uint32_t registerReference = 0x571B70, cleanReference = 0x571A00;
inline constexpr std::uint32_t player = 0xB7CD98, playerIndex = 0xB7CD74;
inline constexpr std::uint32_t pedPool = 0xB74490;
inline constexpr std::uint32_t streamingInitialized = 0x9654B8;
inline constexpr std::uint32_t streamingDisabled = 0x9654B0;
inline constexpr std::uint32_t loadingScene = 0x9654BD, loadingBigModel = 0x8E4A58;
inline constexpr std::uint32_t streamingTable = 0x8E4CC0;
inline constexpr std::uint32_t codePause = 0xB7CB48, userPause = 0xB7CB49;

// Verify public entry windows, observed Hoodlum detour stubs and AddPed up
// to the next documented entry (0x612CD0). These are read-only comparisons with
// the admitted, write/delete-locked disk file, not hook signatures to patch.
struct CodeRegion { std::uint32_t address, size; };
inline constexpr std::array codeRegions{
    // Full CGame::Process prefix through the chosen CALL, then through World.
    // The five installed bytes are reconciled separately by codeMatches().
    CodeRegion{0x53BEE0, phaseCall - 0x53BEE0}, CodeRegion{phaseCall + 5, 0x53C09A - phaseCall - 5},
    CodeRegion{updateWeapons, 0x14}, CodeRegion{0x616650, 32}, CodeRegion{requestModel, 32},
    CodeRegion{0x745560, 0x80}, CodeRegion{0x748710, 0x325}, CodeRegion{0x748A35, 0x3A0},
    CodeRegion{0x619B60, 0x14}, CodeRegion{0x53EC10, 0x11F}, CodeRegion{0x4018CE, 12},
    CodeRegion{loadModels, 32}, CodeRegion{0x40A45E, 10},
    CodeRegion{0x404C95, 11}, CodeRegion{addPed, 0x5C0}, CodeRegion{0x4018C3, 11},
    CodeRegion{removePed, 0x20}, CodeRegion{findGround, 32}, CodeRegion{testSphere, 32},
    CodeRegion{pedRef, 0x30}, CodeRegion{resolvePed, 0x30},
    CodeRegion{registerReference, 32}, CodeRegion{cleanReference, 32}
};
} // namespace grandmp::windows::ped_smoke
