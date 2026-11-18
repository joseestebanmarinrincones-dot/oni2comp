#pragma once

// Parser basico de paquetes GIF de PS2.
//
// Lee un bloque de datos DMA/GIF desde ps2_ram, interpreta el GIFtag de
// 128 bits y extrae:
//
//   - A+D (0x0E) -> BITBLTBUF / TRXPOS / TRXREG / TRXDIR + pixeles
//   - GIF IMAGE con PSMZ32/24/16 -> DepthMap para el pipeline 3D
//   - PRIM / RGBAQ / UV / XYZ2 / XYZF2 (sprite 2D)
//
// En modo PACKED cada registro ocupa un qword de 16 bytes. Las transferencias
// de textura usan el registro A+D (0x0E): los 64 bits bajos son el dato y el
// byte 8 es la direccion del registro GS.
//
// Las texturas se guardan por DBP/TBP0 en Ps2TextureCache. Soporta PSMCT32/24/
// 16/16S y PSMT8/PSMT4 (con CLUT CPSM PSMCT32/24/16) ademas de PSMT8H/4HL/4HH.
// El source GIF se interpreta linealmente; si la textura no es valida, el
// renderer usa un checkerboard de respaldo.

#include "ps2_texture.hpp"
#include "ps2_texture_cache.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ps2::gif {

struct Vertex {
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    std::uint8_t rgba[4] = {255, 255, 255, 255};
};

// Modos de mezcla que usan los efectos del GS. Se derivan de PRIM.ABE y del
// registro ALPHA (0x42). Los efectos de almas y magia Genma suelen usar
// Additive/AdditiveFixed.
enum class BlendMode : std::uint8_t {
    Opaque = 0,    // ABE=0: el alpha se ignora
    Alpha,         // (Cs - Cd) * C + Cd
    Additive,      // Cs * As + Cd
    AdditiveFixed, // Cs * FIX + Cd
};

// Decodifica PRIM.ABE + ALPHA del GS. Si la ecuacion no es una de las
// habituales devuelve Alpha (source-over) como fallback seguro.
BlendMode DecodeGsBlend(std::uint16_t prim, std::uint64_t alpha);
const char *BlendModeName(BlendMode mode);

enum class BlendFactor : std::uint8_t {
    Zero = 0,
    One,
    SrcAlpha,
    OneMinusSrcAlpha,
};

struct BlendEquation {
    bool enabled = true;
    BlendFactor src = BlendFactor::SrcAlpha;
    BlendFactor dst = BlendFactor::OneMinusSrcAlpha;
};

// Traduccion unica que usan el renderer GLES y las pruebas.
BlendEquation GetBlendEquation(BlendMode mode);

struct DrawCommand {
    bool sprite = false;
    int vertex_count = 0;
    Vertex vertices[4];
    Texture texture;
    bool has_texture = false;
    bool textured = false;  // PRIM.TME: el sprite debe muestrear textura
    std::uint16_t prim = 0;
    std::uint64_t alpha_register = 0x00000044ull;
    std::uint32_t tfx = 0;
    BlendMode blend_mode = BlendMode::Alpha;
    std::uint32_t texture_width_hint = 0;
    std::uint32_t texture_height_hint = 0;
    bool has_depth_map = false;
    DepthMap depth_map;
};

struct Tag {
    std::uint16_t nloop = 0;
    bool eop = false;
    bool pre = false;
    std::uint16_t prim = 0;
    std::uint8_t flg = 0;
    std::uint8_t nreg = 0;
    std::array<std::uint8_t, 16> regs{};
    bool valid = false;
};

// Descriptores GIF (4 bits).
enum : std::uint8_t {
    kRegPrim = 0x00,
    kRegRgbaq = 0x01,
    kRegSt = 0x02,
    kRegUv = 0x03,
    kRegXyzf2 = 0x04,
    kRegXyz2 = 0x05,
    kRegTex0_1 = 0x06,
    kRegTex0_2 = 0x07,
    kRegClamp1 = 0x08,
    kRegClamp2 = 0x09,
    kRegFog = 0x0A,
    kRegXyzf3 = 0x0C,
    kRegXyz3 = 0x0D,
    kRegAd = 0x0E,
    kRegNop = 0x0F,
};

