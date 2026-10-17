#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

#include "grandmp/windows/executable_detection.hpp"
#include "ped_smoke_addresses.hpp"
#include "ped_smoke_phase.hpp"
#include "ped_smoke_state.hpp"
#include "ped_smoke_spawn.hpp"
#include "ped_smoke_move.hpp"
#include "ped_smoke_move_native.hpp"
#if defined(GRANDMP_REMOTE_PED_VARIANT)
#include "remote_ped.hpp"
#include "remote_ped_network.hpp"
#if defined(GRANDMP_REMOTE_PED_MOVE_VARIANT)
#include "remote_ped_move.hpp"
#endif
#include "grandmp/windows/observer_network.hpp"
#endif

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <intrin.h>
#include <vector>

static_assert(sizeof(void*) == 4, "Ped smoke requires Win32 x86");
namespace {
namespace ps = grandmp::windows::ped_smoke;
HMODULE module = nullptr;
std::atomic<unsigned> preparation{0}; // 0 unloaded/pre-init, 1 preparing, 2 blocked, 3 installed, 4 failed
std::atomic<unsigned> callbacks{0};
std::atomic<bool> removalRequested{false};
std::atomic<bool> hookPublished{false}, runtimeDisabled{false};
// Bootstrap handshake: 0 idle, 1 armed, 2 executing on window thread, 3 done, 4 cancelled.
std::atomic<unsigned> installation{0};
std::atomic<bool> installationSucceeded{false};
ps::CallBytes installedCall{};
ps::Experiment experiment;
ps::Movement movement;
DWORD gameThread = 0;
HANDLE executableFile = INVALID_HANDLE_VALUE; // Retained for installed profile lifetime.
std::vector<std::byte> executable;

void writeLog(const char* state, const char* reason, std::uintptr_t ped = 0) noexcept {
    wchar_t path[32768]{};
    const DWORD length = GetModuleFileNameW(module, path, 32768);
    if (!length || length >= 32768) return;
    wchar_t* slash = std::wcsrchr(path, L'\\');
    if (!slash) return;
    constexpr wchar_t filename[] =
#if defined(GRANDMP_REMOTE_PED_MOVE_VARIANT)
        L"GrandMPRemotePedMoveExperimental.log";
#elif defined(GRANDMP_REMOTE_PED_VARIANT)
        L"GrandMPRemotePedExperimental.log";
#else
        L"GrandMPPedSmoke.log";
#endif
    if (slash - path + 1 + std::size(filename) > std::size(path)) return;
    wcscpy_s(slash + 1, std::size(path) - (slash - path + 1), filename);
    const HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    char line[320]{};
    const int count = std::snprintf(line, sizeof(line),
        "[GrandMPPedSmoke] state=%s reason=%s pid=%lu tid=%lu ped=%08lX callback=%u\r\n",
        state, reason, GetCurrentProcessId(), GetCurrentThreadId(), ped, callbacks.load());
    DWORD written = 0;
    if (count > 0 && count < sizeof(line)) WriteFile(file, line, static_cast<DWORD>(count), &written, nullptr);
    CloseHandle(file);
}

template<class... Args> void writeSpawnLog(const char* format, Args... args) noexcept {
    char detail[208]{};
    const int count = std::snprintf(detail, sizeof(detail), format, args...);
    if (count > 0 && count < sizeof(detail)) writeLog("spawn_probe", detail);
}

template<class T> bool read(std::uintptr_t address, T& value) noexcept {
    SIZE_T count = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), &value, sizeof(value), &count) && count == sizeof(value);
}
bool imagePage(std::uint32_t address, std::size_t size, bool code) noexcept {
    MEMORY_BASIC_INFORMATION page{};
    return VirtualQuery(reinterpret_cast<LPCVOID>(address), &page, sizeof(page)) == sizeof(page) &&
        page.State == MEM_COMMIT && page.Type == MEM_IMAGE &&
        reinterpret_cast<std::uintptr_t>(page.AllocationBase) == ps::imageBase &&
        (code ? page.Protect == PAGE_EXECUTE_READ : page.Protect == PAGE_READONLY) &&
        std::uint64_t{address} + size <= reinterpret_cast<std::uintptr_t>(page.BaseAddress) + page.RegionSize;
}

