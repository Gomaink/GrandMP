#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../client/windows/tools/ped_smoke_state.hpp"
#include "../client/windows/tools/ped_smoke_addresses.hpp"
#include "../client/windows/tools/ped_smoke_install_diagnostics.hpp"
#include "../client/windows/tools/ped_smoke_phase.hpp"
#include "../client/windows/tools/ped_smoke_spawn.hpp"
#include "../client/windows/tools/ped_smoke_move.hpp"
#include "../client/windows/tools/ped_smoke_move_native.hpp"
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

namespace ps = grandmp::windows::ped_smoke;
namespace {
alignas(16) unsigned char beforeFx[512]{}, originalFx[512]{}, savedFx[512]{};
unsigned bodyCalls = 0, originalCalls = 0, bodyOrder = 0, originalOrder = 0;
std::uintptr_t bodyCaller = 0, expectedReturn = 0, originalReturn = 0;
unsigned observedRegisters[8]{}, observedFlags = 0, beforeStack = 0, afterStack = 0;
unsigned clobberMxcsr = 0x3F80;
__declspec(naked) void fakeOriginal() {
    __asm {
        mov observedRegisters[0], eax
        mov observedRegisters[4], ecx
        mov observedRegisters[8], edx
        mov observedRegisters[12], ebx
        mov observedRegisters[16], ebp
        mov observedRegisters[20], esi
        mov observedRegisters[24], edi
        mov observedRegisters[28], esp
        pushfd
        pop observedFlags
        mov eax, [esp]
        mov originalReturn, eax
        fxsave originalFx
        inc originalCalls
        mov eax, bodyCalls
        inc eax
        mov originalOrder, eax
        ret
    }
}
} // namespace
namespace grandmp::windows::ped_smoke {
const PhaseOriginal originalPhase = &fakeOriginal;
void __cdecl phaseBody(std::uintptr_t caller) noexcept {
    bodyCaller = caller;
    ++bodyCalls;
    bodyOrder = originalCalls + 1;
    __asm {
        fninit
        pxor xmm0, xmm0
        pxor xmm1, xmm1
        pxor xmm2, xmm2
        pxor xmm3, xmm3
        pxor xmm4, xmm4
        pxor xmm5, xmm5
        pxor xmm6, xmm6
        pxor xmm7, xmm7
        ldmxcsr clobberMxcsr
        xor eax, eax
        xor ecx, ecx
        xor edx, edx
        clc
    }
}
}
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct PositionProbe {
    ps::Position origin{100.f, 200.f, 10.f}, right{0.6f, 0.8f, 0.f}, query{}, sphereCenter{};
    bool ready = true, found = true;
    float groundZ = 9.f;
    std::uintptr_t hit = 0;
    unsigned groundCalls = 0, sphereCalls = 0;
    std::vector<std::string> lines;
    template<class... Args> void trace(const char* format, Args... args) {
        char line[208]{};
        const int count = std::snprintf(line, sizeof(line), format, args...);
        require(count > 0 && count < sizeof(line), "diagnostic must fit its actual native buffer");
        lines.emplace_back(line);
    }
    bool player(ps::Position& position, ps::Position& axis) { position = origin; axis = right; return ready; }
    float ground(float x, float y, float z, bool& result) {
        ++groundCalls; query = {x, y, z}; result = found; return groundZ;
    }
    std::uintptr_t sphere(ps::Position center) { ++sphereCalls; sphereCenter = center; return hit; }
    bool contains(const char* fragment) const {
        for (const auto& line : lines) if (line.find(fragment) != std::string::npos) return true;
        return false;
    }
};
struct Fake {
    ps::Experiment* controller = nullptr;
    unsigned requests = 0, creations = 0, removals = 0, transitions = 0;
    bool loaded = true, ground = true, owned = true, renderable = true, allocation = true, reenter = false;
    bool requestOk = true, invalidPosition = false, removeOk = true;
    unsigned revalidations = 0, positionQueries = 0, ownershipChecks = 0;
    bool forbidOwnershipChecks = false;
    PositionProbe* probe = nullptr;
    std::string lastReason;
    bool requestModel() { ++requests; return requestOk; }
    bool modelLoaded() { return loaded; }
    bool revalidateModel() { ++revalidations; return modelLoaded(); }
    bool spawnPosition(ps::Position& position) {
        ++positionQueries;
        if (probe) return ps::findSpawnPosition(*probe, position);
        position = {2.5f, 0.f, invalidPosition ? std::numeric_limits<float>::quiet_NaN() : 1.f};
        return ground;
    }
    std::uintptr_t create(const ps::Position&) {
        ++creations;
        if (reenter) controller->tick(*this, true, true, false, 4);
        return allocation ? 0x1000 : 0;
    }
    bool owns(std::uintptr_t pointer) {
        require(!forbidOwnershipChecks, "terminal state must never inspect a potentially destroyed entity");
        ++ownershipChecks;
        return owned && pointer == 0x1000;
    }
    bool visible(std::uintptr_t) { return renderable; }
    bool remove(std::uintptr_t pointer) { require(pointer == 0x1000 && owned, "remove only our token"); ++removals; return removeOk; }
    void log(ps::State, const char* reason) { ++transitions; lastReason = reason; }
};
void advance(ps::Experiment& controller, Fake& fake) {
    for (unsigned i = 0; i < 3; ++i) controller.tick(fake, true, true, false, i);
}

