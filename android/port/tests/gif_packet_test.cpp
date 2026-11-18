// Prueba del parser GIF con un paquete sintetico de textura + sprite.
// Los registros GS de textura se envian por A+D (0x0E), como en el hardware.
// Compila con:
//   clang++ -std=c++17 -O2 -Isrc tests/gif_packet_test.cpp src/gif_packet.cpp

#include "gif_packet.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

void put8(std::vector<std::uint8_t> &data, std::uint8_t value) {
    data.push_back(value);
}

void put32(std::vector<std::uint8_t> &data, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        data.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFFu));
    }
}

void put64(std::vector<std::uint8_t> &data, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        data.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFFu));
    }
}

void put_tag(std::vector<std::uint8_t> &data, std::uint16_t nloop, bool eop,
             std::uint8_t nreg, const std::array<std::uint8_t, 16> &regs) {
    std::uint64_t lo = nloop & 0x7FFFu;
    if (eop) lo |= (1ull << 15);
    lo |= static_cast<std::uint64_t>(nreg & 0xFu) << 60;
    std::uint64_t hi = 0;
    for (int i = 0; i < 16; ++i) {
        hi |= static_cast<std::uint64_t>(regs[static_cast<std::size_t>(i)] & 0xFu)
              << (i * 4);
    }
    put64(data, lo);
    put64(data, hi);
}

void put_ad(std::vector<std::uint8_t> &data, std::uint64_t value,
            std::uint8_t address) {
    put64(data, value);
    put8(data, address);
    for (int i = 0; i < 7; ++i) put8(data, 0);
}

void put_image_tag(std::vector<std::uint8_t> &data, std::size_t bytes) {
    const std::uint64_t qwords = (bytes + 15u) / 16u;
    std::uint64_t lo = qwords & 0x7FFFu;
    lo |= (2ull << 58);  // FLG = IMAGE
    put64(data, lo);
    put64(data, 0);
}

void pad_to_qword(std::vector<std::uint8_t> &data) {
    while ((data.size() & 15u) != 0u) put8(data, 0);
}

void put_prim(std::vector<std::uint8_t> &data, std::uint32_t prim) {
    put64(data, prim);
    put64(data, 0);
}

void put_rgbaq(std::vector<std::uint8_t> &data, std::uint8_t r,
               std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    std::array<std::uint8_t, 16> q{};
    q[0] = r;
    q[4] = g;
    q[8] = b;
    q[12] = a;
    data.insert(data.end(), q.begin(), q.end());
}

void put_tex0(std::vector<std::uint8_t> &data, std::uint64_t tex0) {
    // TEX0_1: bits 0-31; TEX0_2: bits 32-63.
    put64(data, tex0 & 0xFFFFFFFFull);
    put64(data, 0);
    put64(data, tex0 >> 32);
    put64(data, 0);
}

void put_uv(std::vector<std::uint8_t> &data, std::uint16_t u,
            std::uint16_t v) {
    std::array<std::uint8_t, 16> q{};
    q[0] = static_cast<std::uint8_t>(u & 0xFFu);
    q[1] = static_cast<std::uint8_t>((u >> 8) & 0xFFu);
    q[4] = static_cast<std::uint8_t>(v & 0xFFu);
    q[5] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
    data.insert(data.end(), q.begin(), q.end());
}

void put_xyz2(std::vector<std::uint8_t> &data, std::int16_t x, std::int16_t y,
              std::uint32_t z) {
    std::array<std::uint8_t, 16> q{};
    q[0] = static_cast<std::uint8_t>(static_cast<std::uint16_t>(x) & 0xFFu);
    q[1] = static_cast<std::uint8_t>(
        (static_cast<std::uint16_t>(x) >> 8) & 0xFFu);
    q[4] = static_cast<std::uint8_t>(static_cast<std::uint16_t>(y) & 0xFFu);
    q[5] = static_cast<std::uint8_t>(
        (static_cast<std::uint16_t>(y) >> 8) & 0xFFu);
    q[8] = static_cast<std::uint8_t>(z & 0xFFu);
    q[9] = static_cast<std::uint8_t>((z >> 8) & 0xFFu);
    q[10] = static_cast<std::uint8_t>((z >> 16) & 0xFFu);
    q[11] = static_cast<std::uint8_t>((z >> 24) & 0xFFu);
    data.insert(data.end(), q.begin(), q.end());
}

