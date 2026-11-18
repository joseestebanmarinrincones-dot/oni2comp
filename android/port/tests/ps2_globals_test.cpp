// Prueba de ps2_globals.h sin SDL ni GL.
// Compila con:
//   clang++ -std=c++17 -O2 -Isrc tests/ps2_globals_test.cpp src/ps2_mem.cpp

#include "ps2_globals.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

int main() {
    using namespace ps2;

    Ps2Memory::Reset();

    // 1. Los macros apuntan exactamente al offset de ps2_ram.
    if (&uRam017e0350 !=
        reinterpret_cast<std::uint32_t *>(ps2_ram + 0x0017E0350u)) return 1;
    if (&iGpffff9130 !=
        reinterpret_cast<std::uint32_t *>(ps2_ram + 0x001849130u)) return 2;
    if (&sGpffff93f8 !=
        reinterpret_cast<std::int16_t *>(ps2_ram + 0x0018493F8u)) return 3;
    if (&sGpffff941c !=
        reinterpret_cast<std::uint16_t *>(ps2_ram + 0x00184941Cu)) return 4;

    // 2. Escritura/lectura escalar.
    uRam017e0350 = 0x12345678u;
    if (PS2_MEM_READ<std::uint32_t>(0x0017E0350u) != 0x12345678u) return 5;

    iGpffff9130 = 1;
    if (PS2_MEM_READ<std::uint32_t>(0x001849130u) != 1u) return 6;

    sGpffff93f8 = -12;
    sGpffff93fc = 240;
    if (PS2_MEM_READ<std::int16_t>(0x0018493F8u) != -12) return 7;
    if (PS2_MEM_READ<std::int16_t>(0x0018493FCu) != 240) return 8;

    // 3. Array de flags de 6 bytes.
    for (std::uint32_t i = 0; i < 6; ++i) {
        g_frame_flags[i] = static_cast<std::uint8_t>(i + 1u);
    }
    if (PS2_MEM_READ<std::uint8_t>(0x00172A378u + 5u) != 6u) return 9;

    // 4. Array de canales DMA.
    g_dma_channels[2] = 0x1000A000u;
    if (PS2_MEM_READ<std::uint32_t>(0x0018263E0u + 8u) != 0x1000A000u) return 10;

    // 5. Cadena de modulo.
    std::strcpy(const_cast<char *>(g_module_sio2man), "SIO2MAN.IRX");
    if (std::strcmp(g_module_sio2man, "SIO2MAN.IRX") != 0) return 11;
    if (PS2_MEM_READ<char>(0x002DCC10u) != 'S') return 12;

    // 6. Cola del cargador (0x18 bytes por entrada).
    g_loader_queue[0].type = 1;
    g_loader_queue[0].file_index = 0x0301;
    if (PS2_MEM_READ<std::uint8_t>(0x001847790u) != 1u) return 13;
    if (PS2_MEM_READ<std::uint16_t>(0x001847792u) != 0x0301u) return 14;

    // 7. Tabla de estados de carga.
    PS2_MEM_WRITE<std::uint32_t>(0x002BA0E0u + 4u, 0x00125740u);
    if (g_load_state_table[1] != 0x00125740u) return 15;

    // 8. Tabla de opcodes.
    PS2_MEM_WRITE<std::uint32_t>(0x002BA120u + 0x7Fu * 4u, 0x00123456u);
    if (g_opcode_table_a[0x7Fu] != 0x00123456u) return 16;

    // 9. Paquete GIF estatico.
    g_static_gif_packet[0] = 0x0E;
    g_static_gif_packet[239] = 0xAA;
    if (PS2_MEM_READ<std::uint8_t>(0x002C4040u) != 0x0E) return 17;
    if (PS2_MEM_READ<std::uint8_t>(0x002C4040u + 239u) != 0xAA) return 18;

    // 10. Bloques de 64 bits.
    DAT_002c40f0 = 0x0123456789ABCDEFull;
    if (PS2_MEM_READ<std::uint64_t>(0x002C40F0u) != 0x0123456789ABCDEFull) return 19;

    std::printf("PASS ps2_globals: offsets, escalares, arrays, strings y tablas\n");
    return 0;
}
