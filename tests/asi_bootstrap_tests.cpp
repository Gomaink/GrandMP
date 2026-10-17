#include "grandmp/windows/asi_bootstrap.hpp"

#include <atomic>
#include <cstdio>
#include <string_view>
#include <thread>
#include <vector>

using namespace grandmp::windows;

namespace {

bool run(std::string_view scenario) {
    AsiBootstrapController controller;
    if (scenario == "initialization") {
        return controller.beginDeferred() == AsiBootstrapResult::not_requested &&
               controller.requestEntry() == AsiBootstrapResult::accepted &&
               controller.requestEntry() == AsiBootstrapResult::already_requested &&
               controller.beginDeferred() == AsiBootstrapResult::accepted &&
               controller.publishResidence(true) == AsiBootstrapResult::completed &&
               controller.state() == AsiBootstrapState::resident;
    }
    if (scenario == "partial_failure") {
        return controller.requestEntry() == AsiBootstrapResult::accepted &&
               controller.beginDeferred() == AsiBootstrapResult::accepted &&
               controller.publishResidence(false) == AsiBootstrapResult::residence_failed &&
               controller.state() == AsiBootstrapState::failed &&
               controller.beginDeferred() == AsiBootstrapResult::not_requested;
    }
    if (scenario == "logical_shutdown") {
        return controller.requestEntry() == AsiBootstrapResult::accepted &&
               controller.beginDeferred() == AsiBootstrapResult::accepted &&
               controller.publishResidence(true) == AsiBootstrapResult::completed &&
               controller.disableLogically() == AsiBootstrapResult::disabled &&
               controller.disableLogically() == AsiBootstrapResult::disabled &&
               controller.beginDeferred() == AsiBootstrapResult::disabled &&
               controller.state() == AsiBootstrapState::logically_disabled;
    }
    if (scenario == "concurrent_entry") {
        std::atomic<unsigned> accepted{0};
        std::vector<std::thread> threads;
        for (int i = 0; i < 32; ++i) {
            threads.emplace_back([&] {
                if (controller.requestEntry() == AsiBootstrapResult::accepted) {
                    accepted.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }
        for (auto& thread : threads) thread.join();
        return accepted.load(std::memory_order_relaxed) == 1 &&
               controller.state() == AsiBootstrapState::entry_seen;
    }
    if (scenario == "concurrent_deferred") {
        if (controller.requestEntry() != AsiBootstrapResult::accepted) return false;
        std::atomic<unsigned> accepted{0};
        std::atomic<unsigned> completed{0};
        std::vector<std::thread> threads;
        for (int i = 0; i < 32; ++i) {
            threads.emplace_back([&] {
                if (controller.beginDeferred() == AsiBootstrapResult::accepted) {
                    accepted.fetch_add(1, std::memory_order_relaxed);
                    if (controller.publishResidence(true) == AsiBootstrapResult::completed) {
                        completed.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            });
        }
        for (auto& thread : threads) thread.join();
        return accepted.load(std::memory_order_relaxed) == 1 &&
               completed.load(std::memory_order_relaxed) == 1 &&
               controller.state() == AsiBootstrapState::resident;
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2 || !run(argv[1])) {
        std::fprintf(stderr, "ASI bootstrap scenario failed: %s\n", argc == 2 ? argv[1] : "<missing>");
        return 1;
    }
    return 0;
}
