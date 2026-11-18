// Prueba del crash handler: ultima FUN y formato de crash_log.txt.
// En Android usa sigaction; en PC usa el fallback stdio para poder probar el
// contenido sin provocar un SIGSEGV real.
// Compila con:
//   clang++ -std=c++17 -O2 -Isrc tests/crash_handler_test.cpp `
//     src/crash_handler.cpp -o crash_test.exe

#include "crash_handler.hpp"

#include <csignal>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void check(bool condition, const char *name) {
    std::printf("  %-52s %s\n", name, condition ? "PASS" : "FAIL");
    if (!condition) ++failures;
}

std::string read_all(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

bool contains(const std::string &text, const char *needle) {
    return text.find(needle) != std::string::npos;
}

}  // namespace

int main() {
    const fs::path log = fs::temp_directory_path() / "onimusha_crash_test.txt";
    fs::remove(log);

    ps2::crash::SetLastFunction(0x00123456u);
    check(ps2::crash::LastFunction() == 0x00123456u,
          "SetLastFunction/LastFunction");

    ps2::crash::Install(log.string().c_str());
    check(std::strcmp(ps2::crash::LogPath(), log.string().c_str()) == 0,
          "Install fija la ruta");

    ps2::crash::WriteCrashLog(SIGSEGV, 0x12345678u, 0xDEADBEEFu,
                              0x00123456u);
    const std::string text = read_all(log);
    check(contains(text, "signal=SIGSEGV(11)"), "nombre de senal");
    check(contains(text, "last_function=0x00123456"),
          "direccion de ultima funcion");
    check(contains(text, "FUN_00123456"), "formato FUN_xxxxxxxx");
    check(contains(text, "pc=0x0000000012345678"), "PC");
    check(contains(text, "fault_addr=0x00000000deadbeef"), "direccion fault");

    ps2::crash::Shutdown();
    fs::remove(log);

    std::printf("\nFALLOS: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
