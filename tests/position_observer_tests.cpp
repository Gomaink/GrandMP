#include "grandmp/windows/position_observer.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <span>
#include <string_view>
#include <vector>

namespace {

using namespace grandmp::windows;

constexpr std::uint32_t playerAddress = 0x00B7CD98;
constexpr std::uint32_t pedAddress = 0x01000000;
constexpr std::uint32_t matrixAddress = 0x02000000;

class BufferReader final : public MemoryReader {
public:
    template <typename T>
    void put(std::uint32_t address, const T& value) {
        std::vector<std::byte> bytes(sizeof(T));
        std::memcpy(bytes.data(), &value, sizeof(T));
        values_[address] = std::move(bytes);
    }

    ReadResult read(std::uint32_t address, std::span<std::byte> destination) noexcept override {
        if (address == failedAddress_) return {false, 0};
        const auto found = values_.find(address);
        if (found == values_.end()) return {false, 0};
        const std::size_t count = address == partialAddress_ ? destination.size() - 1
                                                             : destination.size();
        if (found->second.size() < count) return {false, 0};
        std::memcpy(destination.data(), found->second.data(), count);
        if (address == playerAddress && changePedAfterFirstRead_ && ++playerReads_ == 2) {
            const std::uint32_t changed = pedAddress + 0x100;
            std::memcpy(destination.data(), &changed, sizeof(changed));
        }
        return {true, count};
    }

    void failAt(std::uint32_t address) { failedAddress_ = address; }
    void partialAt(std::uint32_t address) { partialAddress_ = address; }
    void changePedAfterFirstRead() { changePedAfterFirstRead_ = true; }

private:
    std::map<std::uint32_t, std::vector<std::byte>> values_;
    std::uint32_t failedAddress_ = 0;
    std::uint32_t partialAddress_ = 0;
    bool changePedAfterFirstRead_ = false;
    int playerReads_ = 0;
};

BufferReader matrixReader() {
    BufferReader reader;
    reader.put(playerAddress, pedAddress);
    reader.put(pedAddress + 0x14, matrixAddress);
    reader.put(matrixAddress + 0x30, Coordinates{12.5F, -3.25F, 101.125F});
    return reader;
}

bool testAbsent() {
    BufferReader reader;
    reader.put(playerAddress, std::uint32_t{0});
    PollState state;
    return readPlayerPosition(reader).status == PositionStatus::player_absent &&
           state.state() == ObserverState::game_not_found &&
           state.update(true, PositionStatus::player_absent) == ObserverState::waiting_for_player;
}

bool testMatrix() {
    auto reader = matrixReader();
    const auto result = readPlayerPosition(reader);
    PollState state;
    return result.status == PositionStatus::available && result.coordinates.x == 12.5F &&
           result.coordinates.y == -3.25F && result.coordinates.z == 101.125F &&
           state.update(true, result.status) == ObserverState::player_available;
}

bool testFallback() {
    BufferReader reader;
    reader.put(playerAddress, pedAddress);
    reader.put(pedAddress + 0x14, std::uint32_t{0});
    reader.put(pedAddress + 0x04, Coordinates{1.0F, 2.0F, 3.0F});
    const auto result = readPlayerPosition(reader);
    return result.status == PositionStatus::available && result.coordinates.x == 1.0F &&
           result.coordinates.y == 2.0F && result.coordinates.z == 3.0F;
}

bool testInvalidPointer() {
    BufferReader reader;
    reader.put(playerAddress, std::uint32_t{0xFFFF'FFFDU});
    if (readPlayerPosition(reader).status != PositionStatus::invalid_pointer) return false;
    reader.put(playerAddress, pedAddress);
    reader.put(pedAddress + 0x14, std::uint32_t{0x00000003});
    return readPlayerPosition(reader).status == PositionStatus::invalid_pointer;
}

bool testReadFailure() {
    auto reader = matrixReader();
    reader.failAt(matrixAddress + 0x30);
    PollState state;
    return readPlayerPosition(reader).status == PositionStatus::read_failed &&
           state.update(true, PositionStatus::read_failed) ==
               ObserverState::temporarily_unavailable;
}

bool testPartial() {
    auto reader = matrixReader();
    reader.partialAt(matrixAddress + 0x30);
    return readPlayerPosition(reader).status == PositionStatus::partial_read;
}

bool testNonFinite() {
    auto reader = matrixReader();
    reader.put(matrixAddress + 0x30, Coordinates{NAN, 1.0F, 2.0F});
    if (readPlayerPosition(reader).status != PositionStatus::non_finite) return false;
    reader.put(matrixAddress + 0x30, Coordinates{1.0F, INFINITY, 2.0F});
    return readPlayerPosition(reader).status == PositionStatus::non_finite;
}

bool testChangingPointer() {
    auto reader = matrixReader();
    reader.changePedAfterFirstRead();
    return readPlayerPosition(reader).status == PositionStatus::changed_pointer;
}

bool testProcessExit() {
    PollState state;
    return state.update(false, PositionStatus::read_failed) == ObserverState::game_not_found &&
           state.update(true, PositionStatus::player_absent) ==
               ObserverState::waiting_for_player &&
           state.update(false, PositionStatus::read_failed) == ObserverState::process_exited;
}

bool testReadOnlyAccess() {
    // PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION; sem escrita ou sincronização.
    return observerProcessAccess == 0x1010U &&
           (observerProcessAccess & (0x0020U | 0x0008U | 0x0002U | 0x00100000U)) == 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string_view test = argv[1];
    bool passed = false;
    if (test == "absent") passed = testAbsent();
    else if (test == "matrix") passed = testMatrix();
    else if (test == "fallback") passed = testFallback();
    else if (test == "pointer") passed = testInvalidPointer();
    else if (test == "read_failure") passed = testReadFailure();
    else if (test == "partial") passed = testPartial();
    else if (test == "non_finite") passed = testNonFinite();
    else if (test == "changing") passed = testChangingPointer();
    else if (test == "process_exit") passed = testProcessExit();
    else if (test == "read_only") passed = testReadOnlyAccess();
    else return 2;
    return passed ? 0 : 1;
}