// Direcciones de registros GS que llegan por A+D.
enum : std::uint8_t {
    kGsPrim = 0x00,
    kGsRgbaq = 0x01,
    kGsSt = 0x02,
    kGsUv = 0x03,
    kGsXyzf2 = 0x04,
    kGsXyz2 = 0x05,
    kGsTex0_1 = 0x06,
    kGsTex0_2 = 0x07,
    kGsAlpha = 0x42,
    kGsAlpha2 = 0x43,
    kGsBitbltbuf = 0x50,
    kGsTrxpos = 0x51,
    kGsTrxreg = 0x52,
    kGsTrxdir = 0x53,
    kGsHwreg = 0x54,
    kGsSignal = 0x60,
    kGsFinish = 0x61,
    kGsLabel = 0x62,
};

std::size_t TagLoopSize(const Tag &tag);

class PacketParser {
public:
    void Reset();

    // Procesa un bloque de datos. Puede contener uno o varios GIFtags.
    // Devuelve true si produjo al menos un DrawCommand.
    bool ParseBlock(const std::uint8_t *data, std::size_t bytes,
                    DrawCommand &out);

    const Texture &CurrentTexture() const { return texture_; }

    // VRAM simulada del GS (4 MB). La usa el menu de depuracion.
    const std::vector<std::uint8_t> &Vram() const { return cache_.Vram(); }

    // Registros de textura para SaveQuickState/LoadQuickState.
    void SaveCacheState(std::vector<std::uint8_t> &out) const {
        cache_.SaveState(out);
    }
    bool LoadCacheState(const std::uint8_t *data, std::size_t size) {
        return cache_.LoadState(data, size);
    }

private:
    bool ParseTag(const std::uint8_t *data, std::size_t bytes,
                  std::size_t &offset, DrawCommand &out);
    void ParseRegister(std::uint8_t reg, const std::uint8_t *data,
                       std::size_t size, DrawCommand &out);
    std::size_t AppendTextureData(const std::uint8_t *data,
                                  std::size_t bytes);
    bool ConvertTexture(const std::uint8_t *data);
    bool ConvertDepthMap(const std::uint8_t *data);
    bool TakeDepthMap(DrawCommand &out);
    void ParseTex0();
    void RefreshTexture();
    void EmitSprite(DrawCommand &out);

    Ps2TextureCache cache_;
    Texture texture_;
    std::vector<std::uint8_t> texture_raw_;
    std::size_t texture_expected_ = 0;
    bool expect_texture_ = false;
    DepthMap depth_;
    bool depth_ready_ = false;

    // BITBLTBUF / TRXPOS de la transferencia actual.
    std::uint32_t trx_width_ = 0;
    std::uint32_t trx_height_ = 0;
    std::uint32_t trx_psm_ = 0;
    std::uint32_t trx_dir_ = 0;
    std::uint32_t trx_dbp_ = 0;
    std::uint32_t trx_dbw_ = 0;
    std::uint32_t trx_dsax_ = 0;
    std::uint32_t trx_dsay_ = 0;

    // Ultima transferencia completada (fallback si el paquete no trae TEX0).
    bool last_upload_valid_ = false;
    std::uint32_t last_upload_dbp_ = 0;
    std::uint32_t last_upload_psm_ = 0;
    std::uint32_t last_upload_width_ = 0;
    std::uint32_t last_upload_height_ = 0;

    // TEX0 (TEX0_1 + TEX0_2).
    std::uint64_t tex0_ = 0;
    bool tex0_valid_ = false;
    std::uint32_t tex0_tbp0_ = 0;
    std::uint32_t tex0_tbw_ = 0;
    std::uint32_t tex0_psm_ = 0;
    std::uint32_t tex0_width_ = 0;
    std::uint32_t tex0_height_ = 0;
    std::uint32_t tex0_tcc_ = 1;
    std::uint32_t tex0_cbp_ = 0;
    std::uint32_t tex0_cpsm_ = 0;
    std::uint32_t tex0_csm_ = 0;
    std::uint32_t tex0_csa_ = 0;
    std::uint32_t tex0_cld_ = 0;
    std::uint32_t tex0_tfx_ = 0;

    bool sprite_ = false;
    std::uint16_t prim_ = 0;
    bool prim_valid_ = false;
    std::uint64_t alpha_ = 0x00000044ull;
    std::uint8_t rgba_[4] = {255, 255, 255, 255};
    std::uint16_t u_raw_ = 0;
    std::uint16_t v_raw_ = 0;

    Vertex pending_[4];
    int pending_count_ = 0;
};

}  // namespace ps2::gif
