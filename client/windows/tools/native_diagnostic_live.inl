// Included ONLY by GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD. No default/Alpha 0.7 target
// defines it. Native providers below borrow the existing RemoteEngine registry.
namespace ndi = grandmp::windows::native_diagnostic_integration;
namespace ndw = grandmp::windows::native_diagnostic_windows;
namespace ndd = grandmp::windows::native_diagnostic_deferred;
inline constexpr bool taskDiagnosticPhysicalApproved = ndi::buildEnabled && rm::physicalExecutionApproved;
inline constexpr std::uint64_t taskDiagnosticArmMs = 120000;

struct ResidentTaskDiagnostic final {
    std::shared_ptr<ndd::Session<true>> source = std::make_shared<ndd::Session<true>>();
    ndi::Producer<true> producer{*source};
#if defined(GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD) && GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD == 1
    std::shared_ptr<grandmp::windows::native_movement_diagnostic::Batch> movement =
        std::make_shared<grandmp::windows::native_movement_diagnostic::Batch>();
    grandmp::windows::native_movement_diagnostic::Capture<true> movementCapture{*movement};
#endif
    std::atomic<bool> serviceClaimed{false};
    std::wstring path;
    ndd::WindowsWorker<true> worker;
    explicit ResidentTaskDiagnostic(std::wstring filename)
        : path(std::move(filename)), worker(source,gameThread,path,{}
#if defined(GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD) && GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD == 1
            , movement
#endif
        ) {}
    void requestStop() const noexcept {
        source->requestStop();
#if defined(GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD) && GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD == 1
        movement->requestStop();
#endif
    }
    template<class View, class Controller>
    void beforeAttempt(View& view, const Controller& controller, unsigned callback, std::uint64_t now) {
#if defined(GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD) && GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD == 1
        if (!view.nativeContext()) { requestStop(); return; } // Atomic only; deferred consumer reports inconclusive.
#endif
        producer.beforeAttempt(view,controller,callback,now);
#if defined(GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD) && GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD == 1
        movementCapture.beforeAttempt(view,controller,callback,now);
#endif
    }
    template<class View, class Controller>
    void afterTick(View& view, const Controller& controller, unsigned callback, std::uint64_t now) {
#if defined(GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD) && GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD == 1
        if (!view.nativeContext()) { requestStop(); return; }
#endif
        producer.afterTick(view,controller,callback,now);
#if defined(GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD) && GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD == 1
        movementCapture.afterTick(view,controller,callback,now);
#endif
    }
    void inconclusive(const char* reason, DWORD error = ERROR_SUCCESS, const char* detail = "") noexcept {
        requestStop();
        char line[480]{};
        const int count = std::snprintf(line,sizeof(line),"diagnostic_history=inconclusive reason=%s win32=%lu attempt=1%s%s",
            reason,error,*detail ? " " : "",detail);
        if (count <= 0 || static_cast<std::size_t>(count) >= sizeof(line)) return;
        ndd::FileSink sink(path.c_str()); // Preparation thread ONLY. No GTA address in this file.
        sink(line);
#if defined(GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD) && GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD == 1
        char passive[192]{};
        const int length = std::snprintf(passive,sizeof(passive),"movement_history=inconclusive reason=%s win32=%lu",reason,error);
        if (length > 0 && static_cast<std::size_t>(length) < sizeof(passive)) sink(passive);
#endif
    }
};
// Process-resident root, deliberately NEVER deleted/reset after publication.
// PIN is established by the existing installer before setup. It keeps producer,
// Session, Worker and State alive across in-flight callbacks/consumer termination.
// One bounded allocation graph + one thread handle, reclaimed by process exit.
std::atomic<ResidentTaskDiagnostic*> taskDiagnosticRoot{nullptr};
std::atomic<bool> taskDiagnosticSetupAttempted{false};

void stopTaskDiagnostic() noexcept {
    if (const auto* root = taskDiagnosticRoot.load(std::memory_order_acquire)) root->requestStop();
}
void prepareTaskDiagnostic() noexcept {
    if (!taskDiagnosticPhysicalApproved || taskDiagnosticSetupAttempted.exchange(true)) return;
    try {
        wchar_t path[32768]{};
        const DWORD length = GetModuleFileNameW(module,path,32768);
        if (!length || length >= 32768) { writeLog("failed","task_diagnostic_path_unavailable_no_retry"); return; }
        const auto file = std::filesystem::path(std::wstring(path,length)).parent_path()/L"GrandMPNativeTaskReleaseDiagnostic.log";
        auto* root = new ResidentTaskDiagnostic(file.wstring()); // Deferred allocation, before preparation=3.
        taskDiagnosticRoot.store(root,std::memory_order_release);
    } catch (...) { writeLog("failed","task_diagnostic_setup_failed_no_retry"); }
}
void serviceTaskDiagnostic() noexcept {
    auto* root = taskDiagnosticRoot.load(std::memory_order_acquire);
    if (!root) return;
    if (root->serviceClaimed.exchange(true)) return;
    if (GetCurrentThreadId() == gameThread) { root->requestStop(); return; } // No wait/I/O on producer.
    const auto result = ndi::awaitPreparation(root->producer,*root->source,runtimeDisabled,
        [] { return GetTickCount64(); },[] { Sleep(10); },taskDiagnosticArmMs);
    // Recheck expiry/stop after waking: a request racing the deadline is NOT an
    // extension of the diagnostic window and cannot restart a stopped session.
    if (!result.ready()) {
        char detail[288]{};
        const bool formatted = ndi::formatPreparation(detail,sizeof(detail),result);
        root->inconclusive("preparation_wait_ended_inconclusive",ERROR_SUCCESS,
            formatted ? detail : "preparation=diagnostic_format_truncated"); return;
    }
    DiagnosticResidence residence;
    if (!root->worker.start(residence)) root->inconclusive("consumer_start_failed",root->worker.error());
    // No retry/join here. Consumer owns its bounded loop; root remains resident.
}

class TaskDiagnosticFrame final {
public:
    TaskDiagnosticFrame(std::uintptr_t caller, unsigned callback)
        : context_(runtime_,caller,callback), ready_(runtime_,context_), reader_(context_),
          view_(remoteEngine,context_,ready_,reader_), root_(taskDiagnosticRoot.load(std::memory_order_acquire)), callback_(callback) {}
    void beforeAttempt(const rm::Controller& controller) {
        if (root_) root_->beforeAttempt(view_,controller,callback_,GetTickCount64());
    }
    void afterTick(const rm::Controller& controller) {
        if (root_) root_->afterTick(view_,controller,callback_,GetTickCount64());
    }
private:
    DiagnosticRuntimePrimitives runtime_;
    ndw::Context<DiagnosticRuntimePrimitives> context_;
    ndw::Ready<DiagnosticRuntimePrimitives,decltype(context_)> ready_;
    ndw::Reader<decltype(context_)> reader_;
    grandmp::windows::native_diagnostic_binding::RuntimeView<RemoteEngine,decltype(context_),decltype(ready_),decltype(reader_)> view_;
    ResidentTaskDiagnostic* root_;
    unsigned callback_;
};
