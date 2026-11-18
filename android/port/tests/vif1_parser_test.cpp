// Test de PC para ps2::vif1::Parser (paquetes VIF1 UNPACK + MSCAL).
//
// Construye un paquete sintetico con la codificacion real de VIFcode:
//   bits 30-24 opcode, 23-16 NUM, 15-0 inmediato.
//
// Contenido:
//   - 3 matrices 4x4 row-major (world/view/projection) en V4_32
//   - 4 vertices V4_32
//   - 4 normales V3_32
//   - 4 UVs V2_32
//   - MSCAL
//
// Verifica BuildPacket() con Layout explicito, BuildPacketAuto(), la cadena
// DMA1 CNT y el doble buffer BASE/OFFSET.

#include "vif1_parser.hpp"
#include "vu1_renderer.hpp"

#include "ps2_mem.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, const char *message) {
    std::printf("  %-58s %s\n", message, condition ? "PASS" : "FAIL");
    if (!condition) ++g_failures;
}

bool nearly(float a, float b) {
    return std::fabs(a - b) <= 1.0e-4f;
}

std::uint32_t vifcode(std::uint32_t opcode, std::uint32_t num,
                      std::uint32_t imm) {
    return ((opcode & 0x7Fu) << 24) | ((num & 0xFFu) << 16) | (imm & 0xFFFFu);
}

std::uint32_t unpack_code(std::uint32_t vn, std::uint32_t vl,
                          std::uint32_t address_qwords, std::uint32_t count,
                          bool dbf = false, bool zero_extend = false) {
    std::uint32_t imm = address_qwords & 0x3FFu;
    if (dbf) imm |= 0x8000u;
    if (zero_extend) imm |= 0x4000u;
    return vifcode(0x60u | ((vn & 0x3u) << 2) | (vl & 0x3u), count, imm);
}

void append32(std::vector<std::uint8_t> &stream, std::uint32_t value) {
    const std::size_t offset = stream.size();
    stream.resize(offset + 4);
    std::memcpy(stream.data() + offset, &value, sizeof(value));
}

void append8(std::vector<std::uint8_t> &stream, std::uint8_t value) {
    stream.push_back(value);
}

void append_float(std::vector<std::uint8_t> &stream, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    append32(stream, bits);
}

void append_matrix(std::vector<std::uint8_t> &stream, float base) {
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            append_float(stream, base + static_cast<float>(row * 4 + col));
        }
    }
}

std::vector<std::uint8_t> build_packet() {
    std::vector<std::uint8_t> stream;

    append32(stream, vifcode(0x03, 0, 0));  // BASE 0
    append32(stream, vifcode(0x04, 0, 0));  // ITOP 0

    // Matrices: 3 * 16 floats = 12 V4_32 en qwords 0..11.
    append32(stream, unpack_code(3, 0, 0, 12));
    append_matrix(stream, 1.0f);
    append_matrix(stream, 101.0f);
    append_matrix(stream, 201.0f);

    // Vertices en qword 12.
    append32(stream, unpack_code(3, 0, 12, 4));
    for (int i = 0; i < 4; ++i) {
        append_float(stream, 10.0f + static_cast<float>(i));
        append_float(stream, 20.0f + static_cast<float>(i));
        append_float(stream, 30.0f + static_cast<float>(i));
        append_float(stream, 1.0f);
    }

    // Normales V3_32 (12 bytes de origen) en qwords 16..19.
    append32(stream, unpack_code(2, 0, 16, 4));
    for (int i = 0; i < 4; ++i) {
        append_float(stream, 0.0f);
        append_float(stream, 1.0f);
        append_float(stream, 0.0f);
    }

    // UVs V2_32 (8 bytes de origen) en qwords 20..23.
    append32(stream, unpack_code(1, 0, 20, 4));
    for (int i = 0; i < 4; ++i) {
        append_float(stream, 0.25f * static_cast<float>(i));
        append_float(stream, 0.5f * static_cast<float>(i));
    }

    // MSCAL: el inmediato es una direccion de instruccion de 8 bytes.
    // 0x10 * 8 = 0x80, sufijo que aparece en los dumps vu1/*_0080.bin.
    append32(stream, vifcode(0x14, 0, 0x10));

    return stream;
}