std::vector<std::uint8_t> make_texture_packet() {
    std::vector<std::uint8_t> data;
    std::array<std::uint8_t, 16> regs{};
    regs[0] = ps2::gif::kRegAd;
    put_tag(data, 4, false, 1, regs);

    put_ad(data, 0, ps2::gif::kGsBitbltbuf);              // DPSM=PSMCT32
    put_ad(data, 0, ps2::gif::kGsTrxpos);
    put_ad(data, 4ull | (4ull << 32), ps2::gif::kGsTrxreg);  // 4x4
    put_ad(data, 0, ps2::gif::kGsTrxdir);                 // host->local

    put_image_tag(data, 16u * 4u);
    for (std::uint32_t i = 0; i < 16; ++i) {
        const std::uint32_t r = (i & 1u) ? 0xFFu : 0x00u;
        const std::uint32_t g = (i & 2u) ? 0xFFu : 0x00u;
        const std::uint32_t b = (i & 4u) ? 0xFFu : 0x00u;
        put32(data, r | (g << 8) | (b << 16) | (0xFFu << 24));
    }
    pad_to_qword(data);
    return data;
}

std::vector<std::uint8_t> make_clut_packet(
    std::uint32_t cbp, std::uint32_t cpsm,
    const std::vector<std::uint32_t> &colors) {
    std::vector<std::uint8_t> data;
    std::array<std::uint8_t, 16> regs{};
    regs[0] = ps2::gif::kRegAd;
    put_tag(data, 4, false, 1, regs);

    const std::uint64_t bitblt =
        (static_cast<std::uint64_t>(cbp) << 32) |
        (static_cast<std::uint64_t>(cpsm) << 56);
    put_ad(data, bitblt, ps2::gif::kGsBitbltbuf);
    put_ad(data, 0, ps2::gif::kGsTrxpos);
    put_ad(data, static_cast<std::uint64_t>(colors.size()) |
                 (1ull << 32),
           ps2::gif::kGsTrxreg);
    put_ad(data, 0, ps2::gif::kGsTrxdir);

    const std::size_t bytes = cpsm == 0u
        ? colors.size() * 4u
        : ((cpsm == 2u || cpsm == 10u) ? colors.size() * 2u
                                       : colors.size() * 3u);
    put_image_tag(data, bytes);
    for (std::uint32_t color : colors) {
        if (cpsm == 0u) {
            put32(data, color);
        } else if (cpsm == 2u || cpsm == 10u) {
            put8(data, static_cast<std::uint8_t>(color & 0xFFu));
            put8(data, static_cast<std::uint8_t>((color >> 8) & 0xFFu));
        } else {
            put8(data, static_cast<std::uint8_t>(color & 0xFFu));
            put8(data, static_cast<std::uint8_t>((color >> 8) & 0xFFu));
            put8(data, static_cast<std::uint8_t>((color >> 16) & 0xFFu));
        }
    }
    pad_to_qword(data);
    return data;
}

std::vector<std::uint8_t> make_indexed_packet(
    std::uint32_t tbp, std::uint32_t psm, std::uint32_t width,
    std::uint32_t height, const std::vector<std::uint8_t> &pixels) {
    std::vector<std::uint8_t> data;
    std::array<std::uint8_t, 16> regs{};
    regs[0] = ps2::gif::kRegAd;
    put_tag(data, 4, false, 1, regs);

    const std::uint64_t bitblt =
        (static_cast<std::uint64_t>(tbp) << 32) |
        (static_cast<std::uint64_t>(psm) << 56);
    put_ad(data, bitblt, ps2::gif::kGsBitbltbuf);
    put_ad(data, 0, ps2::gif::kGsTrxpos);
    put_ad(data, static_cast<std::uint64_t>(width) |
                 (static_cast<std::uint64_t>(height) << 32),
           ps2::gif::kGsTrxreg);
    put_ad(data, 0, ps2::gif::kGsTrxdir);

    put_image_tag(data, pixels.size());
    data.insert(data.end(), pixels.begin(), pixels.end());
    pad_to_qword(data);
    return data;
}

