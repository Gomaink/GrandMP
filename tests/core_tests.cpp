#include "grandmp/core/client_adapter.hpp"
#include "grandmp/core/logger.hpp"
#include "grandmp/core/version.hpp"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace {

using grandmp::core::LogLevel;

struct LogEntry {
    LogLevel level;
    std::string message;
};

class RecordingSink final : public grandmp::core::ILogSink {
public:
    void write(LogLevel level, std::string_view message) override {
        entries.push_back({level, std::string(message)});
    }

    std::vector<LogEntry> entries;
};

class FakeClientAdapter final : public grandmp::core::IClientAdapter {
public:
    bool initialize() override {
        initialized = true;
        return true;
    }

    void shutdown() noexcept override {
        initialized = false;
    }

    grandmp::core::ILogSink& logSink() noexcept override {
        return sink;
    }

    bool initialized = false;
    RecordingSink sink;
};

bool testVersion() {
    const auto current = grandmp::core::version();
    return current.major == 0 && current.minor == 2 && current.patch == 0 &&
           grandmp::core::versionString() == "0.2.0";
}

bool testLogger() {
    RecordingSink sink;
    grandmp::core::Logger logger(sink);
    logger.log(LogLevel::info, "started");
    logger.log(LogLevel::error, "failed");

    return sink.entries.size() == 2 && sink.entries[0].level == LogLevel::info &&
           sink.entries[0].message == "started" && sink.entries[1].level == LogLevel::error &&
           sink.entries[1].message == "failed";
}

bool testAdapter() {
    FakeClientAdapter adapter;
    grandmp::core::IClientAdapter& client = adapter;
    if (!client.initialize() || !adapter.initialized) {
        return false;
    }

    grandmp::core::Logger logger(client.logSink());
    logger.log(LogLevel::debug, "client ready");
    client.shutdown();

    return !adapter.initialized && adapter.sink.entries.size() == 1 &&
           adapter.sink.entries[0].message == "client ready";
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::fputs("expected one test name\n", stderr);
        return 2;
    }

    const std::string_view name(argv[1]);
    const bool passed = (name == "version" && testVersion()) ||
                        (name == "logger" && testLogger()) ||
                        (name == "adapter" && testAdapter());
    if (!passed) {
        std::fprintf(stderr, "test failed or unknown: %.*s\n", static_cast<int>(name.size()),
                     name.data());
        return 1;
    }
    return 0;
}