void test_explicit_layout(const std::vector<std::uint8_t> &stream) {
    std::printf("=== Layout explicito ===\n");

    ps2::vif1::Parser parser;
    parser.Reset();
    const bool submitted =
        parser.SubmitPacket(stream.data(), stream.size());
    check(submitted, "SubmitPacket produce capture");
    check(parser.NewCaptures().size() == 1, "1 capture nuevo");
    if (parser.NewCaptures().empty()) return;

    const ps2::vif1::Capture &capture = parser.NewCaptures().front();
    check(capture.unpacks.size() == 4, "4 UNPACK registrados");
    check(capture.mscal_address == 0x80, "MSCAL PC = 0x80");
    check(capture.unpacks[2].address == 256 &&
              capture.unpacks[2].element_size == 12,
          "normal V3_32 en qword 16");

    ps2::vif1::Layout layout;
    layout.matrix_address = 0;
    layout.matrix_count = 3;
    layout.vertex_address = 192;
    layout.vertex_count = 4;
    layout.vertex_stride = 16;
    layout.normal_address = 256;
    layout.normal_stride = 16;
    layout.has_normals = true;
    layout.uv_address = 320;
    layout.uv_stride = 16;
    layout.has_uvs = true;

    Vu1Renderer::Packet packet;
    const bool built = parser.BuildPacket(capture, layout, packet);
    check(built, "BuildPacket(Layout)");
    if (!built) return;

    // Matriz world: 1..16, row-major.
    check(nearly(packet.world.m[0], 1.0f) &&
              nearly(packet.world.m[5], 6.0f) &&
              nearly(packet.world.m[15], 16.0f),
          "world = 1..16 row-major");
    check(nearly(packet.view.m[0], 101.0f) &&
              nearly(packet.view.m[15], 116.0f),
          "view = 101..116");
    check(nearly(packet.projection.m[0], 201.0f) &&
              nearly(packet.projection.m[15], 216.0f),
          "projection = 201..216");

    check(packet.vertices.size() == 4, "4 vertices");
    check(nearly(packet.vertices[2].position[0], 12.0f) &&
              nearly(packet.vertices[2].position[1], 22.0f) &&
              nearly(packet.vertices[2].position[2], 32.0f) &&
              nearly(packet.vertices[2].position[3], 1.0f),
          "vertice[2] = (12,22,32,1)");
    check(nearly(packet.vertices[3].normal[0], 0.0f) &&
              nearly(packet.vertices[3].normal[1], 1.0f) &&
              nearly(packet.vertices[3].normal[2], 0.0f),
          "normal[3] = (0,1,0)");
    check(nearly(packet.vertices[1].uv[0], 0.25f) &&
              nearly(packet.vertices[1].uv[1], 0.5f),
          "uv[1] = (0.25,0.5)");
    check(nearly(packet.vertices[0].uv[0], 0.0f) &&
              nearly(packet.vertices[0].uv[1], 0.0f),
          "uv[0] = (0,0)");
    check(packet.indices.size() == 4 && packet.indices[3] == 3,
          "indices automaticos 0..3");
    check(packet.lit, "lit activo por normales");
    check(!packet.skinned, "sin skinning");
}

void test_auto_layout(const std::vector<std::uint8_t> &stream) {
    std::printf("\n=== Layout automatico ===\n");

    ps2::vif1::Parser parser;
    parser.Reset();
    parser.SubmitPacket(stream.data(), stream.size());
    if (parser.NewCaptures().empty()) {
        check(false, "capture para auto layout");
        return;
    }

    Vu1Renderer::Packet packet;
    const bool built =
        parser.BuildPacketAuto(parser.NewCaptures().front(), packet);
    check(built, "BuildPacketAuto()");
    if (!built) return;
    check(packet.vertices.size() == 4, "auto: 4 vertices");
    check(nearly(packet.vertices[2].position[0], 12.0f),
          "auto: vertice correcto");
    check(nearly(packet.world.m[0], 1.0f) &&
              nearly(packet.projection.m[0], 201.0f),
          "auto: matrices 1/101/201 detectadas");
    check(nearly(packet.vertices[2].normal[1], 1.0f), "auto: normal");
    check(nearly(packet.vertices[3].uv[0], 0.75f), "auto: uv");
}

