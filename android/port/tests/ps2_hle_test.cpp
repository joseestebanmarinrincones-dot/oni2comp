// Prueba de ps2_hle sin SDL ni GL.
// Compila con: clang++ -std=c++17 -O2 -I src tests/ps2_hle_test.cpp
//              src/ps2_hle.cpp src/ps2_pad.cpp

#include "ps2_hle.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

void write_u64(std::vector<std::uint8_t> &ram, std::uint32_t address, std::uint64_t value) {
    std::memcpy(ram.data() + address, &value, sizeof(value));
}

std::uint64_t dma_tag(std::uint32_t qwc, std::uint8_t id, std::uint32_t address = 0) {
    return static_cast<std::uint64_t>(qwc) |
           (static_cast<std::uint64_t>(id & 0x7u) << 28) |
           (static_cast<std::uint64_t>(address & 0x7FFFFFFFu) << 32);
}

}  // namespace

int main() {
    // ---------------------------------------------------------------- CDVD
    ps2::hle::SetCdReader([](std::uint32_t lba, std::uint32_t sectors, void *buffer) {
        if (lba != 0x1234u || sectors != 2u) return false;
        std::memset(buffer, 0xAB, sectors * ps2::hle::kCdSectorSize);
        return true;
    });

    std::array<std::uint8_t, 2 * ps2::hle::kCdSectorSize> cd{};
    if (::sceCdRead(0x1234, 2, cd.data(), nullptr) != 1) return 1;
    if (cd[0] != 0xAB || cd[cd.size() - 1] != 0xAB) return 2;
    if (ps2::hle::GetCdStats().reads != 1 ||
        ps2::hle::GetCdStats().sectors != 2 ||
        ps2::hle::GetCdStats().bytes != 4096) return 3;

    std::uint8_t bad_mode[4] = {0, 0, 1, 0};
    if (::sceCdRead(0x1234, 1, cd.data(), bad_mode) != 0) return 4;
    if (::sceCdSync(0) != 0 || ::sceCdGetError() != 0) return 5;

    // ----------------------------------------------------------------- Pad
    port::PadState pad;
    pad.set(port::PadButton::Cross);
    ps2::hle::SetPadState(&pad);

    std::uint8_t report[32]{};
    if (::scePadRead(0, 0, report) != 1) return 6;
    if (report[0] != 0x00 || report[1] != 0x73) return 7;
    if (report[2] != 0xBF || report[3] != 0xFF) return 8;   // Cross = 0x4000
    if (report[20] != 0xBF || report[21] != 0xFF) return 9;
    if (report[4] != 0x80 || report[7] != 0x80) return 10;

    // Sticks: -1 = izquierda/arriba (0x00), +1 = derecha/abajo (0xFF).
    pad.clear();
    pad.left_x = 1.0f;
    pad.left_y = -1.0f;
    pad.right_x = 0.0f;
    pad.right_y = 1.0f;
    ps2::hle::SetPadState(&pad);
    ::scePadRead(0, 0, report);
    if (report[4] != 0x80 || report[5] != 0xFF) return 28;
    if (report[6] != 0xFF || report[7] != 0x00) return 29;

    pad.clear();
    pad.set(port::PadButton::L2);
    pad.set(port::PadButton::Select);
    ps2::hle::SetPadState(&pad);
    ::scePadRead(0, 0, report);
    if (report[2] != 0xFE || report[3] != 0xFE) return 30;

    pad.clear();
    pad.set(port::PadButton::Cross);
    ps2::hle::SetPadState(&pad);
    ::scePadRead(0, 0, report);

    if (::scePadInfoMode(0, 0, 1, 0) != 0x73) return 11;
    if (::scePadGetState(0, 0) != 6) return 12;

    std::uint8_t disconnected[32]{};
    if (::scePadRead(1, 0, disconnected) != 0) return 13;
    if (disconnected[0] != 0xFF) return 14;

    // ----------------------------------------------------------------- DMA
    std::vector<std::uint8_t> ram(0x10000, 0);
    ps2::hle::AttachMemory(ram.data(), static_cast<std::uint32_t>(ram.size()));

    // END tag con 4 qwords de datos detras.
    write_u64(ram, 0x100, dma_tag(4, 7));
    for (std::uint32_t i = 0; i < 64; ++i) ram[0x110 + i] = static_cast<std::uint8_t>(i);

    bool called = false;
    ps2::hle::DmaTransfer captured;
    ps2::hle::SetDmaSendHook([&](const ps2::hle::DmaTransfer &dma) {
        called = true;
        captured = dma;
    });
    ::sceDmaSend(reinterpret_cast<void *>(static_cast<std::uintptr_t>(0x1000A000u)),
                 reinterpret_cast<void *>(static_cast<std::uintptr_t>(0x100u)));
    if (!called) return 15;
    if (captured.channel != ps2::hle::DmaChannel::Gif) return 16;
    if (!captured.parsed || captured.tags.size() != 1) return 17;
    const auto &end = captured.tags[0];
    if (end.id != 7 || end.qwords != 4 || !end.data_valid) return 18;
    if (ps2::hle::TagBytes(end) != 64) return 19;
    const std::uint8_t *data = ps2::hle::TagData(end);
    if (!data || data[0] != 0 || data[63] != 63) return 20;

    // REF tag con 2 qwords desde 0x300, seguido de END en 0x210.
    write_u64(ram, 0x200, dma_tag(2, 3, 0x300));
    write_u64(ram, 0x210, dma_tag(0, 7));
    std::vector<ps2::hle::DmaTag> tags;
    if (!ps2::hle::WalkDmaChain(0x200, tags)) return 21;
    if (tags.size() != 2) return 22;
    if (tags[0].id != 3 || tags[0].data_address != 0x300) return 23;
    if (tags[1].id != 7) return 24;

    // Cadena vacia: el hook se llama igual, parsed=false.
    called = false;
    ::sceDmaSend(reinterpret_cast<void *>(static_cast<std::uintptr_t>(0x1000A000u)),
                 reinterpret_cast<void *>(static_cast<std::uintptr_t>(0x400u)));
    if (!called) return 25;
    if (ps2::hle::LastDmaTransfer().parsed) return 26;
    if (!ps2::hle::LastDmaTransfer().tags.empty()) return 27;

    ps2::hle::ClearDmaSendHook();
    ps2::hle::AttachMemory(nullptr, 0);

    std::printf("PASS HLE: CD 2x2048, pad Cross=0x%02X%02X, DMA END qwc=4, REF 2 tags\n",
                report[2], report[3]);
    return 0;
}
