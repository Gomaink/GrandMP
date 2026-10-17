#include "grandmp/core/remote_player_adapter.hpp"

#include <algorithm>
#include <iterator>

namespace grandmp::core {

QueuedRemotePlayerAdapter::QueuedRemotePlayerAdapter(
    IRemotePlayerEntityBackend& backend) noexcept : backend_(backend) {}

QueuedRemotePlayerAdapter::~QueuedRemotePlayerAdapter() { shutdown(); }

bool QueuedRemotePlayerAdapter::initialize() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != AdapterState::cold) return false;
    state_ = AdapterState::ready;
    return true;
}

bool QueuedRemotePlayerAdapter::bindOwnerThread() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pumping_ || state_ != AdapterState::ready) return false;
    const auto currentThread = std::this_thread::get_id();
    if (ownerThread_ != std::thread::id{} && ownerThread_ != currentThread) return false;
    ownerThread_ = currentThread;
    return true;
}

AdapterState QueuedRemotePlayerAdapter::state() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

bool QueuedRemotePlayerAdapter::isAvailable() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ == AdapterState::ready && backendAvailable_;
}

std::size_t QueuedRemotePlayerAdapter::pending() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return commands_.size();
}

std::size_t QueuedRemotePlayerAdapter::droppedCommands() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return droppedCommands_;
}

std::deque<NativeCommand>::iterator QueuedRemotePlayerAdapter::lastFor(
    std::uint64_t id) noexcept {
    for (auto it = commands_.end(); it != commands_.begin();) {
        --it;
        if (it->kind != NativeCommandKind::remove_all && it->playerId == id) return it;
    }
    return commands_.end();
}

void QueuedRemotePlayerAdapter::failClosed() noexcept {
    ++droppedCommands_;
    if (hasInFlightCommand_ && !commands_.empty()) {
        commands_.erase(std::next(commands_.begin()), commands_.end());
    } else {
        commands_.clear();
    }
    state_ = AdapterState::stopping;
    backendAvailable_ = false;
    consecutiveBackendFailures_ = 0;
    consecutiveCleanupFailures_ = 0;
}

bool QueuedRemotePlayerAdapter::enqueue(NativeCommand command) noexcept {
    try {
        if (commands_.size() >= queueCapacity) {
            if (command.kind == NativeCommandKind::update) {
                ++droppedCommands_;
                return false;
            }
            failClosed();
            return false;
        }
        command.generation = generation_;
        commands_.push_back(command);
        return true;
    } catch (...) {
        failClosed();
        return false;
    }
}

void QueuedRemotePlayerAdapter::spawn(const RemotePose& pose) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != AdapterState::ready || pose.id == 0) return;
    try {
        const auto lastSequence = lastSequence_.find(pose.id);
        if (lastSequence != lastSequence_.end() && pose.sequence <= lastSequence->second) {
            ++droppedCommands_;
            return;
        }
        const bool resetPending = std::any_of(commands_.begin(), commands_.end(),
            [](const NativeCommand& command) {
                return command.kind == NativeCommandKind::remove_all;
            });
        auto it = lastFor(pose.id);
        const bool removeWillRun =
            (hasInFlightCommand_ && inFlightPlayerId_ == pose.id &&
             inFlightKind_ == NativeCommandKind::remove) ||
            (it != commands_.end() && it->kind == NativeCommandKind::remove);
        if (hasInFlightCommand_ && inFlightPlayerId_ == pose.id &&
            (inFlightKind_ == NativeCommandKind::create ||
             inFlightKind_ == NativeCommandKind::update)) {
            if (pose.sequence > lastSequence_[pose.id]) {
                const auto kind = (resetPending || removeWillRun)
                    ? NativeCommandKind::create : NativeCommandKind::update;
                if (enqueue(NativeCommand{kind, pose, pose.id, generation_}))
                    lastSequence_[pose.id] = pose.sequence;
            }
            return;
        }
        if (it != commands_.end()) {
            if (it->kind == NativeCommandKind::create || it->kind == NativeCommandKind::update) {
                if (pose.sequence > it->pose.sequence) {
                    it->pose = pose;
                    // A queued recreation remains a create even while remove()
                    // is in flight and the old ID is still materialized.
                    if (resetPending || removeWillRun ||
                        it->kind == NativeCommandKind::create ||
                        !materialized_.contains(pose.id))
                        it->kind = NativeCommandKind::create;
                    else
                        it->kind = NativeCommandKind::update;
                    lastSequence_[pose.id] = pose.sequence;
                }
                return;
            }
            // Preserve queued remove before recreating a representation.
        }
        const auto kind = (!resetPending && !removeWillRun && materialized_.contains(pose.id) &&
                           (it == commands_.end() || it->kind != NativeCommandKind::remove))
            ? NativeCommandKind::update : NativeCommandKind::create;
        if (enqueue(NativeCommand{kind, pose, pose.id, generation_}))
            lastSequence_[pose.id] = pose.sequence;
    } catch (...) {
        failClosed();
    }
}

