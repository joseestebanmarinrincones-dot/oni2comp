// Prueba de ps2_cd_stream con un lector falso en memoria (sin SDL).
// Compila con:
//   clang++ -std=c++17 -O2 -Isrc `
//     tests/ps2_cd_stream_test.cpp src/ps2_cd_stream.cpp src/ps2_mem.cpp

#include "ps2_cd_stream.hpp"
#include "ps2_mem.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

int main() {
    constexpr std::uint32_t kSectors = 4;
    constexpr std::uint32_t kSectorSize = 2048;

    std::array<std::uint8_t, kSectors * kSectorSize> image{};
    for (std::uint32_t i = 0; i < kSectors; ++i) {
        std::memset(image.data() + i * kSectorSize,
                    static_cast<int>(i * 0x11), kSectorSize);
    }

    ps2::Ps2Memory::Reset();

    ps2::cd::SetReader([&image](std::uint32_t lba, std::uint32_t count,
                                void *buffer) -> bool {
        if (lba >= kSectors || count > kSectors - lba) return false;
        std::memcpy(buffer, image.data() + lba * kSectorSize,
                    static_cast<std::size_t>(count) * kSectorSize);
        return true;
    });

    // Stream desde LBA 1; leer 2 sectores al alias uncached 0x20100000.
    if (ps2::cd::sceCdStStart(1, 0) != 0) return 1;
    if (ps2::cd::StreamLba() != 1u) return 2;

    int err = -1;
    if (ps2::cd::sceCdStRead(2, 0x20100000u, 0, &err) != 0) return 3;
    if (err != 0) return 4;
    if (ps2::Ps2Memory::PhysicalAddress(0x20100000u) != 0x00100000u) return 5;
    if (PS2_MEM_READ<std::uint8_t>(0x00100000u) != 0x11u) return 6;
    if (PS2_MEM_READ<std::uint8_t>(0x00100800u) != 0x22u) return 7;
    if (ps2::cd::StreamLba() != 3u) return 8;

    // FUN_00124C20: un sector mas, a direccion normal.
    if (ps2::cd::FUN_00124C20(1, 0x00101000u) != 0) return 9;
    if (PS2_MEM_READ<std::uint8_t>(0x00101000u) != 0x33u) return 10;
    if (ps2::cd::StreamLba() != 4u) return 11;

    // Fuera del ISO: error controlado.
    err = -1;
    if (ps2::cd::sceCdStRead(10, 0x00100000u, 0, &err) != -1) return 12;
    if (err == 0) return 13;

    if (ps2::cd::sceCdStSeek(0) != 0) return 14;
    if (ps2::cd::StreamLba() != 0u) return 15;
    ps2::cd::sceCdStStop();
    if (ps2::cd::StreamLba() != 0u) return 16;

    ps2::cd::ClearReader();
    if (ps2::cd::ReadDvdSectors(0, 1, 0x00100000u)) return 17;

    std::printf("PASS ps2_cd_stream: ISO -> ps2_ram, alias 0x20000000, LBA y errores\n");
    return 0;
}