void spawnDiagnostics(bool nonfinite) {
    auto refused = [](PositionProbe probe, const char* reason, unsigned grounds, unsigned spheres) {
        ps::Experiment controller;
        Fake fake{&controller}; fake.probe = &probe;
        advance(controller, fake);
        const auto lines = probe.lines.size();
        for (unsigned i = 0; i < 1000; ++i) controller.tick(fake, true, true, false, i + 3);
        require(controller.state() == ps::State::failed && !fake.creations && fake.revalidations == 1 && fake.positionQueries == 1,
            "refused attempt never reaches AddPed or repeats diagnostics");
        require(probe.lines.size() == lines && probe.contains(reason), "specific refusal logged once per attempt");
        require(probe.groundCalls == grounds && probe.sphereCalls == spheres, "no later native query after a failed guard");
    };
    if (nonfinite) {
        for (float value : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
            for (unsigned axis = 0; axis < 3; ++axis) {
                PositionProbe input;
                if (axis == 0) input.origin.x = value;
                else if (axis == 1) input.origin.y = value;
                else input.origin.z = value;
                refused(input, "cj_position_nonfinite", 0, 0);
                input = PositionProbe{};
                if (axis == 0) input.right.x = value;
                else if (axis == 1) input.right.y = value;
                else input.right.z = value;
                refused(input, "cj_right_nonfinite", 0, 0);
            }
            PositionProbe ground;
            ground.groundZ = value;
            refused(ground, "ground_return_nonfinite", 1, 0);
        }
        PositionProbe overflow;
        overflow.right = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), 0.f};
        refused(overflow, "right_norm_nonfinite", 0, 0);
        overflow = PositionProbe{};
        overflow.origin.z = -std::numeric_limits<float>::max();
        overflow.groundZ = std::numeric_limits<float>::max();
        refused(overflow, "ground_delta_nonfinite", 1, 0);
        return;
    }
    PositionProbe probe;
    ps::Experiment controller;
    Fake fake{&controller}; fake.probe = &probe;
    advance(controller, fake);
    const auto lines = probe.lines.size();
    for (unsigned i = 0; i < 10000; ++i) controller.tick(fake, true, true, false, i + 3);
    require(fake.creations == 1 && fake.revalidations == 1 && fake.positionQueries == 1 && probe.lines.size() == lines,
        "accepted attempt logs once and creates exactly one ped");
    require(probe.query.x == 101.5f && probe.query.y == 202.f && probe.query.z == 13.f && probe.sphereCenter.z == 10.f,
        "original 2.5m calculation, query height +3 and ground offset +1 preserved");
    require(probe.contains("cj_position") && probe.contains("cj_right") && probe.contains("right_normalized") &&
        probe.contains("candidate") && probe.contains("ground_result return_z=9 found=1") && probe.contains("ground_delta") &&
        probe.contains("spawn_position_accepted"), "successful attempt exposes intermediate values");
    probe = PositionProbe{}; probe.ready = false;
    refused(probe, "player_ready", 0, 0);
    probe = PositionProbe{}; probe.right = {0.49f, 0.f, 0.f};
    refused(probe, "right_norm_range", 0, 0);
    probe.right.x = 1.51f;
    refused(probe, "right_norm_range", 0, 0);
    probe = PositionProbe{}; probe.found = false;
    refused(probe, "ground_not_found", 1, 0);
    probe = PositionProbe{}; probe.groundZ = 12.f;
    refused(probe, "ground_delta_limit", 1, 0);
    probe = PositionProbe{}; probe.hit = 0x1234;
    refused(probe, "sphere_hit", 1, 1);
    for (float ground : {7.f, 11.f}) {
        probe = PositionProbe{}; probe.groundZ = ground;
        ps::Position target{};
        require(ps::findSpawnPosition(probe, target), "delta exactly two remains permitted");
    }
    ps::Experiment lost;
    Fake model{&lost};
    lost.tick(model, true, true, false, 0);
    lost.tick(model, true, true, false, 1);
    model.loaded = false;
    for (unsigned i = 2; i < 1000; ++i) lost.tick(model, true, true, false, i);
    require(model.revalidations == 1 && !model.positionQueries && !model.creations &&
        model.lastReason == "spawn_model_revalidation_failed", "model loss distinguished before any position/native query");
}

