// Exercita diretamente as pequenas rotinas Win32 do executável sem iniciar seu loop.
// O nome de entrada é renomeado para que o teste nunca busque ou abra o GTA.
#define wmain observerEntryNotCalledByTest
#include "../client/windows/tools/position_observer.cpp"
#undef wmain

#include <array>
#include <cstddef>
#include <cstdint>

int main() {
    std::vector<DWORD> pids;
    if (!findProcesses(pids)) return 1;

    stopRequested.store(false, std::memory_order_relaxed);
    if (!onConsoleControl(CTRL_C_EVENT) ||
        !stopRequested.load(std::memory_order_relaxed)) return 2;
    stopRequested.store(false, std::memory_order_relaxed);
    if (!onConsoleControl(CTRL_BREAK_EVENT) ||
        !stopRequested.load(std::memory_order_relaxed)) return 3;
    stopRequested.store(false, std::memory_order_relaxed);

    HANDLE raw = INVALID_HANDLE_VALUE;
    {
        Handle process(OpenProcess(grandmp::windows::observerProcessAccess,
                                   FALSE, GetCurrentProcessId()));
        if (!process.valid()) return 4;
        raw = process.get();
        std::uint32_t value = 0x12345678U;
        ProcessReader reader(process.get());
        std::array<std::byte, sizeof(value)> bytes{};
        const auto result = reader.read(static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(&value)), bytes);
        std::uint32_t observed = 0;
        std::memcpy(&observed, bytes.data(), sizeof(observed));
        if (!result.success || result.bytesRead != bytes.size() || observed != value) return 5;
    }
    DWORD flags = 0;
    return GetHandleInformation(raw, &flags) == FALSE &&
                   GetLastError() == ERROR_INVALID_HANDLE ? 0 : 6;
}
