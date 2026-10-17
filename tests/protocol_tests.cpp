#include "grandmp/core/protocol.hpp"

#include <bit>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string_view>
#include <variant>
#include <vector>

namespace {
using namespace grandmp::protocol;

bool roundTrip() {
    const Message values[] = {
        Hello{0x0102030405060708ULL},
        Welcome{0x0102030405060708ULL, 0x1020304050607080ULL},
        PlayerPosition{0x1020304050607080ULL, 42, 1.5f, -2.25f, 3.0f},
        Disconnect{0x1020304050607080ULL},
        RemoteJoin{0x1020304050607080ULL},
        RemotePosition{0x1020304050607080ULL, 7, 1.5f, -2.5f, 3.5f},
        RemoteLeave{0x1020304050607080ULL},
        SnapshotRequest{0x1020304050607080ULL},
    };
    for (const auto& value : values) {
        const auto bytes = encode(value);
        if (!bytes || bytes->size() > kMaxPacketSize) return false;
        const auto result = decode(*bytes);
        if (result.status != DecodeStatus::ok || !result.message || *result.message != value) {
            return false;
        }
    }
    return true;
}

bool wireFormat() {
    const auto hello = encode(Message{Hello{0x0102030405060708ULL}});
    const std::vector<std::uint8_t> expected = {
        'G', 'M', 'P', '1', 1, 1, 0, 8, 1, 2, 3, 4, 5, 6, 7, 8};
    const auto position = encode(Message{PlayerPosition{1, 0x01020304, 1.0f, -2.0f, 0.5f}});
    return hello == expected && position && position->size() == 32 &&
           (*position)[16] == 1 && (*position)[17] == 2 &&
           (*position)[18] == 3 && (*position)[19] == 4 &&
           (*position)[20] == 0x3f && (*position)[21] == 0x80 &&
           (*position)[24] == 0xc0 && (*position)[25] == 0x00 &&
           (*position)[28] == 0x3f && (*position)[29] == 0x00;
}

bool malformedPackets() {
    const auto original = encode(Message{Hello{1}});
    if (!original) return false;
    auto packet = *original;
    if (decode(std::span<const std::uint8_t>(packet).first(7)).status !=
        DecodeStatus::invalid_size) return false;
    packet[0] = 'X';
    if (decode(packet).status != DecodeStatus::invalid_magic) return false;
    packet = *original;
    packet[4] = 2;
    if (decode(packet).status != DecodeStatus::unsupported_version) return false;
    packet = *original;
    packet[5] = 99;
    if (decode(packet).status != DecodeStatus::invalid_type) return false;
    packet = *original;
    packet[7] = 7;
    if (decode(packet).status != DecodeStatus::invalid_size) return false;
    packet = *original;
    packet.pop_back();
    if (decode(packet).status != DecodeStatus::invalid_size) return false;
    packet = *original;
    packet.push_back(0);
    if (decode(packet).status != DecodeStatus::invalid_size) return false;
    packet.resize(kMaxPacketSize + 1);
    return decode(packet).status == DecodeStatus::invalid_size;
}

bool invalidFields() {
    if (encode(Message{Hello{0}}) || encode(Message{Welcome{1, 0}}) ||
        encode(Message{Disconnect{0}}) ||
        encode(Message{RemoteJoin{0}}) || encode(Message{RemoteLeave{0}}) ||
        encode(Message{SnapshotRequest{0}}) ||
        encode(Message{RemotePosition{1, 0, 0, 0, 0}}) ||
        encode(Message{PlayerPosition{1, 0, 0.0f, 0.0f, 0.0f}})) return false;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    if (encode(Message{RemotePosition{1, 1, nan, 0, 0}}) ||
        encode(Message{PlayerPosition{1, 1, nan, 0, 0}}) ||
        encode(Message{PlayerPosition{1, 1, 0, infinity, 0}}) ||
        encode(Message{PlayerPosition{1, 1, 0, 0, kMaxCoordinate + 1}})) return false;
    auto hello = *encode(Message{Hello{1}});
    hello[15] = 0;
    if (decode(hello).status != DecodeStatus::invalid_field) return false;
    auto position = *encode(Message{PlayerPosition{1, 1, 0, 0, 0}});
    position[20] = 0x7f;
    position[21] = 0xc0;
    return decode(position).status == DecodeStatus::invalid_field;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string_view test = argv[1];
    const bool passed = (test == "round_trip" && roundTrip()) ||
                        (test == "wire_format" && wireFormat()) ||
                        (test == "malformed" && malformedPackets()) ||
                        (test == "invalid_fields" && invalidFields());
    if (!passed) {
        std::fprintf(stderr, "protocol test failed: %.*s\n", static_cast<int>(test.size()),
                     test.data());
        return 1;
    }
    return 0;
}
