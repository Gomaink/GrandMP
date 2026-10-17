#include "grandmp/core/remote_player_adapter.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
using namespace std::chrono_literals;
using grandmp::core::AdapterState;
using grandmp::core::IRemotePlayerEntityBackend;
using grandmp::core::NativeCommandKind;
using grandmp::core::QueuedRemotePlayerAdapter;
using grandmp::core::RemotePlayerScene;
using grandmp::core::RemotePose;
using grandmp::core::RemoteRoster;

class SimulatedBackend final : public IRemotePlayerEntityBackend {
public:
    bool ready = true;
    bool allowCreate = true;
    bool allowUpdate = true;
    bool allowRemove = true;
    bool allowRemoveAll = true;
    std::size_t createAttempts = 0;
    std::size_t updateAttempts = 0;
    std::size_t removeAttempts = 0;
    std::map<std::uint64_t, RemotePose> entities;
    std::vector<std::string> events;
    std::thread::id calledFrom{};
    QueuedRemotePlayerAdapter* adapter = nullptr;
    bool reenterOnCreate = false;
    bool resetBeforeReentrantSpawn = false;
    bool nestedPumpReturnedZero = false;

    bool available() const noexcept override { return ready; }
    bool create(const RemotePose& pose) noexcept override {
        ++createAttempts;
        calledFrom = std::this_thread::get_id();
        events.emplace_back("create");
        if (reenterOnCreate && adapter) {
            reenterOnCreate = false;
            if (resetBeforeReentrantSpawn) {
                resetBeforeReentrantSpawn = false;
                adapter->resetAll();
                adapter->spawn(RemotePose{pose.id, pose.sequence + 1,
                                          pose.x + 1, pose.y, pose.z});
            } else {
                adapter->move(RemotePose{pose.id, pose.sequence + 1,
                                         pose.x + 1, pose.y, pose.z});
            }
            nestedPumpReturnedZero = adapter->pump() == 0;
        }
        if (!allowCreate) return false;
        entities[pose.id] = pose;
        return true;
    }
    bool update(const RemotePose& pose) noexcept override {
        ++updateAttempts;
        calledFrom = std::this_thread::get_id();
        events.emplace_back("update");
        if (!allowUpdate || !entities.contains(pose.id)) return false;
        entities[pose.id] = pose;
        return true;
    }
    bool remove(std::uint64_t id) noexcept override {
        ++removeAttempts;
        calledFrom = std::this_thread::get_id();
        events.emplace_back("remove");
        if (!allowRemove) return false;
        entities.erase(id);
        return true;
    }
    bool removeAll() noexcept override {
        calledFrom = std::this_thread::get_id();
        events.emplace_back("remove_all");
        if (!allowRemoveAll) return false;
        entities.clear();
        return true;
    }
};

bool sceneLifecycle() {
    SimulatedBackend backend;
    QueuedRemotePlayerAdapter adapter(backend);
    RemoteRoster roster;
    RemotePlayerScene scene;
    const auto now = RemoteRoster::Time{};
    if (!adapter.initialize()) return false;
    if (roster.position({17, 1, 10, 20, 30}, now) ==
        RemoteRoster::PositionResult::ignored) return false;
    scene.synchronize(roster, now, adapter);
    if (adapter.pending() != 1 || adapter.pump() != 1) return false;
    if (roster.position({17, 2, 12, 20, 30}, now + 200ms) ==
        RemoteRoster::PositionResult::ignored) return false;
    scene.synchronize(roster, now + 200ms, adapter);
    scene.synchronize(roster, now + 290ms, adapter);
    scene.synchronize(roster, now + 380ms, adapter);
    if (adapter.pending() != 1 || adapter.pump() != 1) return false;
    if (backend.entities.size() != 1 || backend.entities.at(17).x != 12) return false;
    if (!roster.left({17}, now + 400ms)) return false;
    scene.synchronize(roster, now + 400ms, adapter);
    if (adapter.pump() != 1 || !backend.entities.empty()) return false;
    return backend.events == std::vector<std::string>{"create", "update", "remove"};
}

