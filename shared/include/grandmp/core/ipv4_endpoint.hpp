#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace grandmp::core {
using Ipv4Address = std::array<std::uint8_t,4>;
inline constexpr Ipv4Address loopbackAddress{127,0,0,1};
struct Ipv4Endpoint {
    Ipv4Address address=loopbackAddress;
    std::uint16_t port=0;
    bool operator==(const Ipv4Endpoint&) const = default;
};
inline bool parseIpv4Address(std::string_view text,Ipv4Address& output) noexcept {
    Ipv4Address result{};
    for(unsigned i=0;i<4;++i) {
        const auto end=text.find('.');
        const auto part=text.substr(0,end);
        if(part.empty() || part.size()>3 || (part.size()>1 && part.front()=='0')) return false;
        unsigned value=0;
        for(char digit:part) {
            if(digit<'0' || digit>'9') return false;
            value=value*10+static_cast<unsigned>(digit-'0');
        }
        if(value>255 || (i==3)!=(end==std::string_view::npos)) return false;
        result[i]=static_cast<std::uint8_t>(value);
        if(i!=3) text.remove_prefix(end+1);
    }
    output=result;return true;
}
inline bool loopbackIpv4(Ipv4Address ip) noexcept { return ip[0]==127; }
inline bool privateIpv4(Ipv4Address ip) noexcept {
    return ip[0]==10 || (ip[0]==172 && ip[1]>=16 && ip[1]<=31) ||
        (ip[0]==192 && ip[1]==168);
}
inline bool validLabEndpoint(const Ipv4Endpoint& endpoint) noexcept {
    return endpoint.port && (loopbackIpv4(endpoint.address) || privateIpv4(endpoint.address));
}
// Numeric IPv4 only, no DNS, wildcard/public/multicast endpoints or octal forms.
inline bool parseLabEndpoint(std::string_view text,Ipv4Endpoint& output) noexcept {
    const auto colon=text.find(':');
    if(colon==std::string_view::npos) return false;
    Ipv4Endpoint result{};
    if(!parseIpv4Address(text.substr(0,colon),result.address)) return false;
    const auto port=text.substr(colon+1);
    if(port.empty()) return false;
    unsigned value=0;
    for(char digit:port) {
        if(digit<'0' || digit>'9') return false;
        value=value*10+static_cast<unsigned>(digit-'0');
        if(value>65535) return false;
    }
    result.port=static_cast<std::uint16_t>(value);
    if(!validLabEndpoint(result)) return false;
    output=result;return true;
}
} // namespace grandmp::core