void states(std::wstring_view scenario) {
    ps::Experiment controller;
    Fake fake{&controller};
    if (scenario == L"waiting") {
        for (unsigned i = 0; i < 200; ++i) controller.tick(fake, true, false, false, i);
        require(controller.state() == ps::State::waiting && !fake.requests && !fake.creations, "menu/loading cannot call streaming/spawn");
        fake.loaded = false;
        controller.tick(fake, true, true, false, 200);
        controller.tick(fake, true, true, false, 201);
        require(controller.state() == ps::State::model_requested && fake.requests == 1 && !fake.creations, "wait for actual model load");
        controller.tick(fake, true, true, false, 10201);
        require(controller.state() == ps::State::failed, "bounded model wait");
    } else if (scenario == L"one_ped" || scenario == L"reentrant") {
        fake.reenter = scenario == L"reentrant";
        controller.tick(fake, true, true, false, 0);
        require(controller.state() == ps::State::model_requested, "model requested first");
        controller.tick(fake, true, true, false, 1);
        require(controller.state() == ps::State::model_loaded && !fake.creations, "loaded distinguished from created");
        controller.tick(fake, true, true, false, 2);
        for (unsigned i = 0; i < 10000; ++i) controller.tick(fake, true, true, false, i + 3);
        require(controller.state() == ps::State::ped_created && fake.requests == 1 && fake.creations == 1, "exactly one spawn under repeated/nested frames");
    } else if (scenario == L"removal") {
        advance(controller, fake);
        controller.tick(fake, true, false, true, 3);
        require(!fake.removals, "no removal outside gameplay");
        controller.tick(fake, true, true, true, 4);
        for (unsigned i = 0; i < 100; ++i) controller.tick(fake, true, true, true, i + 5);
        require(controller.state() == ps::State::removed && fake.removals == 1 && fake.creations == 1, "idempotent owned removal without respawn");
    } else if (scenario == L"identity_lost") {
        advance(controller, fake);
        fake.owned = false;
        controller.tick(fake, true, true, true, 4);
        require(controller.state() == ps::State::ownership_lost && !fake.removals, "never delete unknown/reused ped");
        for (unsigned i = 0; i < 100; ++i) controller.tick(fake, true, true, false, i + 5);
        require(fake.creations == 1, "loss never respawns");
    } else if (scenario == L"failures") {
        fake.allocation = false;
        advance(controller, fake);
        for (unsigned i = 0; i < 100; ++i) controller.tick(fake, true, true, false, i + 3);
        require(controller.state() == ps::State::failed && fake.creations == 1, "null AddPed is terminal");
        ps::Experiment requestFailed;
        Fake requestError{&requestFailed}; requestError.requestOk = false;
        advance(requestFailed, requestError);
        require(requestFailed.state() == ps::State::failed && !requestError.creations && requestError.requests == 1, "request failure cannot spawn or retry");
        ps::Experiment modelLost;
        Fake lost{&modelLost};
        modelLost.tick(lost, true, true, false, 0);
        modelLost.tick(lost, true, true, false, 1);
        lost.loaded = false;
        modelLost.tick(lost, true, true, false, 2);
        require(modelLost.state() == ps::State::failed && !lost.creations, "recheck streaming immediately before spawn");
        ps::Experiment invalid;
        Fake nan{&invalid}; nan.invalidPosition = true;
        advance(invalid, nan);
        require(invalid.state() == ps::State::failed && !nan.creations, "non-finite ground position rejected");
        ps::Experiment noGround;
        Fake absent{&noGround}; absent.ground = false;
        advance(noGround, absent);
        require(noGround.state() == ps::State::failed && absent.creations == 0, "no ground means no spawn");
        ps::Experiment noRender;
        Fake invisible{&noRender}; invisible.renderable = false;
        advance(noRender, invisible);
        require(noRender.state() == ps::State::cleanup_pending && invisible.creations == 1, "owned but not renderable must schedule cleanup");
        noRender.tick(invisible, true, true, true, 4);
        require(noRender.state() == ps::State::removed && invisible.removals == 1, "failed creation validation can remove only an owned returned ped");
    } else if (scenario == L"post_create") {
        fake.renderable = false;
        advance(controller, fake);
        require(controller.state() == ps::State::cleanup_pending && controller.ped() == 0x1000 && !fake.removals, "retain successful allocation until next safe phase");
        controller.tick(fake, true, false, false, 3);
        require(!fake.removals, "defer cleanup when streaming/gameplay unavailable");
        controller.tick(fake, true, true, false, 4);
        for (unsigned i = 0; i < 1000; ++i) controller.tick(fake, true, true, false, i + 5);
        require(controller.state() == ps::State::removed && !controller.ped() && fake.removals == 1 && fake.creations == 1, "automatic owned cleanup is one-shot");
        ps::Experiment unknown;
        Fake unproven{&unknown}; unproven.owned = false;
        advance(unknown, unproven);
        unknown.tick(unproven, true, true, true, 4);
        require(unknown.state() == ps::State::ownership_lost && unknown.ped() == 0x1000 && !unproven.removals && unproven.creations == 1, "keep unknown creation result; never guess ownership or delete");
        ps::Experiment lostBeforeCleanup;
        Fake lost{&lostBeforeCleanup}; lost.renderable = false;
        advance(lostBeforeCleanup, lost);
        lost.owned = false;
        lostBeforeCleanup.tick(lost, true, true, false, 4);
        require(lostBeforeCleanup.state() == ps::State::ownership_lost && lostBeforeCleanup.ped() == 0x1000 && !lost.removals, "revalidate ownership before deferred cleanup");
        ps::Experiment removalFailed;
        Fake refuse{&removalFailed}; refuse.renderable = false; refuse.removeOk = false;
        advance(removalFailed, refuse);
        removalFailed.tick(refuse, true, true, false, 4);
        require(removalFailed.state() == ps::State::failed && removalFailed.ped() == 0x1000 && refuse.removals == 1, "failed cleanup retains ownership token and stops");
    } else if (scenario == L"wrong_thread") {
        controller.tick(fake, false, true, false, 0);
        require(controller.state() == ps::State::failed && !fake.requests && !fake.creations, "wrong thread cannot enter engine");
    } else if (scenario == L"cancel") {
        controller.tick(fake, true, true, true, 0);
        advance(controller, fake);
        require(controller.state() == ps::State::removed && !fake.creations && !fake.requests, "cancel before spawn prevents it permanently");
    } else throw std::runtime_error("unknown case");
}