bool despawnBeforeSpawnAndRespawnOrdering() {
    SimulatedBackend backend;
    QueuedRemotePlayerAdapter adapter(backend);
    if (!adapter.initialize()) return false;
    adapter.despawn(4);
    adapter.spawn({4, 1, 1, 2, 3});
    if (adapter.pending() != 1 || adapter.pump() != 1) return false;
    if (!backend.entities.contains(4)) return false;
    adapter.despawn(4);
    adapter.spawn({4, 2, 9, 8, 7});
    if (adapter.pending() != 2 || adapter.pump() != 2) return false;
    return backend.events == std::vector<std::string>{"create", "remove", "create"} &&
           backend.entities.at(4).x == 9;
}

bool resetSessionAndTeleport() {
    SimulatedBackend backend;
    QueuedRemotePlayerAdapter adapter(backend);
    RemoteRoster roster;
    RemotePlayerScene scene;
    const auto now = RemoteRoster::Time{};
    if (!adapter.initialize()) return false;
    if (roster.position({7, 1, 0, 0, 0}, now) ==
        RemoteRoster::PositionResult::ignored) return false;
    scene.synchronize(roster, now, adapter);
    if (adapter.pump() != 1) return false;
    if (roster.position({7, 2, 100, 0, 0}, now + 200ms) ==
        RemoteRoster::PositionResult::ignored) return false;
    scene.synchronize(roster, now + 200ms, adapter);
    if (adapter.pending() != 1 || adapter.pump() != 1) return false;
    if (backend.entities.at(7).x != 100) return false;
    scene.reset(adapter);
    roster.clear();
    adapter.resetAll();
    if (roster.position({7, 1, 8, 8, 8}, now + 500ms) ==
        RemoteRoster::PositionResult::ignored) return false;
    scene.synchronize(roster, now + 500ms, adapter);
    if (adapter.pending() != 2 || adapter.pump() != 2 ||
        backend.entities.size() != 1 || !backend.entities.contains(7)) return false;
    if (roster.position({700, 1, 1, 1, 1}, now + 1s) ==
        RemoteRoster::PositionResult::ignored) return false;
    scene.synchronize(roster, now + 1s, adapter);
    // Scene also emits an existing same-session identity above; after reset it
    // must be recreated, not updated after remove_all.
    return adapter.pump() == 1 && backend.entities.size() == 2 &&
           backend.entities.contains(7) && backend.entities.contains(700);
}

bool unavailableAndCreateRetry() {
    SimulatedBackend backend;
    backend.ready = false;
    backend.allowCreate = false;
    QueuedRemotePlayerAdapter adapter(backend);
    if (!adapter.initialize()) return false;
    adapter.spawn({1, 1, 1, 2, 3});
    if (adapter.pump() != 0 || adapter.isAvailable() || adapter.pending() != 1) return false;
    backend.ready = true;
    if (adapter.pump() != 0 || !adapter.isAvailable() || adapter.pending() != 1) return false;
    backend.allowCreate = true;
    return adapter.pump() == 1 && adapter.pending() == 0 && backend.entities.contains(1);
}

bool shutdownDropsPendingAndCleans() {
    SimulatedBackend backend;
    QueuedRemotePlayerAdapter adapter(backend);
    if (!adapter.initialize()) return false;
    adapter.spawn({1, 1, 1, 2, 3});
    adapter.shutdown();
    adapter.move({1, 2, 4, 5, 6});
    if (adapter.state() != AdapterState::stopping || adapter.pending() != 0) return false;
    if (adapter.pump() != 1 || adapter.state() != AdapterState::stopped) return false;
    return backend.entities.empty() && adapter.pump() == 0;
}

bool ownerThreadAndReentrancy() {
    SimulatedBackend backend;
    QueuedRemotePlayerAdapter adapter(backend);
    if (!adapter.initialize()) return false;
    backend.adapter = &adapter;
    backend.reenterOnCreate = true;
    adapter.spawn({9, 1, 10, 20, 30});
    const auto owner = std::this_thread::get_id();
    if (adapter.pump() != 2 || !backend.nestedPumpReturnedZero) return false;
    if (backend.calledFrom != owner || backend.entities.at(9).sequence != 2) return false;
    std::size_t workerPump = 99;
    std::thread worker([&] { workerPump = adapter.pump(); });
    worker.join();
    return workerPump == 0 && backend.calledFrom == owner;
}

