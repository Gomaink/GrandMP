#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdint>

namespace grandmp::windows::ped_smoke {
struct MemoryRead {
    bool success = false;
    std::size_t transferred = 0;
    DWORD error = ERROR_SUCCESS;
    bool complete(std::size_t requested) const noexcept { return success && transferred == requested; }
};
// Same current-process primitive used by the typed runtime read. Preserve the
// Win32 outcome separately from exact transfer; capture error before any call.
inline MemoryRead readMemory(std::uintptr_t address, void* output, std::size_t size) noexcept {
    SIZE_T count = 0;
    const BOOL okay = ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), output, size, &count);
    const DWORD error = okay ? ERROR_SUCCESS : GetLastError();
    return {okay != FALSE, count, error};
}
} // namespace grandmp::windows::ped_smoke