void lifecycle(std::wstring_view scenario) {
    ps::Experiment controller;
    Fake fake{&controller};
    advance(controller, fake);
    if (scenario == L"lifecycle_loss") {
        fake.owned = false; // Simulates null registered reference OR another failed identity proof; no assumed cause.
        const auto checks = fake.ownershipChecks;
        controller.tick(fake, true, false, false, 3);
        require(fake.ownershipChecks == checks, "death/loading does not inspect an entity outside gameplay");
        const auto logs = fake.transitions;
        controller.tick(fake, true, true, false, 4);
        require(controller.state() == ps::State::ownership_lost && controller.terminal() && fake.transitions == logs + 1 &&
            fake.lastReason == "owned_ped_lost_no_respawn", "loss has one explicit terminal transition");
        fake.forbidOwnershipChecks = true;
        fake.owned = true; // Even a reused address / apparent later match cannot reacquire ownership.
        for (unsigned i = 0; i < 10000; ++i) {
            controller.tick(fake, true, true, true, i + 5); // F8 or repeated export requests.
            controller.fail();
        }
        require(controller.state() == ps::State::ownership_lost && fake.transitions == logs + 1 && fake.creations == 1 && !fake.removals,
            "F8/fail after loss is inert: no logging, dereference, delete or respawn");
    } else if (scenario == L"terminal_removal") {
        fake.removeOk = false;
        ps::PressEdge key;
        const auto logs = fake.transitions;
        controller.tick(fake, true, true, key.sample(true, true), 3);
        require(controller.state() == ps::State::failed && controller.terminal() && fake.removals == 1 && fake.transitions == logs + 1,
            "indeterminate removal refusal is terminal");
        fake.forbidOwnershipChecks = true;
        for (unsigned i = 0; i < 1000; ++i) controller.tick(fake, true, true, key.sample(true, true), i + 4);
        key.sample(false, true);
        controller.tick(fake, true, true, key.sample(true, true), 1005);
        controller.tick(fake, true, true, true, 1006); // Export also cannot bypass terminal failure.
        require(fake.removals == 1 && fake.transitions == logs + 1 && fake.creations == 1, "hold/repress/export after failed removal never retries");
    } else if (scenario == L"f8_edges") {
        ps::PressEdge key;
        require(!key.sample(false, true), "initial release is not a request");
        unsigned presses = 0;
        for (unsigned i = 0; i < 1000; ++i) {
            const bool pressed = key.sample(true, true);
            presses += unsigned(pressed);
            controller.tick(fake, true, true, pressed, i + 3);
        }
        require(presses == 1 && fake.removals == 1 && controller.state() == ps::State::removed, "held F8 removes an owned PED once");
        require(!key.sample(false, true) && key.sample(true, true), "release arms the next physical press");
        ps::PressEdge background;
        require(!background.sample(true, false) && !background.sample(true, true), "background/loading hold cannot become a request on recovery");
        background.sample(false, false);
        require(background.sample(true, true), "new eligible press works after background release");
    } else throw std::runtime_error("unknown lifecycle case");
}

__declspec(naked) void invokePhase() {
    using ps::phaseThunk;
    // Exercise the exact production thunk on Win32, with a disposable original.
    // Save the host state, seed the engine's registers/flags/x87/XMM, then make
    // the experimental body destroy volatile state before the original sees it.
    __asm {
        pushfd
        pushad
        fxsave savedFx
        fninit
        fld1
        pcmpeqd xmm0, xmm0
        pcmpeqd xmm1, xmm1
        pcmpeqd xmm2, xmm2
        pcmpeqd xmm3, xmm3
        pcmpeqd xmm4, xmm4
        pcmpeqd xmm5, xmm5
        pcmpeqd xmm6, xmm6
        pcmpeqd xmm7, xmm7
        fxsave beforeFx
        mov beforeStack, esp
        mov expectedReturn, offset returned
        mov eax, 11111111h
        mov ecx, 22222222h
        mov edx, 33333333h
        mov ebx, 44444444h
        mov ebp, 55555555h
        mov esi, 66666666h
        mov edi, 77777777h
        push 247h
        popfd
        call phaseThunk
    returned:
        mov afterStack, esp
        fxrstor savedFx
        popad
        popfd
        ret
    }
}
void phaseAbi() {
    invokePhase();
    require(bodyCalls == 1 && originalCalls == 1 && bodyOrder == 1 && originalOrder == 2, "body before exactly one original");
    require(bodyCaller == expectedReturn && originalReturn == expectedReturn, "native return address survives tail forwarding");
    require(beforeStack == afterStack && observedRegisters[7] == beforeStack - 4, "no stack drift / no extra return frame");
    for (unsigned i = 0; i < 7; ++i) require(observedRegisters[i] == 0x11111111U * (i + 1), "all GPRs restored before original");
    require((observedFlags & 0xCD5U) == (0x247U & 0xCD5U), "arithmetic and direction flags restored");
    require(std::memcmp(beforeFx, originalFx, 160) == 0 && std::memcmp(beforeFx + 160, originalFx + 160, 128) == 0,
        "x87 control/status/stack, MXCSR and all eight XMM registers restored");
    ps::CallBytes call{};
    require(ps::encodePhaseCall(ps::updateWeapons, call) && call == ps::originalCall, "exact Hoodlum CALL encoding and original target");
    require(!ps::encodePhaseCall(0xFFFFFFFFU, call), "reject unreachable rel32 target without writing");
}