void test_dma_chain(const std::vector<std::uint8_t> &stream) {
    std::printf("\n=== Cadena DMA1 (CNT) ===\n");

    ps2::Ps2Memory::Reset();
    std::memset(ps2::ps2_ram, 0, ps2::kPs2RamSize);

    constexpr std::uint32_t kChain = 0x00100000u;
    const std::size_t qwords = (stream.size() + 15u) / 16u;
    const std::uint64_t tag =
        (static_cast<std::uint64_t>(qwords) & 0xFFFFull) |
        (1ull << 28);  // CNT, no interrupcion
    std::memcpy(ps2::ps2_ram + kChain, &tag, sizeof(tag));
    std::memcpy(ps2::ps2_ram + kChain + 16, stream.data(), stream.size());

    ps2::vif1::Parser parser;
    parser.Reset();
    const bool submitted = parser.SubmitChain(kChain);
    check(submitted, "SubmitChain recorre CNT");
    check(parser.NewCaptures().size() == 1, "cadena: 1 capture");
    if (parser.NewCaptures().empty()) return;

    Vu1Renderer::Packet packet;
    const bool built =
        parser.BuildPacketAuto(parser.NewCaptures().front(), packet);
    check(built, "BuildPacketAuto sobre cadena");
    if (!built) return;
    check(packet.vertices.size() == 4, "cadena: 4 vertices");
}

// Matrices partidas en cuatro UNPACKs V4_32 de 4 elementos + V4_8 de pesos e
// indices de hueso. Verifica la deteccion automatica y la expansion de lanes.
// El juego usa un END tag con QWC y los datos detras (0x7000000E). El parser
// debe consumir esos datos, igual que ps2_hle.
void test_end_tag_data(const std::vector<std::uint8_t> &stream) {
    std::printf("\n=== END tag con datos (patron del juego) ===\n");

    ps2::Ps2Memory::Reset();
    std::memset(ps2::ps2_ram, 0, ps2::kPs2RamSize);

    constexpr std::uint32_t kChain = 0x00110000u;
    const std::size_t qwords = (stream.size() + 15u) / 16u;
    const std::uint64_t tag =
        (static_cast<std::uint64_t>(qwords) & 0xFFFFull) |
        (7ull << 28);  // END con QWC
    std::memcpy(ps2::ps2_ram + kChain, &tag, sizeof(tag));
    std::memcpy(ps2::ps2_ram + kChain + 16, stream.data(), stream.size());

    ps2::vif1::Parser parser;
    parser.Reset();
    check(parser.SubmitChain(kChain), "SubmitChain consume END+QWC");
    check(parser.NewCaptures().size() == 1, "END: 1 capture");
}

