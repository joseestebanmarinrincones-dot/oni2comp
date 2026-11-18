#pragma once

// Simulacion de la sincronizacion GS/DMA de PS2.
//
// La funcion original SONY_libgraph_00100b28 / FUN_00100B28 es
// sceGsSyncPath. En la ruta bloqueante (mode == 0) espera a que se limpien:
//
//   0x10009000  DMA Ch.1 / VIF1 CHCR, bit 0x100 (STR)
//   0x1000A000  DMA Ch.2 / GIF  CHCR, bit 0x100 (STR)
//   0x10003C00  VIF1 status, mask 0x1F0003
//   0x10003020  GIF status/mode, mask 0xC00
//   COP2 vi13   bit 0x100 (VU1)
//
// En Android no hay hardware PS2 que limpie esos bits. CompletePath() los
// limpia en el shadow de ps2_mem, marca VSINT en GS_CSR y pone a 1 el flag
// iGpffff9130 (0x001849130) que espera el bucle de main.

#include <cstdint>

namespace ps2::gs {

constexpr std::uint32_t kDmaVif1Chcr = 0x10009000u;
constexpr std::uint32_t kDmaGifChcr  = 0x1000A000u;
constexpr std::uint32_t kDmaStat     = 0x1000E000u;

constexpr std::uint32_t kVif1Stat    = 0x10003C00u;
constexpr std::uint32_t kGifMode     = 0x10003020u;

constexpr std::uint32_t kGsCsr      = 0x12001000u;
constexpr std::uint32_t kVSyncFlag  = 0x001849130u;

// Limpia STR de VIF0/VIF1/GIF, los bits CIS de D_STAT, el estado de VIF1
// y marca VSINT + iGpffff9130.
void CompletePath();

// Sustituto de sceGsSyncPath.
// mode == 0: completa la operacion y devuelve 0.
// mode != 0: consulta no bloqueante; tras CompletePath no hay nada ocupado,
//            asi que devuelve 0.
std::uint32_t sceGsSyncPath(std::uint32_t mode);

}  // namespace ps2::gs
