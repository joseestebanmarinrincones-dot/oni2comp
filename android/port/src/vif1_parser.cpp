#include "vif1_parser.hpp"

#include "ps2_mem.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

#if defined(__ANDROID__)
#include <android/log.h>
#define VIF_TAG "OnimushaVif1"
#define VIF_LOGI(...) __android_log_print(ANDROID_LOG_INFO, VIF_TAG, __VA_ARGS__)
#define VIF_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, VIF_TAG, __VA_ARGS__)
#else
#define VIF_LOGI(...) do { \
    std::fprintf(stdout, "[OnimushaVif1] "); \
    std::fprintf(stdout, __VA_ARGS__); \
    std::fputc('\n', stdout); \
} while (0)
#define VIF_LOGE(...) do { \
    std::fprintf(stderr, "[OnimushaVif1] "); \
    std::fprintf(stderr, __VA_ARGS__); \
    std::fputc('\n', stderr); \
} while (0)
#endif

namespace ps2::vif1 {
namespace {

std::uint32_t read32(const std::uint8_t *data) {
    std::uint32_t value = 0;
    std::memcpy(&value, data, sizeof(value));
    return value;
}

float read_float(const std::array<std::uint8_t, 16384> &memory,
                 std::uint32_t address) {
    float value = 0.0f;
    if (address + sizeof(value) <= memory.size()) {
        std::memcpy(&value, memory.data() + address, sizeof(value));
    }
    return value;
}

bool read_ram(std::uint32_t address, void *out, std::size_t bytes) {
    const std::uint32_t physical = Ps2Memory::PhysicalAddress(address);
    if (physical >= kPs2RamSize) return false;
    if (bytes > kPs2RamSize - physical) return false;
    std::memcpy(out, ps2_ram + physical, bytes);
    return true;
}

const std::uint8_t *ram_ptr(std::uint32_t address, std::size_t bytes) {
    const std::uint32_t physical = Ps2Memory::PhysicalAddress(address);
    if (physical >= kPs2RamSize) return nullptr;
    if (bytes > kPs2RamSize - physical) return nullptr;
    return ps2_ram + physical;
}

bool advance(std::uint32_t base, std::uint32_t qwords,
             std::uint32_t &next) {
    const std::uint64_t delta =
        16ull + static_cast<std::uint64_t>(qwords) * 16ull;
    const std::uint64_t value = static_cast<std::uint64_t>(base) + delta;
    if (value > 0xFFFFFFFFull) return false;
    next = static_cast<std::uint32_t>(value);
    return true;
}

// Escribe un vector UNPACK en memoria VU1. Cada vector ocupa un qword y las
// componentes se expanden a lanes de 32 bits, igual que hace el VIF.
void write_unpack_vector(std::array<std::uint8_t, 16384> &memory,
                         std::uint32_t dest, std::uint8_t format,
                         std::uint8_t vn, const std::uint8_t *source,
                         std::uint32_t element_size, bool zero_extend) {
    if (dest + 16u > memory.size()) return;

    const std::uint32_t vl = format & 0x03u;
    std::uint32_t lanes[4] = {};
    std::memcpy(lanes, memory.data() + dest, sizeof(lanes));

    if (vl == 0u) {  // 32 bits por componente
        const std::uint32_t count = std::min<std::uint32_t>(vn + 1u, 4u);
        for (std::uint32_t c = 0; c < count; ++c) {
            std::uint32_t value = 0;
            std::memcpy(&value, source + c * 4u, sizeof(value));
            lanes[c] = value;
        }
        if (vn == 0u) {
            lanes[1] = lanes[2] = lanes[3] = lanes[0];
        }
    } else if (vl == 1u) {  // 16 bits por componente
        const std::uint32_t count = std::min<std::uint32_t>(vn + 1u, 4u);
        for (std::uint32_t c = 0; c < count; ++c) {
            std::uint16_t raw = 0;
            std::memcpy(&raw, source + c * 2u, sizeof(raw));
            lanes[c] = zero_extend
                           ? static_cast<std::uint32_t>(raw)
                           : static_cast<std::uint32_t>(
                                 static_cast<std::int32_t>(
                                     static_cast<std::int16_t>(raw)));
        }
        if (vn == 0u) {
            lanes[1] = lanes[2] = lanes[3] = lanes[0];
        }
    } else if (vl == 2u) {  // 8 bits por componente
        const std::uint32_t count = std::min<std::uint32_t>(vn + 1u, 4u);
        for (std::uint32_t c = 0; c < count; ++c) {
            const std::uint8_t raw = source[c];
            lanes[c] = zero_extend
                           ? static_cast<std::uint32_t>(raw)
                           : static_cast<std::uint32_t>(
                                 static_cast<std::int32_t>(
                                     static_cast<std::int8_t>(raw)));
        }
        if (vn == 0u) {
            lanes[1] = lanes[2] = lanes[3] = lanes[0];
        }
    } else if (vn == 3u) {  // V4_5: 16 bits empaquetados
        std::uint16_t packed = 0;
        std::memcpy(&packed, source, sizeof(packed));
        lanes[0] = packed & 0x1Fu;
        lanes[1] = (packed >> 5) & 0x1Fu;
        lanes[2] = (packed >> 10) & 0x1Fu;
        lanes[3] = (packed >> 15) & 0x01u;
    } else {
        // Formato no soportado: copia cruda para no perder los bytes.
        std::memcpy(memory.data() + dest, source,
                    std::min<std::uint32_t>(element_size, 16u));
        return;
    }

    std::memcpy(memory.data() + dest, lanes, sizeof(lanes));
}

}  // namespace

void Parser::Reset() {
    stream_.clear();
    memory_.fill(0);
    current_ = Capture{};
    last_capture_ = Capture{};
    captures_.clear();
    has_capture_ = false;
    base_ = 0;
    tops_ = 0;
    itop_ = 0;
    offset_ = 0;
    cl_ = 1;
    wl_ = 1;
    dbf_ = false;
    last_mscal_ = 0;
}

std::uint8_t Parser::NormalizeFormat(std::uint8_t opcode) {
    const std::uint8_t vn = static_cast<std::uint8_t>((opcode >> 2) & 0x3u);
    const std::uint8_t vl = static_cast<std::uint8_t>(opcode & 0x3u);
    switch (vn) {
    case 0:  // S
        return vl < 3u ? vl : 0xFFu;
    case 1:  // V2
        return vl < 3u ? static_cast<std::uint8_t>(0x08u + vl) : 0xFFu;
    case 2:  // V3
        return vl < 3u ? static_cast<std::uint8_t>(0x0Cu + vl) : 0xFFu;
    case 3:  // V4
        return static_cast<std::uint8_t>(0x04u + vl);
    default:
        return 0xFFu;
    }
}

std::uint32_t Parser::ElementSize(std::uint8_t format,
                                  std::uint8_t components) {
    switch (format) {
    case 0x00: return 4u * components;  // S_32
    case 0x01: return 2u * components;  // S_16
    case 0x02: return 1u * components;  // S_8
    case 0x04: return 16u;              // V4_32
    case 0x05: return 8u;               // V4_16
    case 0x06: return 4u;               // V4_8
    case 0x07: return 2u;               // V4_5
    case 0x08: return 8u;               // V2_32
    case 0x09: return 4u;               // V2_16
    case 0x0A: return 2u;               // V2_8
    case 0x0C: return 12u;              // V3_32
    case 0x0D: return 6u;               // V3_16
    case 0x0E: return 3u;               // V3_8
    default: return 0;
    }
}

void Parser::Finalize(std::uint32_t mscal_address) {
    current_.mscal_address = mscal_address;
    current_.tops = tops_;
    current_.itop = itop_;
    current_.completed = true;
    current_.memory = memory_;
    last_capture_ = current_;
    captures_.push_back(current_);
    has_capture_ = true;
    last_mscal_ = mscal_address;

    VIF_LOGI("MSCAL PC=0x%04X tops=0x%03X itop=0x%03X unpacks=%zu",
             mscal_address, tops_, itop_, current_.unpacks.size());

    // Traza del orden real de atributos que ha subido VIF1 en este draw.
    static int trace_logs = 0;
    if (trace_logs < 8) {
        ++trace_logs;
        for (const Unpack &unpack : current_.unpacks) {
            VIF_LOGI("  UNPACK fmt=0x%02X addr=0x%04X n=%u elem=%u",
                     unpack.format, unpack.address, unpack.count,
                     unpack.element_size);
        }
    }

    current_ = Capture{};
}

void Parser::ParseStream() {
    std::size_t position = 0;
    while (position + 4 <= stream_.size()) {
        const std::uint32_t command = read32(stream_.data() + position);
        const std::uint8_t opcode =
            static_cast<std::uint8_t>((command >> 24) & 0x7Fu);
        const std::uint32_t num = (command >> 16) & 0xFFu;
        const std::uint32_t imm = command & 0xFFFFu;

        if ((opcode & 0x60u) == 0x60u) {  // UNPACK
            const std::uint8_t vn =
                static_cast<std::uint8_t>((opcode >> 2) & 0x3u);
            const std::uint8_t components =
                static_cast<std::uint8_t>(vn + 1u);
            const std::uint8_t format = NormalizeFormat(opcode);
            const std::uint32_t element = ElementSize(format, components);
            if (format == 0xFFu || element == 0) {
                VIF_LOGE("UNPACK opcode 0x%02X no soportado", opcode);
                position += 4;
                continue;
            }

            const std::uint32_t write_count = num == 0u ? 256u : num;
            const std::uint32_t cl = cl_ == 0u ? 1u : cl_;
            const std::uint32_t wl = wl_ == 0u ? 1u : wl_;

            std::uint32_t source_count = write_count;
            if (cl < wl) {
                const std::uint32_t full = write_count / wl;
                const std::uint32_t rest =
                    std::min<std::uint32_t>(write_count % wl, cl);
                source_count = full * cl + rest;
            }

            const std::uint64_t data_size =
                static_cast<std::uint64_t>(source_count) * element;
            const std::uint64_t padded = (data_size + 3ull) & ~3ull;
            if (position + 4ull + data_size > stream_.size()) break;

            std::uint32_t vu_address = imm & 0x3FFu;
            if ((imm & 0x8000u) != 0u) {
                vu_address = (vu_address + tops_) & 0x3FFu;
            }
            const bool zero_extend = (imm & 0x4000u) != 0u;

            const std::uint8_t *source = stream_.data() + position + 4;
            std::uint32_t source_index = 0;
            std::uint32_t first_dest = 0xFFFFFFFFu;
            for (std::uint32_t i = 0; i < write_count; ++i) {
                const std::uint32_t cycle = i % wl;
                const bool has_source =
                    (cl >= wl) || (cycle < cl);
                std::uint32_t dest_vec = 0;
                if (cl >= wl) {
                    dest_vec = vu_address + (i / wl) * cl + cycle;
                } else {
                    dest_vec = vu_address + i;
                }
                dest_vec &= 0x3FFu;

                if (!has_source || source_index >= source_count) continue;
                const std::uint8_t *element_data =
                    source + source_index * element;
                ++source_index;

                if (dest_vec < first_dest) first_dest = dest_vec;
                write_unpack_vector(memory_, dest_vec * 16u, format, vn,
                                    element_data, element, zero_extend);
            }
            if (first_dest == 0xFFFFFFFFu) first_dest = vu_address;

            Unpack unpack;
            unpack.address = first_dest * 16u;
            unpack.format = format;
            unpack.components = components;
            unpack.count = write_count;
            unpack.element_size = element;
            unpack.data.assign(source, source + data_size);
            current_.unpacks.push_back(std::move(unpack));

            position += 4 + static_cast<std::size_t>(padded);
            continue;
        }

        switch (opcode) {
        case 0x00:  // NOP
            position += 4;
            break;
        case 0x01:  // STCYCL
            cl_ = static_cast<std::uint8_t>(imm & 0xFFu);
            wl_ = static_cast<std::uint8_t>((imm >> 8) & 0xFFu);
            if (cl_ == 0u) cl_ = 1;
            if (wl_ == 0u) wl_ = 1;
            position += 4;
            break;
        case 0x02:  // OFFSET: reinicia TOPS a BASE
            offset_ = imm & 0x3FFu;
            tops_ = base_;
            dbf_ = false;
            position += 4;
            break;
        case 0x03:  // BASE
            base_ = imm & 0x3FFu;
            position += 4;
            break;
        case 0x04:  // ITOP
            itop_ = imm & 0x3FFu;
            position += 4;
            break;
        case 0x05:  // STMOD
        case 0x06:  // MSKPATH3
        case 0x07:  // MARK
        case 0x10:  // FLUSHE
        case 0x11:  // FLUSH
        case 0x13:  // FLUSHA
            position += 4;
            break;
        case 0x20:  // STMASK: un dword de datos
            if (position + 8u > stream_.size()) return;
            position += 8;
            break;
        case 0x21:  // STROW: un qword con la fila
        case 0x22:  // STCOL: un qword con la columna
            if (position + 20u > stream_.size()) return;
            position += 20;
            break;
        case 0x14:  // MSCAL
        case 0x15:  // MSCALF
            // El inmediato es una direccion de instruccion de 8 bytes.
            Finalize((imm & 0x7FFu) * 8u);
            position += 4;
            // Doble buffer: alterna BASE y BASE+OFFSET para el proximo uso.
            if (dbf_) {
                tops_ = base_;
                dbf_ = false;
            } else {
                tops_ = (base_ + offset_) & 0x3FFu;
                dbf_ = true;
            }
            break;
        case 0x17:  // MSCNT: continua el ultimo microprograma
            Finalize(last_mscal_);
            position += 4;
            if (dbf_) {
                tops_ = base_;
                dbf_ = false;
            } else {
                tops_ = (base_ + offset_) & 0x3FFu;
                dbf_ = true;
            }
            break;
        case 0x4A: {  // MPG: num instrucciones de 8 bytes (0 = 256)
            const std::uint64_t bytes =
                static_cast<std::uint64_t>(num == 0u ? 256u : num) * 8ull;
            if (position + 4ull + bytes > stream_.size()) return;
            position += 4 + static_cast<std::size_t>(bytes);
            break;
        }
        case 0x50:  // DIRECT
        case 0x51: {  // DIRECTHL
            const std::uint32_t qwords = imm == 0u ? 65536u : imm;
            const std::uint64_t bytes =
                static_cast<std::uint64_t>(qwords) * 16ull;
            if (position + 4ull + bytes > stream_.size()) return;
            position += 4 + static_cast<std::size_t>(bytes);
            break;
        }
        default:
            position += 4;
            break;
        }
    }

    if (position != 0) {
        stream_.erase(stream_.begin(),
                      stream_.begin() + static_cast<std::ptrdiff_t>(position));
    }
}

bool Parser::SubmitPacket(const std::uint8_t *data, std::size_t bytes) {
    if (data == nullptr || bytes == 0) return false;
    const std::size_t before = captures_.size();
    stream_.insert(stream_.end(), data, data + bytes);
    ParseStream();
    return captures_.size() != before;
}

bool Parser::SubmitChain(std::uint32_t source_address) {
    std::uint32_t current = source_address;
    std::uint32_t returns[2] = {0, 0};
    int asp = 0;
    bool any = false;

    for (int index = 0; index < 4096; ++index) {
        std::uint64_t qword = 0;
        if (!read_ram(current, &qword, sizeof(qword))) break;
        if (qword == 0 || qword == ~0ull) break;

        const std::uint32_t qwc = static_cast<std::uint32_t>(qword & 0xFFFFu);
        const std::uint8_t id = static_cast<std::uint8_t>((qword >> 28) & 0x7u);
        const std::uint32_t addr =
            static_cast<std::uint32_t>((qword >> 32) & 0x7FFFFFFFu);

        // El juego usa tambien END/NEXT con QWC y datos detras del tag
        // (0x7000000E en el paquete estatico), igual que ps2_hle.
        const bool has_data =
            (id == 0u || id == 1u || id == 2u || id == 3u || id == 4u ||
             id == 5u || id == 7u);
        const std::uint32_t data_address =
            (id == 0u || id == 3u || id == 4u) ? addr : current + 16u;
        const std::size_t data_bytes =
            has_data ? static_cast<std::size_t>(qwc) * 16u : 0u;

        if (data_bytes != 0) {
            if (const std::uint8_t *data =
                    ram_ptr(data_address, data_bytes)) {
                SubmitPacket(data, data_bytes);
                any = true;
            }
        }

        switch (id) {
        case 0:  // REFE
            return any;
        case 1:  // CNT
            if (!advance(current, qwc, current)) return any;
            break;
        case 2:  // NEXT
            if (addr == 0) return any;
            current = addr;
            break;
        case 3:  // REF
        case 4:  // REFS
            current += 16u;
            break;
        case 5: {  // CALL
            if (asp >= 2) return any;
            std::uint32_t resume = 0;
            if (!advance(current, qwc, resume)) return any;
            returns[asp++] = resume;
            if (addr == 0) return any;
            current = addr;
            break;
        }
        case 6:  // RET
            if (asp == 0) return any;
            current = returns[--asp];
            break;
        case 7:  // END
            return any;
        default:
            return any;
        }
        if (current == 0) return any;
    }
    return any;
}

bool Parser::BuildPacket(const Layout &layout,
                         Vu1Renderer::Packet &out) const {
    return BuildPacket(last_capture_, layout, out);
}

bool Parser::BuildPacket(const Capture &capture, const Layout &layout,
                         Vu1Renderer::Packet &out) const {
    if (!capture.completed || layout.vertex_count == 0) return false;
    const auto &memory = capture.memory;

    out = Vu1Renderer::Packet{};

    // Matrices: [world, view, projection], row-major.
    if (layout.matrix_count >= 1 &&
        layout.matrix_address + 64u <= memory.size()) {
        for (int m = 0; m < 16; ++m) {
            out.world.m[m] =
                read_float(memory, layout.matrix_address + m * 4u);
        }
    }
    if (layout.matrix_count >= 2 &&
        layout.matrix_address + 128u <= memory.size()) {
        const std::uint32_t base = layout.matrix_address + 64u;
        for (int m = 0; m < 16; ++m) {
            out.view.m[m] = read_float(memory, base + m * 4u);
        }
    }
    if (layout.matrix_count >= 3 &&
        layout.matrix_address + 192u <= memory.size()) {
        const std::uint32_t base = layout.matrix_address + 128u;
        for (int m = 0; m < 16; ++m) {
            out.projection.m[m] = read_float(memory, base + m * 4u);
        }
    }

    if (layout.bone_count != 0) {
        out.bones.resize(layout.bone_count);
        for (std::uint32_t b = 0; b < layout.bone_count; ++b) {
            const std::uint32_t base = layout.bone_matrix_address + b * 64u;
            for (int m = 0; m < 16; ++m) {
                out.bones[b].m[m] = read_float(memory, base + m * 4u);
            }
        }
    }

    out.vertices.resize(layout.vertex_count);
    out.lit = layout.has_normals;
    out.skinned = layout.skinned && !out.bones.empty();

    for (std::uint32_t i = 0; i < layout.vertex_count; ++i) {
        Vu1Renderer::Vertex &vertex = out.vertices[i];
        const std::uint32_t position_address =
            layout.vertex_address + i * layout.vertex_stride;
        if (position_address + 16u > memory.size()) return false;
        for (int c = 0; c < 4; ++c) {
            vertex.position[c] =
                read_float(memory, position_address + c * 4u);
        }

        if (layout.has_normals) {
            const std::uint32_t address =
                layout.normal_address + i * layout.normal_stride;
            for (int c = 0; c < 3; ++c) {
                vertex.normal[c] = read_float(memory, address + c * 4u);
            }
        }
        if (layout.has_uvs) {
            const std::uint32_t address =
                layout.uv_address + i * layout.uv_stride;
            vertex.uv[0] = read_float(memory, address);
            vertex.uv[1] = read_float(memory, address + 4u);
        }
        if (layout.has_colors) {
            // V4_8 expandido: cada componente es un lane de 32 bits.
            const std::uint32_t address = layout.color_address + i * 16u;
            for (int c = 0; c < 4; ++c) {
                const std::uint32_t lane = address + c * 4u;
                if (lane < memory.size()) vertex.color[c] = memory[lane];
            }
        }
        if (layout.skinned) {
            // Pesos e indices V4_8 expandidos a cuatro lanes de 32 bits.
            const std::uint32_t weight_address =
                layout.weight_address + i * 16u;
            const std::uint32_t bone_address =
                layout.bone_index_address + i * 16u;
            for (int c = 0; c < 4; ++c) {
                const std::uint32_t wlane = weight_address + c * 4u;
                const std::uint32_t blane = bone_address + c * 4u;
                if (wlane < memory.size()) vertex.weights[c] = memory[wlane];
                if (blane < memory.size()) vertex.bones[c] = memory[blane];
            }
        }
    }

    out.triangle_strip = layout.triangle_strip;

    if (layout.index_address != 0 && layout.index_count != 0) {
        out.indices.resize(layout.index_count);
        for (std::uint32_t i = 0; i < layout.index_count; ++i) {
            const std::uint32_t address = layout.index_address + i * 2u;
            if (address + 2u > memory.size()) return false;
            std::uint16_t value = 0;
            std::memcpy(&value, memory.data() + address, sizeof(value));
            out.indices[i] = value;
        }
    } else {
        out.indices.resize(layout.vertex_count);
        for (std::uint32_t i = 0; i < layout.vertex_count; ++i) {
            out.indices[i] = static_cast<std::uint16_t>(i);
        }
    }

    return true;
}

bool Parser::BuildPacketOnimusha(const Capture &capture,
                                 Vu1Renderer::Packet &out) const {
    if (!capture.completed) return false;

    const auto find = [&capture](std::uint32_t address,
                                 std::uint8_t format) -> const Unpack * {
        for (const Unpack &unpack : capture.unpacks) {
            if (unpack.address == address && unpack.format == format) {
                return &unpack;
            }
        }
        return nullptr;
    };

    // Familia A: matrices en 0xD00. Familia B: matrices en 0x000.
    // Pueden venir en un solo UNPACK de 16 o en cuatro de 4.
    const auto matrix_block = [&](std::uint32_t base)
        -> std::pair<std::uint32_t, std::uint32_t> {
        const Unpack *single = find(base, 0x04u);
        if (single != nullptr && single->count >= 12u) {
            return {base, std::min<std::uint32_t>(single->count / 4u, 4u)};
        }
        bool complete = true;
        for (std::uint32_t row = 0; row < 4u && complete; ++row) {
            const Unpack *part = find(base + row * 64u, 0x04u);
            complete = part != nullptr && part->count == 4u;
        }
        return complete ? std::make_pair(base, 4u)
                        : std::make_pair(0u, 0u);
    };

    std::uint32_t matrix_address = 0;
    std::uint32_t matrix_count = 0;
    const auto block_a = matrix_block(kOnimushaMatrixBaseA);
    if (block_a.second != 0u) {
        matrix_address = block_a.first;
        matrix_count = block_a.second;
    } else {
        const auto block_b = matrix_block(kOnimushaMatrixBaseB);
        matrix_address = block_b.first;
        matrix_count = block_b.second;
    }

    const auto is_matrix = [&](const Unpack &unpack) {
        return matrix_count != 0u &&
               unpack.address >= matrix_address &&
               unpack.address < matrix_address + matrix_count * 64u;
    };

    // Orden real: matrices -> posiciones V4_32 -> normales V3_32 -> UVs V2_32
    // -> pesos V4_8. Se prefieren las direcciones conocidas y, si el juego
    // usa otra base, se conserva el orden de los UNPACK.
    const Unpack *positions = find(kOnimushaPositions, 0x04u);
    std::size_t position_index = 0;
    if (positions != nullptr) {
        for (std::size_t i = 0; i < capture.unpacks.size(); ++i) {
            if (&capture.unpacks[i] == positions) {
                position_index = i;
                break;
            }
        }
    } else {
        for (std::size_t i = 0; i < capture.unpacks.size(); ++i) {
            const Unpack &unpack = capture.unpacks[i];
            if (unpack.format == 0x04u && unpack.count >= 3u &&
                !is_matrix(unpack)) {
                positions = &unpack;
                position_index = i;
                break;
            }
        }
    }
    if (positions == nullptr || positions->count < 3u) return false;

    const Unpack *normals = find(kOnimushaNormals, 0x0Cu);      // V3_32
    const Unpack *uvs = find(kOnimushaUvs, 0x08u);              // V2_32
    for (std::size_t i = position_index + 1;
         i < capture.unpacks.size(); ++i) {
        const Unpack &unpack = capture.unpacks[i];
        if (normals == nullptr && unpack.format == 0x0Cu &&
            unpack.count >= positions->count) {
            normals = &unpack;
        }
        if (uvs == nullptr && unpack.format == 0x08u &&
            unpack.count >= positions->count) {
            uvs = &unpack;
        }
    }

    const Unpack *weights = find(kOnimushaWeights, 0x06u);      // V4_8
    const Unpack *bones = find(kOnimushaWeights + 0x100u, 0x06u);
    if (weights == nullptr) {
        for (std::size_t i = position_index + 1;
             i < capture.unpacks.size(); ++i) {
            const Unpack &unpack = capture.unpacks[i];
            if (unpack.format != 0x06u ||
                unpack.count < positions->count) {
                continue;
            }
            if (weights == nullptr) weights = &unpack;
            else if (bones == nullptr) bones = &unpack;
        }
    }

    Layout layout;
    layout.matrix_address = matrix_address;
    layout.matrix_count = matrix_count;
    layout.vertex_address = positions->address;
    layout.vertex_count = positions->count;
    layout.vertex_stride = 16;
    layout.normal_stride = 16;
    layout.uv_stride = 16;

    if (layout.matrix_count > 3u) {
        layout.bone_matrix_address = layout.matrix_address + 3u * 64u;
        layout.bone_count = layout.matrix_count - 3u;
    }

    if (normals != nullptr && normals->count >= layout.vertex_count) {
        layout.normal_address = normals->address;
        layout.has_normals = true;
    }
    if (uvs != nullptr && uvs->count >= layout.vertex_count) {
        layout.uv_address = uvs->address;
        layout.has_uvs = true;
    }
    if (weights != nullptr && weights->count >= layout.vertex_count &&
        bones != nullptr && bones->count >= layout.vertex_count) {
        layout.weight_address = weights->address;
        layout.bone_index_address = bones->address;
        layout.skinned = true;
    }

    VIF_LOGI("OnimushaLayout: verts=%u@0x%04X matrices=%u@0x%04X "
             "normals=%d uvs=%d skin=%d",
             layout.vertex_count, layout.vertex_address,
             layout.matrix_count, layout.matrix_address,
             layout.has_normals ? 1 : 0, layout.has_uvs ? 1 : 0,
             layout.skinned ? 1 : 0);
    return BuildPacket(capture, layout, out);
}

bool Parser::BuildPacketAuto(Vu1Renderer::Packet &out) const {
    return BuildPacketAuto(last_capture_, out);
}

bool Parser::BuildPacketAuto(const Capture &capture,
                             Vu1Renderer::Packet &out) const {
    if (!capture.completed) return false;

    Layout layout;
    const Unpack *vertices = nullptr;
    const Unpack *normals = nullptr;
    const Unpack *uvs = nullptr;
    const Unpack *first_v4_8 = nullptr;
    const Unpack *second_v4_8 = nullptr;

    // Los microprogramas de Onimusha cargan 4 matrices desde 0x0000 o 0x0D00.
    // Normalmente es un solo UNPACK de 16 elementos; tambien puede venir
    // partido en cuatro UNPACKs de 4.
    const Unpack *constant_block = nullptr;
    for (const Unpack &unpack : capture.unpacks) {
        if (unpack.format != 0x04u || unpack.count < 12u) continue;
        if (unpack.address != kOnimushaMatrixBaseA &&
            unpack.address != kOnimushaMatrixBaseB) {
            continue;
        }
        if (constant_block == nullptr ||
            unpack.count > constant_block->count) {
            constant_block = &unpack;
        }
    }

    std::uint32_t constant_begin = 0;
    std::uint32_t constant_end = 0;
    bool has_constant_block = false;
    if (constant_block != nullptr) {
        constant_begin = constant_block->address / 16u;
        constant_end = constant_begin + constant_block->count;
        has_constant_block = true;
    } else {
        for (const std::uint32_t base :
             {kOnimushaMatrixBaseA, kOnimushaMatrixBaseB}) {
            bool complete = true;
            for (std::uint32_t row = 0; row < 4 && complete; ++row) {
                const std::uint32_t address = base + row * 64u;
                bool found = false;
                for (const Unpack &unpack : capture.unpacks) {
                    if (unpack.format == 0x04u &&
                        unpack.address == address && unpack.count == 4u) {
                        found = true;
                        break;
                    }
                }
                complete = found;
            }
            if (complete) {
                constant_begin = base / 16u;
                constant_end = constant_begin + 16u;
                has_constant_block = true;
                break;
            }
        }
    }

    for (const Unpack &unpack : capture.unpacks) {
        if (unpack.format == 0x04u) {  // V4_32
            const std::uint32_t start = unpack.address / 16u;
            const bool overlaps_constant =
                has_constant_block &&
                start < constant_end && start + unpack.count > constant_begin;
            if (!overlaps_constant && unpack.count >= 3u &&
                (vertices == nullptr ||
                 unpack.address > vertices->address)) {
                vertices = &unpack;
            }
        } else if (unpack.format == 0x0Cu) {  // V3_32
            if (normals == nullptr) normals = &unpack;
        } else if (unpack.format == 0x08u) {  // V2_32
            if (uvs == nullptr) uvs = &unpack;
        } else if (unpack.format == 0x06u) {  // V4_8 (pesos/colores)
            if (first_v4_8 == nullptr) first_v4_8 = &unpack;
            else if (second_v4_8 == nullptr) second_v4_8 = &unpack;
        }
    }

    if (vertices == nullptr || vertices->count < 3u) {
        VIF_LOGE("BuildPacketAuto: no encontre UNPACK de vertices V4_32");
        return false;
    }

    layout.vertex_address = vertices->address;
    layout.vertex_count = vertices->count;
    layout.vertex_stride = 16;

    if (has_constant_block) {
        layout.matrix_address = constant_begin * 16u;
        layout.matrix_count = std::min<std::uint32_t>(
            (constant_end - constant_begin) / 4u, 4u);
    } else {
        // Fallback: cualquier bloque V4_32 multiple de 4 fuera de vertices.
        std::uint32_t matrix_address = 0;
        std::uint32_t matrix_count = 0;
        for (const Unpack &unpack : capture.unpacks) {
            if (&unpack == vertices) continue;
            if (unpack.format == 0x04u && unpack.count % 4u == 0u &&
                unpack.count <= 4u * 32u) {
                if (matrix_count == 0 || unpack.address < matrix_address) {
                    matrix_address = unpack.address;
                }
                matrix_count += unpack.count / 4u;
            }
        }
        layout.matrix_address = matrix_address;
        layout.matrix_count = std::min<std::uint32_t>(matrix_count, 4u);
    }

    if (layout.matrix_count > 3u) {
        layout.bone_matrix_address = layout.matrix_address + 3u * 64u;
        layout.bone_count = layout.matrix_count - 3u;
    }

    if (normals != nullptr && normals->count >= layout.vertex_count) {
        layout.normal_address = normals->address;
        layout.normal_stride = 16;
        layout.has_normals = true;
    }
    if (uvs != nullptr && uvs->count >= layout.vertex_count) {
        layout.uv_address = uvs->address;
        layout.uv_stride = 16;
        layout.has_uvs = true;
    }
    if (first_v4_8 != nullptr &&
        first_v4_8->count >= layout.vertex_count) {
        if (second_v4_8 != nullptr &&
            second_v4_8->count >= layout.vertex_count) {
            layout.weight_address = first_v4_8->address;
            layout.bone_index_address = second_v4_8->address;
            layout.skinned = true;
        } else {
            layout.color_address = first_v4_8->address;
            layout.has_colors = true;
        }
    }

    VIF_LOGI("AutoLayout: verts@0x%04X n=%u, matrices@0x%04X n=%u, "
             "normals=%d uvs=%d skin=%d",
             layout.vertex_address, layout.vertex_count,
             layout.matrix_address, layout.matrix_count,
             layout.has_normals ? 1 : 0, layout.has_uvs ? 1 : 0,
             layout.skinned ? 1 : 0);
    return BuildPacket(capture, layout, out);
}

}  // namespace ps2::vif1
