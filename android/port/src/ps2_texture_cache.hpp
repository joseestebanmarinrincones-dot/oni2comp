#pragma once

// Cache de texturas GS para el port.
//
// El juego sube imagenes con GIF IMAGE (BITBLTBUF/TRXREG/TRXDIR host->local).
// Cada transferencia se guarda por su DBP (destino en bloques de 256 bytes),
// que es el mismo valor que luego aparece en TEX0.TBP0. Las CLUT se guardan
// igual, pero se referencian por TEX0.CBP.
//
// La decodificacion soporta:
//   - PSMCT32 / PSMCT24 / PSMCT16 / PSMCT16S
//   - PSMT8 / PSMT4 con CLUT (CPSM = PSMCT32/24/16/16S)
//   - PSMT8H / PSMT4HL / PSMT4HH (indices en los bits altos de palabras 32-bit)
//
// El source de una transferencia GIF es lineal; el GS lo swizzlea al escribir
// VRAM. Para el port guardamos el source lineal y lo muestreamos linealmente,
// que reproduce la misma imagen logica. La CLUT tambien se interpreta en el
// orden lineal de la transferencia (habitual en menus y fuentes).

#include "ps2_texture.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace ps2::gif {

class Ps2TextureCache {
public:
    static constexpr std::uint32_t kVramSize = 4u * 1024u * 1024u;
    static constexpr std::uint32_t kPsmCt32 = 0;
    static constexpr std::uint32_t kPsmCt24 = 1;
    static constexpr std::uint32_t kPsmCt16 = 2;
    static constexpr std::uint32_t kPsmCt16S = 10;
    static constexpr std::uint32_t kPsmT8 = 19;
    static constexpr std::uint32_t kPsmT4 = 20;
    static constexpr std::uint32_t kPsmT4HL = 26;
    static constexpr std::uint32_t kPsmT8H = 27;
    static constexpr std::uint32_t kPsmT4HH = 44;

    void Reset();

    // Registra una transferencia host->local completa.
    // dbp: destino (TBP0/CBP en bloques de 256 bytes).
    // dbw: ancho de buffer GS (solo informativo).
    // dpsm: formato de destino.
    // data: pixeles lineales tal como llegan por el GIF.
    void UploadImage(std::uint32_t dbp, std::uint32_t dbw,
                     std::uint32_t dpsm, std::uint32_t width,
                     std::uint32_t height, const std::uint8_t *data,
                     std::size_t bytes, std::uint32_t dsax = 0,
                     std::uint32_t dsay = 0);

    bool HasImage(std::uint32_t base) const;

    // Decodifica la textura TBP0 con la CLUT CBP. width/height son los de
    // TEX0 (1 << TW, 1 << TH); si son 0 se usan los de la transferencia.
    Texture Resolve(std::uint32_t tbp0, std::uint32_t psm,
                    std::uint32_t width, std::uint32_t height,
                    std::uint32_t cbp, std::uint32_t cpsm,
                    std::uint32_t csm, std::uint32_t csa,
                    std::uint32_t cld, bool tcc) const;

    // Igual que Resolve pero usando directamente la ultima transferencia de
    // un DBP (fallback cuando el paquete GIF no trae TEX0).
    Texture ResolveImage(std::uint32_t base, std::uint32_t psm,
                         std::uint32_t width, std::uint32_t height,
                         std::uint32_t cbp, std::uint32_t cpsm,
                         std::uint32_t csm, std::uint32_t csa,
                         std::uint32_t cld, bool tcc) const;

    std::size_t ImageCount() const { return records_.size(); }

    // Shadow lineal de los 4 MB de VRAM del GS. El decoder actual usa los
    // registros de subida (source lineal); este buffer queda disponible para
    // futuras lecturas con swizzle.
    const std::vector<std::uint8_t> &Vram() const { return vram_; }

    // Instantanea de los registros de textura (sin los 4 MB de VRAM, que
    // gestiona quick_state). Se usa en SaveQuickState/LoadQuickState.
    void SaveState(std::vector<std::uint8_t> &out) const;
    bool LoadState(const std::uint8_t *data, std::size_t size);

private:
    struct Record {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t psm = 0;
        std::uint32_t dbw = 0;
        std::uint32_t dsax = 0;
        std::uint32_t dsay = 0;
        std::vector<std::uint8_t> data;
        std::uint64_t generation = 0;
    };

    const Record *Find(std::uint32_t base) const;

    Texture Decode(const Record &tex, const Record *clut, std::uint32_t psm,
                   std::uint32_t width, std::uint32_t height,
                   std::uint32_t cbp, std::uint32_t cpsm,
                   std::uint32_t csm, std::uint32_t csa,
                   std::uint32_t cld, bool tcc) const;

    static std::vector<std::uint32_t> DecodeClut(const Record &clut,
                                                 std::uint32_t cpsm,
                                                 bool tcc);

    std::unordered_map<std::uint32_t, Record> records_;
    mutable std::unordered_map<std::uint64_t, Texture> cache_;
    std::vector<std::uint8_t> vram_ =
        std::vector<std::uint8_t>(kVramSize, 0);
    std::uint64_t generation_ = 1;
};

}  // namespace ps2::gif
