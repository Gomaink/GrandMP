#pragma once

#include <string_view>

namespace grandmp::core {

enum class LogLevel {
    debug,
    info,
    warning,
    error,
};

class ILogSink {
public:
    virtual ~ILogSink() = default;

    // A implementação deve copiar a mensagem se precisar guardá-la após write().
    virtual void write(LogLevel level, std::string_view message) = 0;
};

class Logger {
public:
    explicit Logger(ILogSink& sink) noexcept;

    void log(LogLevel level, std::string_view message);

private:
    ILogSink& sink_;
};

}  // namespace grandmp::core
