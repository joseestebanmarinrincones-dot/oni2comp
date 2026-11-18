// Prueba de game_stubs sin SDL ni GL.
// Compila con:
//   clang++ -std=c++17 -O2 -DGAME_MAIN_NO_SDL -Isrc `
//     tests/game_stubs_test.cpp src/game_stubs.cpp

#include "game_stubs.h"

#include <cstdint>
#include <cstdio>

int main() {
    using namespace game;

    // Direcciones de main con stub.
    if (!stubs::Has(0x001701B0u)) return 1;
    if (!stubs::Has(0x0019AE90u)) return 2;

    // Direcciones nativas: declaradas pero no interceptadas por Call().
    if (!stubs::Has(0x00124D90u)) return 3;
    if (stubs::Call(0x00124D90u, 0, 0, 0, 0, nullptr)) return 4;
    if (stubs::Call(0x0014A340u, 0, 0, 0, 0, nullptr)) return 5;

    // Stub normal: devuelve 0.
    std::uint32_t result = 0xFFFFFFFFu;
    if (!stubs::Call(0x001701B0u, 0, 0, 0, 0, &result)) return 6;
    if (result != 0u) return 7;

    // Llamada directa a la funcion generada.
    if (FUN_001701B0() != 0u) return 8;
    if (FUN_0019AE90(1, 2, 3, 4) != 0u) return 9;

    // Init stub.
    FUN_00149BF0();

    if (stubs::NameOf(0x001701B0u)[0] != 'F') return 10;
    if (stubs::NameOf(0x00000000u)[0] != 'u') return 11;

    std::printf("PASS game_stubs: tabla, nativas, retorno 0 y nombres\n");
    return 0;
}