bool resetAndRespawnReentrancy() {
    SimulatedBackend backend;
    QueuedRemotePlayerAdapter adapter(backend);
    if (!adapter.initialize()) return false;
    backend.adapter = &adapter;
    backend.reenterOnCreate = true;
    backend.resetBeforeReentrantSpawn = true;
    adapter.spawn({41, 1, 10, 20, 30});
    if (adapter.pump() != 3 || !backend.nestedPumpReturnedZero) return false;
    return backend.events == std::vector<std::string>{"create", "remove_all", "create"} &&
           backend.entities.size() == 1 && backend.entities.at(41).sequence == 2;
}

bool concurrentProducers() {
    SimulatedBackend backend;
    QueuedRemotePlayerAdapter adapter(backend);
    if (!adapter.initialize()) return false;
    constexpr std::uint64_t producerCount = 4;
    std::vector<std::thread> producers;
    for (std::uint64_t producer = 0; producer < producerCount; ++producer) {
        producers.emplace_back([&, producer] {
            const auto id = 100 + producer;
            adapter.spawn({id, 1, static_cast<float>(id), 0, 0});
            adapter.move({id, 2, static_cast<float>(id + 1), 1, 0});
        });
    }
    for (auto& producer : producers) producer.join();
    const auto owner = std::this_thread::get_id();
    if (adapter.pending() != producerCount || adapter.pump() != producerCount) return false;
    if (backend.entities.size() != producerCount || backend.calledFrom != owner) return false;
    for (std::uint64_t producer = 0; producer < producerCount; ++producer) {
        const auto& pose = backend.entities.at(100 + producer);
        if (pose.sequence != 2 || pose.x != static_cast<float>(101 + producer)) return false;
    }
    return true;
}

bool queueFullFailsClosed() {
    SimulatedBackend backend;
    QueuedRemotePlayerAdapter adapter(backend);
    if (!adapter.initialize()) return false;
    for (std::size_t i = 0; i < QueuedRemotePlayerAdapter::queueCapacity; ++i)
        adapter.spawn({static_cast<std::uint64_t>(i + 1), 1, 0, 0, 0});
    if (adapter.pending() != QueuedRemotePlayerAdapter::queueCapacity) return false;
    adapter.spawn({10000, 1, 0, 0, 0});
    if (adapter.state() != AdapterState::stopping || adapter.pending() != 0) return false;
    return adapter.pump() == 1 && adapter.state() == AdapterState::stopped &&
           backend.entities.empty() && !backend.events.empty() &&
           backend.events.back() == "remove_all";
}

bool entityLimitFailsClosed() {
    SimulatedBackend backend;
    QueuedRemotePlayerAdapter adapter(backend);
    if (!adapter.initialize()) return false;
    for (std::size_t i = 0; i <= QueuedRemotePlayerAdapter::maxMaterializedPlayers; ++i)
        adapter.spawn({static_cast<std::uint64_t>(i + 1), 1, 0, 0, 0});
    if (adapter.pending() != QueuedRemotePlayerAdapter::maxMaterializedPlayers + 1) return false;
    if (adapter.pump(QueuedRemotePlayerAdapter::queueCapacity) !=
        QueuedRemotePlayerAdapter::maxMaterializedPlayers) return false;
    if (adapter.state() != AdapterState::stopping ||
        backend.entities.size() != QueuedRemotePlayerAdapter::maxMaterializedPlayers) return false;
    return adapter.pump() == 1 && adapter.state() == AdapterState::stopped &&
           backend.entities.empty();
}

bool createFailureCanReset() {
    SimulatedBackend backend;
    backend.allowCreate = false;
    QueuedRemotePlayerAdapter adapter(backend);
    if (!adapter.initialize()) return false;
    adapter.spawn({3, 1, 0, 0, 0});
    if (adapter.pump() != 0 || adapter.pending() != 1) return false;
    adapter.resetAll();
    if (adapter.pending() != 1) return false;
    return adapter.pump() == 1 && adapter.pending() == 0 && backend.entities.empty();
}

bool persistentCreateFailureFailsClosedAndCleans() {
    SimulatedBackend backend;
    backend.allowCreate = false;
    QueuedRemotePlayerAdapter adapter(backend);
    if (!adapter.initialize()) return false;
    adapter.spawn({1, 1, 1, 2, 3});
    adapter.spawn({2, 1, 4, 5, 6});
    for (std::size_t i = 0; i < QueuedRemotePlayerAdapter::maxConsecutiveBackendFailures; ++i) {
        if (adapter.pump() != 0) return false;
    }
    if (backend.createAttempts != QueuedRemotePlayerAdapter::maxConsecutiveBackendFailures ||
        adapter.state() != AdapterState::stopping || adapter.pending() != 0) return false;
    // Recovery is explicit and owner-thread cleanup removes any earlier materialization.
    backend.allowCreate = true;
    return adapter.pump() == 1 && adapter.state() == AdapterState::stopped &&
           backend.entities.empty() && backend.events.back() == "remove_all";
}

