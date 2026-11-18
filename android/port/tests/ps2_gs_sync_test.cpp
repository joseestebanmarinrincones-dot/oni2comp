// Prueba de ps2_gs_sync sin SDL ni GL.
// Compila con:
//   clang++ -std=c++17 -O2 -Isrc tests/ps2_gs_sync_test.cpp src/ps2_gs_sync.cpp src/ps2_mem.cpp

#include "ps2_gs_sync.hpp"

#include "ps2_mem.hpp"

#include <cstdint>
#include <cstdio>

int main() {
    using namespace ps2;

    Ps2Memory::Reset();

    // Estado "ocupado" como lo dejaria el hardware original.
    // VIF0 no lo espera sceGsSyncPath: debe quedar intacto.
    PS2_MEM_WRITE<std::uint32_t>(0x10008000u,       0x00000100u);
    PS2_MEM_WRITE<std::uint32_t>(gs::kDmaVif1Chcr, 0x00000100u);
    PS2_MEM_WRITE<std::uint32_t>(gs::kDmaGifChcr,  0x00000100u);
    PS2_MEM_WRITE<std::uint32_t>(gs::kDmaStat,     0x00000007u);
    PS2_MEM_WRITE<std::uint32_t>(gs::kVif1Stat,    0x001F0003u);
    PS2_MEM_WRITE<std::uint32_t>(gs::kGifMode,     0x00000C00u);
    PS2_MEM_WRITE<std::uint64_t>(gs::kGsCsr,       0u);
    PS2_MEM_WRITE<std::uint32_t>(gs::kVSyncFlag,   0u);

    // sceGsSyncPath(0) debe completar instantaneamente.
    if (gs::sceGsSyncPath(0) != 0u) return 1;

    if ((PS2_MEM_READ<std::uint32_t>(0x10008000u) & 0x100u) == 0u) return 2;
    if ((PS2_MEM_READ<std::uint32_t>(gs::kDmaVif1Chcr) & 0x100u) != 0u) return 3;
    if ((PS2_MEM_READ<std::uint32_t>(gs::kDmaGifChcr) & 0x100u) != 0u) return 4;
    if (PS2_MEM_READ<std::uint32_t>(gs::kDmaStat) != 0x1u) return 5;
    if ((PS2_MEM_READ<std::uint32_t>(gs::kVif1Stat) & 0x1F0003u) != 0u) return 6;
    if ((PS2_MEM_READ<std::uint32_t>(gs::kGifMode) & 0xC00u) != 0u) return 7;

    // Banderas que espera main.
    if (PS2_MEM_READ<std::uint32_t>(gs::kVSyncFlag) != 1u) return 8;
    if ((PS2_MEM_READ<std::uint64_t>(gs::kGsCsr) & (1ull << 13)) == 0u) return 9;

    // Consulta no bloqueante.
    if (gs::sceGsSyncPath(1) != 0u) return 10;

    // D_STAT conserva bits de otros canales.
    PS2_MEM_WRITE<std::uint32_t>(gs::kDmaStat, 0x000000F8u);
    gs::CompletePath();
    if (PS2_MEM_READ<std::uint32_t>(gs::kDmaStat) != 0x000000F8u) return 11;

    std::printf("PASS ps2_gs_sync: STR/CIS/VIF1/GIF limpios, VSINT y flag en 1\n");
    return 0;
}