// Map an admitted PE32 file RVA to raw bytes, with bounds at every step.
const std::byte* diskBytes(std::uint32_t address, std::size_t size) noexcept {
    if (executable.size() < sizeof(IMAGE_DOS_HEADER) || address < ps::imageBase) return nullptr;
    IMAGE_DOS_HEADER dos{};
    std::memcpy(&dos, executable.data(), sizeof(dos));
    if (dos.e_lfanew < 0 || std::uint64_t(dos.e_lfanew) + sizeof(IMAGE_NT_HEADERS32) > executable.size()) return nullptr;
    IMAGE_NT_HEADERS32 nt{};
    std::memcpy(&nt, executable.data() + dos.e_lfanew, sizeof(nt));
    const auto table = std::uint64_t(dos.e_lfanew) + 24 + nt.FileHeader.SizeOfOptionalHeader;
    if (nt.FileHeader.NumberOfSections > 32 || table + nt.FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER) > executable.size()) return nullptr;
    const auto rva = address - ps::imageBase;
    for (unsigned i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
        IMAGE_SECTION_HEADER section{};
        std::memcpy(&section, executable.data() + table + i * sizeof(section), sizeof(section));
        if (rva < section.VirtualAddress || std::uint64_t{rva} + size > std::uint64_t{section.VirtualAddress} + section.SizeOfRawData) continue;
        const auto offset = std::uint64_t{section.PointerToRawData} + rva - section.VirtualAddress;
        return offset + size <= executable.size() ? executable.data() + offset : nullptr;
    }
    return nullptr;
}
bool codeMatches() noexcept {
    std::uint32_t table = 0;
    std::array<std::byte, 512> headers{};
    SIZE_T headerCount = 0;
    if (executable.size() < headers.size() || !read(0x408B25, table) || table != ps::streamingTable ||
        !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(ps::imageBase), headers.data(), headers.size(), &headerCount) ||
        headerCount != headers.size() || std::memcmp(headers.data(), executable.data(), headers.size())) return false;
    for (const auto region : ps::codeRegions) {
        const auto* original = diskBytes(region.address, region.size);
        std::array<std::byte, 0x5C0> observed{};
        SIZE_T count = 0;
        if (!original || region.size > observed.size() || !imagePage(region.address, region.size, true) ||
            !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(region.address), observed.data(), region.size, &count) ||
            count != region.size || std::memcmp(original, observed.data(), region.size)) return false;
    }
    ps::CallBytes observed{};
    const auto* disk = diskBytes(ps::phaseCall, observed.size());
    return disk && !std::memcmp(disk, ps::originalCall.data(), observed.size()) &&
        imagePage(ps::phaseCall, observed.size(), true) && read(ps::phaseCall, observed) &&
        observed == (hookPublished.load() ? installedCall : ps::originalCall);
}
bool admitExecutable() {
    wchar_t path[32768]{};
    wchar_t ownPath[32768]{};
    const DWORD ownLength = GetModuleFileNameW(module, ownPath, 32768);
    if (!ownLength || ownLength >= 32768 || std::filesystem::canonical(ownPath) !=
        std::filesystem::path(
#if defined(GRANDMP_REMOTE_PED_MOVE_VARIANT)
            L"D:/Jogos/GrandMP-Smoke/scripts/GrandMPRemotePedMoveExperimental.asi"
#elif defined(GRANDMP_REMOTE_PED_VARIANT)
            L"D:/Jogos/GrandMP-Smoke/scripts/GrandMPRemotePedExperimental.asi"
#else
            L"D:/Jogos/GrandMP-Smoke/scripts/GrandMPPedSmoke.asi"
#endif
        )) return false;
    const DWORD length = GetModuleFileNameW(nullptr, path, 32768);
    if (!length || length >= 32768 || reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)) != ps::imageBase ||
        std::filesystem::canonical(path) != std::filesystem::path(L"D:/Jogos/GrandMP-Smoke/gta_sa.exe")) return false;
    executableFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (executableFile == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(executableFile, &size) || size.QuadPart != 14383616) return false;
    executable.resize(static_cast<std::size_t>(size.QuadPart));
    DWORD count = 0;
    if (!ReadFile(executableFile, executable.data(), static_cast<DWORD>(executable.size()), &count, nullptr) || count != executable.size()) return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) return false;
    std::array<std::byte, 32> digest{};
    const auto result = BCryptHash(algorithm, nullptr, 0, reinterpret_cast<PUCHAR>(executable.data()),
        static_cast<ULONG>(executable.size()), reinterpret_cast<PUCHAR>(digest.data()), static_cast<ULONG>(digest.size()));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return BCRYPT_SUCCESS(result) && grandmp::windows::recognizeGameVersion(
        grandmp::windows::inspectExecutable(executable), digest) == grandmp::windows::GameVersion::gta_sa_10us_hoodlum && codeMatches();
}