std::vector<std::uint8_t> make_indexed_sprite(
    std::uint64_t tex0, std::uint32_t width, std::uint32_t height) {
    std::vector<std::uint8_t> data;
    std::array<std::uint8_t, 16> regs{};
    regs[0] = ps2::gif::kRegTex0_1;
    regs[1] = ps2::gif::kRegTex0_2;
    regs[2] = ps2::gif::kRegPrim;
    regs[3] = ps2::gif::kRegRgbaq;
    regs[4] = ps2::gif::kRegUv;
    regs[5] = ps2::gif::kRegXyz2;
    put_tag(data, 2, true, 6, regs);

    for (int i = 0; i < 2; ++i) {
        put_tex0(data, tex0);
        put_prim(data, 6u | (1u << 4));
        put_rgbaq(data, 255, 255, 255, 255);
        if (i == 0) {
            put_uv(data, 0, 0);
            put_xyz2(data, 100 * 16, 50 * 16, 0);
        } else {
            put_uv(data, static_cast<std::uint16_t>(width * 16),
                   static_cast<std::uint16_t>(height * 16));
            put_xyz2(data, 104 * 16, 54 * 16, 0);
        }
    }
    return data;
}

std::vector<std::uint8_t> make_depth_packet(std::uint32_t psm,
                                            std::uint32_t width,
                                            std::uint32_t height,
                                            const std::vector<std::uint32_t> &z) {
    std::vector<std::uint8_t> data;
    std::array<std::uint8_t, 16> regs{};
    regs[0] = ps2::gif::kRegAd;
    put_tag(data, 4, false, 1, regs);

    put_ad(data, static_cast<std::uint64_t>(psm) << 56,
           ps2::gif::kGsBitbltbuf);
    put_ad(data, 0, ps2::gif::kGsTrxpos);
    put_ad(data, static_cast<std::uint64_t>(width) |
                 (static_cast<std::uint64_t>(height) << 32),
           ps2::gif::kGsTrxreg);
    put_ad(data, 0, ps2::gif::kGsTrxdir);  // host->local

    const std::size_t bytes = psm == 0x31u
        ? z.size() * 3u
        : ((psm == 0x32u || psm == 0x3Au) ? z.size() * 2u
                                          : z.size() * 4u);
    put_image_tag(data, bytes);
    for (std::uint32_t value : z) {
        if (psm == 0x31u) {  // PSMZ24
            put8(data, static_cast<std::uint8_t>(value & 0xFFu));
            put8(data, static_cast<std::uint8_t>((value >> 8) & 0xFFu));
            put8(data, static_cast<std::uint8_t>((value >> 16) & 0xFFu));
        } else if (psm == 0x32u || psm == 0x3Au) {  // PSMZ16/16S
            put8(data, static_cast<std::uint8_t>(value & 0xFFu));
            put8(data, static_cast<std::uint8_t>((value >> 8) & 0xFFu));
        } else {  // PSMZ32
            put32(data, value);
        }
    }
    pad_to_qword(data);
    return data;
}