void QueuedRemotePlayerAdapter::move(const RemotePose& pose) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != AdapterState::ready || pose.id == 0) return;
    try {
        const auto sequence = lastSequence_.find(pose.id);
        // RemotePlayerScene emits multiple interpolated poses with the same
        // network sequence, so equal sequence numbers remain valid here.
        if (sequence != lastSequence_.end() && pose.sequence < sequence->second) {
            ++droppedCommands_;
            return;
        }
        if (hasInFlightCommand_ && inFlightPlayerId_ == pose.id &&
            (inFlightKind_ == NativeCommandKind::create ||
             inFlightKind_ == NativeCommandKind::update)) {
            if (enqueue(NativeCommand{NativeCommandKind::update, pose, pose.id, generation_}))
                lastSequence_[pose.id] = pose.sequence;
            return;
        }
        auto it = lastFor(pose.id);
        if (it != commands_.end()) {
            if (it->kind == NativeCommandKind::remove) return;
            if (it->kind == NativeCommandKind::create || it->kind == NativeCommandKind::update) {
                it->pose = pose;
                lastSequence_[pose.id] = pose.sequence;
                return;
            }
        }
        if (!materialized_.contains(pose.id)) {
            ++droppedCommands_;
            return;
        }
        if (enqueue(NativeCommand{NativeCommandKind::update, pose, pose.id, generation_}))
            lastSequence_[pose.id] = pose.sequence;
    } catch (...) {
        failClosed();
    }
}

void QueuedRemotePlayerAdapter::despawn(std::uint64_t playerId) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != AdapterState::ready || playerId == 0) return;
    try {
        lastSequence_.erase(playerId);
        const bool isMaterialized = materialized_.contains(playerId);
        for (auto it = commands_.begin(); it != commands_.end();) {
            if (hasInFlightCommand_ && it == commands_.begin() &&
                inFlightPlayerId_ == playerId && it->playerId == playerId &&
                it->kind == inFlightKind_) {
                ++it;
                continue;
            }
            if (it->playerId == playerId &&
                (it->kind == NativeCommandKind::create ||
                 it->kind == NativeCommandKind::update ||
                 it->kind == NativeCommandKind::remove)) {
                it = commands_.erase(it);
            } else {
                ++it;
            }
        }
        if (isMaterialized || (hasInFlightCommand_ && inFlightPlayerId_ == playerId &&
            (inFlightKind_ == NativeCommandKind::create ||
             inFlightKind_ == NativeCommandKind::update))) {
            (void)enqueue(NativeCommand{NativeCommandKind::remove, {}, playerId, generation_});
        }
    } catch (...) {
        failClosed();
    }
}

void QueuedRemotePlayerAdapter::resetSession() noexcept { resetAll(); }

void QueuedRemotePlayerAdapter::resetAll() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != AdapterState::ready) return;
    try {
        ++generation_;
        if (generation_ == 0) generation_ = 1;
        if (hasInFlightCommand_ && !commands_.empty())
            commands_.erase(std::next(commands_.begin()), commands_.end());
        else
            commands_.clear();
        lastSequence_.clear();
        consecutiveBackendFailures_ = 0;
        (void)enqueue(NativeCommand{NativeCommandKind::remove_all, {}, 0, generation_});
    } catch (...) {
        failClosed();
    }
}

void QueuedRemotePlayerAdapter::shutdown() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == AdapterState::cold) {
        state_ = AdapterState::stopped;
        return;
    }
    if (state_ == AdapterState::ready) {
        ++generation_;
        if (hasInFlightCommand_ && !commands_.empty())
            commands_.erase(std::next(commands_.begin()), commands_.end());
        else
            commands_.clear();
        lastSequence_.clear();
        consecutiveBackendFailures_ = 0;
        state_ = AdapterState::stopping;
        backendAvailable_ = false;
    }
}

bool QueuedRemotePlayerAdapter::retryCleanup() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != AdapterState::cleanup_failed) return false;
    consecutiveCleanupFailures_ = 0;
    state_ = AdapterState::stopping;
    return true;
}