struct MoveFake : Fake {
    ps::Position position{0.f, 0.f, 1.f}, destination{};
    unsigned assignments = 0, poseReads = 0;
    const char* assignFailure = nullptr;
    const char* poseFailure = nullptr;
    ps::Movement* nested = nullptr;
    std::vector<std::pair<std::string, std::string>> moves;
    void moveLog(const char* state, const char* reason) { moves.emplace_back(state, reason); }
    unsigned count(std::string_view state) const {
        unsigned result = 0;
        for (const auto& event : moves) result += event.first == state;
        return result;
    }
    const char* movePosition(std::uintptr_t, ps::Position& output) { ++poseReads; output = position; return poseFailure; }
    const char* moveDestination(std::uintptr_t, ps::Position origin, ps::Position& target) {
        target = {origin.x, origin.y + 4.f, origin.z}; destination = target; return nullptr;
    }
    const char* assignMove(std::uintptr_t, const ps::Position&) {
        ++assignments;
        if (nested) nested->tick(*this, *controller, true, true, true, false, 1);
        return assignFailure;
    }
};

void movementTest(std::wstring_view scenario) {
    ps::Experiment life;
    MoveFake fake; fake.controller = &life;
    advance(life, fake);
    ps::Movement move;
    auto tick = [&](bool request, std::uint64_t now, bool gameplay = true, bool remove = false, bool thread = true) {
        life.tick(fake, thread, gameplay, remove, now);
        move.tick(fake, life, thread, gameplay, request, remove, now);
    };
    if (scenario == L"move_command") {
        ps::PressEdge f7;
        fake.nested = &move;
        for (unsigned i = 0; i < 1000; ++i) tick(f7.sample(true, true), i + 3);
        f7.sample(false, true); tick(f7.sample(true, true), 1004);
        for (unsigned i = 0; i < 1000; ++i) tick(true, i + 1005);
        require(fake.assignments == 1 && fake.count("move_requested") == 1 && fake.count("move_task_assigned") == 1 &&
            fake.count("move_rejected") == 1 && !fake.count("move_arrived"), "hold/repress/reentry cannot submit a second task or claim arrival");
    } else if (scenario == L"move_arrival") {
        tick(true, 3);
        tick(false, 4);
        require(move.state() == ps::MoveState::assigned && !fake.count("move_started") && !fake.count("move_arrived"), "task acceptance is not movement");
        for (unsigned i = 1; i <= 8; ++i) { fake.position.y = i * 0.5f; tick(false, i * 100); }
        require(move.state() == ps::MoveState::moving && fake.count("move_started") == 1 && !fake.count("move_arrived"), "observe actual motion before arrival");
        tick(false, 900); tick(false, 1100);
        require(move.state() == ps::MoveState::arrived && fake.count("move_arrived") == 1, "arrival requires actual proximity and stationary interval");
        fake.forbidOwnershipChecks = true; // Movement does not access PED after arrival; lifetime still owns it.
        for (unsigned i = 0; i < 1000; ++i) move.tick(fake, life, true, true, false, false, i + 1200);
        require(fake.count("move_arrived") == 1, "arrival log one-shot");
        fake.forbidOwnershipChecks = false;
        tick(false, 2300, true, true);
        require(life.state() == ps::State::removed && fake.removals == 1, "normal F8 also works after arrival");
    } else if (scenario == L"move_loss") {
        tick(true, 3);
        fake.owned = false;
        tick(false, 4);
        const auto reads = fake.poseReads;
        fake.forbidOwnershipChecks = true;
        for (unsigned i = 0; i < 1000; ++i) tick(true, i + 5, true, true);
        require(life.state() == ps::State::ownership_lost && move.state() == ps::MoveState::failed && fake.poseReads == reads &&
            !fake.removals && fake.assignments == 1 && fake.count("move_failed") == 1, "lost identity terminates without inspecting/removing/reacquiring PED");
    } else if (scenario == L"move_removal") {
        tick(true, 3);
        fake.position.y = 0.5f; tick(false, 100);
        const auto reads = fake.poseReads;
        tick(true, 200, true, true); // Simultaneous F7/F8: removal wins.
        fake.forbidOwnershipChecks = true;
        for (unsigned i = 0; i < 1000; ++i) tick(true, i + 201);
        require(life.state() == ps::State::removed && fake.removals == 1 && fake.assignments == 1 && fake.poseReads == reads &&
            fake.count("move_failed") == 1 && !fake.count("move_arrived"), "F8 while moving removes normally; no later move query");
    } else if (scenario == L"move_gates") {
        ps::Experiment waiting;
        move.tick(fake, waiting, true, true, true, false, 0);
        move.tick(fake, life, false, true, true, false, 1);
        move.tick(fake, life, true, false, true, false, 2);
        require(!fake.assignments && !fake.poseReads, "no task outside owned creation, correct thread and admitted gameplay");
        ps::PressEdge key;
        require(!key.sample(true, false) && !key.sample(true, true), "F7 held across loading/focus recovery is not a new command");
        key.sample(false, true); tick(key.sample(true, true), 3);
        require(fake.assignments == 1, "new eligible edge works");
        constexpr bool moveBuild =
#if defined(GRANDMP_PED_MOVE_PHYSICAL_BUILD) && GRANDMP_PED_MOVE_PHYSICAL_BUILD == 1
            true;
#else
            false;
#endif
        require(ps::movementExecutionApproved == (ps::physicalExecutionApproved && moveBuild), "movement requires both build gates");
    } else if (scenario == L"move_failures") {
        fake.assignFailure = "task_not_retained_by_manager_no_retry_no_delete";
        tick(true, 3);
        for (unsigned i = 0; i < 1000; ++i) tick(true, i + 4);
        require(fake.assignments == 1 && move.state() == ps::MoveState::failed && fake.count("move_failed") == 1 &&
            fake.moves[1].second == fake.assignFailure, "failed transfer is not retried or interpreted as arrival");
        tick(false, 1005, true, true);
        require(fake.removals == 1, "F8 survives task failure");
        ps::Experiment another; MoveFake stalled; stalled.controller = &another; advance(another, stalled);
        ps::Movement timeout; timeout.tick(stalled, another, true, true, true, false, 0);
        timeout.tick(stalled, another, true, true, false, false, 20001);
        require(timeout.state() == ps::MoveState::failed && stalled.moves.back().second == "arrival_timeout_no_task_retry_use_F8", "no actual progress fails once");
        ps::Movement nonfinite; stalled.position.x = std::numeric_limits<float>::infinity();
        nonfinite.tick(stalled, another, true, true, true, false, 0);
        require(nonfinite.state() == ps::MoveState::failed && stalled.assignments == 1, "nonfinite read cannot submit task");
        ps::Movement jump; stalled.position = {0, 0, 1};
        jump.tick(stalled, another, true, true, true, false, 0);
        stalled.position.y = 4;
        jump.tick(stalled, another, true, true, false, false, 1);
        require(jump.state() == ps::MoveState::failed && stalled.moves.back().second == "position_discontinuity", "jump to target is not native walking proof");
        ps::Movement drift; stalled.position = {0, 0, 1};
        drift.tick(stalled, another, true, true, true, false, 0);
        for (unsigned i = 1; i <= 8; ++i) {
            stalled.position.y = i * 0.5f; drift.tick(stalled, another, true, true, false, false, i * 100);
        }
        for (unsigned i = 1; i <= 60; ++i) {
            stalled.position.y = 4.f + i * 0.005f;
            drift.tick(stalled, another, true, true, false, false, 800 + i * 10);
        }
        require(drift.state() == ps::MoveState::moving, "continuous small frame steps cannot satisfy stationary arrival");
    } else throw std::runtime_error("unknown movement case");
}