void test_skin_and_split_matrices() {
    std::printf("\n=== Matrices partidas + skinning V4_8 ===\n");

    std::vector<std::uint8_t> stream;
    append32(stream, vifcode(0x03, 0, 0));  // BASE 0

    for (int matrix = 0; matrix < 4; ++matrix) {
        append32(stream, unpack_code(3, 0, matrix * 4, 4));
        append_matrix(stream, 1.0f + static_cast<float>(matrix) * 100.0f);
    }

    // Vertices en qword 16.
    append32(stream, unpack_code(3, 0, 16, 4));
    for (int i = 0; i < 4; ++i) {
        append_float(stream, static_cast<float>(i));
        append_float(stream, static_cast<float>(i));
        append_float(stream, static_cast<float>(i));
        append_float(stream, 1.0f);
    }

    // Pesos V4_8 en qword 20: un byte por componente.
    append32(stream, unpack_code(3, 2, 20, 4));
    const std::uint8_t weights[4][4] = {
        {255, 0, 0, 0},
        {0, 128, 0, 0},
        {0, 0, 64, 0},
        {0, 0, 0, 32},
    };
    for (const auto &row : weights) {
        for (std::uint8_t value : row) append8(stream, value);
    }

    // Indices de hueso V4_8 en qword 24.
    append32(stream, unpack_code(3, 2, 24, 4));
    const std::uint8_t bones[4][4] = {
        {0, 0, 0, 0},
        {1, 0, 0, 0},
        {2, 0, 0, 0},
        {3, 0, 0, 0},
    };
    for (const auto &row : bones) {
        for (std::uint8_t value : row) append8(stream, value);
    }

    append32(stream, unpack_code(2, 0, 28, 4));  // normales
    for (int i = 0; i < 4; ++i) {
        append_float(stream, 0.0f);
        append_float(stream, 1.0f);
        append_float(stream, 0.0f);
    }

    append32(stream, unpack_code(1, 0, 32, 4));  // UVs
    for (int i = 0; i < 4; ++i) {
        append_float(stream, 0.1f * static_cast<float>(i));
        append_float(stream, 0.2f * static_cast<float>(i));
    }

    append32(stream, vifcode(0x14, 0, 0x10));

    ps2::vif1::Parser parser;
    parser.Reset();
    parser.SubmitPacket(stream.data(), stream.size());
    if (parser.NewCaptures().empty()) {
        check(false, "capture skinning");
        return;
    }

    Vu1Renderer::Packet packet;
    const bool built =
        parser.BuildPacketAuto(parser.NewCaptures().front(), packet);
    check(built, "BuildPacketAuto skinning");
    if (!built) return;

    check(nearly(packet.world.m[0], 1.0f) &&
              nearly(packet.projection.m[0], 201.0f),
          "matrices 1/101/201");
    check(packet.bones.size() == 1 && nearly(packet.bones[0].m[0], 301.0f),
          "cuarta matriz como hueso");
    check(packet.skinned, "skinning activo");
    check(packet.vertices[0].weights[0] == 255 &&
              packet.vertices[0].weights[1] == 0,
          "peso[0] = 255,0,0,0");
    check(packet.vertices[1].weights[1] == 128,
          "peso[1] = 0,128,0,0");
    check(packet.vertices[3].weights[3] == 32,
          "peso[3] = 0,0,0,32");
    check(packet.vertices[2].bones[0] == 2,
          "hueso[2] = 2");
    check(nearly(packet.vertices[2].uv[0], 0.2f) &&
              nearly(packet.vertices[2].uv[1], 0.4f),
          "UV[2] con stride 16");
}

// Layout fijo de Onimusha 2: matrices en 0x000/0xD00, posiciones en 0x1000,
// normales en 0x1800, UVs en 0x2000. Los buffers son paralelos.
void test_onimusha_layout(bool family_a) {
    std::printf("\n=== Layout Onimusha (%s) ===\n",
                family_a ? "matrices 0xD00" : "matrices 0x000");

    std::vector<std::uint8_t> stream;
    append32(stream, vifcode(0x03, 0, 0));  // BASE 0
    append32(stream, vifcode(0x04, 0, 0));  // ITOP 0

    const std::uint32_t matrix_qword = family_a ? 0xD0u : 0u;
    append32(stream, unpack_code(3, 0, matrix_qword, 16));  // 4 matrices
    for (int m = 0; m < 4; ++m) {
        append_matrix(stream, 1.0f + static_cast<float>(m) * 100.0f);
    }

    // Posiciones V4_32 en qword 0x100 (byte 0x1000).
    append32(stream, unpack_code(3, 0, 0x100u, 4));
    for (int i = 0; i < 4; ++i) {
        append_float(stream, 10.0f + static_cast<float>(i));
        append_float(stream, 20.0f + static_cast<float>(i));
        append_float(stream, 30.0f + static_cast<float>(i));
        append_float(stream, 1.0f);
    }

    // Normales V3_32 en qword 0x180.
    append32(stream, unpack_code(2, 0, 0x180u, 4));
    for (int i = 0; i < 4; ++i) {
        append_float(stream, 0.0f);
        append_float(stream, 1.0f);
        append_float(stream, 0.0f);
    }

    // UVs V2_32 en qword 0x200.
    append32(stream, unpack_code(1, 0, 0x200u, 4));
    for (int i = 0; i < 4; ++i) {
        append_float(stream, 0.25f * static_cast<float>(i));
        append_float(stream, 0.5f * static_cast<float>(i));
    }

    append32(stream, vifcode(0x14, 0, 0x10));

    ps2::vif1::Parser parser;
    parser.Reset();
    parser.SubmitPacket(stream.data(), stream.size());
    if (parser.NewCaptures().empty()) {
        check(false, "capture Onimusha");
        return;
    }

    Vu1Renderer::Packet packet;
    const bool built =
        parser.BuildPacketOnimusha(parser.NewCaptures().front(), packet);
    check(built, "BuildPacketOnimusha");
    if (!built) return;

    const float matrix0 = family_a ? 1.0f : 1.0f;
    check(nearly(packet.world.m[0], matrix0) &&
              nearly(packet.projection.m[0], 201.0f),
          "matrices Onimusha");
    check(packet.vertices.size() == 4 &&
              nearly(packet.vertices[2].position[0], 12.0f),
          "posiciones 0x1000");
    check(nearly(packet.vertices[2].normal[1], 1.0f),
          "normales 0x1800");
    check(nearly(packet.vertices[3].uv[0], 0.75f),
          "UVs 0x2000");
    check(!packet.triangle_strip, "triangulos por defecto");
}