std::size_t QueuedRemotePlayerAdapter::pump(std::size_t budget) noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pumping_ || state_ == AdapterState::cold || state_ == AdapterState::stopped) return 0;
        const auto currentThread = std::this_thread::get_id();
        if (ownerThread_ == std::thread::id{}) ownerThread_ = currentThread;
        if (ownerThread_ != currentThread) return 0;
        pumping_ = true;
    }

    auto finish = [this]() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        pumping_ = false;
    };

    // Availability may itself touch platform state, so it also runs outside the
    // queue/registry lock. The pumping flag blocks nested pump() calls.
    const bool available = backend_.available();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        backendAvailable_ = available;
        if (!available) {
            pumping_ = false;
            return 0;
        }
    }

    std::size_t completed = 0;
    while (completed < budget) {
        NativeCommand command{};
        bool cleanup = false;
        bool haveCommand = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (state_ == AdapterState::stopping) {
                cleanup = true;
                hasInFlightCommand_ = true;
                inFlightPlayerId_ = 0;
                inFlightKind_ = NativeCommandKind::remove_all;
            } else if (state_ != AdapterState::ready || commands_.empty()) {
                pumping_ = false;
                return completed;
            } else if (commands_.front().generation != generation_) {
                commands_.pop_front();
                ++droppedCommands_;
                ++completed;
                continue;
            } else {
                command = commands_.front();
                commands_.pop_front();
                hasInFlightCommand_ = true;
                inFlightPlayerId_ = command.playerId;
                inFlightKind_ = command.kind;
                haveCommand = true;
            }
        }

        bool succeeded = true;
        bool createRejectedAtCapacity = false;
        try {
            if (cleanup) {
                succeeded = backend_.removeAll();
            } else if (haveCommand) {
                switch (command.kind) {
                case NativeCommandKind::create: {
                    bool capacityAvailable = false;
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        capacityAvailable = materialized_.contains(command.playerId) ||
                                            materialized_.size() < maxMaterializedPlayers;
                    }
                    if (!capacityAvailable) {
                        succeeded = false;
                        createRejectedAtCapacity = true;
                    } else {
                        succeeded = backend_.create(command.pose);
                    }
                    break;
                }
                case NativeCommandKind::update: {
                    bool exists = false;
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        exists = materialized_.contains(command.playerId);
                    }
                    succeeded = !exists || backend_.update(command.pose);
                    break;
                }
                case NativeCommandKind::remove: {
                    bool exists = false;
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        exists = materialized_.contains(command.playerId);
                    }
                    succeeded = !exists || backend_.remove(command.playerId);
                    break;
                }
                case NativeCommandKind::remove_all:
                    succeeded = backend_.removeAll();
                    break;
                }
            }
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex_);
            hasInFlightCommand_ = false;
            failClosed();
            pumping_ = false;
            return completed;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            hasInFlightCommand_ = false;
            if (succeeded) {
                try {
                    if (cleanup) {
                        materialized_.clear();
                        commands_.clear();
                        consecutiveCleanupFailures_ = 0;
                        state_ = AdapterState::stopped;
                    } else if (haveCommand) {
                        switch (command.kind) {
                        case NativeCommandKind::create:
                            materialized_.insert(command.playerId);
                            consecutiveBackendFailures_ = 0;
                            break;
                        case NativeCommandKind::update:
                            consecutiveBackendFailures_ = 0;
                            break;
                        case NativeCommandKind::remove:
                            materialized_.erase(command.playerId);
                            consecutiveBackendFailures_ = 0;
                            break;
                        case NativeCommandKind::remove_all:
                            materialized_.clear();
                            consecutiveBackendFailures_ = 0;
                            break;
                        }
                    }
                } catch (...) {
                    failClosed();
                }
                ++completed;
                if (state_ == AdapterState::stopped) {
                    pumping_ = false;
                    return completed;
                }
                continue;
            }

            if (cleanup) {
                ++consecutiveCleanupFailures_;
                if (consecutiveCleanupFailures_ >= maxConsecutiveBackendFailures)
                    state_ = AdapterState::cleanup_failed;
                pumping_ = false;
                return completed;
            }

            ++consecutiveBackendFailures_;
            if (createRejectedAtCapacity || command.generation != generation_ ||
                state_ != AdapterState::ready || consecutiveBackendFailures_ >= maxConsecutiveBackendFailures) {
                failClosed();
            } else {
                try {
                    commands_.push_front(command);
                } catch (...) {
                    failClosed();
                }
            }
            pumping_ = false;
            return completed;
        }
    }
    finish();
    return completed;
}

}  // namespace grandmp::core
