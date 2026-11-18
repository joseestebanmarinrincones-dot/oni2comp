// Prueba de ps2_sdk_stubs sin SDL ni GL.
// Compila con:
//   clang++ -std=c++17 -O2 -I src tests/ps2_sdk_stubs_test.cpp src/ps2_sdk_stubs.cpp

#include "ps2_sdk_stubs.h"

#include <cstdint>
#include <cstdio>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
    if (!condition) {
        std::printf("FAIL: %s\n", message);
        ++failures;
    }
}

}  // namespace

int main() {
    using namespace ps2::sdk;

    // Librerias con HLE nativa.
    expect(IsNativeAddress(0x00100358u), "libgraph 0x100358 debe ser nativa");
    expect(LibraryOf(0x00100358u) == Library::LibGraph, "0x100358 -> libgraph");
    expect(IsNativeAddress(0x00100150u), "sceGsResetGraph 0x100150 debe ser libgraph");
    expect(LibraryOf(0x00100150u) == Library::LibGraph, "0x100150 -> libgraph");
    expect(IsNativeAddress(0x00101668u), "libdma 0x101668 debe ser nativa");
    expect(LibraryOf(0x00101668u) == Library::LibDma, "0x101668 -> libdma");
    expect(IsNativeAddress(0x0010C7D8u), "libcdvd 0x10C7D8 debe ser nativa");
    expect(LibraryOf(0x0010C7D8u) == Library::LibCdvd, "0x10C7D8 -> libcdvd");
    expect(!IsNativeAddress(0x0010C5F0u), "0x10C5F0 es exit, no libcdvd");
    expect(LibraryOf(0x0010C5F0u) == Library::LibC, "0x10C5F0 -> libc");
    expect(IsNativeAddress(0x0010E288u), "libmc 0x10E288 debe ser nativa");
    expect(LibraryOf(0x0010E288u) == Library::LibMc, "0x10E288 -> libmc");
    expect(IsNativeAddress(0x0025C650u), "libsd 0x25C650 debe ser nativa");
    expect(LibraryOf(0x0025C650u) == Library::LibSd, "0x25C650 -> libsd");
    expect(IsNativeAddress(0x00285398u), "libpad2 0x285398 debe ser nativa");
    expect(LibraryOf(0x00285398u) == Library::LibPad2, "0x285398 -> libpad2");

    // Identificadas pero no interceptadas todavia.
    expect(IsSdkAddress(0x00100008u), "crt entry debe estar identificada");
    expect(!IsNativeAddress(0x00100008u), "crt no debe interceptarse");
    expect(LibraryOf(0x00100008u) == Library::Crt, "0x100008 -> crt");
    expect(IsSdkAddress(0x00101FE8u), "libvu0 0x101FE8 debe estar identificada");
    expect(!IsNativeAddress(0x00101FE8u), "libvu0 no debe interceptarse");
    expect(IsSdkAddress(0x00103360u), "libkernel 0x103360 debe estar identificada");
    expect(!IsNativeAddress(0x00103360u), "libkernel no debe interceptarse");
    expect(LibraryOf(0x00103360u) == Library::LibKernel, "0x103360 -> libkernel");

    // Falsos positivos que no deben aparecer.
    expect(!IsSdkAddress(0x00149BF0u), "0x149BF0 es init del juego");
    expect(!IsSdkAddress(0x00160A30u), "0x160A30 es wrapper del juego");
    expect(!IsSdkAddress(0x0025C610u), "0x25C610 es codigo del juego");
    expect(!IsSdkAddress(0x0025C620u), "0x25C620 es codigo del juego");
    expect(!IsSdkAddress(0x0025C640u), "0x25C640 es codigo del juego");
    expect(!IsSdkAddress(0x0025C950u), "0x25C950 es codigo del juego");

    // Busqueda por direccion contenida en el cuerpo.
    expect(Find(0x00100358u) != nullptr, "Find exacto");
    expect(Find(0x00100359u) == nullptr, "Find no acepta direcciones internas");
    expect(FindContaining(0x00100359u) != nullptr, "FindContaining acepta el cuerpo");
    expect(LibraryOf(0x00100B29u) == Library::LibGraph, "LibraryOf por cuerpo");

    // Stub nativo por defecto: devuelve 0 y no ejecuta el codigo PS2.
    std::uint32_t result = 0xFFFFFFFFu;
    expect(Call(0x00100358u, 0, 0, 0, 0, &result), "Call libgraph debe responder");
    expect(result == 0u, "stub libgraph devuelve 0");
    expect(!Call(0x00103360u, 0, 0, 0, 0, &result), "Call libkernel no debe interceptar");

    // Hook global: permite enlazar una funcion concreta a C++/SDL2.
    SetLogging(false);
    SetHook([](const Function &function, std::uint32_t, std::uint32_t,
               std::uint32_t, std::uint32_t, std::uint32_t *out) -> bool {
        if (function.address == 0x00100358u) {
            *out = 42u;
            return true;
        }
        return false;
    });
    result = 0;
    expect(Call(0x00100358u, 0, 0, 0, 0, &result), "Call con hook global");
    expect(result == 42u, "hook global escribe el resultado");
    ClearHooks();

    // Hook por libreria.
    SetLibraryHook(Library::LibDma,
        [](const Function &, std::uint32_t, std::uint32_t, std::uint32_t,
           std::uint32_t, std::uint32_t *out) -> bool {
            *out = 7u;
            return true;
        });
    result = 0;
    expect(Call(0x00101668u, 0, 0, 0, 0, &result), "Call con hook de libdma");
    expect(result == 7u, "hook de libdma escribe el resultado");
    ClearHooks();

    if (failures == 0) {
        std::printf("PASS ps2_sdk_stubs: tabla, librerias, falsos positivos y hooks\n");
        return 0;
    }
    std::printf("FAIL ps2_sdk_stubs: %d fallos\n", failures);
    return 1;
}
