// CLEO host adapters only. The capture, Controller and original-control replay
// below remain the existing Alpha 0.9/0.8 implementations, not a second backend.
auto& nativeApi=bootstrapApi;
auto& nativeAccess=access;
bool nativeContext(void* value) noexcept { return context(value); }
bool nativeReady(void* value) noexcept { return ready(value); }
bool inputWindowMatches(ps::CodeRegion region) noexcept {
    for(std::size_t offset=0;offset<region.size;){
        const auto count=std::min<std::size_t>(4096,region.size-offset);
        if(!game.matches(region.address+offset,count))return false;
        offset+=count;
    }return true;
}
bool codeMatches() noexcept {
    return game.matches(0x469F00,256) && game.matches(0x46A000,0x230) &&
        game.matches(0x53BFC7,5) && cleo.matches(cleo.base+0x32B0,39) && cleo.matches(cleo.base+0xEF00,0x79);
}
void writeLog(const char*,const char*) noexcept;
void inputInstallRefused(const char* stage) noexcept {
    diagnostic.emit(diag::Note::input_guard,stage,inputHook.error(),inputHook.installed());
    const auto value=inputHook.observation();
    if(value.failure==ni::CellFailure::none)return; // The cell was not necessarily reached.
    diagnostic.emit(diag::Note::input_cell,"input_hook.cell_first_failure",inputHook.error(),
        static_cast<unsigned>(value.failure),0,value.readBytes);
    diagnostic.emit(diag::Note::input_cell_page,"input_hook.cell_page",0,value.protection,
        PAGE_READWRITE|PAGE_WRITECOPY,(std::uint64_t{value.state}<<32)|value.type);
}
#include "../tools/native_player_input_runtime.inl"
#include "../tools/local_player_capture_runtime.inl"

bool movementObserved=false;
void writeLog(const char* state,const char* reason) noexcept {
    if(!std::strcmp(state,"failed")){
        stopped.store(true);status.store(108);
        diagnostic.emit(diag::Note::sync_failure,reason,inputHook.error());return;
    }
    if(std::strstr(reason,"input_hook_installed")){
        diagnostic.emit(diag::Note::replay_installed,"sync.replay_installed_atomic_data_cell");return;
    }
    if(!std::strstr(reason,"input_replay="))return;
    if(inputNotice!=InputNotice::walking && inputNotice!=InputNotice::turning && inputNotice!=InputNotice::stopped)return;
    if(inputNotice==InputNotice::stopped && !movementObserved)return;
    if(inputNotice==InputNotice::walking || inputNotice==InputNotice::turning)movementObserved=true;
    const auto note=inputNotice==InputNotice::walking ? diag::Note::replay_walking :
        inputNotice==InputNotice::turning ? diag::Note::replay_turning :
        diag::Note::replay_stopped;
    diagnostic.emit(note,"sync.original_control_replay",0,inputSerial,0,inputProcessed);
}