bool persistentUpdateFailureFailsClosedAndCleans() {
    SimulatedBackend backend;
    QueuedRemotePlayerAdapter adapter(backend);
    if (!adapter.initialize()) return false;
    adapter.spawn({1, 1, 1, 2, 3});
    adapter.spawn({2, 1, 4, 5, 6});
    if (adapter.pump(2) != 2 || backend.entities.size() != 2) return false;
    backend.allowUpdate = false;
    adapter.move({1, 2, 7, 8, 9});
    adapter.despawn(2);  // This structural operation must not be blocked forever.
    for (std::size_t i = 0; i < QueuedRemotePlayerAdapter::maxConsecutiveBackendFailures; ++i) {
        if (adapter.pump() != 0) return false;
    }
    if (backend.updateAttempts != QueuedRemotePlayerAdapter::maxConsecutiveBackendFailures ||
        adapter.state() != AdapterState::stopping || adapter.pending() != 0) return false;
    return adapter.pump() == 1 && adapter.state() == AdapterState::stopped &&
           backend.entities.empty() && backend.events.back() == "remove_all";
}

bool shutdownWaitsForAvailabilityThenCleans() {
    SimulatedBackend backend;
    QueuedRemotePlayerAdapter adapter(backend);
    if (!adapter.initialize()) return false;
    adapter.spawn({1, 1, 1, 2, 3});
    if (adapter.pump() != 1 || backend.entities.size() != 1) return false;
    backend.ready = false;
    adapter.shutdown();
    if (adapter.pump() != 0 || adapter.state() != AdapterState::stopping ||
        backend.entities.size() != 1) return false;
    backend.ready = true;
    return adapter.pump() == 1 && adapter.state() == AdapterState::stopped &&
           backend.entities.empty();
}

bool persistentCleanupFailureIsExplicitAndRecoverable() {
    SimulatedBackend backend;
    QueuedRemotePlayerAdapter adapter(backend);
    if (!adapter.initialize()) return false;
    adapter.spawn({1, 1, 1, 2, 3});
    if (adapter.pump() != 1 || backend.entities.size() != 1) return false;
    backend.allowRemoveAll = false;
    adapter.shutdown();
    for (std::size_t i = 0; i < QueuedRemotePlayerAdapter::maxConsecutiveBackendFailures; ++i)
        if (adapter.pump() != 0) return false;
    if (adapter.state() != AdapterState::cleanup_failed || backend.entities.size() != 1)
        return false;
    if (adapter.pump() != 0 || !adapter.retryCleanup() || adapter.retryCleanup()) return false;
    backend.allowRemoveAll = true;
    return adapter.pump() == 1 && adapter.state() == AdapterState::stopped &&
           backend.entities.empty();
}

bool persistentEntityRemovalFailsClosed() {
    SimulatedBackend backend;
    QueuedRemotePlayerAdapter adapter(backend);
    if (!adapter.initialize()) return false;
    adapter.spawn({1, 1, 1, 2, 3});
    if (adapter.pump() != 1) return false;
    backend.allowRemove = false;
    adapter.despawn(1);
    for (std::size_t i = 0; i < QueuedRemotePlayerAdapter::maxConsecutiveBackendFailures; ++i)
        if (adapter.pump() != 0) return false;
    if (backend.removeAttempts != QueuedRemotePlayerAdapter::maxConsecutiveBackendFailures ||
        adapter.state() != AdapterState::stopping) return false;
    return adapter.pump() == 1 && adapter.state() == AdapterState::stopped &&
           backend.entities.empty();
}

bool transientEntityRemovalRecovers() {
    SimulatedBackend backend;
    QueuedRemotePlayerAdapter adapter(backend);
    if (!adapter.initialize()) return false;
    adapter.spawn({1, 1, 1, 2, 3});
    if (adapter.pump() != 1) return false;
    backend.allowRemove = false;
    adapter.despawn(1);
    if (adapter.pump() != 0 || !backend.entities.contains(1)) return false;
    backend.allowRemove = true;
    return adapter.pump() == 1 && adapter.state() == AdapterState::ready &&
           backend.entities.empty();
}