bool playerReady(void*& ped, ps::Position* position = nullptr, ps::Position* right = nullptr, bool diagnose = false) noexcept {
    auto field = [&](std::uintptr_t address, auto& value, const char* name) {
        const bool okay = read(address, value);
        if (!okay && diagnose) writeSpawnLog("player_ready_read_failed field=%s address=%08lX", name, static_cast<unsigned long>(address));
        return okay;
    };
    auto require = [&](bool okay, const char* name, double observed) {
        if (!okay && diagnose) writeSpawnLog("player_ready_rejected field=%s observed=%.9g", name, observed);
        return okay;
    };
    unsigned char index = 0, paused = 0, userPaused = 0, area = 0;
    int streaming = 0;
    void* pool = nullptr;
    std::uintptr_t vtable = 0, matrix = 0, intelligence = 0, data = 0;
    float health = 0;
    int type = -1;
    if (!field(ps::playerIndex, index, "player_index") || !require(index == 0, "player_index", index) ||
        !field(ps::player, ped, "player_pointer") || !require(ped != nullptr, "player_pointer", reinterpret_cast<std::uintptr_t>(ped)) ||
        !field(ps::codePause, paused, "code_pause") || !require(!paused, "code_pause", paused) ||
        !field(ps::userPause, userPaused, "user_pause") || !require(!userPaused, "user_pause", userPaused) ||
        !field(ps::streamingInitialized, streaming, "streaming_initialized") || !require(streaming == 1, "streaming_initialized", streaming) ||
        !field(ps::pedPool, pool, "ped_pool") || !require(pool != nullptr, "ped_pool", reinterpret_cast<std::uintptr_t>(pool))) return false;
    const auto address = reinterpret_cast<std::uintptr_t>(ped);
    if (!field(address, vtable, "vtable") || !require(vtable == ps::playerVtable, "vtable", vtable) ||
        !field(address + 0x2F, area, "area") || !require(area == 0, "area", area) ||
        !field(address + 0x47C, intelligence, "intelligence") || !require(intelligence != 0, "intelligence", intelligence) ||
        !field(address + 0x480, data, "player_data") || !require(data != 0, "player_data", data) ||
        !field(address + 0x540, health, "health") || !require(std::isfinite(health), "health_nonfinite", health) ||
        !require(health > 0, "health_positive", health) || !field(address + 0x598, type, "ped_type") || !require(type == 0, "ped_type", type) ||
        !field(address + 0x14, matrix, "matrix") || !require(matrix != 0, "matrix", matrix)) return false;
    if (position) {
        if (!field(matrix + 0x30, *position, "position")) return false;
        if (diagnose && !ps::finite(*position))
            writeSpawnLog("cj_position x=%.9g y=%.9g z=%.9g", position->x, position->y, position->z);
        if (!require(std::isfinite(position->x), "position_x_nonfinite", position->x) ||
            !require(std::isfinite(position->y), "position_y_nonfinite", position->y) ||
            !require(std::isfinite(position->z), "position_z_nonfinite", position->z)) return false;
    }
    if (right && !field(matrix, *right, "right")) return false;
    if (diagnose) writeSpawnLog("player_ready_ok cj=%08lX matrix=%08lX", static_cast<unsigned long>(address), static_cast<unsigned long>(matrix));
    return true;
}

bool streamingReady() noexcept {
    unsigned char disabled = 1, scene = 1, bigModel = 1;
    return read(ps::streamingDisabled, disabled) && !disabled &&
        read(ps::loadingScene, scene) && !scene && read(ps::loadingBigModel, bigModel) && !bigModel;
}

struct SpawnProbe {
    void* cj = nullptr;
    template<class... Args> void trace(const char* format, Args... args) { writeSpawnLog(format, args...); }
    bool player(ps::Position& origin, ps::Position& right) { return playerReady(cj, &origin, &right, true); }
    float ground(float x, float y, float z, bool& found) {
        return reinterpret_cast<float(__cdecl*)(float, float, float, bool*, void**)>(ps::findGround)(x, y, z, &found, nullptr);
    }
    std::uintptr_t sphere(ps::Position target) {
        trace("sphere_query x=%.9g y=%.9g z=%.9g radius=0.35 ignore_cj=%08lX buildings=1 vehicles=1 peds=1 objects=1 dummies=1 camera=0",
            target.x, target.y, target.z, static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(cj)));
        using Sphere = void*(__cdecl*)(ps::Position, float, void*, bool, bool, bool, bool, bool, bool);
        const auto hit = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Sphere>(ps::testSphere)(target, 0.35f, cj, true, true, true, true, true, false));
        trace("sphere_result hit=%08lX", static_cast<unsigned long>(hit));
        return hit;
    }
};

