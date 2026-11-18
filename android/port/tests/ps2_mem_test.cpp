// Prueba de ps2_mem sin SDL ni GL.
// Compila con: clang++ -std=c++17 -O2 -I src tests/ps2_mem_test.cpp
//              src/ps2_mem.cpp

#include "ps2_mem.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

int hw_writes = 0;
ps2::Ps2Memory::HardwareAccess last_write{};

void on_hw_write(const ps2::Ps2Memory::HardwareAccess &access) {
    ++hw_writes;
    last_write = access;
}

int hw_reads = 0;
bool on_hw_read(const ps2::Ps2Memory::HardwareAccess &access, void *out) {
    ++hw_reads;
    if (access.address == 0x10008000u && access.size == 4) {
        const std::uint32_t value = 0;   // simula CHCR con STR ya limpio
        std::memcpy(out, &value, sizeof(value));
        return true;
    }
    return false;
}

}  // namespace

int main() {
    using ps2::Ps2Memory;

    Ps2Memory::Reset();

    PS2_MEM_WRITE<std::uint32_t>(0x00100000u, 0x11223344u);
    if (PS2_MEM_READ<std::uint32_t>(0x00100000u) != 0x11223344u) return 1;

    PS2_MEM_WRITE<std::uint16_t>(0x00100004u, 0xABCDu);
    PS2_MEM_WRITE<std::uint8_t>(0x00100006u, 0xEFu);
    if (PS2_MEM_READ<std::uint16_t>(0x00100004u) != 0xABCDu) return 2;
    if (PS2_MEM_READ<std::uint8_t>(0x00100006u) != 0xEFu) return 3;

    PS2_MEM_WRITE<std::uint64_t>(0x00100008u, 0x1122334455667788ull);
    if (PS2_MEM_READ<std::uint64_t>(0x00100008u) != 0x1122334455667788ull) return 4;

    // DMA / INTC / GS: no tocan la RAM; se guardan en shadow y se registran.
    PS2_MEM_WRITE<std::uint32_t>(0x10008000u, 0x00000105u);
    if (PS2_MEM_READ<std::uint32_t>(0x10008000u) != 0x00000105u) return 5;

    PS2_MEM_WRITE<std::uint32_t>(0x1000F000u, 0x00000001u);
    if (PS2_MEM_READ<std::uint32_t>(0x1000F000u) != 0x00000001u) return 6;

    PS2_MEM_WRITE<std::uint64_t>(0x12001000u, 0x8000000000000000ull);
    if (PS2_MEM_READ<std::uint64_t>(0x12001000u) != 0x8000000000000000ull) return 7;
    if (PS2_MEM_READ<std::uint32_t>(0x12001000u) != 0x00000000u) return 8;
    if (PS2_MEM_READ<std::uint32_t>(0x12001004u) != 0x80000000u) return 9;

    // Alias KSEG0 / KSEG1.
    PS2_MEM_WRITE<std::uint32_t>(0x80100010u, 0xCAFEBABEu);
    if (PS2_MEM_READ<std::uint32_t>(0x00100010u) != 0xCAFEBABEu) return 10;
    PS2_MEM_WRITE<std::uint32_t>(0xB000F000u, 0x00000002u);
    if (PS2_MEM_READ<std::uint32_t>(0x1000F000u) != 0x00000002u) return 11;

    if (Ps2Memory::PhysicalAddress(0x80100000u) != 0x00100000u) return 12;
    if (Ps2Memory::PhysicalAddress(0xB000F000u) != 0x1000F000u) return 13;
    if (!Ps2Memory::IsRamAddress(0x00100000u)) return 14;
    if (Ps2Memory::IsRamAddress(0x10000000u)) return 15;
    if (!Ps2Memory::IsHardwareAddress(0x12001000u)) return 16;
    if (Ps2Memory::Classify(0x10008000u) != Ps2Memory::HwKind::Dma) return 17;
    if (Ps2Memory::Classify(0x1000F000u) != Ps2Memory::HwKind::Intc) return 18;
    if (Ps2Memory::Classify(0x12001000u) != Ps2Memory::HwKind::Gs) return 19;

    // Hook de escritura: solo hardware.
    Ps2Memory::SetHardwareWriteHook(&on_hw_write);
    hw_writes = 0;
    PS2_MEM_WRITE<std::uint32_t>(0x1000F000u, 0x10u);
    if (hw_writes != 1) return 20;
    if (last_write.kind != Ps2Memory::HwKind::Intc) return 21;
    if (last_write.address != 0x1000F000u || last_write.size != 4) return 22;
    PS2_MEM_WRITE<std::uint32_t>(0x00100020u, 0x55u);
    if (hw_writes != 1) return 23;
    Ps2Memory::SetHardwareWriteHook(nullptr);

    // Hook de lectura: permite emular el fin de una DMA (CHCR.STR = 0).
    Ps2Memory::SetHardwareReadHook(&on_hw_read);
    hw_reads = 0;
    PS2_MEM_WRITE<std::uint32_t>(0x10008000u, 0x105u);
    if (PS2_MEM_READ<std::uint32_t>(0x10008000u) != 0u) return 24;
    if (hw_reads == 0) return 25;
    Ps2Memory::SetHardwareReadHook(nullptr);

    // Alias uncached 0x20000000..0x3FFFFFFF (lo usa el loader para el
    // double buffer de lectura de CD).
    PS2_MEM_WRITE<std::uint32_t>(0x20100020u, 0xDEADBEEFu);
    if (PS2_MEM_READ<std::uint32_t>(0x00100020u) != 0xDEADBEEFu) return 27;
    if (Ps2Memory::PhysicalAddress(0x20100020u) != 0x00100020u) return 28;

    // Direccion fuera de mapa: no debe romper.
    PS2_MEM_WRITE<std::uint32_t>(0x03000000u, 0x1234u);
    if (PS2_MEM_READ<std::uint32_t>(0x03000000u) != 0u) return 26;

    std::printf("PASS ps2_mem: RAM 32MB, DMA/INTC/GS shadow, KSEG, hooks\n");
    return 0;
}
