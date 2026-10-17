#include <atomic>

namespace {
using Target = int(__cdecl*)(int);
Target original = nullptr;
std::atomic<unsigned> callbacks{0};
}

extern "C" __declspec(dllexport) void __cdecl ProbeSetOriginal(void* trampoline) {
    original = reinterpret_cast<Target>(trampoline);
}

extern "C" __declspec(dllexport) int __cdecl ProbeDetour(int value) {
    callbacks.fetch_add(1, std::memory_order_relaxed);
    return original ? original(value) + 100 : -1;
}

extern "C" __declspec(dllexport) unsigned __cdecl ProbeCallbackCount() {
    return callbacks.load(std::memory_order_relaxed);
}