struct NativeEngine {
    void* owned = nullptr; // Stable storage registered with the engine reference system.
    std::uintptr_t creationResult = 0; // Keep the exact AddPed result even if its registered reference is cleared.
    int reference = -1;
    const char* ownershipFailure = "not_observed";
    void log(ps::State state, const char* reason) {
        if (state == ps::State::ownership_lost) {
            char detail[192]{};
            std::snprintf(detail, sizeof(detail), "%s detail=%s", reason, ownershipFailure);
            writeLog(ps::name(state), detail, creationResult); // Raw result is diagnostic only, never dereferenced here.
        } else writeLog(ps::name(state), reason, creationResult);
    }
    bool requestModel() {
        reinterpret_cast<void(__cdecl*)(int, int)>(ps::requestModel)(ps::male01, 0); // DEFAULT, no global ownership flags.
        reinterpret_cast<void(__cdecl*)(bool)>(ps::loadModels)(false);
        return true;
    }
    bool modelLoaded() {
        unsigned char state = 0;
        return read(ps::streamingTable + ps::male01 * 0x14 + 0x10, state) && state == 1;
    }
    bool revalidateModel() {
        unsigned char state = 0;
        const bool okay = read(ps::streamingTable + ps::male01 * 0x14 + 0x10, state);
        writeSpawnLog("model_revalidate model=%u read_ok=%u load_state=%u loaded=%u", ps::male01, unsigned(okay), unsigned(state), unsigned(okay && state == 1));
        return okay && state == 1;
    }
    bool spawnPosition(ps::Position& target) {
        SpawnProbe probe;
        return ps::findSpawnPosition(probe, target);
    }
    bool registered() const noexcept {
        if (!owned) return false;
        std::uintptr_t link = 0;
        if (!read(reinterpret_cast<std::uintptr_t>(owned) + 0x24, link)) return false;
        for (unsigned i = 0; link && i < 128; ++i) {
            std::uintptr_t destination = 0, next = 0;
            if (!read(link + 4, destination) || !read(link, next)) return false;
            if (destination == reinterpret_cast<std::uintptr_t>(&owned)) return true;
            link = next;
        }
        return false;
    }
    std::uintptr_t create(const ps::Position& position) {
        using Add = void*(__cdecl*)(int, unsigned, const ps::Position&, bool);
        owned = reinterpret_cast<Add>(ps::addPed)(4, ps::male01, position, false);
        creationResult = reinterpret_cast<std::uintptr_t>(owned);
        if (!owned) return creationResult;
        using Register = void(__thiscall*)(void*, void**);
        reinterpret_cast<Register>(ps::registerReference)(owned, &owned);
        if (owned) reference = reinterpret_cast<int(__cdecl*)(void*)>(ps::pedRef)(owned);
        return creationResult;
    }
    bool owns(std::uintptr_t ped) {
        auto refuse = [&](const char* reason) { ownershipFailure = reason; return false; };
        if (!owned) return refuse("registered_pointer_null_observed");
        if (reinterpret_cast<std::uintptr_t>(owned) != ped) return refuse("tracked_pointer_mismatch");
        void* cj = nullptr;
        if (!read(ps::player, cj) || owned == cj) return refuse("cj_identity_unavailable_or_equal");
        if (reference < 0) return refuse("pool_reference_invalid");
        // Hoodlum GetPed (54FF90) does not bounds-check the slot. CPool layout:
        // objects +0, byte map +4, size +8 (plugin-sdk 15f15b60 CPool.h).
        std::uintptr_t pool = 0, objects = 0, flags = 0;
        int size = 0;
        unsigned char flag = 0;
        const auto index = static_cast<unsigned>(reference) >> 8;
        if (!read(ps::pedPool, pool) || !pool || !read(pool, objects) || !objects ||
            !read(pool + 4, flags) || !flags || !read(pool + 8, size) || size <= 0 || index >= static_cast<unsigned>(size))
            return refuse("pool_or_slot_unavailable");
        if (!read(flags + index, flag) || (flag & 0x80U) || flag != (reference & 0xFF))
            return refuse("pool_slot_unreadable_free_or_generation_changed");
        if (reinterpret_cast<void*(__cdecl*)(int)>(ps::resolvePed)(reference) != owned)
            return refuse("pool_handle_identity_mismatch");
        // Only now may the reference list of a live, pool-authenticated object be read.
        if (!registered()) return refuse("registered_link_not_proven");
        return true;
    }
    bool visible(std::uintptr_t ped) {
        std::uintptr_t clump = 0;
        unsigned short model = 0;
        std::uint32_t flags = 0;
        return read(ped + 0x18, clump) && clump && read(ped + 0x22, model) && model == ps::male01 &&
            read(ped + 0x1C, flags) && (flags & 0x80U); // CEntity::bIsVisible, not a screen-space proof.
    }
    void moveLog(const char* state, const char* reason) { writeLog(state, reason, creationResult); }
    const char* movePosition(std::uintptr_t ped, ps::Position& position) {
        // Authenticate the pool/reference before inspecting any entity fields.
        if (!owns(ped)) return "ownership_unproven";
        void* cj = nullptr;
        ps::Position playerPosition{};
        std::uintptr_t matrix = 0;
        float health = 0;
        int type = -1;
        unsigned char area = 0;
        std::uint32_t flags = 0;
        if (!playerReady(cj, &playerPosition) || !streamingReady()) return "gameplay_unavailable";
        if (!visible(ped) || !read(ped + 0x14, matrix) || !matrix || !read(matrix + 0x30, position) ||
            !read(ped + 0x540, health) || !read(ped + 0x598, type) || !read(ped + 0x2F, area) || !read(ped + 0x46C, flags))
            return "ped_pose_or_state_unavailable";
        if (!ps::finite(position) || !std::isfinite(health)) return "ped_pose_or_health_nonfinite";
        if (health <= 0 || type != 4 || area != 0 || (flags & 0x100U)) return "ped_dead_wrong_type_area_or_vehicle";
        if (ps::distance2D(position, playerPosition) > 12.f || std::abs(position.z - playerPosition.z) > 3.f)
            return "cj_ped_proximity_limit";
        return nullptr;
    }
    const char* moveDestination(std::uintptr_t ped, ps::Position origin, ps::Position& destination) {
        if (!owns(ped)) return "ownership_unproven";
        std::uintptr_t matrix = 0;
        ps::Position forward{};
        if (!read(ped + 0x14, matrix) || !matrix || !read(matrix + 0x10, forward)) return "ped_forward_unavailable";
        struct Probe {
            void* ignore;
            float ground(float x, float y, float z, bool& found) {
                return reinterpret_cast<float(__cdecl*)(float, float, float, bool*, void**)>(ps::findGround)(x, y, z, &found, nullptr);
            }
            std::uintptr_t sphere(ps::Position center) {
                using Sphere = void*(__cdecl*)(ps::Position, float, void*, bool, bool, bool, bool, bool, bool);
                return reinterpret_cast<std::uintptr_t>(reinterpret_cast<Sphere>(ps::testSphere)(center, 0.35f, ignore, true, true, true, true, true, false));
            }
        } probe{owned};
        const char* failure = ps::findMoveDestination(probe, origin, forward, destination);
        if (!failure) {
            void* cj = nullptr;
            ps::Position playerPosition{};
            if (!playerReady(cj, &playerPosition)) return "destination_player_unavailable";
            if (ps::distance2D(destination, playerPosition) > 12.f || std::abs(destination.z - playerPosition.z) > 3.f)
                return "destination_cj_proximity_limit";
            char detail[192]{};
            std::snprintf(detail, sizeof(detail), "origin=(%.3f,%.3f,%.3f) target=(%.3f,%.3f,%.3f) distance=4 walk=4 slot=3",
                origin.x, origin.y, origin.z, destination.x, destination.y, destination.z);
            moveLog("move_probe", detail);
        }
        return failure;
    }
    const char* assignMove(std::uintptr_t ped, const ps::Position& destination) {
        namespace mn = ps::movement_native;
#if defined(GRANDMP_REMOTE_PED_MOVE_VARIANT)
        if (!grandmp::windows::remote_ped_move::physicalExecutionApproved) return "remote_movement_physical_review_missing";
#else
        if (!ps::movementExecutionApproved) return "movement_physical_review_missing";
#endif
        if (GetCurrentThreadId() != gameThread || !owns(ped)) return "thread_or_ownership_unproven";
        // The existing executable identity/CALL gates remain intact. Add read-only
        // comparisons for new task code and its vtable before any allocation.
        if (!codeMatches()) return "live_code_changed";
        for (const auto region : mn::regions) {
            const auto* disk = diskBytes(region.address, region.size);
            std::array<std::byte, 0x450> observed{};
            SIZE_T count = 0;
            if (!disk || region.size > observed.size() || !imagePage(region.address, region.size, region.code) ||
                !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(region.address), observed.data(), region.size, &count) ||
                count != region.size || std::memcmp(disk, observed.data(), region.size)) return "task_code_or_vtable_changed";
        }
        std::uintptr_t intelligence = 0, intelligencePed = 0, managerPed = 0;
        if (!read(ped + 0x47C, intelligence) || !intelligence || !read(intelligence, intelligencePed) || intelligencePed != ped ||
            !read(intelligence + 4 + 0x2C, managerPed) || managerPed != ped) return "intelligence_manager_owner_mismatch";
        const auto manager = intelligence + 4;
        // Never replace a primary task or override physical/event responses.
        for (unsigned i = 0; i <= mn::primarySlot; ++i) {
            std::uintptr_t task = 0;
            if (!read(manager + i * 4, task) || task) return "primary_task_slot_busy_or_unreadable";
        }
        std::uintptr_t pool = 0, objects = 0, flags = 0;
        int size = 0;
        if (!read(mn::taskPool, pool) || !pool || !read(pool, objects) || !objects || !read(pool + 4, flags) || !flags ||
            !read(pool + 8, size) || size <= 0 || size > 10000) return "task_pool_unavailable";
        unsigned free = 0;
        for (int i = 0; i < size && free < 8; ++i) {
            unsigned char flag = 0;
            if (!read(flags + i, flag)) return "task_pool_map_unreadable";
            free += (flag & 0x80U) != 0;
        }
        if (free < 8) return "task_pool_capacity_low";
        void* memory = reinterpret_cast<mn::Allocate>(mn::allocateTask)(mn::taskSize);
        if (!memory) return "task_allocation_null";
        const auto address = reinterpret_cast<std::uintptr_t>(memory);
        unsigned char flag = 0;
        if (address < objects || (address - objects) % mn::taskStride || (address - objects) / mn::taskStride >= static_cast<unsigned>(size) ||
            !read(flags + (address - objects) / mn::taskStride, flag) || (flag & 0x80U))
            return "task_allocation_identity_unknown_retained_no_delete";
        void* task = mn::construct(reinterpret_cast<mn::Construct>(mn::constructWalk), memory, destination);
        std::uintptr_t vtable = 0;
        if (task != memory || !read(address, vtable) || vtable != mn::walkVtable)
            return "task_constructor_identity_unknown_retained_no_delete";
        mn::transfer(reinterpret_cast<mn::SetTask>(mn::setTask), reinterpret_cast<void*>(manager), task);
        // SetTask owns (and may already have deleted) the task. Compare only the
        // manager slot; never dereference/free the transferred pointer on failure.
        if (!owns(ped)) return "post_transfer_ownership_unproven_no_task_delete";
        std::uintptr_t installed = 0;
        if (!read(manager + mn::primarySlot * 4, installed) || installed != address)
            return "task_not_retained_by_manager_no_retry_no_delete";
        return nullptr;
    }
    bool remove(std::uintptr_t ped) {
        if (!owns(ped)) return false;
        void* victim = owned;
        reinterpret_cast<void(__thiscall*)(void*, void**)>(ps::cleanReference)(victim, &owned);
        owned = nullptr;
        reference = -1;
        reinterpret_cast<void(__cdecl*)(void*)>(ps::removePed)(victim); // CWorld::Remove + virtual deleting destructor.
        return true;
    }
} engine;

