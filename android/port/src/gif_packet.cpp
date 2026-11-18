#include "gif_packet.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

namespace ps2::gif {
namespace {

// Formatos de Z-buffer del GS (campo PSM de BITBLTBUF/TEX0).
constexpr std::uint32_t kPsmZ32 = 0x30;
constexpr std::uint32_t kPsmZ24 = 0x31;
constexpr std::uint32_t kPsmZ16 = 0x32;
constexpr std::uint32_t kPsmZ16S = 0x3A;

bool is_depth_psm(std::uint32_t psm) {
    return psm == kPsmZ32 || psm == kPsmZ24 ||
           psm == kPsmZ16 || psm == kPsmZ16S;
}

std::uint16_t read16(const std::uint8_t *data) {
    std::uint16_t value = 0;
    std::memcpy(&value, data, sizeof(value));
    return value;
}

std::uint32_t read32(const std::uint8_t *data) {
    std::uint32_t value = 0;
    std::memcpy(&value, data, sizeof(value));
    return value;
}

std::uint64_t read64(const std::uint8_t *data) {
    std::uint64_t value = 0;
    std::memcpy(&value, data, sizeof(value));
    return value;
}

bool looks_like_tag(const std::uint8_t *data, std::size_t bytes) {
    if (data == nullptr || bytes < 16) return false;
    const std::uint64_t lo = read64(data);
    const std::uint32_t nloop = static_cast<std::uint32_t>(lo & 0x7FFFu);
    const std::uint8_t nreg = static_cast<std::uint8_t>((lo >> 60) & 0xFu);
    return nloop >= 1u && nreg <= 15u;
}

std::size_t bits_per_pixel(std::uint32_t psm) {
    switch (psm) {
    case 0:  return 32;  // PSMCT32
    case 1:  return 24;  // PSMCT24
    case 2:  return 16;  // PSMCT16
    case 10: return 16;  // PSMCT16S
    case 19: return 8;   // PSMT8
    case 20: return 4;   // PSMT4
    case 26:             // PSMT4HL
    case 27:             // PSMT8H
    case 44: return 32;  // PSMT4HH
    case kPsmZ32:  return 32;
    case kPsmZ24:  return 24;
    case kPsmZ16:
    case kPsmZ16S: return 16;
    default: return 0;
    }
}

bool parse_tag(const std::uint8_t *data, Tag &tag) {
    if (data == nullptr) return false;
    const std::uint64_t lo = read64(data);
    const std::uint64_t hi = read64(data + 8);

    tag.nloop = static_cast<std::uint16_t>(lo & 0x7FFFu);
    tag.eop = ((lo >> 15) & 1u) != 0;
    tag.pre = ((lo >> 46) & 1u) != 0;
    tag.prim = static_cast<std::uint16_t>((lo >> 47) & 0x7FFu);
    tag.flg = static_cast<std::uint8_t>((lo >> 58) & 0x3u);
    tag.nreg = static_cast<std::uint8_t>((lo >> 60) & 0xFu);
    for (int i = 0; i < 16; ++i) {
        tag.regs[static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>((hi >> (i * 4)) & 0xFu);
    }
    tag.valid = tag.nloop != 0;
    return tag.valid;
}

}  // namespace

std::size_t TagLoopSize(const Tag &tag) {
    const std::size_t count = tag.nreg == 0 ? 16u : tag.nreg;
    return count * 16u;  // PACKED: un qword por registro
}

BlendMode DecodeGsBlend(std::uint16_t prim, std::uint64_t alpha) {
    if ((prim & 0x40u) == 0u) return BlendMode::Opaque;

    // A/B: 0=Cs, 1=Cd, 2=0. C: 0=As, 1=Ad, 2=FIX. D: 0=Cs, 1=Cd, 2=0.
    const std::uint32_t a = static_cast<std::uint32_t>(alpha & 0x3u);
    const std::uint32_t b = static_cast<std::uint32_t>((alpha >> 2) & 0x3u);
    const std::uint32_t c = static_cast<std::uint32_t>((alpha >> 4) & 0x3u);
    const std::uint32_t d = static_cast<std::uint32_t>((alpha >> 6) & 0x3u);

    if (a == 0u && b == 2u && c == 2u && d == 1u) {
        return BlendMode::AdditiveFixed;  // Cs * FIX + Cd
    }
    if (a == 0u && b == 2u && c == 0u && d == 1u) {
        return BlendMode::Additive;  // Cs * As + Cd
    }
    if (a == 0u && b == 1u && d == 1u && (c == 0u || c == 2u)) {
        return BlendMode::Alpha;  // interpolacion Cs/Cd
    }
    // Subtract/framebuffer-alpha: el renderer usa DecodePs2AlphaBlend para la
    // ecuacion exacta; para el log/menu se clasifican como alpha.
    return BlendMode::Alpha;
}

const char *BlendModeName(BlendMode mode) {
    switch (mode) {
    case BlendMode::Opaque: return "opaque";
    case BlendMode::Alpha: return "alpha";
    case BlendMode::Additive: return "additive";
    case BlendMode::AdditiveFixed: return "additive-fixed";
    }
    return "alpha";
}

BlendEquation GetBlendEquation(BlendMode mode) {
    switch (mode) {
    case BlendMode::Opaque:
        return {false, BlendFactor::One, BlendFactor::Zero};
    case BlendMode::Additive:
        return {true, BlendFactor::SrcAlpha, BlendFactor::One};
    case BlendMode::AdditiveFixed:
        // Vista de alto nivel; la ecuacion exacta (FIX) la aplica
        // ps2::gfx::ApplyPs2AlphaBlend.
        return {true, BlendFactor::One, BlendFactor::One};
    case BlendMode::Alpha:
    default:
        return {true, BlendFactor::SrcAlpha,
                BlendFactor::OneMinusSrcAlpha};
    }
}

void PacketParser::Reset() {
    texture_ = Texture{};
    texture_raw_.clear();
    texture_expected_ = 0;
    expect_texture_ = false;
    depth_ = DepthMap{};
    depth_ready_ = false;
    cache_.Reset();
    texture_ = Texture{};
    trx_width_ = 0;
    trx_height_ = 0;
    trx_psm_ = 0;
    trx_dir_ = 0;
    trx_dbp_ = 0;
    trx_dbw_ = 0;
    trx_dsax_ = 0;
    trx_dsay_ = 0;
    last_upload_valid_ = false;
    last_upload_dbp_ = 0;
    last_upload_psm_ = 0;
    last_upload_width_ = 0;
    last_upload_height_ = 0;
    tex0_ = 0;
    tex0_valid_ = false;
    tex0_tbp0_ = 0;
    tex0_tbw_ = 0;
    tex0_psm_ = 0;
    tex0_width_ = 0;
    tex0_height_ = 0;
    tex0_tcc_ = 1;
    tex0_cbp_ = 0;
    tex0_cpsm_ = 0;
    tex0_csm_ = 0;
    tex0_csa_ = 0;
    tex0_cld_ = 0;
    tex0_tfx_ = 0;
    sprite_ = false;
    prim_ = 0;
    prim_valid_ = false;
    alpha_ = 0x00000044ull;
    rgba_[0] = rgba_[1] = rgba_[2] = rgba_[3] = 255;
    u_raw_ = 0;
    v_raw_ = 0;
    pending_count_ = 0;
}

bool PacketParser::ParseBlock(const std::uint8_t *data, std::size_t bytes,
                              DrawCommand &out) {
    out = DrawCommand{};
    if (data == nullptr || bytes == 0) return false;

    // Si venimos de un TRXDIR host->local, este bloque empieza con pixeles.
    // Puede empezar con bytes que parezcan un GIFtag (p. ej. indices 0,1,2...);
    // no hay que interpretarlo como tag.
    std::size_t offset = 0;
    if (expect_texture_) {
        // Si el bloque empieza con un GIFtag IMAGE, hay que parsearlo; sus
        // pixeles vienen detras. Si no, es un bloque de datos puro (una
        // transferencia partida en varios tags DMA).
        bool starts_with_image_tag = false;
        if (bytes >= 16) {
            const std::uint64_t lo = read64(data);
            const std::uint8_t flg =
                static_cast<std::uint8_t>((lo >> 58) & 0x3u);
            const std::uint32_t nloop =
                static_cast<std::uint32_t>(lo & 0x7FFFu);
            const std::uint8_t nreg =
                static_cast<std::uint8_t>((lo >> 60) & 0xFu);
            starts_with_image_tag = flg == 2u && nloop != 0u && nreg <= 15u;
        }
        if (!starts_with_image_tag) {
            offset = AppendTextureData(data, bytes);
            if (expect_texture_ || offset >= bytes) {
                if (TakeDepthMap(out)) return true;
                return false;
            }
            // La textura termino y quedan bytes: puede seguir un GIFtag.
        }
    }

    bool produced = false;
    while (offset + 16 <= bytes) {
        if (!looks_like_tag(data + offset, bytes - offset)) break;
        if (!ParseTag(data, bytes, offset, out)) break;
        if (out.vertex_count >= 2) produced = true;
    }
    if (TakeDepthMap(out)) produced = true;
    return produced;
}

bool PacketParser::TakeDepthMap(DrawCommand &out) {
    if (!depth_ready_) return false;
    out.has_depth_map = true;
    out.depth_map = std::move(depth_);
    depth_ = DepthMap{};
    depth_ready_ = false;
    return true;
}

bool PacketParser::ParseTag(const std::uint8_t *data, std::size_t bytes,
                            std::size_t &offset, DrawCommand &out) {
    Tag tag;
    if (offset + 16 > bytes || !parse_tag(data + offset, tag)) return false;
    offset += 16;

    // IMAGE mode: NLOOP qwords de pixeles para HWREG. Es el atajo que usa
    // el GS para subir texturas.
    if (tag.flg == 2) {
        const std::size_t image_bytes =
            static_cast<std::size_t>(tag.nloop) * 16u;
        if (offset + image_bytes > bytes) return false;
        if (expect_texture_) {
            AppendTextureData(data + offset, image_bytes);
        }
        offset += image_bytes;
        return true;
    }

    const std::size_t nreg = tag.nreg == 0 ? 16u : tag.nreg;
    const std::size_t loop_size = nreg * 16u;
    const std::size_t total = static_cast<std::size_t>(tag.nloop) * loop_size;
    if (offset + total > bytes) return false;

    if (tag.pre && tag.prim != 0) {
        prim_ = tag.prim;
        prim_valid_ = true;
        sprite_ = ((prim_ & 0x7u) == 6u);
    }

    for (std::uint32_t loop = 0; loop < tag.nloop; ++loop) {
        for (std::size_t i = 0; i < nreg; ++i) {
            ParseRegister(tag.regs[i], data + offset + i * 16u, 16u, out);
        }
        offset += loop_size;
    }

    // Si el tag dejo pendiente una subida de textura, los pixeles pueden
    // venir detras. Solo se consumen si el bloque no continua con otro tag.
    if (expect_texture_ && !looks_like_tag(data + offset, bytes - offset)) {
        // Payload suelto (transferencia partida). AppendTextureData consume
        // solo texture_expected_ bytes y deja el resto para el siguiente tag.
        offset += AppendTextureData(data + offset, bytes - offset);
    }

    return true;
}

void PacketParser::ParseRegister(std::uint8_t reg, const std::uint8_t *data,
                                 std::size_t size, DrawCommand &out) {
    if (data == nullptr || size < 16) return;

    switch (reg) {
    case kRegPrim:
        prim_ = static_cast<std::uint16_t>(read64(data) & 0x7FFu);
        prim_valid_ = true;
        sprite_ = ((prim_ & 0x7u) == 6u);
        break;

    case kRegRgbaq:
        // PACKED: R 0-7, G 32-39, B 64-71, A 96-103.
        rgba_[0] = data[0];
        rgba_[1] = data[4];
        rgba_[2] = data[8];
        rgba_[3] = data[12];
        break;

    case kRegUv:
        // U 0-13, V 32-45. Valor 12.4 fijo (1.0 = 16).
        u_raw_ = static_cast<std::uint16_t>(read16(data) & 0x3FFFu);
        v_raw_ = static_cast<std::uint16_t>(read16(data + 4) & 0x3FFFu);
        break;

    case kRegXyz2:
    case kRegXyzf2:
    case kRegXyz3:
    case kRegXyzf3:
        if (sprite_) {
            Vertex vertex;
            vertex.x = static_cast<float>(
                static_cast<std::int16_t>(read16(data))) / 16.0f;
            vertex.y = static_cast<float>(
                static_cast<std::int16_t>(read16(data + 4))) / 16.0f;

            const float tex_w = tex0_width_ != 0
                ? static_cast<float>(tex0_width_)
                : (last_upload_width_ != 0
                       ? static_cast<float>(last_upload_width_)
                       : (texture_.valid
                              ? static_cast<float>(texture_.width)
                              : 64.0f));
            const float tex_h = tex0_height_ != 0
                ? static_cast<float>(tex0_height_)
                : (last_upload_height_ != 0
                       ? static_cast<float>(last_upload_height_)
                       : (texture_.valid
                              ? static_cast<float>(texture_.height)
                              : 64.0f));

            vertex.u = (static_cast<float>(u_raw_) / 16.0f) / tex_w;
            vertex.v = (static_cast<float>(v_raw_) / 16.0f) / tex_h;
            std::memcpy(vertex.rgba, rgba_, sizeof(vertex.rgba));

            if (pending_count_ < 4) {
                pending_[pending_count_++] = vertex;
                if (pending_count_ == 2) EmitSprite(out);
            }
        }
        break;

    case kRegTex0_1:
        // TEX0_1 lleva los bits 0-31 del registro TEX0.
        tex0_ = (tex0_ & 0xFFFFFFFF00000000ull) |
                (static_cast<std::uint64_t>(read32(data)) & 0xFFFFFFFFull);
        ParseTex0();
        break;

    case kRegTex0_2:
        // TEX0_2 lleva los bits 32-63 del registro TEX0.
        tex0_ = (tex0_ & 0xFFFFFFFFull) |
                ((static_cast<std::uint64_t>(read32(data)) & 0xFFFFFFFFull)
                 << 32);
        ParseTex0();
        break;

    case kRegAd: {
        // A+D: 64 bits de dato, byte 8 = direccion del registro GS.
        const std::uint64_t value = read64(data);
        const std::uint8_t address = data[8];
        switch (address) {
        case kGsPrim:
            prim_ = static_cast<std::uint16_t>(value & 0x7FFu);
            prim_valid_ = true;
            sprite_ = ((prim_ & 0x7u) == 6u);
            break;
        case kGsAlpha:
        case kGsAlpha2:
            alpha_ = value;
            break;
        case kGsTex0_1:
            tex0_ = (tex0_ & 0xFFFFFFFF00000000ull) |
                    (value & 0xFFFFFFFFull);
            ParseTex0();
            break;
        case kGsTex0_2:
            tex0_ = (tex0_ & 0xFFFFFFFFull) |
                    ((value & 0xFFFFFFFFull) << 32);
            ParseTex0();
            break;
        case kGsBitbltbuf:
            // SBP 0-13, SBW 16-21, SPSM 24-29,
            // DBP 32-45, DBW 48-53, DPSM 56-61.
            trx_dbp_ = static_cast<std::uint32_t>((value >> 32) & 0x3FFFu);
            trx_dbw_ = static_cast<std::uint32_t>((value >> 48) & 0x3Fu);
            trx_psm_ = static_cast<std::uint32_t>((value >> 56) & 0x3Fu);
            break;
        case kGsTrxpos:
            // DSAX 32-42, DSAY 43-53.
            trx_dsax_ = static_cast<std::uint32_t>((value >> 32) & 0x7FFu);
            trx_dsay_ = static_cast<std::uint32_t>((value >> 43) & 0x7FFu);
            break;
        case kGsTrxreg:
            trx_width_ = static_cast<std::uint32_t>(value & 0xFFFu);
            trx_height_ =
                static_cast<std::uint32_t>((value >> 32) & 0xFFFu);
            texture_expected_ = 0;  // se calcula al saber el PSM
            break;
        case kGsTrxdir:
            trx_dir_ = static_cast<std::uint32_t>(value & 0x3u);
            expect_texture_ = (trx_dir_ == 0u);
            if (expect_texture_) {
                texture_raw_.clear();
                texture_expected_ = 0;
            }
            break;
        default:
            break;
        }
        break;
    }

    default:
        break;
    }
}

std::size_t PacketParser::AppendTextureData(const std::uint8_t *data,
                                             std::size_t bytes) {
    if (!expect_texture_ || data == nullptr || bytes == 0) return 0;
    if (trx_width_ == 0 || trx_height_ == 0) return 0;

    const std::uint32_t bpp = static_cast<std::uint32_t>(
        bits_per_pixel(trx_psm_));
    if (bpp == 0) return 0;

    if (texture_expected_ == 0) {
        texture_expected_ =
            static_cast<std::size_t>(trx_width_) * trx_height_ * bpp / 8u;
        texture_raw_.clear();
    }
    if (texture_raw_.size() >= texture_expected_) {
        ConvertTexture(texture_raw_.data());
        expect_texture_ = false;
        return 0;
    }

    const std::size_t remaining = texture_expected_ - texture_raw_.size();
    const std::size_t take = std::min(bytes, remaining);
    texture_raw_.insert(texture_raw_.end(), data, data + take);

    if (texture_raw_.size() >= texture_expected_) {
        ConvertTexture(texture_raw_.data());
        expect_texture_ = false;
    }
    return take;
}

bool PacketParser::ConvertDepthMap(const std::uint8_t *data) {
    if (data == nullptr || trx_width_ == 0 || trx_height_ == 0) return false;
    if (trx_width_ > 2048u || trx_height_ > 2048u) return false;

    const std::size_t count =
        static_cast<std::size_t>(trx_width_) * trx_height_;
    if (count == 0 || count > 4u * 1024u * 1024u) return false;

    DepthMap map;
    map.width = trx_width_;
    map.height = trx_height_;
    map.psm = trx_psm_;
    map.depth.resize(count);

    switch (trx_psm_) {
    case kPsmZ32:
        for (std::size_t i = 0; i < count; ++i) {
            // El GS usa 24 bits significativos aunque el formato sea de 32.
            map.depth[i] = read32(data + i * 4u) & 0xFFFFFFu;
        }
        break;
    case kPsmZ24:
        for (std::size_t i = 0; i < count; ++i) {
            const std::uint8_t *pixel = data + i * 3u;
            map.depth[i] = static_cast<std::uint32_t>(pixel[0]) |
                           (static_cast<std::uint32_t>(pixel[1]) << 8) |
                           (static_cast<std::uint32_t>(pixel[2]) << 16);
        }
        break;
    case kPsmZ16:
    case kPsmZ16S:
        for (std::size_t i = 0; i < count; ++i) {
            const std::uint32_t z16 = read16(data + i * 2u);
            // Expansion 16 -> 24 bits replicando los bits altos.
            map.depth[i] = ((z16 << 8) | (z16 >> 8)) & 0xFFFFFFu;
        }
        break;
    default:
        return false;
    }

    map.valid = true;
    depth_ = std::move(map);
    depth_ready_ = true;
    return true;
}

void PacketParser::ParseTex0() {
    tex0_valid_ = true;
    tex0_tbp0_ = static_cast<std::uint32_t>(tex0_ & 0x3FFFu);
    tex0_tbw_ = static_cast<std::uint32_t>((tex0_ >> 14) & 0x3Fu);
    tex0_psm_ = static_cast<std::uint32_t>((tex0_ >> 20) & 0x3Fu);
    const std::uint32_t tw =
        static_cast<std::uint32_t>((tex0_ >> 26) & 0xFu);
    const std::uint32_t th =
        static_cast<std::uint32_t>((tex0_ >> 30) & 0xFu);
    tex0_width_ = 1u << std::min<std::uint32_t>(tw, 10u);
    tex0_height_ = 1u << std::min<std::uint32_t>(th, 10u);
    tex0_tcc_ = static_cast<std::uint32_t>((tex0_ >> 34) & 0x1u);
    tex0_cbp_ = static_cast<std::uint32_t>((tex0_ >> 37) & 0x3FFFu);
    tex0_cpsm_ = static_cast<std::uint32_t>((tex0_ >> 51) & 0xFu);
    tex0_csm_ = static_cast<std::uint32_t>((tex0_ >> 55) & 0x1u);
    tex0_csa_ = static_cast<std::uint32_t>((tex0_ >> 56) & 0x1Fu);
    tex0_cld_ = static_cast<std::uint32_t>((tex0_ >> 61) & 0x7u);
    tex0_tfx_ = static_cast<std::uint32_t>((tex0_ >> 35) & 0x3u);
    RefreshTexture();
}

void PacketParser::RefreshTexture() {
    if (tex0_valid_) {
        texture_ = cache_.Resolve(
            tex0_tbp0_, tex0_psm_, tex0_width_, tex0_height_,
            tex0_cbp_, tex0_cpsm_, tex0_csm_, tex0_csa_, tex0_cld_,
            tex0_tcc_ != 0u);
        return;
    }

    if (last_upload_valid_) {
        texture_ = cache_.ResolveImage(
            last_upload_dbp_, last_upload_psm_,
            last_upload_width_, last_upload_height_,
            0, 0, 0, 0, 0, true);
        return;
    }

    texture_ = Texture{};
}

bool PacketParser::ConvertTexture(const std::uint8_t *data) {
    if (data == nullptr || trx_width_ == 0 || trx_height_ == 0) return false;

    if (is_depth_psm(trx_psm_)) {
        return ConvertDepthMap(data);
    }

    const std::uint32_t bpp = static_cast<std::uint32_t>(
        bits_per_pixel(trx_psm_));
    if (bpp == 0) return false;

    std::size_t bytes = texture_expected_;
    if (bytes == 0) {
        bytes = static_cast<std::size_t>(trx_width_) * trx_height_ * bpp / 8u;
    }

    cache_.UploadImage(trx_dbp_, trx_dbw_, trx_psm_, trx_width_, trx_height_,
                       data, bytes, trx_dsax_, trx_dsay_);
    last_upload_valid_ = true;
    last_upload_dbp_ = trx_dbp_;
    last_upload_psm_ = trx_psm_;
    last_upload_width_ = trx_width_;
    last_upload_height_ = trx_height_;
    RefreshTexture();
    return true;
}

void PacketParser::EmitSprite(DrawCommand &out) {
    if (pending_count_ < 2) return;

    RefreshTexture();
    out = DrawCommand{};
    out.sprite = true;
    out.vertex_count = pending_count_;
    for (int i = 0; i < pending_count_; ++i) {
        out.vertices[i] = pending_[i];
    }
    out.texture = texture_;
    out.has_texture = texture_.valid;
    out.prim = prim_;
    out.alpha_register = alpha_;
    out.tfx = tex0_tfx_;
    out.blend_mode = prim_valid_ ? DecodeGsBlend(prim_, alpha_)
                                 : BlendMode::Alpha;
    // Sin PRIM explicito se mantiene el comportamiento anterior: si hay
    // textura, se muestrea. Con PRIM, manda TME (bit 4).
    out.textured = prim_valid_ ? ((prim_ & 0x10u) != 0u) : texture_.valid;
    out.texture_width_hint = texture_.valid
        ? texture_.width
        : (tex0_width_ != 0 ? tex0_width_ : last_upload_width_);
    out.texture_height_hint = texture_.valid
        ? texture_.height
        : (tex0_height_ != 0 ? tex0_height_ : last_upload_height_);
    pending_count_ = 0;
}

}  // namespace ps2::gif
