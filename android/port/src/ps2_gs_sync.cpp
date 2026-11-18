#include "ps2_gs_sync.hpp"

#include "ps2_mem.hpp"

namespace ps2::gs {
namespace {

void clear_str(std::uint32_t channel) {
    std::uint32_t chcr = PS2_MEM_READ<std::uint32_t>(channel);
    if ((chcr & 0x100u) == 0) return;  // ya termino: no reescribir
    chcr &= ~0x100u;                   // D_CHCR.STR
    PS2_MEM_WRITE<std::uint32_t>(channel, chcr);
}

}  // namespace

void CompletePath() {
    // Fin instantaneo de las transferencias que espera sceGsSyncPath.
    // El original no espera VIF0; no se toca.
    clear_str(kDmaVif1Chcr);
    clear_str(kDmaGifChcr);

    // D_STAT.CIS: VIF1 (bit 1), GIF (bit 2).
    std::uint32_t dstat = PS2_MEM_READ<std::uint32_t>(kDmaStat);
    if ((dstat & 0x6u) != 0) {
        dstat &= ~0x6u;
        PS2_MEM_WRITE<std::uint32_t>(kDmaStat, dstat);
    }

    // VIF1 status: bits que consulta sceGsSyncPath.
    std::uint32_t vif1 = PS2_MEM_READ<std::uint32_t>(kVif1Stat);
    if ((vif1 & 0x1F0003u) != 0) {
        vif1 &= ~0x1F0003u;
        PS2_MEM_WRITE<std::uint32_t>(kVif1Stat, vif1);
    }

    // GIF status/mode: el original lo consulta para "GIF does not terminate".
    std::uint32_t gif_mode = PS2_MEM_READ<std::uint32_t>(kGifMode);
    if ((gif_mode & 0xC00u) != 0) {
        gif_mode &= ~0xC00u;
        PS2_MEM_WRITE<std::uint32_t>(kGifMode, gif_mode);
    }

    // VSINT en el modelo del port (bit 13 de GS_CSR).
    std::uint64_t csr = PS2_MEM_READ<std::uint64_t>(kGsCsr);
    if ((csr & (1ull << 13)) == 0) {
        csr |= (1ull << 13);
        PS2_MEM_WRITE<std::uint64_t>(kGsCsr, csr);
    }

    // Flag que espera el bucle de main (0x00149AE0 / 0x00149B30).
    if (PS2_MEM_READ<std::uint32_t>(kVSyncFlag) == 0) {
        PS2_MEM_WRITE<std::uint32_t>(kVSyncFlag, 1u);
    }

    // Nota: COP2 vi13 no es memoria. El original lo consulta para VU0; al
    // sustituir sceGsSyncPath por esta funcion, ese chequeo se omite.
}

std::uint32_t sceGsSyncPath(std::uint32_t mode) {
    if (mode != 0) {
        // Consulta no bloqueante: no queda nada ocupado.
        return 0u;
    }

    CompletePath();
    return 0u;
}

}  // namespace ps2::gs
