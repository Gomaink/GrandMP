#pragma once
#include "grandmp/windows/observer_network.hpp"

namespace grandmp::windows::remote_ped {
enum class ReceiverState { idle, starting, endpoint_validated, observer_initialized, ready, failed };

// Local readiness does not imply Welcome/server connectivity. No engine calls.
template<class Network, class Report>
bool initializeReceiver(Network& network, std::wstring_view endpoint, Report report,
                        bool allowLan=false) noexcept {
    try {
        std::uint16_t port = 0;
        core::Ipv4Endpoint target{};
        if (allowLan ? !parseLanEndpoint(endpoint,target) : !parseLoopbackEndpoint(endpoint, port)) {
            report(ReceiverState::failed, "remote_receiver_failed_endpoint_invalid");
            return false;
        }
        report(ReceiverState::endpoint_validated, allowLan ? "remote_endpoint_validated_private_or_loopback" : "remote_endpoint_validated_loopback");
        bool started=false;
        if constexpr (requires { network.start(target); }) {
            started=allowLan ? network.start(target) : network.start(port);
        } else {
            if (!allowLan) started=network.start(port);
        }
        if (!started) {
            report(ReceiverState::failed, "remote_receiver_failed_observer_start");
            return false;
        }
        report(ReceiverState::observer_initialized, "remote_observer_initialized");
        if (!network.beginRemoteScenePreview()) {
            report(ReceiverState::failed, "remote_receiver_failed_preview_start");
            return false;
        }
        report(ReceiverState::ready, "remote_receiver_ready_no_engine_calls");
        return true;
    } catch (...) {
        report(ReceiverState::failed, "remote_receiver_failed_initialization_exception");
        return false;
    }
}
} // namespace grandmp::windows::remote_ped
