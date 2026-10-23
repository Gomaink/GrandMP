#pragma once
#include "native_diagnostic_deferred.hpp"
#if defined(GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD) && GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD == 1
#include "native_movement_diagnostic.hpp"
#endif
#include <windows.h>
#include <memory>
#include <new>
#include <string>

namespace grandmp::windows::native_diagnostic_deferred {
// Dedicated deferred file; NEVER call the existing writeLog from the producer.
class FileSink final {
public:
    explicit FileSink(const wchar_t* path) noexcept {
        file_ = CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        error_ = file_ == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    }
    ~FileSink() { if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_); }
    FileSink(const FileSink&) = delete;
    bool operator()(const char* detail) noexcept {
        if (file_ == INVALID_HANDLE_VALUE || error_ != ERROR_SUCCESS) return false; // Latch the first I/O failure; no retry.
        char line[512]{};
        const int count = std::snprintf(line,sizeof(line),"[GrandMPTaskRelease] %s\r\n",detail);
        if (count <= 0 || static_cast<std::size_t>(count) >= sizeof(line)) { error_ = ERROR_INSUFFICIENT_BUFFER; return false; }
        DWORD written = 0;
        const BOOL okay = WriteFile(file_,line,static_cast<DWORD>(count),&written,nullptr);
        error_ = okay ? ERROR_SUCCESS : GetLastError();
        if (okay && written != static_cast<DWORD>(count)) error_ = ERROR_WRITE_FAULT;
        return okay && written == static_cast<DWORD>(count);
    }
    DWORD error() const noexcept { return error_; }
private:
    HANDLE file_ = INVALID_HANDLE_VALUE;
    DWORD error_ = ERROR_SUCCESS;
};

// Explicit start from deferred preparation, AFTER module residence is proven.
// Selected only by diagnostic targets/closed hosts. Start/join refuse the producer thread. Shared
// State survives a bounded join timeout/destruction; no TerminateThread or UAF.
// The producer owner must retain its own shared_ptr until dispatch is quiescent.
template<bool Enabled = false>
class WindowsWorker final {
public:
    using Source = Session<Enabled>;
    WindowsWorker(std::shared_ptr<Source> source, DWORD producerThread, std::wstring path, Limits limits = {}
#if defined(GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD) && GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD == 1
        , std::shared_ptr<native_movement_diagnostic::Batch> movement = {}
#endif
    ) : state_(std::make_shared<State>(std::move(source),producerThread,std::move(path),limits
#if defined(GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD) && GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD == 1
        , std::move(movement)
#endif
    )) {}
    ~WindowsWorker() {
        requestStop();
        if (thread_) CloseHandle(thread_); // State is still held by a running worker.
    }
    WindowsWorker(const WindowsWorker&) = delete;
    template<class Residence>
    bool start(Residence& residence) noexcept {
        if constexpr (!Enabled) return false;
        if (started_ || !state_->source || !state_->producerThread || GetCurrentThreadId() == state_->producerThread) return false;
        started_ = true;
        if (!residence()) { error_ = ERROR_ACCESS_DENIED; requestStop(); return false; }
        // Allocation happens only in this deferred entry, never on the producer.
        auto* ticket = new (std::nothrow) std::shared_ptr<State>(state_);
        if (!ticket) { error_ = ERROR_NOT_ENOUGH_MEMORY; requestStop(); return false; }
        thread_ = CreateThread(nullptr,0,entry,ticket,0,nullptr);
        error_ = thread_ ? ERROR_SUCCESS : GetLastError();
        if (!thread_) { delete ticket; requestStop(); return false; }
        return true;
    }
    bool join(DWORD timeoutMs) noexcept {
        if (!thread_ || timeoutMs == INFINITE || GetCurrentThreadId() == state_->producerThread) return false;
        return WaitForSingleObject(thread_,timeoutMs) == WAIT_OBJECT_0;
    }
    void requestStop() noexcept {
        if (state_->source) state_->source->requestStop();
#if defined(GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD) && GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD == 1
        if (state_->movement) state_->movement->requestStop();
#endif
    }
    Outcome outcome() const noexcept { return state_->outcome.load(std::memory_order_acquire); }
    DWORD error() const noexcept { return error_ ? error_ : state_->sinkError.load(std::memory_order_acquire); }
#if defined(GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD) && GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD == 1
    auto movementOutcome() const noexcept { return state_->movementOutcome.load(std::memory_order_acquire); }
#endif
private:
    struct State {
        State(std::shared_ptr<Source> value, DWORD thread, std::wstring filename, Limits budget
#if defined(GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD) && GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD == 1
            , std::shared_ptr<native_movement_diagnostic::Batch> samples
#endif
        ) : source(std::move(value)), producerThread(thread), path(std::move(filename)), limits(budget)
#if defined(GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD) && GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD == 1
            , movement(std::move(samples))
#endif
        {}
        std::shared_ptr<Source> source;
        DWORD producerThread;
        std::wstring path;
        Limits limits;
        std::atomic<Outcome> outcome{Outcome::observing};
        std::atomic<DWORD> sinkError{ERROR_SUCCESS};
#if defined(GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD) && GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD == 1
        std::shared_ptr<native_movement_diagnostic::Batch> movement;
        std::atomic<native_movement_diagnostic::Consumer::Outcome> movementOutcome{native_movement_diagnostic::Consumer::Outcome::waiting};
#endif
    };
    static DWORD WINAPI entry(void* value) noexcept {
        std::unique_ptr<std::shared_ptr<State>> ticket(static_cast<std::shared_ptr<State>*>(value));
        const auto state = *ticket;
        FileSink sink(state->path.c_str());
        Consumer consumer(GetTickCount64(),state->limits);
#if defined(GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD) && GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD == 1
        native_movement_diagnostic::Consumer movementConsumer(GetTickCount64());
        bool movementDone = !state->movement;
#endif
        do {
            if (consumer.outcome() == Outcome::observing) consumer.poll(*state->source,GetTickCount64(),sink);
#if defined(GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD) && GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD == 1
            if (!movementDone) {
                const auto result = movementConsumer.poll(*state->movement,GetTickCount64(),sink);
                state->movementOutcome.store(result,std::memory_order_release);
                movementDone = result != native_movement_diagnostic::Consumer::Outcome::waiting;
            }
            if (movementDone && consumer.outcome() != Outcome::observing) break;
#else
            if (consumer.outcome() != Outcome::observing) break;
#endif
            Sleep(10); // Only deferred worker; never callback, no endless polling.
        } while (true);
        state->sinkError.store(sink.error(),std::memory_order_release);
        state->outcome.store(consumer.outcome(),std::memory_order_release);
        return 0;
    }
    std::shared_ptr<State> state_;
    HANDLE thread_ = nullptr;
    DWORD error_ = ERROR_SUCCESS;
    bool started_ = false;
};
} // namespace grandmp::windows::native_diagnostic_deferred
