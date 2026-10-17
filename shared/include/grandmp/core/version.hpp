#pragma once

#include <string>

namespace grandmp::core {

struct Version {
    int major;
    int minor;
    int patch;
};

[[nodiscard]] Version version() noexcept;
[[nodiscard]] std::string versionString();

}  // namespace grandmp::core
