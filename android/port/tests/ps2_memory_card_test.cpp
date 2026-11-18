// Prueba de la HLE de Memory Card: init, getInfo, open/read/write/seek,
// sync asincrono, directorios, listado, borrado y proteccion de rutas.
// Compila con:
//   clang++ -std=c++17 -O2 -Isrc tests/ps2_memory_card_test.cpp `
//     src/ps2_memory_card.cpp -o mc_test.exe

#include "ps2_memory_card.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void check(bool condition, const char *name) {
    std::printf("  %-52s %s\n", name, condition ? "PASS" : "FAIL");
    if (!condition) ++failures;
}

void put_string(std::vector<std::uint8_t> &ee, std::uint32_t address,
                const std::string &text) {
    std::memcpy(ee.data() + address, text.c_str(), text.size() + 1);
}

std::uint32_t get_u32(const std::vector<std::uint8_t> &ee,
                      std::uint32_t address) {
    std::uint32_t value = 0;
    std::memcpy(&value, ee.data() + address, sizeof(value));
    return value;
}

std::string get_string(const std::vector<std::uint8_t> &ee,
                       std::uint32_t address) {
    return std::string(reinterpret_cast<const char *>(ee.data() + address));
}

int sync(ps2::mc::Ps2MemoryCard &mc, std::uint32_t cmd_addr,
         std::uint32_t result_addr) {
    return mc.Sync(1, cmd_addr, result_addr);
}

}  // namespace

