// Prueba de intercambio de personaje: Jubei, Ekei, Magoichi, Kotaro y Oyu
// suben sus texturas a los mismos TBP0/CBP de VRAM (como hace el juego al
// cambiar de overlay). Verifica que Ps2TextureCache no devuelva pixeles,
// paletas ni metadatos del personaje anterior.
//
// Compila con:
//   clang++ -std=c++17 -O2 -Isrc `
//     tests/character_texture_swap_test.cpp src/ps2_texture_cache.cpp `
//     -o character_swap_test.exe
//   .\character_swap_test.exe

#include "ps2_texture_cache.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const std::string &name) {
    std::printf("  %-62s %s\n", name.c_str(), condition ? "PASS" : "FAIL");
    if (!condition) ++failures;
}

void check_pixel(const ps2::gif::Texture &texture, std::size_t pixel,
                 std::uint32_t rgba, const std::string &name) {
    if (!texture.valid || texture.rgba.size() < (pixel + 1u) * 4u) {
        check(false, name + " (textura ausente)");
        return;
    }
    const std::uint8_t *p = texture.rgba.data() + pixel * 4u;
    const std::uint8_t r = static_cast<std::uint8_t>(rgba & 0xFFu);
    const std::uint8_t g = static_cast<std::uint8_t>((rgba >> 8) & 0xFFu);
    const std::uint8_t b = static_cast<std::uint8_t>((rgba >> 16) & 0xFFu);
    const std::uint8_t a = static_cast<std::uint8_t>((rgba >> 24) & 0xFFu);
    const bool ok = p[0] == r && p[1] == g && p[2] == b && p[3] == a;
    check(ok, name);
    if (!ok) {
        std::printf("      esperado RGBA %02X %02X %02X %02X, obtenido "
                    "%02X %02X %02X %02X\n",
                    r, g, b, a, p[0], p[1], p[2], p[3]);
    }
}

std::uint32_t pack_rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                        std::uint8_t a = 255u) {
    return static_cast<std::uint32_t>(r) |
           (static_cast<std::uint32_t>(g) << 8) |
           (static_cast<std::uint32_t>(b) << 16) |
           (static_cast<std::uint32_t>(a) << 24);
}

std::vector<std::uint8_t> ct32_bytes(const std::vector<std::uint32_t> &colors) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(colors.size() * 4u);
    for (std::uint32_t color : colors) {
        for (int i = 0; i < 4; ++i) {
            bytes.push_back(static_cast<std::uint8_t>(
                (color >> (i * 8)) & 0xFFu));
        }
    }
    return bytes;
}

std::vector<std::uint32_t> character_palette(std::uint32_t seed) {
    std::vector<std::uint32_t> palette(16);
    for (std::uint32_t i = 0; i < palette.size(); ++i) {
        const std::uint8_t r = static_cast<std::uint8_t>(
            (seed * 37u + i * 11u) & 0xFFu);
        const std::uint8_t g = static_cast<std::uint8_t>(
            (seed * 53u + i * 29u) & 0xFFu);
        const std::uint8_t b = static_cast<std::uint8_t>(
            (seed * 91u + i * 7u) & 0xFFu);
        palette[i] = pack_rgba(r, g, b);
    }
    return palette;
}

std::vector<std::uint32_t> character_color_texture(std::uint32_t seed) {
    std::vector<std::uint32_t> colors(4);
    for (std::uint32_t i = 0; i < colors.size(); ++i) {
        colors[i] = pack_rgba(
            static_cast<std::uint8_t>((seed * 31u + i * 17u) & 0xFFu),
            static_cast<std::uint8_t>((seed * 47u + i * 23u) & 0xFFu),
            static_cast<std::uint8_t>((seed * 61u + i * 13u) & 0xFFu));
    }
    return colors;
}

struct CharacterSet {
    const char *name = "";
    std::uint32_t psm = ps2::gif::Ps2TextureCache::kPsmT8;
    std::uint32_t width = 4;
    std::uint32_t height = 4;
    std::vector<std::uint8_t> indices;
    std::vector<std::uint32_t> palette;
    std::vector<std::uint32_t> colors;
};