// OFFSET/BASE + bit FLG: comprueba que TOPS se suma y que MSCAL alterna el
// doble buffer BASE <-> BASE+OFFSET.
void test_double_buffer() {
    std::printf("\n=== Doble buffer OFFSET/BASE ===\n");

    std::vector<std::uint8_t> stream;
    append32(stream, vifcode(0x03, 0, 0));  // BASE 0
    append32(stream, vifcode(0x02, 0, 4));  // OFFSET 4, TOPS = BASE

    // Primer buffer: direccion 1 + TOPS(0) = qword 1.
    append32(stream, unpack_code(3, 0, 1, 1, true));
    append_float(stream, 11.0f);
    append_float(stream, 12.0f);
    append_float(stream, 13.0f);
    append_float(stream, 1.0f);
    append32(stream, vifcode(0x14, 0, 0));  // MSCAL -> TOPS = BASE+OFFSET

    // Segundo buffer: misma direccion 1 + TOPS(4) = qword 5.
    append32(stream, unpack_code(3, 0, 1, 1, true));
    append_float(stream, 21.0f);
    append_float(stream, 22.0f);
    append_float(stream, 23.0f);
    append_float(stream, 1.0f);
    append32(stream, vifcode(0x14, 0, 0));  // MSCAL -> TOPS = BASE

    ps2::vif1::Parser parser;
    parser.Reset();
    parser.SubmitPacket(stream.data(), stream.size());
    check(parser.NewCaptures().size() == 2, "2 captures (doble buffer)");
    if (parser.NewCaptures().size() != 2) return;

    const auto &first = parser.NewCaptures()[0];
    const auto &second = parser.NewCaptures()[1];
    check(first.unpacks.size() == 1 && first.unpacks[0].address == 16,
          "buffer A en qword 1");
    check(second.unpacks.size() == 1 && second.unpacks[0].address == 80,
          "buffer B en qword 5");

    float value = 0.0f;
    std::memcpy(&value, first.memory.data() + 16, sizeof(value));
    check(nearly(value, 11.0f), "buffer A contiene 11");
    std::memcpy(&value, second.memory.data() + 80, sizeof(value));
    check(nearly(value, 21.0f), "buffer B contiene 21");
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::vector<std::uint8_t> stream = build_packet();

    test_explicit_layout(stream);
    test_auto_layout(stream);
    test_dma_chain(stream);
    test_end_tag_data(stream);
    test_skin_and_split_matrices();
    test_onimusha_layout(false);
    test_onimusha_layout(true);
    test_double_buffer();

    std::printf("\nFALLOS: %d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
