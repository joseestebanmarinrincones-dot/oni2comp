#include "ps2_texture_cache.hpp"

#include <algorithm>
#include <cstring>

namespace ps2::gif {
namespace {

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

std::uint8_t expand5(std::uint32_t value) {
    return static_cast<std::uint8_t>((value << 3) | (value >> 2));
}

bool is_indexed(std::uint32_t psm) {
    return psm == Ps2TextureCache::kPsmT8 ||
           psm == Ps2TextureCache::kPsmT4 ||
           psm == Ps2TextureCache::kPsmT8H ||
           psm == Ps2TextureCache::kPsmT4HL ||
           psm == Ps2TextureCache::kPsmT4HH;
}

std::uint32_t color_bpp(std::uint32_t psm) {
    switch (psm) {
    case Ps2TextureCache::kPsmCt32: return 32;
    case Ps2TextureCache::kPsmCt24: return 24;
    case Ps2TextureCache::kPsmCt16:
    case Ps2TextureCache::kPsmCt16S: return 16;
    default: return 0;
    }
}

void put(std::vector<std::uint8_t> &out, std::size_t offset,
         std::uint32_t rgba) {
    out[offset + 0] = static_cast<std::uint8_t>(rgba & 0xFFu);
    out[offset + 1] = static_cast<std::uint8_t>((rgba >> 8) & 0xFFu);
    out[offset + 2] = static_cast<std::uint8_t>((rgba >> 16) & 0xFFu);
    out[offset + 3] = static_cast<std::uint8_t>((rgba >> 24) & 0xFFu);
}

std::uint32_t pack_rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                        std::uint8_t a) {
    return static_cast<std::uint32_t>(r) |
           (static_cast<std::uint32_t>(g) << 8) |
           (static_cast<std::uint32_t>(b) << 16) |
           (static_cast<std::uint32_t>(a) << 24);
}

std::uint64_t mix(std::uint64_t key, std::uint64_t value) {
    key ^= value + 0x9E3779B97F4A7C15ull + (key << 6) + (key >> 2);
    return key;
}

}  // namespace

void Ps2TextureCache::Reset() {
    records_.clear();
    cache_.clear();
    std::fill(vram_.begin(), vram_.end(), 0u);
    generation_ = 1;
}

void Ps2TextureCache::UploadImage(std::uint32_t dbp, std::uint32_t dbw,
                                  std::uint32_t dpsm, std::uint32_t width,
                                  std::uint32_t height,
                                  const std::uint8_t *data, std::size_t bytes,
                                  std::uint32_t dsax, std::uint32_t dsay) {
    if (data == nullptr || bytes == 0 || width == 0 || height == 0) return;
    if (width > 4096u || height > 4096u) return;

    Record record;
    record.width = width;
    record.height = height;
    record.psm = dpsm;
    record.dbw = dbw;
    record.dsax = dsax;
    record.dsay = dsay;
    record.data.assign(data, data + bytes);
    record.generation = generation_++;

    // Shadow lineal de VRAM: DBP esta en bloques de 256 bytes.
    const std::size_t vram_offset =
        static_cast<std::size_t>(dbp) * 256u;
    if (vram_offset < vram_.size()) {
        const std::size_t copy =
            std::min<std::size_t>(bytes, vram_.size() - vram_offset);
        std::memcpy(vram_.data() + vram_offset, data, copy);
    }

    records_[dbp] = std::move(record);
    cache_.clear();
}

bool Ps2TextureCache::HasImage(std::uint32_t base) const {
    return records_.find(base) != records_.end();
}

const Ps2TextureCache::Record *Ps2TextureCache::Find(
    std::uint32_t base) const {
    const auto it = records_.find(base);
    return it == records_.end() ? nullptr : &it->second;
}