CharacterSet make_character(const char *name, std::uint32_t seed,
                            std::uint32_t psm, std::uint32_t width,
                            std::uint32_t height) {
    CharacterSet character;
    character.name = name;
    character.psm = psm;
    character.width = width;
    character.height = height;
    character.palette = character_palette(seed);
    character.colors = character_color_texture(seed);

    const std::size_t pixels =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<std::uint8_t> indices(pixels);
    for (std::size_t i = 0; i < pixels; ++i) {
        indices[i] = static_cast<std::uint8_t>(i & 15u);
    }

    if (psm == ps2::gif::Ps2TextureCache::kPsmT4) {
        character.indices.resize((pixels + 1u) / 2u);
        for (std::size_t i = 0; i < pixels; i += 2) {
            const std::uint8_t low = indices[i];
            const std::uint8_t high =
                i + 1 < pixels ? indices[i + 1] : 0u;
            character.indices[i / 2u] =
                static_cast<std::uint8_t>(low | (high << 4));
        }
    } else {
        character.indices = std::move(indices);
    }
    return character;
}

constexpr std::uint32_t kColorBase = 0x040;    // textura CT32 2x2
constexpr std::uint32_t kIndexedBase = 0x080;  // PSMT8/PSMT4 4x4
constexpr std::uint32_t kClutBase = 0x100;     // paleta 16 colores
constexpr std::uint32_t kHudBase = 0x180;      // textura compartida (HUD)

// Fuente de texto HUD que no cambia entre personajes.
const std::vector<std::uint32_t> kHudColors = {
    pack_rgba(255, 255, 255), pack_rgba(128, 128, 128),
    pack_rgba(32, 32, 32), pack_rgba(255, 200, 0)};

void upload_character(ps2::gif::Ps2TextureCache &cache,
                      const CharacterSet &character) {
    const std::vector<std::uint8_t> palette = ct32_bytes(character.palette);
    cache.UploadImage(kClutBase, 1, ps2::gif::Ps2TextureCache::kPsmCt32,
                      static_cast<std::uint32_t>(character.palette.size()), 1,
                      palette.data(), palette.size());

    cache.UploadImage(kIndexedBase, 1, character.psm, character.width,
                      character.height, character.indices.data(),
                      character.indices.size());

    const std::vector<std::uint8_t> colors = ct32_bytes(character.colors);
    cache.UploadImage(kColorBase, 1, ps2::gif::Ps2TextureCache::kPsmCt32, 2,
                      2, colors.data(), colors.size());
}

std::uint32_t expected_index_color(const CharacterSet &character,
                                   std::size_t pixel) {
    if (character.psm == ps2::gif::Ps2TextureCache::kPsmT4) {
        const std::uint8_t byte = character.indices[pixel / 2u];
        const std::uint8_t index =
            (pixel & 1u) ? (byte >> 4) : (byte & 0x0Fu);
        return character.palette[index & 15u];
    }
    return character.palette[character.indices[pixel] & 15u];
}

