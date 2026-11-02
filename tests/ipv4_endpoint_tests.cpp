#include "grandmp/core/ipv4_endpoint.hpp"
#include <cstdio>
#include <stdexcept>

using namespace grandmp::core;
void check(bool okay,const char* reason) { if(!okay) throw std::runtime_error(reason); }
int main() {
    try {
        for(const auto text:{"127.0.0.1:7777","127.0.0.2:1","10.1.2.3:65535",
                "172.16.0.1:7777","172.31.255.254:7777","192.168.1.10:7777"}) {
            Ipv4Endpoint endpoint{};check(parseLabEndpoint(text,endpoint),"allowed explicit endpoint");
            check(validLabEndpoint(endpoint),"policy matches parser");
        }
        const Ipv4Endpoint before{{192,168,1,1},42};
        for(const auto text:{"","localhost:7777","0.0.0.0:7777","8.8.8.8:7777",
                "224.1.2.3:7777","255.255.255.255:7777","169.254.1.1:7777",
                "172.15.0.1:7777","172.32.0.1:7777","192.169.1.1:7777",
                "192.168.1:7777","192.168.1.1.2:7777","192.168.01.1:7777",
                "192.168.256.1:7777","192.168.1.-1:7777","192.168.1.1:0",
                "192.168.1.1:65536","192.168.1.1:999999999999999999999",
                "192.168.1.1:7777garbage","192.168.1.1:7777:8","[::1]:7777"}) {
            auto endpoint=before;check(!parseLabEndpoint(text,endpoint) && endpoint==before,
                "invalid endpoint refused without changing output");
        }
        std::puts("IPv4 lab endpoint policy PASS");return 0;
    } catch(const std::exception& error) { std::fprintf(stderr,"%s\n",error.what());return 1; }
}