#if defined(GRANDMP_REMOTE_PED_VARIANT)
#include "remote_ped_runtime.inl"
#endif

void runPhase(std::uintptr_t caller) noexcept {
    if (callbacks.fetch_add(1, std::memory_order_relaxed) == 0) writeLog("waiting", "callback_executed");
    if (preparation.load() != 3) return; // Do not spawn while publication/protection checks are still in progress.
    const bool correctThread = gameThread && GetCurrentThreadId() == gameThread;
    if (!correctThread || caller != ps::phaseCall + 5) {
        runtimeDisabled.store(true); writeLog("failed", "wrong_phase_caller_or_thread"); return;
    }
    if (runtimeDisabled.load()) {
#if defined(GRANDMP_REMOTE_PED_VARIANT)
        remoteController.disable();
#else
        experiment.fail();
#endif
        return;
    }
    // Also guard validation/native queries against any nested engine dispatch.
    static bool busy = false;
    if (busy) return;
    busy = true;
    struct Leave { bool& value; ~Leave() { value = false; } } leave{busy};
#if defined(GRANDMP_REMOTE_PED_VARIANT)
    // Fail permanently even outside gameplay; never clean up unknown identity.
    if (!codeMatches()) {
        runtimeDisabled.store(true);
        remoteController.disable();
#if defined(GRANDMP_REMOTE_PED_MOVE_VARIANT)
        remoteEngine.moveLog("remote_move_failed", "native_code_identity_terminal_no_cleanup");
#endif
        writeLog("failed", "live_code_changed_remote_terminal_no_cleanup");
        return;
    }
#endif
    void* cj = nullptr;
    const bool gameplay = playerReady(cj) && streamingReady();
#if defined(GRANDMP_REMOTE_PED_VARIANT)
    if (rp::physicalExecutionApproved && remoteReceiver.load() == rp::ReceiverState::ready)
        remoteController.tick(remoteEngine, remoteIntents.snapshot(), true, gameplay, GetTickCount64());
#else
    if (gameplay && !codeMatches()) { experiment.fail(); writeLog("failed", "live_code_changed"); return; }
    DWORD foregroundPid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
    static ps::PressEdge f8, f7;
    if (f8.sample((GetAsyncKeyState(VK_F8) & 0x8000) != 0, gameplay && foregroundPid == GetCurrentProcessId())) removalRequested.store(true);
    const bool remove = gameplay && removalRequested.exchange(false);
    const bool move = f7.sample((GetAsyncKeyState(VK_F7) & 0x8000) != 0, gameplay && foregroundPid == GetCurrentProcessId());
    const auto now = GetTickCount64();
    experiment.tick(engine, true, gameplay, remove, now);
    movement.tick(engine, experiment, true, gameplay, move, remove, now);
#endif
}

