// Prueba de blending GS para efectos de almas / magia Genma.
//
// Los efectos aditivos de PS2 llegan como PRIM.ABE=1 + ALPHA=0x48
// (Cs*As+Cd) o ALPHA=0x68 con FIX=0x80 (Cs+Cd). El parser debe conservar
// esos registros y el renderer aplica glBlendFuncSeparate equivalente.
// renderer aplica GL_SRC_ALPHA/GL_ONE o GL_ONE/GL_ONE. Si se usara siempre
// alpha normal, las particulas negras se verian como cuadros solidos.
//
// Compila con:
//   clang++ -std=c++17 -O2 -Isrc tests/gs_blend_test.cpp `
//     src/gif_packet.cpp src/ps2_texture_cache.cpp -o gs_blend_test.exe

#include "gif_packet.hpp"
#include "ps2_alpha_blend.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char *name) {
    std::printf("  %-58s %s\n", name, condition ? "PASS" : "FAIL");
    if (!condition) ++failures;
}

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

std::vector<std::uint8_t> make_ad_packet(
    const std::vector<std::pair<std::uint8_t, std::uint64_t>> &regs) {
    std::vector<std::uint8_t> data;
    std::array<std::uint8_t, 16> descriptors{};
    descriptors[0] = ps2::gif::kRegAd;
    put_tag(data, static_cast<std::uint16_t>(regs.size()), false, 1,
            descriptors);
    for (const auto &[address, value] : regs) {
        put_ad(data, value, address);
    }
    return data;
}

std::vector<std::uint8_t> make_texture_packet() {
    std::vector<std::uint8_t> data;
    std::array<std::uint8_t, 16> regs{};
    regs[0] = ps2::gif::kRegAd;
    put_tag(data, 4, false, 1, regs);

    put_ad(data, 0, ps2::gif::kGsBitbltbuf);  // DBP=0, PSMCT32
    put_ad(data, 0, ps2::gif::kGsTrxpos);
    put_ad(data, 4ull | (4ull << 32), ps2::gif::kGsTrxreg);
    put_ad(data, 0, ps2::gif::kGsTrxdir);

    put_image_tag(data, 16u * 4u);
    for (std::uint32_t i = 0; i < 16; ++i) {
        put32(data, 0x000000FFu | (i << 8));
    }
    pad_to_qword(data);
    return data;
}