void movementGeometry() {
    PositionProbe probe;
    ps::Position target{};
    require(!ps::findMoveDestination(probe, probe.origin, {0.6f, 0.8f, 0.f}, target) && probe.groundCalls == 8 && probe.sphereCalls == 8 &&
        std::abs(ps::distance2D(probe.origin, target) - 4.f) < 0.0001f, "normalized forward, 4m target, eight ground and collision samples");
    probe.hit = 0x1234;
    require(std::string_view(ps::findMoveDestination(probe, probe.origin, probe.right, target)) == "corridor_sphere_hit", "collision (including CJ) rejects corridor");
    probe.hit = 0; probe.found = false;
    require(std::string_view(ps::findMoveDestination(probe, probe.origin, probe.right, target)) == "ground_not_found", "missing ground fails closed");
    probe.found = true; probe.groundZ = 12.f;
    require(std::string_view(ps::findMoveDestination(probe, probe.origin, probe.right, target)) == "ground_step_or_height_limit", "height discontinuity fails closed");
    probe.groundZ = std::numeric_limits<float>::quiet_NaN();
    require(std::string_view(ps::findMoveDestination(probe, probe.origin, probe.right, target)) == "ground_nonfinite", "NaN ground refused");
    require(std::string_view(ps::findMoveDestination(probe, probe.origin, {0, 0, 0}, target)) == "forward_norm_range", "zero forward refused");
}

void* abiMemory = nullptr;
ps::Position abiTarget{};
unsigned abiConstructs = 0, abiTransfers = 0;
void* __fastcall fakeWalkConstructor(void* memory, void*, int state, const ps::Position& target, float radius, float slow, bool overshoot, bool exact) {
    require(memory == abiMemory && state == 4 && radius == 0.5f && slow == 0.f && !overshoot && exact, "six native constructor arguments / ECX this");
    abiTarget = target; ++abiConstructs; return memory;
}
void __fastcall fakeSetTask(void* manager, void*, void* task, int slot, bool flag) {
    require(manager == &abiTarget && task == abiMemory && slot == 3 && !flag, "SetTask ECX manager and three original arguments");
    ++abiTransfers;
}
void movementAbi() {
    namespace mn = ps::movement_native;
    std::array<std::byte, mn::taskSize> storage{}; abiMemory = storage.data();
    const ps::Position target{1, 2, 3};
    unsigned before = 0, after = 0;
    __asm mov before, esp
    for (unsigned i = 0; i < 1000; ++i) {
        void* task = mn::construct(reinterpret_cast<mn::Construct>(&fakeWalkConstructor), abiMemory, target);
        require(task == abiMemory && abiTarget.x == 1 && abiTarget.y == 2 && abiTarget.z == 3, "constructor returns original allocation, const reference ABI");
        mn::transfer(reinterpret_cast<mn::SetTask>(&fakeSetTask), &abiTarget, task);
    }
    __asm mov after, esp
    require(before == after && abiConstructs == 1000 && abiTransfers == 1000, "Win32 calls preserve stack across ret18/ret0C");
    require(mn::allocateTask == 0x61A5A0 && mn::constructWalk == 0x668120 && mn::setTask == 0x681AF0 && mn::taskSize == 0x28 &&
        mn::taskStride == 0x80 && mn::walkVtable == 0x86FDA8, "Hoodlum task contracts");
}