class UnlockedBackend final : public IRemotePlayerEntityBackend {
public:
    QueuedRemotePlayerAdapter* adapter = nullptr;
    std::map<std::uint64_t, RemotePose> entities;
    std::thread producer;
    std::mutex mutex;
    std::condition_variable changed;
    bool producerCompletedDuringCreate = false;
    std::size_t updateCalls = 0;

    bool available() const noexcept override { return true; }
    bool create(const RemotePose& pose) noexcept override {
        producer = std::thread([this, pose] {
            adapter->move(RemotePose{pose.id, pose.sequence + 1, pose.x + 1, pose.y, pose.z});
            std::lock_guard lock(mutex);
            producerCompletedDuringCreate = true;
            changed.notify_all();
        });
        {
            std::unique_lock lock(mutex);
            (void)changed.wait_for(lock, std::chrono::seconds(2),
                                   [&] { return producerCompletedDuringCreate; });
        }
        entities[pose.id] = pose;
        return true;
    }
    bool update(const RemotePose& pose) noexcept override {
        ++updateCalls;
        const auto found = entities.find(pose.id);
        if (found == entities.end()) return false;
        found->second = pose;
        return true;
    }
    bool remove(std::uint64_t id) noexcept override { entities.erase(id); return true; }
    bool removeAll() noexcept override { entities.clear(); return true; }
};

bool backendCallsRunWithoutAdapterLock() {
    UnlockedBackend backend;
    QueuedRemotePlayerAdapter adapter(backend);
    backend.adapter = &adapter;
    if (!adapter.initialize() || !adapter.bindOwnerThread()) return false;
    adapter.spawn({41, 1, 10, 20, 30});
    const auto processed = adapter.pump();
    if (backend.producer.joinable()) backend.producer.join();
    if (!backend.producerCompletedDuringCreate || processed != 2 || backend.updateCalls != 1 ||
        !backend.entities.contains(41) || backend.entities.at(41).sequence != 2 ||
        backend.entities.at(41).x != 11) return false;
    std::size_t wrongThreadPump = 1;
    std::thread wrongThread([&] { wrongThreadPump = adapter.pump(); });
    wrongThread.join();
    if (wrongThreadPump != 0) return false;
    adapter.shutdown();
    return adapter.pump() == 1 && backend.entities.empty() &&
           adapter.state() == AdapterState::stopped;
}

class LifecycleRaceBackend final : public IRemotePlayerEntityBackend {
public:
    enum class BlockOn { none, create, remove };

    std::map<std::uint64_t, RemotePose> entities;
    std::vector<std::string> events;
    std::mutex gateMutex;
    std::condition_variable gateChanged;
    BlockOn blockOn = BlockOn::none;
    std::uint64_t blockedId = 0;
    bool entered = false;
    bool released = false;

    bool available() const noexcept override { return true; }
    bool create(const RemotePose& pose) noexcept override {
        if (!block(BlockOn::create, pose.id)) return false;
        entities[pose.id] = pose;
        events.emplace_back("create");
        return true;
    }
    bool update(const RemotePose& pose) noexcept override {
        const auto found = entities.find(pose.id);
        if (found == entities.end()) return false;
        found->second = pose;
        events.emplace_back("update");
        return true;
    }
    bool remove(std::uint64_t id) noexcept override {
        if (!block(BlockOn::remove, id)) return false;
        entities.erase(id);
        events.emplace_back("remove");
        return true;
    }
    bool removeAll() noexcept override { entities.clear(); return true; }

    void configure(BlockOn operation, std::uint64_t id) {
        std::lock_guard lock(gateMutex);
        blockOn = operation;
        blockedId = id;
        entered = false;
        released = false;
    }
    bool awaitGateAndRelease(const std::function<void()>& duringGate) {
        std::unique_lock lock(gateMutex);
        const bool observed = gateChanged.wait_for(lock, 2s, [&] { return entered; });
        lock.unlock();
        if (observed) duringGate();
        lock.lock();
        released = true;
        gateChanged.notify_all();
        return observed;
    }

private:
    bool block(BlockOn operation, std::uint64_t id) noexcept {
        std::unique_lock lock(gateMutex);
        if (blockOn != operation || blockedId != id) return true;
        entered = true;
        gateChanged.notify_all();
        if (!gateChanged.wait_for(lock, 2s, [&] { return released; })) return false;
        blockOn = BlockOn::none;
        return true;
    }
};