std::vector<std::uint32_t> Ps2TextureCache::DecodeClut(
    const Record &clut, std::uint32_t cpsm, bool tcc) {
    std::vector<std::uint32_t> palette;
    if (clut.data.empty()) return palette;

    const std::size_t pixels =
        static_cast<std::size_t>(clut.width) * clut.height;
    if (pixels == 0) return palette;

    if (cpsm == kPsmCt32) {
        const std::size_t count =
            std::min(pixels, clut.data.size() / 4u);
        palette.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            const std::uint32_t value = read32(clut.data.data() + i * 4u);
            const std::uint8_t a = tcc
                ? static_cast<std::uint8_t>((value >> 24) & 0xFFu)
                : 255u;
            palette.push_back(pack_rgba(
                static_cast<std::uint8_t>(value & 0xFFu),
                static_cast<std::uint8_t>((value >> 8) & 0xFFu),
                static_cast<std::uint8_t>((value >> 16) & 0xFFu), a));
        }
    } else if (cpsm == kPsmCt24) {
        const std::size_t count =
            std::min(pixels, clut.data.size() / 3u);
        palette.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            const std::uint8_t *pixel = clut.data.data() + i * 3u;
            palette.push_back(pack_rgba(pixel[0], pixel[1], pixel[2], 255u));
        }
    } else if (cpsm == kPsmCt16 || cpsm == kPsmCt16S) {
        const std::size_t count =
            std::min(pixels, clut.data.size() / 2u);
        palette.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            const std::uint16_t value = read16(clut.data.data() + i * 2u);
            const std::uint8_t a = tcc
                ? (((value >> 15) & 1u) != 0u ? 255u : 0u)
                : 255u;
            palette.push_back(pack_rgba(
                expand5(value & 0x1Fu),
                expand5((value >> 5) & 0x1Fu),
                expand5((value >> 10) & 0x1Fu), a));
        }
    }

    return palette;
}

Texture Ps2TextureCache::Decode(const Record &tex, const Record *clut,
                                std::uint32_t psm, std::uint32_t width,
                                std::uint32_t height, std::uint32_t cbp,
                                std::uint32_t cpsm, std::uint32_t csm,
                                std::uint32_t csa, std::uint32_t cld,
                                bool tcc) const {
    (void)cbp;
    (void)csm;
    (void)cld;

    Texture out;
    if (tex.data.empty()) return out;

    const std::uint32_t w = width != 0
        ? std::min(width, tex.width)
        : tex.width;
    const std::uint32_t h = height != 0
        ? std::min(height, tex.height)
        : tex.height;
    if (w == 0 || h == 0) return out;

    out.width = w;
    out.height = h;
    out.psm = psm;

    if (psm == kPsmCt32 || psm == kPsmCt24 || psm == kPsmCt16 ||
        psm == kPsmCt16S) {
        const std::uint32_t bpp = color_bpp(psm);
        const std::size_t bytes_per_pixel = bpp / 8u;
        // Se leen filas completas del source (tex.width), aunque TEX0
        // recorte el ancho logico.
        const std::size_t required =
            static_cast<std::size_t>(tex.width) * h * bytes_per_pixel;
        if (tex.data.size() < required) return out;

        out.rgba.assign(static_cast<std::size_t>(w) * h * 4u, 255u);
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                // El stride de origen es el ancho de la transferencia, no
                // el ancho logico de TEX0.
                const std::size_t pixel =
                    static_cast<std::size_t>(y) * tex.width + x;
                const std::uint8_t *src = tex.data.data() + pixel * bytes_per_pixel;
                std::uint32_t rgba = 0;
                if (psm == kPsmCt32) {
                    const std::uint32_t value = read32(src);
                    rgba = pack_rgba(
                        static_cast<std::uint8_t>(value & 0xFFu),
                        static_cast<std::uint8_t>((value >> 8) & 0xFFu),
                        static_cast<std::uint8_t>((value >> 16) & 0xFFu),
                        tcc ? static_cast<std::uint8_t>((value >> 24) & 0xFFu)
                            : 255u);
                } else if (psm == kPsmCt24) {
                    rgba = pack_rgba(src[0], src[1], src[2], 255u);
                } else {
                    const std::uint16_t value = read16(src);
                    rgba = pack_rgba(
                        expand5(value & 0x1Fu),
                        expand5((value >> 5) & 0x1Fu),
                        expand5((value >> 10) & 0x1Fu),
                        tcc ? (((value >> 15) & 1u) != 0u ? 255u : 0u)
                            : 255u);
                }
                put(out.rgba, pixel * 4u, rgba);
            }
        }
        out.valid = true;
        return out;
    }

    if (!is_indexed(psm) || clut == nullptr) return out;

    const std::vector<std::uint32_t> palette =
        DecodeClut(*clut, cpsm, tcc);
    if (palette.empty()) return out;

    out.rgba.assign(static_cast<std::size_t>(w) * h * 4u, 0u);

    const std::uint32_t base = (csa & 15u) * 16u;
    const bool four_bit = psm == kPsmT4 || psm == kPsmT4HL ||
                          psm == kPsmT4HH;

    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            std::uint32_t index = 0;
            if (psm == kPsmT8) {
                const std::size_t offset =
                    static_cast<std::size_t>(y) * tex.width + x;
                if (offset >= tex.data.size()) return out;
                index = tex.data[offset];
            } else if (psm == kPsmT4) {
                const std::size_t row_bytes = (tex.width + 1u) / 2u;
                const std::size_t offset =
                    static_cast<std::size_t>(y) * row_bytes + x / 2u;
                if (offset >= tex.data.size()) return out;
                const std::uint8_t byte = tex.data[offset];
                index = (x & 1u) ? (byte >> 4) : (byte & 0x0Fu);
            } else {
                const std::size_t pixel =
                    static_cast<std::size_t>(y) * tex.width + x;
                const std::size_t offset = pixel * 4u;
                if (offset + 3u >= tex.data.size()) return out;
                const std::uint8_t high = tex.data[offset + 3u];
                if (psm == kPsmT8H) {
                    index = high;
                } else if (psm == kPsmT4HL) {
                    index = high & 0x0Fu;
                } else {  // kPsmT4HH
                    index = (high >> 4) & 0x0Fu;
                }
            }

            std::size_t entry = base + index;
            if (four_bit) {
                if (entry >= palette.size()) {
                    entry = palette.size() - 1u;
                }
            } else if (entry >= palette.size()) {
                // El CLUT de 8 bits enmascara el offset; PCSX2 lo satura
                // al ultimo bloque de 16 colores.
                entry = palette.size() - 1u;
            }

            put(out.rgba,
                (static_cast<std::size_t>(y) * w + x) * 4u,
                palette[entry]);
        }
    }

    out.valid = true;
    return out;
}