void installDiagnostics() {
    using ps::InstallDiagnostic;
    using ps::InstallFailure;
    using ps::InstallOutcome;
    require(ps::f6WaitResult(false).failure == InstallFailure::f6_timeout_120s &&
        ps::f6WaitResult(true).failure == InstallFailure::none,
        "F6 observation deterministically distinguishes timeout from an initiated attempt");
    require(std::string_view(ps::installFailureReason(InstallFailure::f6_timeout_120s)) == "f6_timeout_120s",
        "120-second F6 timeout has a distinct diagnostic");
    require(std::string_view(ps::installFailureReason(InstallFailure::window_thread_unavailable)) == "window_thread_unavailable" &&
        std::string_view(ps::installFailureReason(InstallFailure::module_pin_failed)) == "module_pin_failed" &&
        std::string_view(ps::installFailureReason(InstallFailure::set_windows_hook_failed)) == "set_windows_hook_failed" &&
        std::string_view(ps::installFailureReason(InstallFailure::post_thread_message_failed)) == "post_thread_message_failed" &&
        std::string_view(ps::installFailureReason(InstallFailure::callback_timeout_5000ms)) == "callback_timeout_5000ms" &&
        std::string_view(ps::installFailureReason(InstallFailure::unhook_failed)) == "bootstrap_unhook_failed_module_pinned",
        "bootstrap stages have distinct diagnostic reasons");
    require(std::string_view(ps::installFailureReason(InstallFailure::candidate_player_not_ready)) ==
        "install_candidate_player_not_ready" &&
        std::string_view(ps::installFailureReason(InstallFailure::candidate_streaming_not_ready)) ==
        "install_candidate_streaming_not_ready" &&
        std::string_view(ps::installFailureReason(InstallFailure::candidate_code_identity_mismatch)) ==
        "install_candidate_code_identity_mismatch" &&
        std::string_view(ps::installFailureReason(InstallFailure::candidate_call_encoding_failed)) ==
        "install_candidate_call_encoding_failed" &&
        std::string_view(ps::installFailureReason(InstallFailure::candidate_virtual_protect_failed)) ==
        "install_candidate_virtual_protect_failed" &&
        std::string_view(ps::installFailureReason(InstallFailure::candidate_unexpected_page_protection)) ==
        "install_candidate_unexpected_page_protection" &&
        std::string_view(ps::installFailureReason(InstallFailure::candidate_instruction_cache_flush_failed)) ==
        "install_candidate_instruction_cache_flush_failed_module_pinned_no_engine_dispatch" &&
        std::string_view(ps::installFailureReason(InstallFailure::candidate_protection_restore_failed)) ==
        "install_candidate_protection_restore_failed_module_pinned_no_engine_dispatch" &&
        std::string_view(ps::installFailureReason(InstallFailure::candidate_post_publish_code_mismatch)) ==
        "install_candidate_post_publish_code_mismatch_module_pinned_no_engine_dispatch",
        "candidate rejection identifies its failed guard and CALL publication verification");
    const InstallDiagnostic apiFailure{InstallFailure::post_thread_message_failed, 5, true};
    require(apiFailure.hasWin32Error && apiFailure.win32Error == 5,
        "Win32 failure retains the error captured at the API call site");
    char formatted[128]{};
    require(ps::formatInstallDiagnostic(formatted, sizeof(formatted), apiFailure) &&
        std::string_view(formatted) == "post_thread_message_failed win32_error=5",
        "Win32 API failure renders its captured error in the later worker diagnostic");
    char tooSmall[8]{};
    require(!ps::formatInstallDiagnostic(tooSmall, sizeof(tooSmall), apiFailure) && tooSmall[sizeof(tooSmall)-1] == '\0',
        "bootstrap diagnostic formatting rejects truncation safely");
    InstallOutcome callbackSucceededAfterPostWarning{};
    callbackSucceededAfterPostWarning.bootstrapWarning = apiFailure;
    require(callbackSucceededAfterPostWarning.succeeded(),
        "a callback already observed remains successful if its extra wake-message post failed");
    callbackSucceededAfterPostWarning.primary.failure = InstallFailure::candidate_player_not_ready;
    require(!callbackSucceededAfterPostWarning.succeeded(), "candidate rejection remains failed");
    callbackSucceededAfterPostWarning.primary.failure = InstallFailure::none;
    callbackSucceededAfterPostWarning.primary = {InstallFailure::candidate_instruction_cache_flush_failed, 31, true};
    callbackSucceededAfterPostWarning.candidateCleanup = {InstallFailure::candidate_protection_restore_failed, 5, true};
    callbackSucceededAfterPostWarning.unhookFailure = {InstallFailure::unhook_failed, 6, true};
    require(!callbackSucceededAfterPostWarning.succeeded() &&
        callbackSucceededAfterPostWarning.primary.failure == InstallFailure::candidate_instruction_cache_flush_failed &&
        callbackSucceededAfterPostWarning.primary.win32Error == 31 &&
        callbackSucceededAfterPostWarning.candidateCleanup.failure == InstallFailure::candidate_protection_restore_failed &&
        callbackSucceededAfterPostWarning.candidateCleanup.win32Error == 5 &&
        callbackSucceededAfterPostWarning.unhookFailure.failure == InstallFailure::unhook_failed &&
        callbackSucceededAfterPostWarning.unhookFailure.win32Error == 6,
        "primary candidate failure, protection-restore cleanup, and bootstrap unhook failure coexist without masking");
    char primaryText[128]{};
    char cleanupText[128]{};
    char unhookText[128]{};
    require(ps::formatInstallDiagnostic(primaryText, sizeof(primaryText), callbackSucceededAfterPostWarning.primary) &&
        ps::formatInstallDiagnostic(cleanupText, sizeof(cleanupText), callbackSucceededAfterPostWarning.candidateCleanup) &&
        ps::formatInstallDiagnostic(unhookText, sizeof(unhookText), callbackSucceededAfterPostWarning.unhookFailure) &&
        std::string_view(primaryText) == "install_candidate_instruction_cache_flush_failed_module_pinned_no_engine_dispatch win32_error=31" &&
        std::string_view(cleanupText) == "install_candidate_protection_restore_failed_module_pinned_no_engine_dispatch win32_error=5" &&
        std::string_view(unhookText) == "bootstrap_unhook_failed_module_pinned win32_error=6",
        "primary, page-protection restore, and Unhook failures remain individually reportable");
}

