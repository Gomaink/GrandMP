#include "grandmp/core/logger.hpp"

namespace grandmp::core {

Logger::Logger(ILogSink& sink) noexcept : sink_(sink) {}

void Logger::log(LogLevel level, std::string_view message) {
    sink_.write(level, message);
}

}  // namespace grandmp::core