void put_tex0(std::vector<std::uint8_t> &data, std::uint64_t tex0) {
    put64(data, tex0 & 0xFFFFFFFFull);
    put64(data, 0);
    put64(data, tex0 >> 32);
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

std::uint64_t texture_tex0(std::uint32_t tfx) {
    return (1ull << 14) |                       // TBW 1
           (0ull << 20) |                       // PSMCT32
           (1ull << 26) | (1ull << 30) |        // TW=TH=1 -> 2x2? se recorta
           (1ull << 34) |                       // TCC
           (static_cast<std::uint64_t>(tfx) << 35);
}

std::vector<std::uint8_t> make_sprite_packet(std::uint64_t tex0,
                                             bool include_prim,
                                             std::uint16_t prim) {
    std::vector<std::uint8_t> data;
    std::array<std::uint8_t, 16> regs{};
    std::uint8_t nreg = 0;
    if (include_prim) regs[nreg++] = ps2::gif::kRegPrim;
    regs[nreg++] = ps2::gif::kRegTex0_1;
    regs[nreg++] = ps2::gif::kRegTex0_2;
    regs[nreg++] = ps2::gif::kRegRgbaq;
    regs[nreg++] = ps2::gif::kRegUv;
    regs[nreg++] = ps2::gif::kRegXyz2;
    put_tag(data, 2, true, nreg, regs);

    for (int i = 0; i < 2; ++i) {
        if (include_prim) {
            put64(data, prim);
            put64(data, 0);
        }
        put_tex0(data, tex0);
        put_rgbaq(data, 255, 255, 255, 255);
        if (i == 0) {
            put_uv(data, 0, 0);
            put_xyz2(data, 100 * 16, 50 * 16, 0);
        } else {
            put_uv(data, 4 * 16, 4 * 16);
            put_xyz2(data, 104 * 16, 54 * 16, 0);
        }
    }
    return data;
}

void check_decode() {
    std::printf("\n=== Ecuaciones GS ===\n");
    check(ps2::gif::DecodeGsBlend(0x56, 0x44ull) ==
              ps2::gif::BlendMode::Alpha,
          "ABE + ALPHA 0x44 -> alpha normal");
    check(ps2::gif::DecodeGsBlend(0x56, 0x48ull) ==
              ps2::gif::BlendMode::Additive,
          "ABE + ALPHA 0x48 -> aditivo con alpha");
    check(ps2::gif::DecodeGsBlend(0x56, 0x68ull | (0x80ull << 32)) ==
              ps2::gif::BlendMode::AdditiveFixed,
          "ABE + ALPHA 0x68 FIX=0x80 -> aditivo fijo");
    check(ps2::gif::DecodeGsBlend(0x56, 0x64ull | (0x80ull << 32)) ==
              ps2::gif::BlendMode::Alpha,
          "ABE + ALPHA 0x64 FIX=0x80 -> interpolacion");
    check(ps2::gif::DecodeGsBlend(0x16, 0x48ull) ==
              ps2::gif::BlendMode::Opaque,
          "ABE=0 -> opaco aunque ALPHA sea aditivo");
    check(ps2::gif::DecodeGsBlend(0x56, 0x04ull) ==
              ps2::gif::BlendMode::Alpha,
          "ecuacion desconocida -> alpha seguro");
}

void check_alpha_states() {
    std::printf("\n=== ALPHA_1/ALPHA_2 -> GLES ===\n");

    const auto alpha = ps2::gfx::DecodePs2AlphaBlend(0x44);
    check(alpha.supported && alpha.name == std::string("alpha"),
          "0x44: alpha blend soportado");
    check(alpha.rgb_op == ps2::gfx::Ps2AlphaOp::Add &&
              alpha.rgb_src == ps2::gfx::Ps2AlphaFactor::SrcAlpha &&
              alpha.rgb_dst ==
                  ps2::gfx::Ps2AlphaFactor::OneMinusSrcAlpha,
          "0x44: RGB SRC_ALPHA / 1-SRC_ALPHA");

    const auto additive = ps2::gfx::DecodePs2AlphaBlend(0x48);
    check(additive.supported && additive.name == std::string("additive"),
          "0x48: aditivo soportado");
    check(additive.rgb_src == ps2::gfx::Ps2AlphaFactor::SrcAlpha &&
              additive.rgb_dst == ps2::gfx::Ps2AlphaFactor::One,
          "0x48: RGB SRC_ALPHA / ONE");

    const auto fixed =
        ps2::gfx::DecodePs2AlphaBlend(0x68ull | (0x80ull << 32));
    check(fixed.supported && fixed.name == std::string("additive-fixed"),
          "0x68 FIX=0x80: aditivo fijo");
    check(fixed.rgb_src == ps2::gfx::Ps2AlphaFactor::One &&
              fixed.rgb_dst == ps2::gfx::Ps2AlphaFactor::One,
          "0x68 FIX=0x80: RGB ONE / ONE");

    const auto scaled =
        ps2::gfx::DecodePs2AlphaBlend(0x68ull | (0x40ull << 32));
    check(scaled.rgb_src == ps2::gfx::Ps2AlphaFactor::ConstantAlpha &&
              scaled.rgb_dst == ps2::gfx::Ps2AlphaFactor::One &&
              scaled.fix == 0x40,
          "0x68 FIX=0x40: GL_CONSTANT_ALPHA / ONE");

    const auto scaled_interp =
        ps2::gfx::DecodePs2AlphaBlend(0x64ull | (0x40ull << 32));
    check(scaled_interp.rgb_src ==
              ps2::gfx::Ps2AlphaFactor::ConstantAlpha &&
              scaled_interp.rgb_dst ==
                  ps2::gfx::Ps2AlphaFactor::OneMinusConstantAlpha,
          "0x64 FIX=0x40: interpolacion con constante");

    const auto replace =
        ps2::gfx::DecodePs2AlphaBlend(0x64ull | (0x80ull << 32));
    check(replace.supported && replace.name == std::string("replace"),
          "0x64 FIX=0x80: reemplazo");
    check(replace.rgb_src == ps2::gfx::Ps2AlphaFactor::One &&
              replace.rgb_dst == ps2::gfx::Ps2AlphaFactor::Zero,
          "0x64 FIX=0x80: RGB ONE / ZERO");

    const auto fade = ps2::gfx::DecodePs2AlphaBlend(0x46);
    check(fade.supported && fade.name == std::string("destination-fade"),
          "0x46: fade al framebuffer");
    check(fade.rgb_src == ps2::gfx::Ps2AlphaFactor::Zero &&
              fade.rgb_dst ==
                  ps2::gfx::Ps2AlphaFactor::OneMinusSrcAlpha,
          "0x46: RGB ZERO / 1-SRC_ALPHA");

    const auto subtract = ps2::gfx::DecodePs2AlphaBlend(0x84);
    check(subtract.supported &&
              subtract.rgb_op == ps2::gfx::Ps2AlphaOp::Subtract,
          "0x84: GL_FUNC_SUBTRACT");
    check(subtract.rgb_src == ps2::gfx::Ps2AlphaFactor::SrcAlpha &&
              subtract.rgb_dst == ps2::gfx::Ps2AlphaFactor::SrcAlpha,
          "0x84: SRC_ALPHA / SRC_ALPHA");

    const auto source = ps2::gfx::DecodePs2AlphaBlend(0x88);
    check(source.supported && source.name == std::string("source"),
          "0x88: solo fuente");
    check(source.rgb_src == ps2::gfx::Ps2AlphaFactor::SrcAlpha &&
              source.rgb_dst == ps2::gfx::Ps2AlphaFactor::Zero,
          "0x88: RGB SRC_ALPHA / ZERO");

    const auto dst_alpha = ps2::gfx::DecodePs2AlphaBlend(0x54);
    check(dst_alpha.rgb_src == ps2::gfx::Ps2AlphaFactor::DstAlpha &&
              dst_alpha.rgb_dst ==
                  ps2::gfx::Ps2AlphaFactor::OneMinusDstAlpha,
          "0x54: RGB DST_ALPHA / 1-DST_ALPHA");

    const auto fallback = ps2::gfx::DecodePs2AlphaBlend(0x04);
    check(!fallback.supported &&
              fallback.rgb_src == ps2::gfx::Ps2AlphaFactor::SrcAlpha,
          "combinacion no soportada -> alpha seguro");
}

struct Pixel {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

float factor_value(ps2::gif::BlendFactor factor, const Pixel &source) {
    switch (factor) {
    case ps2::gif::BlendFactor::Zero: return 0.0f;
    case ps2::gif::BlendFactor::One: return 1.0f;
    case ps2::gif::BlendFactor::SrcAlpha: return source.a;
    case ps2::gif::BlendFactor::OneMinusSrcAlpha: return 1.0f - source.a;
    }
    return 0.0f;
}

// Simula el fixed-function blend de GLES para el pixel que cubre el quad.
Pixel simulate(ps2::gif::BlendMode mode, const Pixel &source,
               const Pixel &destination) {
    const ps2::gif::BlendEquation equation =
        ps2::gif::GetBlendEquation(mode);
    if (!equation.enabled) return source;
    const float sf = factor_value(equation.src, source);
    const float df = factor_value(equation.dst, source);
    Pixel out;
    out.r = source.r * sf + destination.r * df;
    out.g = source.g * sf + destination.g * df;
    out.b = source.b * sf + destination.b * df;
    out.a = source.a * sf + destination.a * df;
    return out;
}

bool close(float a, float b) {
    const float diff = a > b ? a - b : b - a;
    return diff < 0.001f;
}

void check_pixels() {
    std::printf("\n=== Pixel: particulas negras/blancas ===\n");
    const Pixel background{0.20f, 0.40f, 0.60f, 1.0f};
    const Pixel black{0.0f, 0.0f, 0.0f, 1.0f};
    const Pixel black_soft{0.0f, 0.0f, 0.0f, 0.5f};
    const Pixel glow{1.0f, 1.0f, 1.0f, 0.5f};

    const Pixel additive_black =
        simulate(ps2::gif::BlendMode::Additive, black, background);
    check(close(additive_black.r, background.r) &&
              close(additive_black.g, background.g) &&
              close(additive_black.b, background.b),
          "aditivo con RGB negro no pinta cuadro negro");

    const Pixel additive_glow =
        simulate(ps2::gif::BlendMode::Additive, glow, background);
    check(close(additive_glow.r, 0.70f) && close(additive_glow.g, 0.90f) &&
              close(additive_glow.b, 1.10f),
          "aditivo con alpha 0.5 suma 0.5 al fondo");

    const Pixel additive_fixed_black =
        simulate(ps2::gif::BlendMode::AdditiveFixed, black, background);
    check(close(additive_fixed_black.r, background.r) &&
              close(additive_fixed_black.g, background.g) &&
              close(additive_fixed_black.b, background.b),
          "aditivo fijo con RGB negro no pinta cuadro negro");

    const Pixel alpha_black =
        simulate(ps2::gif::BlendMode::Alpha, black_soft, background);
    check(close(alpha_black.r, 0.10f) && close(alpha_black.g, 0.20f) &&
              close(alpha_black.b, 0.30f),
          "alpha normal con negro oscurece (bug que se corrige)");

    const Pixel opaque_black =
        simulate(ps2::gif::BlendMode::Opaque, black, background);
    check(close(opaque_black.r, 0.0f) && close(opaque_black.g, 0.0f) &&
              close(opaque_black.b, 0.0f),
          "ABE=0 si escribe negro (opaco real)");
}

void check_parser() {
    std::printf("\n=== Parser GIF: PRIM/ALPHA/TFX ===\n");

    ps2::gif::PacketParser parser;
    ps2::gif::DrawCommand command;
    const std::vector<std::uint8_t> texture = make_texture_packet();
    parser.ParseBlock(texture.data(), texture.size(), command);

    struct Case {
        std::uint16_t prim;
        std::uint64_t alpha;
        ps2::gif::BlendMode expected;
        const char *name;
    };
    const Case cases[] = {
        {0x56, 0x44ull, ps2::gif::BlendMode::Alpha,
         "PRIM+ALPHA por A+D: alpha normal"},
        {0x56, 0x48ull, ps2::gif::BlendMode::Additive,
         "PRIM+ALPHA por A+D: aditivo"},
        {0x56, 0x68ull | (0x80ull << 32),
         ps2::gif::BlendMode::AdditiveFixed,
         "PRIM por A+D: aditivo fijo"},
        {0x16, 0x48ull, ps2::gif::BlendMode::Opaque,
         "PRIM sin ABE: opaco"},
    };

    const std::uint64_t tex0 = texture_tex0(0);
    for (const Case &test : cases) {
        const std::vector<std::uint8_t> state = make_ad_packet({
            {ps2::gif::kGsPrim, test.prim},
            {ps2::gif::kGsAlpha, test.alpha},
        });
        parser.ParseBlock(state.data(), state.size(), command);

        const std::vector<std::uint8_t> sprite =
            make_sprite_packet(tex0, false, 0);
        const bool parsed =
            parser.ParseBlock(sprite.data(), sprite.size(), command);
        check(parsed && command.vertex_count == 2, test.name);
        check(command.blend_mode == test.expected,
              std::string(std::string(test.name) + " (modo)").c_str());
        check(command.prim == test.prim, "conserva PRIM");
        check(command.alpha_register == test.alpha, "conserva ALPHA");
    }

    // TME=0: primitiva de color plano, no debe muestrear una textura.
    const std::vector<std::uint8_t> flat_state = make_ad_packet({
        {ps2::gif::kGsPrim, 0x46},   // sprite + ABE, sin TME
        {ps2::gif::kGsAlpha, 0x44},
    });
    parser.ParseBlock(flat_state.data(), flat_state.size(), command);
    const std::vector<std::uint8_t> flat =
        make_sprite_packet(tex0, false, 0);
    parser.ParseBlock(flat.data(), flat.size(), command);
    check(!command.textured, "TME=0 no usa textura");

    // PRIM PACKED sigue funcionando y conserva ABE.
    const std::vector<std::uint8_t> packed_state = make_ad_packet({
        {ps2::gif::kGsAlpha, 0x48},
    });
    parser.ParseBlock(packed_state.data(), packed_state.size(), command);
    const std::vector<std::uint8_t> packed_sprite =
        make_sprite_packet(tex0, true, 0x56);
    parser.ParseBlock(packed_sprite.data(), packed_sprite.size(), command);
    check(command.blend_mode == ps2::gif::BlendMode::Additive,
          "PRIM PACKED con ABE + ALPHA aditivo");
    check(command.textured, "PRIM PACKED con TME texturiza");

    // ALPHA_2 usa la misma ecuacion que ALPHA_1.
    const std::vector<std::uint8_t> alpha2_state = make_ad_packet({
        {ps2::gif::kGsAlpha2, 0x48},
    });
    parser.ParseBlock(alpha2_state.data(), alpha2_state.size(), command);
    const std::vector<std::uint8_t> alpha2_sprite =
        make_sprite_packet(tex0, false, 0);
    parser.ParseBlock(alpha2_sprite.data(), alpha2_sprite.size(), command);
    check(command.alpha_register == 0x48 &&
              command.blend_mode == ps2::gif::BlendMode::Additive,
          "ALPHA_2 (0x43) se procesa igual que ALPHA_1");

    // TFX se conserva para diagnosticar DECAL/HILIGHT.
    const std::vector<std::uint8_t> tfx_state = make_ad_packet({
        {ps2::gif::kGsPrim, 0x56},
        {ps2::gif::kGsAlpha, 0x44},
    });
    parser.ParseBlock(tfx_state.data(), tfx_state.size(), command);
    const std::vector<std::uint8_t> tfx_sprite =
        make_sprite_packet(texture_tex0(2), false, 0);
    parser.ParseBlock(tfx_sprite.data(), tfx_sprite.size(), command);
    check(command.tfx == 2, "TEX0.TFX=HILIGHT conservado");
}

}  // namespace

int main() {
    std::printf("=== Pruebas de blending GS (almas / Genma) ===\n");
    check_decode();
    check_alpha_states();
    check_pixels();
    check_parser();

    if (failures == 0) {
        std::printf("\nTodos los blends de efectos pasaron.\n");
        return 0;
    }
    std::printf("\n%d comprobacion(es) fallaron.\n", failures);
    return 1;
}
