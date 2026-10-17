// Exercise the exact private Image/readFile implementation without its CLI,
// starting a process, issuing a debug event, or calling the transaction engine.
#define GRANDMP_NATIVE_COUNTER_IMAGE_TEST
#include "../client/windows/tools/native_counter_transaction.cpp"

namespace {
void requireInventory(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Action>
void rejectsInventory(Action action, const char* message) {
    try { action(); } catch (const std::runtime_error&) { return; }
    throw std::runtime_error(message);
}
}

int wmain(int argc, wchar_t** argv) {
    using namespace grandmp::windows;
    if (argc != 3) return 2;
    try {
        const auto root = std::filesystem::path(argv[2]) /
            (std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        std::filesystem::create_directories(root.parent_path());
        requireInventory(std::filesystem::create_directory(root), "unique fixture directory");
        Handle source(CreateFileW(argv[1], GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
        auto bytes = readFile(source.get());
        const auto writeFixture = [&](const wchar_t* name, std::uint32_t size, std::uint8_t tail = 0) {
            const auto path = root / name;
            bytes.resize(size, 0);
            if (size) bytes.back() = tail;
            Handle output(CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, 0, nullptr));
            DWORD written = 0;
            requireInventory(output.valid() && WriteFile(output.get(), bytes.data(), size, &written, nullptr) &&
                written == size, "write own new fixture");
            return path;
        };
        const auto openFixture = [](const std::filesystem::path& path) {
            return Handle(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
        };
        const auto atDefault = writeFixture(L"at-32MiB.exe", nativeCounterAdmissionFileLimit);
        { Image image(atDefault);
          requireInventory(image.evidence().bytes.size() == nativeCounterAdmissionFileLimit, "default inclusive boundary");
          requireInventory(readNativeCounterFile(atDefault).has_value(), "fingerprint default boundary"); }
        const auto aboveDefault = writeFixture(L"above-32MiB.exe", nativeCounterAdmissionFileLimit + 1, 1);
        rejectsInventory([&] { Image image(aboveDefault); }, "default Image must reject above 32MiB");
        requireInventory(!readNativeCounterFile(aboveDefault), "default fingerprint must reject above 32MiB");
        std::array<std::byte, 32> firstDigest{};
        { Image image(aboveDefault, nativeCounterInventoryFileLimit);
          auto file = openFixture(aboveDefault);
          requireInventory(image.sameBackingFile(file.get()), "inventory re-read must use the same limit");
          requireInventory(image.evidence().inspection.status == PeStatus::pe32_i386 &&
              image.evidence().bytes.size() == nativeCounterAdmissionFileLimit + 1, "large PE32 admitted in full");
          firstDigest = image.evidence().digest;
          // A reader holds the same write/delete exclusion as before.
          Handle denied(CreateFileW(aboveDefault.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr, OPEN_EXISTING, 0, nullptr));
          requireInventory(!denied.valid(), "admitted backing file must stay locked against writes");
          requireInventory(!image.sameBackingFile(source.get()), "different file ID must be rejected"); }
        const auto changed = writeFixture(L"changed-tail.exe", nativeCounterAdmissionFileLimit + 1, 2);
        { Image image(changed, nativeCounterInventoryFileLimit);
          requireInventory(image.evidence().digest != firstDigest, "hash must include bytes beyond 32MiB"); }
        const auto atMaximum = writeFixture(L"at-64MiB.exe", nativeCounterInventoryFileLimit);
        { Image image(atMaximum, nativeCounterInventoryFileLimit);
          auto file = openFixture(atMaximum);
          requireInventory(image.evidence().bytes.size() == nativeCounterInventoryFileLimit &&
              image.sameBackingFile(file.get()), "inventory inclusive boundary and full byte comparison"); }
        const auto aboveMaximum = writeFixture(L"above-64MiB.exe", nativeCounterInventoryFileLimit + 1);
        rejectsInventory([&] { Image image(aboveMaximum, nativeCounterInventoryFileLimit); }, "inventory must reject above cap");
        requireInventory(!readNativeCounterFile(aboveMaximum, nativeCounterInventoryFileLimit), "fingerprint hard cap");
        for (const auto invalid : {0U, nativeCounterInventoryFileLimit + 1}) {
            rejectsInventory([&] { Image image(atDefault, invalid); }, "Image must reject invalid requested bound");
            requireInventory(!readNativeCounterFile(atDefault, invalid), "fingerprint must reject invalid requested bound");
        }
        bytes[0] = 0; // Only our copied host bytes; no system/game image is changed.
        const auto malformed = writeFixture(L"malformed-large.exe", nativeCounterAdmissionFileLimit + 1);
        rejectsInventory([&] { Image image(malformed, nativeCounterInventoryFileLimit); }, "large invalid PE must be rejected");
        const auto malformedEvidence = readNativeCounterFile(malformed, nativeCounterInventoryFileLimit);
        requireInventory(malformedEvidence && malformedEvidence->inspection.status == PeStatus::invalid,
                         "larger bound must not change PE inspection");
        const auto empty = writeFixture(L"empty.exe", 0);
        rejectsInventory([&] { Image image(empty, nativeCounterInventoryFileLimit); }, "empty Image must be rejected");
        requireInventory(!readNativeCounterFile(empty, nativeCounterInventoryFileLimit), "empty fingerprint must be rejected");
        requireInventory(!readNativeCounterFile(root / L"missing.exe", nativeCounterInventoryFileLimit), "missing file");
        std::printf("inventory_limits_passed=1 default=33554432 inventory=67108864 attach=0 patch=0\n");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "inventory file test: %s\n", error.what());
        return 1;
    }
}
