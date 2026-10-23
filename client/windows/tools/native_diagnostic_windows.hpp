#pragma once
#include "native_diagnostic_binding.hpp"
#include "ped_smoke_runtime_read.hpp"

namespace grandmp::windows::native_diagnostic_windows {
namespace native = continuous_remote_native;
namespace ps = ped_smoke;

// Borrowed for ONE admitted phase. Runtime is the existing Windows primitives
// adapter (or a deterministic fake). Neither provider is sent to the consumer.
template<class Runtime>
class Context final {
public:
    Context(Runtime& runtime, std::uintptr_t caller, unsigned callback)
        : runtime_(runtime), caller_(caller), callback_(callback) {}
    bool operator()() const noexcept { return runtime_.context(caller_,callback_); }
private:
    Runtime& runtime_;
    std::uintptr_t caller_;
    unsigned callback_;
};
template<class Runtime, class Phase>
class Ready final {
public:
    Ready(Runtime& runtime, Phase& phase) : runtime_(runtime), phase_(phase) {}
    bool operator()() noexcept {
        return phase_() && runtime_.codeReady() && phase_() && runtime_.gameplayReady() && phase_();
    }
private:
    Runtime& runtime_;
    Phase& phase_;
};
struct ProcessMemory {
    ps::MemoryRead operator()(std::uintptr_t address, void* output, std::size_t size) const noexcept {
        return ps::readMemory(address,output,size);
    }
};
template<class Phase, class Memory = ProcessMemory>
class Reader final {
public:
    explicit Reader(Phase& phase, Memory memory = {}) : phase_(phase), memory_(memory) {}
    native::ReadResult operator()(std::uint32_t address, void* output, std::size_t size) noexcept {
        if (!phase_()) { last_ = {false,0,ERROR_ACCESS_DENIED}; return {false,0}; }
        last_ = memory_(address,output,size);
        return {last_.success,last_.transferred}; // Partial success is NOT upgraded to a complete read.
    }
    ps::MemoryRead lastRead() const noexcept { return last_; }
private:
    Phase& phase_;
    Memory memory_;
    ps::MemoryRead last_{};
};
} // namespace grandmp::windows::native_diagnostic_windows