void initialization(const wchar_t* path, bool enabled = false) {
    require(ps::physicalExecutionApproved == enabled, "test build gate must match the requested artifact");
    SetEnvironmentVariableW(L"GRANDMP_PED_SMOKE_ENABLE", L"1"); // Alone cannot override the build/identity gates.
    HMODULE module = LoadLibraryW(path);
    require(module != nullptr, "load new isolated ASI in disposable process");
    using Entry = void(__cdecl*)();
    using Query = unsigned(__cdecl*)();
    const auto entry = reinterpret_cast<Entry>(GetProcAddress(module, "InitializeASI"));
    const auto status = reinterpret_cast<Query>(GetProcAddress(module, "GrandMPPedSmokeStatus"));
    const auto count = reinterpret_cast<Query>(GetProcAddress(module, "GrandMPPedSmokeCallbackCount"));
    const auto remove = reinterpret_cast<Entry>(GetProcAddress(module, "GrandMPPedSmokeRequestRemoval"));
    require(entry && status && count && remove && status() == 0 && count() == 0, "DllMain is inert");
    std::vector<std::thread> callers;
    for (unsigned i = 0; i < 16; ++i) callers.emplace_back([&] { entry(); remove(); });
    for (auto& thread : callers) thread.join();
    const auto deadline = GetTickCount64() + 5000;
    while (status() == 1 && GetTickCount64() < deadline) Sleep(1);
    const unsigned expectedStatus = enabled ? 4 : 2;
    require(status() == expectedStatus && count() == 0,
        "default stays closed; enabled artifact rejects disposable host identity/path without dispatch");
    entry(); entry();
    require(status() == expectedStatus && count() == 0, "duplicate initialization is inert");
    std::ifstream log(std::filesystem::path(path).parent_path() / "GrandMPPedSmoke.log");
    const auto pid = std::string(" pid=") + std::to_string(GetCurrentProcessId()) + " ";
    bool diagnosed = false;
    for (std::string line; std::getline(log, line);) {
        if (line.find(pid) != std::string::npos && line.find(enabled ?
            "reason=hoodlum_identity_path_or_live_signature" :
            "reason=installation_blocked_physical_execution_review_missing") != std::string::npos) diagnosed = true;
    }
    require(diagnosed, "actual artifact must diagnose its build/identity refusal for this host PID");
    require(FreeLibrary(module) != FALSE, "release disposable loader reference");
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 2;
    try {
        if (std::wstring_view(argv[1]) == L"initialization" || std::wstring_view(argv[1]) == L"initialization_enabled") {
            if (argc != 3) return 2;
            initialization(argv[2], std::wstring_view(argv[1]) == L"initialization_enabled");
        } else if (std::wstring_view(argv[1]) == L"move_abi") movementAbi();
        else if (std::wstring_view(argv[1]) == L"move_geometry") movementGeometry();
        else if (std::wstring_view(argv[1]).starts_with(L"move_")) movementTest(argv[1]);
        else if (std::wstring_view(argv[1]) == L"phase_abi") phaseAbi();
        else if (std::wstring_view(argv[1]) == L"install_diagnostics") installDiagnostics();
        else if (std::wstring_view(argv[1]) == L"spawn_diagnostics") spawnDiagnostics(false);
        else if (std::wstring_view(argv[1]) == L"spawn_nonfinite") spawnDiagnostics(true);
        else if (std::wstring_view(argv[1]) == L"lifecycle_loss" || std::wstring_view(argv[1]) == L"terminal_removal" ||
            std::wstring_view(argv[1]) == L"f8_edges") lifecycle(argv[1]);
        else states(argv[1]);
        require(ps::phaseCall == 0x53C05C && ps::updateWeapons == 0x73A360 && ps::phaseCall != 0x53E981, "only the post-population phase is admitted");
        std::printf("ped_smoke=%ls pass physical_gate=%s\n", argv[1], ps::physicalExecutionApproved ? "enabled" : "closed");
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "ped smoke test: %s\n", error.what()); return 1; }
}
