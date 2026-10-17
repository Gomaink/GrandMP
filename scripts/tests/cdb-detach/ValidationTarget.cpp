#include <windows.h>

#include <fstream>
#include <string>

extern "C" __declspec(dllexport) volatile unsigned char GateBytes[5] = {
    0xE8, 0x5A, 0xD5, 0xFF, 0xFF};
extern "C" __declspec(dllexport) volatile unsigned char TargetBytes[6] = {
    0x83, 0xEC, 0x0C, 0x53, 0x56, 0x57};

namespace {
void Mark(const char* name, const char* value) {
    std::ofstream marker(name, std::ios::binary);
    marker << value;
}
}

extern "C" __declspec(noinline) void ProbeFunction() {
    Sleep(150);
}

extern "C" __declspec(noinline) void ValidationPoint() {
    Sleep(10);
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "bad-call") {
        GateBytes[0] = 0x90;
    } else if (argc > 1 && std::string(argv[1]) == "bad-target") {
        TargetBytes[0] = 0x90;
    }

    Mark("ready.txt", "ready");
    Sleep(800);
    ValidationPoint();
    if (argc <= 1 || std::string(argv[1]) != "skip-hit") {
        ProbeFunction();
    }
    Mark("done.txt", "complete");
    return 0;
}