void verify_character(ps2::gif::Ps2TextureCache &cache,
                      const CharacterSet &character,
                      const std::vector<std::uint32_t> &hud_colors,
                      bool check_vram = true) {
    const std::string prefix = std::string(character.name) + ": ";

    const ps2::gif::Texture indexed = cache.Resolve(
        kIndexedBase, character.psm, character.width, character.height,
        kClutBase, ps2::gif::Ps2TextureCache::kPsmCt32, 0, 0, 0, true);
    check(indexed.valid, prefix + "textura indexada valida");
    check(indexed.width == character.width &&
              indexed.height == character.height,
          prefix + "dimensiones de textura");

    const std::size_t pixels =
        static_cast<std::size_t>(character.width) * character.height;
    bool all_pixels = indexed.valid && indexed.rgba.size() == pixels * 4u;
    if (all_pixels) {
        for (std::size_t i = 0; i < pixels; ++i) {
            const std::uint8_t *p = indexed.rgba.data() + i * 4u;
            const std::uint32_t expected = expected_index_color(character, i);
            if (p[0] != static_cast<std::uint8_t>(expected & 0xFFu) ||
                p[1] != static_cast<std::uint8_t>((expected >> 8) & 0xFFu) ||
                p[2] != static_cast<std::uint8_t>((expected >> 16) & 0xFFu) ||
                p[3] != static_cast<std::uint8_t>((expected >> 24) & 0xFFu)) {
                all_pixels = false;
                break;
            }
        }
    }
    check(all_pixels, prefix + "16 pixeles con la paleta correcta");

    const ps2::gif::Texture colors = cache.Resolve(
        kColorBase, ps2::gif::Ps2TextureCache::kPsmCt32, 2, 2, 0, 0, 0, 0, 0,
        true);
    check(colors.valid, prefix + "textura CT32 valida");
    bool all_colors = colors.valid && colors.rgba.size() == 16u;
    if (all_colors) {
        for (std::size_t i = 0; i < 4; ++i) {
            const std::uint8_t *p = colors.rgba.data() + i * 4u;
            const std::uint32_t expected = character.colors[i];
            if (p[0] != static_cast<std::uint8_t>(expected & 0xFFu) ||
                p[1] != static_cast<std::uint8_t>((expected >> 8) & 0xFFu) ||
                p[2] != static_cast<std::uint8_t>((expected >> 16) & 0xFFu) ||
                p[3] != static_cast<std::uint8_t>((expected >> 24) & 0xFFu)) {
                all_colors = false;
                break;
            }
        }
    }
    check(all_colors, prefix + "textura CT32 sin residuos");

    const ps2::gif::Texture hud = cache.Resolve(
        kHudBase, ps2::gif::Ps2TextureCache::kPsmCt32, 2, 2, 0, 0, 0, 0, 0,
        true);
    check(hud.valid, prefix + "textura HUD compartida intacta");
    bool all_hud = hud.valid && hud.rgba.size() == 16u;
    if (all_hud) {
        for (std::size_t i = 0; i < 4; ++i) {
            const std::uint8_t *p = hud.rgba.data() + i * 4u;
            const std::uint32_t expected = hud_colors[i];
            if (p[0] != static_cast<std::uint8_t>(expected & 0xFFu) ||
                p[1] != static_cast<std::uint8_t>((expected >> 8) & 0xFFu) ||
                p[2] != static_cast<std::uint8_t>((expected >> 16) & 0xFFu) ||
                p[3] != static_cast<std::uint8_t>((expected >> 24) & 0xFFu)) {
                all_hud = false;
                break;
            }
        }
    }
    check(all_hud, prefix + "HUD no se contamina con el personaje");

    // El shadow de VRAM debe contener exactamente los ultimos pixeles.
    if (!check_vram) return;
    const std::vector<std::uint8_t> &vram = cache.Vram();
    const std::size_t offset =
        static_cast<std::size_t>(kIndexedBase) * 256u;
    bool vram_ok = offset + character.indices.size() <= vram.size();
    if (vram_ok) {
        vram_ok = std::equal(character.indices.begin(),
                             character.indices.end(),
                             vram.begin() + static_cast<std::ptrdiff_t>(offset));
    }
    check(vram_ok, prefix + "VRAM shadow con los indices nuevos");
}

