#include "grandmp/core/version.hpp"

#include <string>

namespace grandmp::core {

Version version() noexcept {
    return {GRANDMP_VERSION_MAJOR, GRANDMP_VERSION_MINOR, GRANDMP_VERSION_PATCH};
}

std::string versionString() {
    const Version current = version();
    return std::to_string(current.major) + "." + std::to_string(current.minor) + "." +
           std::to_string(current.patch);
}

}  // namespace grandmp::core