DWORD engineWindowThread() noexcept {
    std::uintptr_t platform = 0;
    HWND window = nullptr;
    DWORD pid = 0;
    if (!read(ps::platformGlobal, platform) || !platform || !read(platform, window) ||
        !window || !IsWindow(window) || !IsWindowVisible(window)) return 0;
    const DWORD thread = GetWindowThreadProcessId(window, &pid);
    return pid == GetCurrentProcessId() ? thread : 0;
}

bool installCandidate() {
    // The reviewed build cannot enter a writer. No opt-in overrides this gate.
    if (!ps::physicalExecutionApproved) return false;
    void* cj = nullptr;
    // A five-byte CALL is not atomic. Install only on the engine/window thread
    // while it is in GetMessage/PeekMessage, never from the preparation worker.
    int cpu[4]{};
    __cpuid(cpu, 1);
    if (!gameThread || engineWindowThread() != gameThread || GetCurrentThreadId() != gameThread || !(cpu[3] & (1 << 24)) ||
        !IsProcessorFeaturePresent(PF_XMMI64_INSTRUCTIONS_AVAILABLE) || !playerReady(cj) || !streamingReady() ||
        !codeMatches() || !ps::encodePhaseCall(reinterpret_cast<std::uintptr_t>(&ps::phaseThunk), installedCall)) return false;
    DWORD protection = 0;
    auto* entry = reinterpret_cast<void*>(ps::phaseCall);
    if (!VirtualProtect(entry, installedCall.size(), PAGE_EXECUTE_READWRITE, &protection)) return false;
    if (protection != PAGE_EXECUTE_READ) {
        DWORD ignored = 0;
        VirtualProtect(entry, installedCall.size(), protection, &ignored);
        return false;
    }
    std::memcpy(entry, installedCall.data(), installedCall.size());
    hookPublished.store(true);
    const BOOL flushed = FlushInstructionCache(GetCurrentProcess(), entry, installedCall.size());
    DWORD ignored = 0;
    const BOOL restored = VirtualProtect(entry, installedCall.size(), protection, &ignored);
    if (!flushed || !restored || !codeMatches()) {
        writeLog("failed", "phase_publication_failed_module_pinned_no_engine_dispatch");
        return false; // Immutable original remains available; no unload/automatic repair.
    }
    return true;
}