Texture Ps2TextureCache::Resolve(std::uint32_t tbp0, std::uint32_t psm,
                                 std::uint32_t width, std::uint32_t height,
                                 std::uint32_t cbp, std::uint32_t cpsm,
                                 std::uint32_t csm, std::uint32_t csa,
                                 std::uint32_t cld, bool tcc) const {
    const Record *tex = Find(tbp0);
    if (tex == nullptr) return Texture{};
    const Record *clut = is_indexed(psm) ? Find(cbp) : nullptr;

    std::uint64_t key = 0xCBF29CE484222325ull;
    key = mix(key, tbp0);
    key = mix(key, psm);
    key = mix(key, width);
    key = mix(key, height);
    key = mix(key, cbp);
    key = mix(key, cpsm);
    key = mix(key, csm);
    key = mix(key, csa);
    key = mix(key, cld);
    key = mix(key, tcc ? 1u : 0u);
    key = mix(key, tex->generation);
    if (clut != nullptr) key = mix(key, clut->generation);

    const auto cached = cache_.find(key);
    if (cached != cache_.end()) return cached->second;

    Texture result = Decode(*tex, clut, psm, width, height, cbp, cpsm,
                            csm, csa, cld, tcc);
    cache_[key] = result;
    return result;
}

Texture Ps2TextureCache::ResolveImage(std::uint32_t base, std::uint32_t psm,
                                      std::uint32_t width,
                                      std::uint32_t height,
                                      std::uint32_t cbp, std::uint32_t cpsm,
                                      std::uint32_t csm, std::uint32_t csa,
                                      std::uint32_t cld, bool tcc) const {
    const Record *tex = Find(base);
    if (tex == nullptr) return Texture{};
    const std::uint32_t effective_psm =
        psm == 0xFFFFFFFFu ? tex->psm : psm;
    const std::uint32_t effective_width = width != 0 ? width : tex->width;
    const std::uint32_t effective_height = height != 0 ? height : tex->height;
    return Resolve(base, effective_psm, effective_width, effective_height,
                   cbp, cpsm, csm, csa, cld, tcc);
}

