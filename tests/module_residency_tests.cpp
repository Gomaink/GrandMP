#include "grandmp/windows/module_residency.hpp"

#include <cstdio>
#include <memory>
#include <string_view>

namespace {

using namespace grandmp::windows;

class FakePinApi final : public IModulePinApi {
public:
    void* acquired = reinterpret_cast<void*>(0x1000);
    void* pinned = reinterpret_cast<void*>(0x1000);
    int acquireCalls = 0;
    int pinCalls = 0;
    int releaseCalls = 0;

    void* acquireContaining(const void*) noexcept override { ++acquireCalls; return acquired; }
    void* pinContaining(const void*) noexcept override { ++pinCalls; return pinned; }
    void releaseAcquired(void*) noexcept override { ++releaseCalls; }
};

constexpr auto hoodlum = GameVersion::gta_sa_10us_hoodlum;
constexpr auto loader = LoaderIdentity::smoke_ual_win32_770;
void* const expected = reinterpret_cast<void*>(0x1000);
const void* const anchor = reinterpret_cast<const void*>(0x2000);

bool onceAndResident() {
    ModuleResidency residency;
    FakePinApi api;
    return residency.pinHoodlum(hoodlum, loader, expected, anchor, api) == ResidenceResult::pinned &&
           residency.state() == ResidenceState::pinned && residency.permitsFutureHookPreparation() &&
           residency.pinHoodlum(hoodlum, loader, expected, anchor, api) == ResidenceResult::already_pinned &&
           api.acquireCalls == 1 && api.pinCalls == 1 && api.releaseCalls == 1;
}

bool rejectsUnknown() {
    ModuleResidency game;
    ModuleResidency asiLoader;
    FakePinApi gameApi;
    FakePinApi loaderApi;
    return game.pinHoodlum(GameVersion::unknown, loader, expected, anchor, gameApi) == ResidenceResult::unsupported_game &&
           asiLoader.pinHoodlum(hoodlum, LoaderIdentity::unknown, expected, anchor, loaderApi) == ResidenceResult::unsupported_loader &&
           gameApi.acquireCalls == 0 && loaderApi.acquireCalls == 0 &&
           !game.permitsFutureHookPreparation() && !asiLoader.permitsFutureHookPreparation();
}

bool rejectsWrongModule() {
    ModuleResidency residency;
    FakePinApi api;
    api.acquired = reinterpret_cast<void*>(0x3000);
    return residency.pinHoodlum(hoodlum, loader, expected, anchor, api) == ResidenceResult::module_mismatch &&
           residency.state() == ResidenceState::failed && api.pinCalls == 0 && api.releaseCalls == 1 &&
           !residency.permitsFutureHookPreparation();
}

bool pinFailureDoesNotRetry() {
    ModuleResidency residency;
    FakePinApi api;
    api.pinned = nullptr;
    return residency.pinHoodlum(hoodlum, loader, expected, anchor, api) == ResidenceResult::pin_failed &&
           residency.pinHoodlum(hoodlum, loader, expected, anchor, api) == ResidenceResult::previous_failure &&
           api.pinCalls == 1 && api.releaseCalls == 1 && !residency.permitsFutureHookPreparation();
}

bool noPinForInvalidIdentity() {
    ModuleResidency residency;
    FakePinApi api;
    return residency.pinHoodlum(hoodlum, loader, nullptr, anchor, api) == ResidenceResult::invalid_identity &&
           api.acquireCalls == 0 && api.pinCalls == 0 && residency.state() == ResidenceState::failed;
}

bool noAutomaticCleanup() {
    FakePinApi api;
    {
        ModuleResidency residency;
        if (residency.pinHoodlum(hoodlum, loader, expected, anchor, api) != ResidenceResult::pinned) return false;
    }
    // The one release balances the temporary acquired reference. The policy
    // neither unpins nor patches memory when its own object is destroyed.
    return api.acquireCalls == 1 && api.pinCalls == 1 && api.releaseCalls == 1;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) return 2;
    const std::string_view name(argv[1]);
    const bool passed = (name == "once" && onceAndResident()) ||
                        (name == "unknown" && rejectsUnknown()) ||
                        (name == "module" && rejectsWrongModule()) ||
                        (name == "failure" && pinFailureDoesNotRetry()) ||
                        (name == "identity" && noPinForInvalidIdentity()) ||
                        (name == "cleanup" && noAutomaticCleanup());
    if (!passed) {
        std::fprintf(stderr, "module residency test failed: %.*s\n", static_cast<int>(name.size()), name.data());
        return 1;
    }
    return 0;
}