LRESULT CALLBACK installMessage(int code, WPARAM word, LPARAM message) {
    unsigned armed = 1;
    if (code >= 0 && installation.compare_exchange_strong(armed, 2)) {
        installationSucceeded.store(installCandidate());
        installation.store(3);
    }
    return CallNextHookEx(nullptr, code, word, message);
}

bool installOnGameThread() {
    if (!ps::physicalExecutionApproved || !gameThread) return false;
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&ps::phaseThunk), &pinned) || pinned != module) return false;
    installation.store(1);
    // This bootstrap does no engine work. Its sole job is same-thread publication.
    const HHOOK hook = SetWindowsHookExW(WH_GETMESSAGE, installMessage, module, gameThread);
    if (!hook) { installation.store(4); return false; }
    const bool posted = PostThreadMessageW(gameThread, WM_NULL, 0, 0) != FALSE;
    const auto deadline = GetTickCount64() + 5000;
    while (posted && installation.load() == 1 && GetTickCount64() < deadline) Sleep(1);
    unsigned armed = 1;
    installation.compare_exchange_strong(armed, 4); // Cancel only if not yet executing.
    while (installation.load() == 2) Sleep(1); // Do not close identity storage under the installer.
    if (!UnhookWindowsHookEx(hook)) {
        runtimeDisabled.store(true);
        writeLog("failed", "bootstrap_unhook_failed_module_pinned");
        return false;
    }
    return installationSucceeded.load(); // Worker opens dispatch only after bootstrap removal.
}