std::vector<std::uint8_t> make_sprite_packet() {
    std::vector<std::uint8_t> data;
    std::array<std::uint8_t, 16> regs{};
    regs[0] = ps2::gif::kRegPrim;
    regs[1] = ps2::gif::kRegRgbaq;
    regs[2] = ps2::gif::kRegUv;
    regs[3] = ps2::gif::kRegXyz2;
    put_tag(data, 2, true, 4, regs);

    for (int i = 0; i < 2; ++i) {
        put_prim(data, 6u | (1u << 4));  // sprite + textura
        put_rgbaq(data, 255, 128, 64, 255);
        if (i == 0) {
            put_uv(data, 0, 0);
            put_xyz2(data, 100 * 16, 50 * 16, 0);
        } else {
            put_uv(data, 4 * 16, 4 * 16);  // textura 4x4 -> 1.0
            put_xyz2(data, 164 * 16, 114 * 16, 0);
        }
    }
    return data;
}

}  // namespace

int main() {
    ps2::gif::PacketParser parser;
    ps2::gif::DrawCommand command;

    const std::vector<std::uint8_t> texture = make_texture_packet();
    if (parser.ParseBlock(texture.data(), texture.size(), command)) return 1;
    if (!parser.CurrentTexture().valid) return 2;
    if (parser.CurrentTexture().width != 4u ||
        parser.CurrentTexture().height != 4u) return 3;
    if (parser.CurrentTexture().rgba[3] != 255u) return 4;
    if (parser.CurrentTexture().rgba[4] != 255u) return 5;

    const std::vector<std::uint8_t> sprite = make_sprite_packet();
    if (!parser.ParseBlock(sprite.data(), sprite.size(), command)) return 6;
    if (!command.sprite || command.vertex_count != 2) return 7;
    if (!command.has_texture) return 8;
    if (command.vertices[0].x < 99.0f || command.vertices[0].x > 101.0f) return 9;
    if (command.vertices[0].y < 49.0f || command.vertices[0].y > 51.0f) return 10;
    if (command.vertices[1].x < 163.0f || command.vertices[1].x > 165.0f) return 11;
    if (command.vertices[1].u < 0.99f || command.vertices[1].v < 0.99f) return 12;

    std::vector<std::uint32_t> z32(16);
    for (int i = 0; i < 16; ++i) {
        z32[static_cast<std::size_t>(i)] =
            (static_cast<std::uint32_t>(i) * 0x111111u) & 0xFFFFFFu;
    }
    const std::vector<std::uint8_t> depth32 =
        make_depth_packet(0x30u, 4, 4, z32);
    ps2::gif::PacketParser depth_parser;
    if (!depth_parser.ParseBlock(depth32.data(), depth32.size(), command)) {
        return 13;
    }
    if (!command.has_depth_map || !command.depth_map.valid) return 14;
    if (command.depth_map.width != 4u || command.depth_map.height != 4u) {
        return 15;
    }
    if (command.depth_map.psm != 0x30u) return 16;
    if (command.depth_map.depth[0] != 0u) return 17;
    if (command.depth_map.depth[1] != 0x111111u) return 18;
    if (command.depth_map.depth[15] != 0xFFFFFFu) return 19;

    const std::vector<std::uint8_t> depth24 =
        make_depth_packet(0x31u, 4, 4, z32);
    if (!depth_parser.ParseBlock(depth24.data(), depth24.size(), command)) {
        return 20;
    }
    if (!command.has_depth_map || !command.depth_map.valid) return 21;
    if (command.depth_map.depth[1] != 0x111111u) return 22;

    std::vector<std::uint32_t> z16(16);
    for (int i = 0; i < 16; ++i) {
        z16[static_cast<std::size_t>(i)] =
            static_cast<std::uint32_t>(i) * 0x1111u;
    }
    const std::vector<std::uint8_t> depth16 =
        make_depth_packet(0x32u, 4, 4, z16);
    if (!depth_parser.ParseBlock(depth16.data(), depth16.size(), command)) {
        return 23;
    }
    if (!command.has_depth_map || !command.depth_map.valid) return 24;
    if (command.depth_map.depth[1] != 0x111111u) return 25;

    // CLUT de 16 colores + textura PSMT8 4x4.
    std::vector<std::uint32_t> palette(16);
    for (std::uint32_t i = 0; i < 16; ++i) {
        const std::uint8_t r = static_cast<std::uint8_t>(i * 16u);
        const std::uint8_t g = static_cast<std::uint8_t>(255u - i * 16u);
        const std::uint8_t b = static_cast<std::uint8_t>(i * 8u);
        palette[i] = r | (static_cast<std::uint32_t>(g) << 8) |
                     (static_cast<std::uint32_t>(b) << 16) | (255u << 24);
    }

    ps2::gif::PacketParser indexed_parser;
    const std::vector<std::uint8_t> clut =
        make_clut_packet(0x100u, 0u, palette);
    indexed_parser.ParseBlock(clut.data(), clut.size(), command);

    std::vector<std::uint8_t> indices8(16);
    for (std::uint32_t i = 0; i < 16; ++i) {
        indices8[i] = static_cast<std::uint8_t>(i);
    }
    const std::vector<std::uint8_t> texture8 =
        make_indexed_packet(0x200u, 19u, 4, 4, indices8);
    indexed_parser.ParseBlock(texture8.data(), texture8.size(), command);

    const std::uint64_t tex0_8 =
        static_cast<std::uint64_t>(0x200u) | (1ull << 14) |
        (19ull << 20) | (2ull << 26) | (2ull << 30) |
        (1ull << 34) | (0x100ull << 37);
    const std::vector<std::uint8_t> sprite8 =
        make_indexed_sprite(tex0_8, 4, 4);
    if (!indexed_parser.ParseBlock(sprite8.data(), sprite8.size(), command)) {
        return 26;
    }
    if (!command.has_texture || !command.texture.valid) return 27;
    if (command.texture.width != 4u || command.texture.height != 4u) return 28;
    if (command.texture.rgba[0] != 0u ||
        command.texture.rgba[1] != 255u ||
        command.texture.rgba[2] != 0u ||
        command.texture.rgba[3] != 255u) return 29;
    if (command.texture.rgba[4] != 16u ||
        command.texture.rgba[5] != 239u ||
        command.texture.rgba[6] != 8u) return 30;
    if (command.texture.rgba[15 * 4] != 240u ||
        command.texture.rgba[15 * 4 + 1] != 15u ||
        command.texture.rgba[15 * 4 + 2] != 120u) return 31;

    // CLUT de 16 colores + textura PSMT4 4x4 (nibbles).
    ps2::gif::PacketParser indexed4_parser;
    indexed4_parser.ParseBlock(clut.data(), clut.size(), command);

    std::vector<std::uint8_t> indices4(8);
    for (std::uint32_t i = 0; i < 8; ++i) {
        indices4[i] = static_cast<std::uint8_t>((i * 2) | ((i * 2 + 1) << 4));
    }
    const std::vector<std::uint8_t> texture4 =
        make_indexed_packet(0x300u, 20u, 4, 4, indices4);
    indexed4_parser.ParseBlock(texture4.data(), texture4.size(), command);

    const std::uint64_t tex0_4 =
        static_cast<std::uint64_t>(0x300u) | (1ull << 14) |
        (20ull << 20) | (2ull << 26) | (2ull << 30) |
        (1ull << 34) | (0x100ull << 37);
    const std::vector<std::uint8_t> sprite4 =
        make_indexed_sprite(tex0_4, 4, 4);
    if (!indexed4_parser.ParseBlock(sprite4.data(), sprite4.size(), command)) {
        return 32;
    }
    if (!command.has_texture || !command.texture.valid) return 33;
    if (command.texture.width != 4u || command.texture.height != 4u) return 34;
    if (command.texture.rgba[4] != 16u ||
        command.texture.rgba[5] != 239u ||
        command.texture.rgba[6] != 8u) return 35;
    if (command.texture.rgba[8] != 32u ||
        command.texture.rgba[9] != 223u ||
        command.texture.rgba[10] != 16u) return 36;

    std::printf("PASS gif_packet: GIFtag, textura, sprite, Z-buffer "
                "PSMZ32/24/16 y CLUT PSMT8/PSMT4\n");
    return 0;
}