void test_character_swaps() {
    std::printf("\n=== Intercambio de personaje (4 aliados + Jubei) ===\n");

    ps2::gif::Ps2TextureCache cache;
    cache.Reset();
    check(cache.ImageCount() == 0, "cache vacia al inicio");

    const std::vector<std::uint8_t> hud = ct32_bytes(kHudColors);
    cache.UploadImage(kHudBase, 1, ps2::gif::Ps2TextureCache::kPsmCt32, 2, 2,
                      hud.data(), hud.size());

    const std::array<CharacterSet, 5> characters = {
        make_character("Jubei", 1, ps2::gif::Ps2TextureCache::kPsmT8, 4, 4),
        make_character("Ekei", 2, ps2::gif::Ps2TextureCache::kPsmT8, 4, 4),
        make_character("Magoichi", 3, ps2::gif::Ps2TextureCache::kPsmT4, 4, 4),
        make_character("Kotaro", 4, ps2::gif::Ps2TextureCache::kPsmT8, 8, 2),
        make_character("Oyu", 5, ps2::gif::Ps2TextureCache::kPsmT8, 4, 4),
    };

    for (const CharacterSet &character : characters) {
        upload_character(cache, character);
        check(cache.ImageCount() == 4,
              std::string(character.name) + ": 4 registros (sin acumular)");
        verify_character(cache, character, kHudColors);
    }

    // Volver a Jubei tras Oyu, como al terminar el capitulo del aliado.
    upload_character(cache, characters[0]);
    verify_character(cache, characters[0], kHudColors);

    // Cambiar solo la CLUT (misma textura PSMT8 de Jubei) debe repintar los
    // indices con la paleta nueva, sin reutilizar la decodificacion anterior.
    const std::vector<std::uint8_t> ekei_palette =
        ct32_bytes(characters[1].palette);
    cache.UploadImage(kClutBase, 1, ps2::gif::Ps2TextureCache::kPsmCt32, 16, 1,
                      ekei_palette.data(), ekei_palette.size());
    const ps2::gif::Texture repainted = cache.Resolve(
        kIndexedBase, ps2::gif::Ps2TextureCache::kPsmT8, 4, 4, kClutBase,
        ps2::gif::Ps2TextureCache::kPsmCt32, 0, 0, 0, true);
    check(repainted.valid, "CLUT nueva: textura valida");
    check_pixel(repainted, 0, characters[1].palette[0],
                "CLUT nueva: pixel 0 usa la paleta de Ekei");
    check_pixel(repainted, 15, characters[1].palette[15],
                "CLUT nueva: pixel 15 usa la paleta de Ekei");

    // Restaurar la paleta de Jubei y comprobar que vuelve a su color.
    const std::vector<std::uint8_t> jubei_palette =
        ct32_bytes(characters[0].palette);
    cache.UploadImage(kClutBase, 1, ps2::gif::Ps2TextureCache::kPsmCt32, 16, 1,
                      jubei_palette.data(), jubei_palette.size());
    const ps2::gif::Texture restored = cache.Resolve(
        kIndexedBase, ps2::gif::Ps2TextureCache::kPsmT8, 4, 4, kClutBase,
        ps2::gif::Ps2TextureCache::kPsmCt32, 0, 0, 0, true);
    check_pixel(restored, 0, characters[0].palette[0],
                "CLUT restaurada: vuelve el color de Jubei");

    // QuickState: guardar a Oyu, cambiar a Jubei, cargar y verificar Oyu.
    upload_character(cache, characters[4]);
    std::vector<std::uint8_t> snapshot;
    cache.SaveState(snapshot);
    upload_character(cache, characters[0]);
    verify_character(cache, characters[0], kHudColors);
    check(cache.LoadState(snapshot.data(), snapshot.size()),
          "LoadState del set de Oyu");
    verify_character(cache, characters[4], kHudColors, false);

    cache.Reset();
    check(cache.ImageCount() == 0, "Reset limpia los registros");
    check(!cache.Resolve(kIndexedBase, ps2::gif::Ps2TextureCache::kPsmT8, 4, 4,
                         kClutBase, ps2::gif::Ps2TextureCache::kPsmCt32, 0, 0,
                         0, true)
               .valid,
          "Reset invalida las texturas");
}

}  // namespace

int main() {
    std::printf("=== Pruebas de texturas al cambiar de personaje ===\n");
    test_character_swaps();

    if (failures == 0) {
        std::printf("\nTodos los intercambios de personaje pasaron.\n");
        return 0;
    }
    std::printf("\n%d comprobacion(es) fallaron.\n", failures);
    return 1;
}