namespace {

struct StateWriter {
    std::vector<std::uint8_t> &out;
    void u8(std::uint8_t value) { out.push_back(value); }
    void u16(std::uint16_t value) {
        const std::uint8_t bytes[2] = {
            static_cast<std::uint8_t>(value & 0xFF),
            static_cast<std::uint8_t>((value >> 8) & 0xFF)};
        out.insert(out.end(), bytes, bytes + 2);
    }
    void u32(std::uint32_t value) {
        std::uint8_t bytes[4];
        std::memcpy(bytes, &value, sizeof(value));
        out.insert(out.end(), bytes, bytes + 4);
    }
    void u64(std::uint64_t value) {
        std::uint8_t bytes[8];
        std::memcpy(bytes, &value, sizeof(value));
        out.insert(out.end(), bytes, bytes + 8);
    }
    void bytes(const void *data, std::size_t size) {
        const auto *source = static_cast<const std::uint8_t *>(data);
        out.insert(out.end(), source, source + size);
    }
};

struct StateReader {
    const std::uint8_t *data = nullptr;
    std::size_t size = 0;
    std::size_t offset = 0;
    bool ok = true;

    bool can(std::size_t bytes) const {
        return ok && bytes <= size - std::min(offset, size);
    }
    std::uint8_t u8() {
        if (!can(1)) { ok = false; return 0; }
        return data[offset++];
    }
    std::uint16_t u16() {
        if (!can(2)) { ok = false; return 0; }
        std::uint16_t value = 0;
        std::memcpy(&value, data + offset, 2);
        offset += 2;
        return value;
    }
    std::uint32_t u32() {
        if (!can(4)) { ok = false; return 0; }
        std::uint32_t value = 0;
        std::memcpy(&value, data + offset, 4);
        offset += 4;
        return value;
    }
    std::uint64_t u64() {
        if (!can(8)) { ok = false; return 0; }
        std::uint64_t value = 0;
        std::memcpy(&value, data + offset, 8);
        offset += 8;
        return value;
    }
    const std::uint8_t *take(std::size_t bytes) {
        if (!can(bytes)) { ok = false; return nullptr; }
        const std::uint8_t *result = data + offset;
        offset += bytes;
        return result;
    }
};

}  // namespace

void Ps2TextureCache::SaveState(std::vector<std::uint8_t> &out) const {
    StateWriter writer{out};
    writer.u32(static_cast<std::uint32_t>(records_.size()));
    for (const auto &pair : records_) {
        const Record &record = pair.second;
        writer.u32(pair.first);
        writer.u32(record.width);
        writer.u32(record.height);
        writer.u32(record.psm);
        writer.u32(record.dbw);
        writer.u32(record.dsax);
        writer.u32(record.dsay);
        writer.u64(record.generation);
        writer.u64(static_cast<std::uint64_t>(record.data.size()));
        if (!record.data.empty()) {
            writer.bytes(record.data.data(), record.data.size());
        }
    }
}

bool Ps2TextureCache::LoadState(const std::uint8_t *data, std::size_t size) {
    if (data == nullptr || size == 0) return false;
    StateReader reader{data, size};
    const std::uint32_t count = reader.u32();
    if (!reader.ok || count > 100000u) return false;

    std::unordered_map<std::uint32_t, Record> records;
    records.reserve(count);
    std::uint64_t max_generation = generation_;

    for (std::uint32_t i = 0; i < count; ++i) {
        Record record;
        const std::uint32_t base = reader.u32();
        record.width = reader.u32();
        record.height = reader.u32();
        record.psm = reader.u32();
        record.dbw = reader.u32();
        record.dsax = reader.u32();
        record.dsay = reader.u32();
        record.generation = reader.u64();
        const std::uint64_t bytes = reader.u64();
        if (!reader.ok || bytes > reader.size - reader.offset) return false;
        const std::uint8_t *payload = reader.take(static_cast<std::size_t>(bytes));
        if (payload == nullptr) return false;
        record.data.assign(payload, payload + bytes);
        max_generation = std::max(max_generation, record.generation);
        records[base] = std::move(record);
    }

    if (!reader.ok) return false;
    records_ = std::move(records);
    cache_.clear();
    generation_ = max_generation;
    return true;
}

}  // namespace ps2::gif