DWORD WINAPI prepare(void* reference) {
    try {
        writeLog("loaded", "experimental_module_no_engine_calls_in_loader");
        writeLog("waiting", "preparation_read_only");
        if (!ps::physicalExecutionApproved
#if defined(GRANDMP_REMOTE_PED_VARIANT)
            || !rp::physicalExecutionApproved
#if defined(GRANDMP_REMOTE_PED_MOVE_VARIANT)
            || !grandmp::windows::remote_ped_move::physicalExecutionApproved
#endif
#endif
        ) {
            writeLog("waiting", "installation_blocked_physical_execution_review_missing");
            preparation.store(2);
        } else if (!admitExecutable()) {
            writeLog("failed", "hoodlum_identity_path_or_live_signature");
            preparation.store(4);
        } else {
            wchar_t enabled[4]{};
            GetEnvironmentVariableW(L"GRANDMP_PED_SMOKE_ENABLE", enabled, 4);
            if (std::wstring_view(enabled) != L"1") {
                writeLog("waiting", "explicit_opt_in_missing");
                preparation.store(2);
            } else {
                writeLog("waiting", "press_F6_in_gameplay_to_install_candidate");
                const auto deadline = GetTickCount64() + 120000;
                bool installed = false;
                while (GetTickCount64() < deadline) {
                    DWORD pid = 0;
                    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
                    if (pid == GetCurrentProcessId() && (GetAsyncKeyState(VK_F6) & 0x8000)) {
                        gameThread = engineWindowThread();
                        installed = installOnGameThread();
                        break;
                    }
                    Sleep(50);
                }
#if defined(GRANDMP_REMOTE_PED_VARIANT)
                if (installed && !startRemoteNetwork()) {
                    runtimeDisabled.store(true);
                    installed = false;
                    writeLog("failed", "remote_worker_start_refused_module_pinned");
                }
#endif
                preparation.store(installed ? 3 : 4);
                writeLog(installed ? "waiting" : "failed", installed ? "post_population_phase_installed_original_immutable_module_pinned" : "installation_refused");
            }
        }
    } catch (...) { preparation.store(4); writeLog("failed", "preparation_exception_no_retry"); }
    if (!hookPublished.load() && executableFile != INVALID_HANDLE_VALUE) {
        CloseHandle(executableFile);
        executableFile = INVALID_HANDLE_VALUE;
    }
    FreeLibraryAndExitThread(static_cast<HMODULE>(reference), 0);
}
} // namespace

namespace grandmp::windows::ped_smoke {
const PhaseOriginal originalPhase = reinterpret_cast<PhaseOriginal>(updateWeapons);
void __cdecl phaseBody(std::uintptr_t caller) noexcept { runPhase(caller); }
}

extern "C" void __cdecl InitializeASI() noexcept {
    unsigned expected = 0;
    if (!preparation.compare_exchange_strong(expected, 1)) return;
    HMODULE reference = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&InitializeASI), &reference)) {
        preparation.store(4);
        return;
    }
    const HANDLE worker = CreateThread(nullptr, 0, prepare, reference, 0, nullptr);
    if (!worker) { preparation.store(4); FreeLibrary(reference); return; }
    CloseHandle(worker); // Preparation only. No thread ever drives gameplay.
}
extern "C" unsigned __cdecl GrandMPPedSmokeStatus() noexcept { return preparation.load(); }
extern "C" unsigned __cdecl GrandMPPedSmokeCallbackCount() noexcept { return callbacks.load(); }
extern "C" void __cdecl GrandMPPedSmokeRequestRemoval() noexcept { removalRequested.store(true); }
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) module = instance;
    return TRUE;
}