bool idReuseAfterInflightRemove() {
    LifecycleRaceBackend backend;
    QueuedRemotePlayerAdapter adapter(backend);
    if (!adapter.initialize() || !adapter.bindOwnerThread()) return false;
    adapter.spawn({54, 1, 1, 2, 3});
    if (adapter.pump() != 1 || !backend.entities.contains(54)) return false;

    backend.configure(LifecycleRaceBackend::BlockOn::remove, 54);
    adapter.despawn(54);
    bool producerSawRemove = false;
    std::thread producer([&] {
        producerSawRemove = backend.awaitGateAndRelease([&] {
            adapter.spawn({54, 2, 8, 9, 10});
            // Coalesce a second spawn while the old representation still exists
            // inside remove(). The queued recreation must remain a create.
            adapter.spawn({54, 3, 11, 12, 13});
        });
    });
    const auto processed = adapter.pump();
    producer.join();
    return producerSawRemove && processed == 2 &&
           backend.events == std::vector<std::string>{"create", "remove", "create"} &&
           backend.entities.contains(54) && backend.entities.at(54).sequence == 3 &&
           backend.entities.at(54).x == 11;
}

bool idReuseAfterInflightCreateAndDespawn() {
    LifecycleRaceBackend backend;
    QueuedRemotePlayerAdapter adapter(backend);
    if (!adapter.initialize() || !adapter.bindOwnerThread()) return false;
    backend.configure(LifecycleRaceBackend::BlockOn::create, 55);
    adapter.spawn({55, 1, 1, 2, 3});
    bool producerSawCreate = false;
    std::thread producer([&] {
        producerSawCreate = backend.awaitGateAndRelease([&] {
            adapter.despawn(55);
            adapter.spawn({55, 2, 8, 9, 10});
        });
    });
    const auto processed = adapter.pump();
    producer.join();
    return producerSawCreate && processed == 3 &&
           backend.events == std::vector<std::string>{"create", "remove", "create"} &&
           backend.entities.contains(55) && backend.entities.at(55).sequence == 2;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string_view test(argv[1]);
    if (test == "scene") return sceneLifecycle() ? 0 : 1;
    if (test == "ordering") return despawnBeforeSpawnAndRespawnOrdering() ? 0 : 1;
    if (test == "session") return resetSessionAndTeleport() ? 0 : 1;
    if (test == "availability") return unavailableAndCreateRetry() ? 0 : 1;
    if (test == "shutdown") return shutdownDropsPendingAndCleans() ? 0 : 1;
    if (test == "thread") return ownerThreadAndReentrancy() ? 0 : 1;
    if (test == "reentrant_reset") return resetAndRespawnReentrancy() ? 0 : 1;
    if (test == "concurrent") return concurrentProducers() ? 0 : 1;
    if (test == "queue") return queueFullFailsClosed() ? 0 : 1;
    if (test == "entity_limit") return entityLimitFailsClosed() ? 0 : 1;
    if (test == "failure") return createFailureCanReset() ? 0 : 1;
    if (test == "persistent_create") return persistentCreateFailureFailsClosedAndCleans() ? 0 : 1;
    if (test == "persistent_update") return persistentUpdateFailureFailsClosedAndCleans() ? 0 : 1;
    if (test == "shutdown_recovery") return shutdownWaitsForAvailabilityThenCleans() ? 0 : 1;
    if (test == "cleanup_failure") return persistentCleanupFailureIsExplicitAndRecoverable() ? 0 : 1;
    if (test == "persistent_remove") return persistentEntityRemovalFailsClosed() ? 0 : 1;
    if (test == "remove_recovery") return transientEntityRemovalRecovers() ? 0 : 1;
    if (test == "backend_unlocked") return backendCallsRunWithoutAdapterLock() ? 0 : 1;
    if (test == "reuse_during_remove") return idReuseAfterInflightRemove() ? 0 : 1;
    if (test == "reuse_after_create_despawn") return idReuseAfterInflightCreateAndDespawn() ? 0 : 1;
    return 2;
}