int main() {
    const fs::path root =
        fs::temp_directory_path() / "ps2mc_test_onimusha2";
    fs::remove_all(root);

    constexpr std::uint32_t kName = 0x0100;
    constexpr std::uint32_t kName2 = 0x0300;
    constexpr std::uint32_t kBuffer = 0x0400;
    constexpr std::uint32_t kCmd = 0x0800;
    constexpr std::uint32_t kResult = 0x0804;
    constexpr std::uint32_t kType = 0x0808;
    constexpr std::uint32_t kFree = 0x080C;
    constexpr std::uint32_t kFormat = 0x0810;
    constexpr std::uint32_t kTable = 0x1000;
    constexpr std::uint32_t kCurrent = 0x0900;

    std::vector<std::uint8_t> ee(1u << 20, 0);
    ps2::mc::Ps2MemoryCard mc;
    check(mc.Initialize(root.generic_string()), "Initialize");
    mc.SetEeMemory(ee.data(), static_cast<std::uint32_t>(ee.size()));
    check(fs::exists(root / "memcard0"), "crea memcard0");

    std::printf("\n=== Init/GetInfo ===\n");
    check(mc.Init(0) == 0, "sceMcInit devuelve 0");
    mc.GetInfo(0, 0, kType, kFree, kFormat);
    check(mc.PendingCommand() == ps2::mc::kFuncGetInfo,
          "GetInfo queda pendiente");
    check(sync(mc, kCmd, kResult) == 1, "Sync completa GetInfo");
    check(get_u32(ee, kResult) == 0, "resultado GetInfo = 0");
    check(get_u32(ee, kType) == ps2::mc::kTypePS2, "tipo PS2");
    check(get_u32(ee, kFree) > 0, "espacio libre > 0");
    check(get_u32(ee, kFormat) == 1, "formateada");
    check(sync(mc, kCmd, kResult) == -1, "Sync sin operacion = -1");

    std::printf("\n=== Open/Write/Seek/Read/Close ===\n");
    put_string(ee, kName, "BISLPS-TEST/save.dat");
    const int mode = ps2::mc::kOpenRead | ps2::mc::kOpenWrite |
                     ps2::mc::kOpenCreate;
    check(mc.Open(0, 0, kName, mode) == 0, "Open aceptado");
    check(sync(mc, kCmd, kResult) == 1, "Sync Open");
    const int fd = static_cast<int>(get_u32(ee, kResult));
    check(fd >= 0, "fd >= 0");

    for (int i = 0; i < 64; ++i) {
        ee[kBuffer + i] = static_cast<std::uint8_t>(i * 3);
    }
    check(mc.Write(fd, kBuffer, 64) == 0, "Write aceptado");
    check(sync(mc, kCmd, kResult) == 1 &&
              get_u32(ee, kResult) == 64,
          "Write 64 bytes");

    check(mc.Seek(fd, 0, 0) == 0, "Seek aceptado");
    check(sync(mc, kCmd, kResult) == 1 && get_u32(ee, kResult) == 0,
          "Seek al inicio");

    std::memset(ee.data() + kBuffer, 0, 64);
    check(mc.Read(fd, kBuffer, 64) == 0, "Read aceptado");
    check(sync(mc, kCmd, kResult) == 1 &&
              get_u32(ee, kResult) == 64,
          "Read 64 bytes");
    bool pattern_ok = true;
    for (int i = 0; i < 64; ++i) {
        if (ee[kBuffer + i] != static_cast<std::uint8_t>(i * 3)) {
            pattern_ok = false;
        }
    }
    check(pattern_ok, "datos leidos coinciden");

    check(mc.Close(fd) == 0, "Close aceptado");
    check(sync(mc, kCmd, kResult) == 1 && get_u32(ee, kResult) == 0,
          "Close OK");

    const fs::path save_path =
        root / "memcard0" / "BISLPS-TEST" / "save.dat";
    check(fs::exists(save_path) && fs::file_size(save_path) == 64,
          "archivo persistido en memcard0");

    std::printf("\n=== Reapertura y errores ===\n");
    check(mc.Open(0, 0, kName, ps2::mc::kOpenRead) == 0, "Open read-only");
    check(sync(mc, kCmd, kResult) == 1, "Sync Open read-only");
    const int fd2 = static_cast<int>(get_u32(ee, kResult));
    check(fd2 >= 0, "fd read-only >= 0");
    mc.Close(fd2);
    sync(mc, kCmd, kResult);

    put_string(ee, kName, "BISLPS-TEST/no-existe.dat");
    mc.Open(0, 0, kName, ps2::mc::kOpenRead);
    sync(mc, kCmd, kResult);
    check(static_cast<int>(get_u32(ee, kResult)) < 0,
          "abrir inexistente falla");

    put_string(ee, kName, "../evil.dat");
    mc.Open(0, 0, kName, ps2::mc::kOpenCreate | ps2::mc::kOpenWrite);
    sync(mc, kCmd, kResult);
    check(static_cast<int>(get_u32(ee, kResult)) < 0,
          "rechaza salir de memcard0");
    check(!fs::exists(root / "evil.dat") &&
              !fs::exists(root.parent_path() / "evil.dat"),
          "no crea archivo fuera");

    std::printf("\n=== Directorios y GetDir ===\n");
    put_string(ee, kName, "BISLPS-DIR");
    check(mc.MkDir(0, 0, kName) == 0, "MkDir aceptado");
    check(sync(mc, kCmd, kResult) == 1 &&
              get_u32(ee, kResult) == 0,
          "MkDir OK");
    check(fs::is_directory(root / "memcard0" / "BISLPS-DIR"),
          "directorio creado");

    check(mc.ChDir(0, 0, kName, kCurrent) == 0, "ChDir aceptado");
    check(sync(mc, kCmd, kResult) == 1 &&
              get_u32(ee, kResult) == 0,
          "ChDir OK");
    check(get_string(ee, kCurrent).find("BISLPS-DIR") != std::string::npos,
          "ChDir devuelve ruta actual");

    put_string(ee, kName2, "inside.dat");
    mc.Open(0, 0, kName2, ps2::mc::kOpenCreate | ps2::mc::kOpenWrite);
    sync(mc, kCmd, kResult);
    const int fd3 = static_cast<int>(get_u32(ee, kResult));
    check(fd3 >= 0, "Open relativo al directorio actual");
    if (fd3 >= 0) {
        mc.Write(fd3, kBuffer, 16);
        sync(mc, kCmd, kResult);
        mc.Close(fd3);
        sync(mc, kCmd, kResult);
    }
    check(fs::exists(root / "memcard0" / "BISLPS-DIR" / "inside.dat"),
          "archivo dentro del directorio");

    put_string(ee, kName, "/");
    mc.ChDir(0, 0, kName, 0);
    sync(mc, kCmd, kResult);

    put_string(ee, kName, "BISLPS-DIR");
    check(mc.GetDir(0, 0, kName, 0, 8, kTable) == 0,
          "GetDir aceptado");
    check(sync(mc, kCmd, kResult) == 1 &&
              get_u32(ee, kResult) >= 1,
          "GetDir devuelve entradas");
    check(get_string(ee, kTable + 32) == "inside.dat",
          "nombre de entrada correcto");

    std::printf("\n=== Delete/Format ===\n");
    put_string(ee, kName, "BISLPS-DIR/inside.dat");
    check(mc.Delete(0, 0, kName) == 0, "Delete aceptado");
    check(sync(mc, kCmd, kResult) == 1 &&
              get_u32(ee, kResult) == 0,
          "Delete OK");
    check(!fs::exists(root / "memcard0" / "BISLPS-DIR" / "inside.dat"),
          "archivo borrado");

    put_string(ee, kName, "BISLPS-DIR");
    mc.Delete(0, 0, kName);
    sync(mc, kCmd, kResult);
    check(!fs::exists(root / "memcard0" / "BISLPS-DIR"),
          "directorio vacio borrado");

    put_string(ee, kName, "BISLPS-TEST/save.dat");
    mc.Open(0, 0, kName, ps2::mc::kOpenCreate | ps2::mc::kOpenWrite);
    sync(mc, kCmd, kResult);
    mc.Close(static_cast<int>(get_u32(ee, kResult)));
    sync(mc, kCmd, kResult);

    check(mc.Format(0, 0) == 0, "Format aceptado");
    check(sync(mc, kCmd, kResult) == 1 &&
              get_u32(ee, kResult) == 0,
          "Format OK");
    check(!fs::exists(root / "memcard0" / "BISLPS-TEST"),
          "Format borra el contenido");

    std::printf("\n=== Dispatcher por direccion ===\n");
    const fs::path root2 = root / "dispatch";
    check(ps2::mc::System().Initialize(root2.generic_string()),
          "System Initialize");
    ps2::mc::SetEeMemory(ee.data(), static_cast<std::uint32_t>(ee.size()));

    std::uint32_t ret = 0xFFFFFFFFu;
    check(ps2::mc::Invoke(0x0010E288u, 0, 0, 0, 0, &ret) && ret == 0,
          "Invoke sceMcInit");
    check(ps2::mc::Invoke(0x0010EDC0u, 0, 0, kType, kFree, &ret) &&
              ret == 0,
          "Invoke sceMcGetInfo");
    check(ps2::mc::Invoke(0x0010EC88u, 1, kCmd, kResult, 0, &ret) &&
              ret == 1,
          "Invoke sceMcSync");
    check(!ps2::mc::Invoke(0x00100000u, 0, 0, 0, 0, &ret),
          "Invoke ignora direccion ajena");

    ps2::mc::System().Shutdown();
    mc.Shutdown();
    fs::remove_all(root);

    std::printf("\nFALLOS: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
