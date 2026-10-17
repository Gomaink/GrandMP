// Compile the actual private readers without the debugger CLI. Only this test
// translation unit can ask the API shim for a successful short transfer.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
BOOL WINAPI inventoryReadProcessMemory(HANDLE, LPCVOID, LPVOID, SIZE_T, SIZE_T*);
#define ReadProcessMemory inventoryReadProcessMemory
#define GRANDMP_NATIVE_COUNTER_IMAGE_TEST
#include "../client/windows/tools/native_counter_transaction.cpp"
#undef ReadProcessMemory

#include <io.h>

namespace {
bool shortenRead = false;
SIZE_T lastRead = 0;
DWORD lastReadError = 0;

void requireGuard(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class StderrCapture final {
public:
    StderrCapture() {
        if (tmpfile_s(&file_) != 0 || !file_) throw std::runtime_error("capture file");
        std::fflush(stderr);
        saved_ = _dup(_fileno(stderr));
        if (saved_ < 0 || _dup2(_fileno(file_), _fileno(stderr)) != 0) {
            if (saved_ >= 0) _close(saved_);
            std::fclose(file_);
            throw std::runtime_error("capture stderr");
        }
    }
    ~StderrCapture() {
        std::fflush(stderr);
        _dup2(saved_, _fileno(stderr));
        _close(saved_);
        std::fclose(file_);
    }
    std::string text() {
        std::fflush(stderr);
        std::rewind(file_);
        std::string result;
        char buffer[512]{};
        while (const auto size = std::fread(buffer, 1, sizeof(buffer), file_)) result.append(buffer, size);
        return result;
    }
private:
    FILE* file_ = nullptr;
    int saved_ = -1;
};

constexpr DWORD permissiveSharing = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

void checkSharing(const std::filesystem::path& path, bool denied) {
    for (const DWORD access : {DWORD{GENERIC_WRITE}, DWORD{DELETE}}) {
        Handle candidate(CreateFileW(path.c_str(), access, permissiveSharing, nullptr, OPEN_EXISTING, 0, nullptr));
        const DWORD error = candidate.valid() ? ERROR_SUCCESS : GetLastError();
        requireGuard(denied ? !candidate.valid() && error == ERROR_SHARING_VIOLATION : candidate.valid(),
            "supervisor lock must exclude both write and delete access, and release both on unload");
    }
    Handle reader(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
    requireGuard(reader.valid(), "compatible read-only open remains permitted");
}

void retainedHandle(const std::filesystem::path& source, const std::filesystem::path& root) {
    const auto path = root / L"retained.exe";
    const auto other = root / L"other.exe";
    std::filesystem::copy_file(source, path);
    std::filesystem::copy_file(source, other);
    // Deliberately permissive: duplicating this event stand-in must fail the
    // sharing assertions after the temporary Image has been destroyed.
    Handle event(CreateFileW(path.c_str(), GENERIC_READ, permissiveSharing, nullptr, OPEN_EXISTING, 0, nullptr));
    requireGuard(event.valid(), "open permissive event stand-in");
    checkSharing(path, false);
    auto record = [&] {
        Image image(path, grandmp::windows::nativeCounterInventoryFileLimit);
        requireGuard(image.sameBackingFile(event.get()), "supervisor open matches event identity and all bytes");
        Handle foreign(CreateFileW(other.c_str(), GENERIC_READ, permissiveSharing, nullptr, OPEN_EXISTING, 0, nullptr));
        requireGuard(foreign.valid() && !image.sameBackingFile(foreign.get()), "byte-identical different file ID refused");
        return ModuleEvent{image.identity(), image.takeBackingFile()};
    }(); // No Image remains alive; only ModuleEvent can enforce the lock now.
    requireGuard(readFile(record.backing.get()) == readFile(event.get()), "retained backing bytes match original event");
    checkSharing(path, true);
    event = Handle();
    checkSharing(path, true);
    DWORD written = 0;
    const char byte = 'M';
    const BOOL writable = WriteFile(record.backing.get(), &byte, 1, &written, nullptr);
    const DWORD writeError = writable ? ERROR_SUCCESS : GetLastError();
    requireGuard(!writable && writeError == ERROR_ACCESS_DENIED, "retained supervisor handle is read-only");
    record.backing = Handle(); // Same lifetime boundary as erasing the unload record.
    checkSharing(path, false);
    // An existing permissive writer must prevent admission of a strict reader.
    Handle writer(CreateFileW(path.c_str(), GENERIC_WRITE, permissiveSharing, nullptr, OPEN_EXISTING, 0, nullptr));
    requireGuard(writer.valid(), "open conflicting pre-existing writer");
    bool refused = false;
    try { Image image(path, grandmp::windows::nativeCounterInventoryFileLimit); }
    catch (const std::runtime_error& error) {
        refused = std::string_view(error.what()) == "open expected image win32=32";
    }
    requireGuard(refused, "supervisor open must fail-closed on sharing conflict");
    std::puts("inventory_retained_handle=pass permissive_event=1 image_destroyed=1 write_delete_blocked=1 unload_releases=1");
}

void readExactCases() {
    const std::array<std::uint8_t, 8> source{1, 2, 3, 4, 5, 6, 7, 8};
    std::array<std::uint8_t, 8> destination{};
    const auto address = reinterpret_cast<std::uintptr_t>(source.data());
    {
        StderrCapture capture;
        requireGuard(readExact(GetCurrentProcess(), address, destination.data(), source.size()) && destination == source,
            "real complete read accepted");
        requireGuard(capture.text().empty(), "complete read has no refusal diagnostic");
    }
    {
        StderrCapture capture;
        shortenRead = true;
        const bool accepted = readExact(GetCurrentProcess(), address, destination.data(), source.size());
        shortenRead = false;
        requireGuard(!accepted && lastRead == 7, "successful API short transfer must be refused");
        char expected[256]{};
        std::snprintf(expected, sizeof(expected),
            "read_refusal operation=ReadProcessMemory address=%08lX requested=8 returned=7 reason=incomplete win32=0\n", address);
        requireGuard(capture.text() == expected, "short success logs exact address/sizes without stale GetLastError");
    }
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    struct Pages {
        void* data;
        ~Pages() { if (data) VirtualFree(data, 0, MEM_RELEASE); }
    } pages{VirtualAlloc(nullptr, 2ULL * system.dwPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)};
    requireGuard(pages.data != nullptr, "allocate own disposable read boundary");
    DWORD previous = 0;
    requireGuard(VirtualProtect(static_cast<char*>(pages.data) + system.dwPageSize, system.dwPageSize,
        PAGE_NOACCESS, &previous) != FALSE, "make second page unreadable");
    std::array<std::uint8_t, 64> crossing{};
    const auto boundary = reinterpret_cast<std::uintptr_t>(pages.data) + system.dwPageSize - 32;
    {
        StderrCapture capture;
        requireGuard(!readExact(GetCurrentProcess(), boundary, crossing.data(), crossing.size()), "real incomplete boundary read refused");
        requireGuard(lastRead < crossing.size() && lastReadError != ERROR_SUCCESS, "real Win32 incomplete/error result observed");
        char expected[256]{};
        std::snprintf(expected, sizeof(expected),
            "read_refusal operation=ReadProcessMemory address=%08lX requested=64 returned=%zu win32=%lu\n",
            boundary, lastRead, lastReadError);
        requireGuard(capture.text() == expected, "real read failure logs address, both sizes and immediate Win32 error");
    }
    std::puts("inventory_read_exact=pass real_complete=1 injected_short_success=1 real_boundary_failure=1");
}
} // namespace

BOOL WINAPI inventoryReadProcessMemory(HANDLE process, LPCVOID address, LPVOID data, SIZE_T length, SIZE_T* read) {
    const BOOL result = ::ReadProcessMemory(process, address, data, shortenRead && length ? length - 1 : length, read);
    const DWORD error = result ? ERROR_SUCCESS : GetLastError();
    lastRead = *read;
    lastReadError = error;
    // A successful Win32 call need not clear the last error. The diagnostic of
    // short success must not report this deliberately stale value as a cause.
    SetLastError(result && shortenRead ? ERROR_ACCESS_DENIED : error);
    return result;
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 4) return 2;
    try {
        const std::wstring_view scenario(argv[1]);
        if (scenario == L"read_exact") readExactCases();
        else if (scenario == L"retained_handle") {
            const auto root = std::filesystem::path(argv[3]) /
                (std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
            std::filesystem::create_directories(root.parent_path());
            requireGuard(std::filesystem::create_directory(root), "unique fixture directory");
            retainedHandle(argv[2], root);
        } else return 2;
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "inventory guard test: %s\n", error.what());
        return 1;
    }
}
