#include "quick_state.hpp"

#include "overlay_dispatcher.hpp"
#include "ps2_audio_system.hpp"
#include "ps2_mem.hpp"

#if !defined(PS2_STATE_NO_GIF)
#include "gif_renderer.hpp"
#endif

#if !defined(PS2_STATE_NO_ZLIB)
#include <zlib.h>
#else
#include <cstring>
#endif

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <vector>

#if defined(__ANDROID__)
#include <android/log.h>
#define STATE_TAG "OnimushaState"
#define STATE_LOGI(...) \
    __android_log_print(ANDROID_LOG_INFO, STATE_TAG, __VA_ARGS__)
#define STATE_LOGE(...) \
    __android_log_print(ANDROID_LOG_ERROR, STATE_TAG, __VA_ARGS__)
#else
#define STATE_LOGI(...)                       \
    do {                                      \
        std::fprintf(stdout, "[State] ");     \
        std::fprintf(stdout, __VA_ARGS__);    \
        std::fputc('\n', stdout);             \
        std::fflush(stdout);                  \
    } while (0)
#define STATE_LOGE(...)                       \
    do {                                      \
        std::fprintf(stderr, "[State] ");     \
        std::fprintf(stderr, __VA_ARGS__);    \
        std::fputc('\n', stderr);             \
        std::fflush(stderr);                  \
    } while (0)
#endif

namespace fs = std::filesystem;

namespace ps2::state {
namespace {

constexpr std::array<char, 8> kMagic = {
    'Q', 'P', 'S', '2', 'Q', 'S', 'T', '1'};
constexpr std::uint32_t kVersion = 1;
constexpr std::uint32_t kFlagZlib = 1;
constexpr std::uint64_t kMaxSection = 128ull * 1024ull * 1024ull;

using Tag = std::array<char, 4>;

constexpr Tag kTagMeta = {'M', 'E', 'T', 'A'};
constexpr Tag kTagPs2Ram = {'P', 'S', '2', 'R'};
constexpr Tag kTagGameRam = {'G', 'A', 'M', 'R'};
constexpr Tag kTagGsVram = {'G', 'S', 'V', 'R'};
constexpr Tag kTagGfxCache = {'G', 'F', 'X', 'C'};
constexpr Tag kTagAudio = {'A', 'U', 'D', 'S'};
constexpr Tag kTagOverlay = {'O', 'V', 'D', 'S'};

std::string g_storage_dir;
std::uint8_t *g_game_ram = nullptr;
std::uint32_t g_game_size = 0;
std::uint8_t *g_gs_vram = nullptr;
std::uint32_t g_gs_size = 0;
OverlayDispatcher *g_dispatcher = nullptr;
std::string g_last_error;

struct Section {
    Tag tag{};
    const std::uint8_t *data = nullptr;
    std::uint64_t size = 0;
    std::vector<std::uint8_t> owned;

    const std::uint8_t *bytes() const {
        return owned.empty() ? data : owned.data();
    }
    std::uint64_t length() const {
        return owned.empty() ? size : owned.size();
    }
};

void set_error(const std::string &message) {
    g_last_error = message;
    STATE_LOGE("%s", g_last_error.c_str());
}

void write_u32(std::ostream &out, std::uint32_t value) {
    std::uint8_t bytes[4];
    std::memcpy(bytes, &value, sizeof(value));
    out.write(reinterpret_cast<const char *>(bytes), sizeof(bytes));
}

void write_u64(std::ostream &out, std::uint64_t value) {
    std::uint8_t bytes[8];
    std::memcpy(bytes, &value, sizeof(value));
    out.write(reinterpret_cast<const char *>(bytes), sizeof(bytes));
}

bool read_u32(std::istream &in, std::uint32_t &value) {
    std::uint8_t bytes[4];
    in.read(reinterpret_cast<char *>(bytes), sizeof(bytes));
    if (!in) return false;
    std::memcpy(&value, bytes, sizeof(value));
    return true;
}

bool read_u64(std::istream &in, std::uint64_t &value) {
    std::uint8_t bytes[8];
    in.read(reinterpret_cast<char *>(bytes), sizeof(bytes));
    if (!in) return false;
    std::memcpy(&value, bytes, sizeof(value));
    return true;
}

bool compress_section(const Section &section, std::vector<std::uint8_t> &out,
                      std::uint32_t &flags, std::uint32_t &crc) {
    const std::uint8_t *data = section.bytes();
    const std::uint64_t raw_size = section.length();
    flags = 0;
    crc = 0;
    if (raw_size == 0) {
        out.clear();
        return true;
    }

#if !defined(PS2_STATE_NO_ZLIB)
    crc = static_cast<std::uint32_t>(
        crc32(0L, data, static_cast<uInt>(raw_size)));

    const uLong source_size = static_cast<uLong>(raw_size);
    uLongf compressed_size = compressBound(source_size);
    out.resize(static_cast<std::size_t>(compressed_size));
    // Nivel 1: la RAM del juego esta mayormente a cero y comprime igual de
    // bien, pero evita congelar el hilo de render varios segundos.
    const int result = compress2(out.data(), &compressed_size, data,
                                 source_size, Z_BEST_SPEED);
    if (result == Z_OK) {
        out.resize(static_cast<std::size_t>(compressed_size));
        flags = kFlagZlib;
        return true;
    }
    STATE_LOGE("compress2 fallo: %d; se guarda sin comprimir", result);
#endif

    out.assign(data, data + raw_size);
    return true;
}

bool decompress_section(const std::vector<std::uint8_t> &input,
                        std::uint64_t raw_size, std::uint32_t flags,
                        std::uint32_t expected_crc,
                        std::vector<std::uint8_t> &out) {
    out.clear();
    if (raw_size > kMaxSection) return false;
    if (raw_size == 0) return input.empty();

#if !defined(PS2_STATE_NO_ZLIB)
    if ((flags & kFlagZlib) != 0) {
        out.resize(static_cast<std::size_t>(raw_size));
        uLongf destination_size = static_cast<uLongf>(raw_size);
        const int result = uncompress(out.data(), &destination_size,
                                      input.data(),
                                      static_cast<uLong>(input.size()));
        if (result != Z_OK || destination_size != raw_size) {
            STATE_LOGE("uncompress fallo: %d (%lu/%llu)", result,
                       static_cast<unsigned long>(destination_size),
                       static_cast<unsigned long long>(raw_size));
            out.clear();
            return false;
        }
    } else
#else
    (void)flags;
#endif
    {
        if (input.size() != raw_size) return false;
        out = input;
    }

#if !defined(PS2_STATE_NO_ZLIB)
    if (expected_crc != 0) {
        const std::uint32_t actual = static_cast<std::uint32_t>(
            crc32(0L, out.data(), static_cast<uInt>(out.size())));
        if (actual != expected_crc) {
            STATE_LOGE("CRC distinto: 0x%08X != 0x%08X", actual,
                       expected_crc);
            out.clear();
            return false;
        }
    }
#else
    (void)expected_crc;
#endif
    return true;
}

std::string timestamp_text() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                  tm.tm_min, tm.tm_sec);
    return buffer;
}

bool write_section(std::ostream &out, const Section &section) {
    std::vector<std::uint8_t> compressed;
    std::uint32_t flags = 0;
    std::uint32_t crc = 0;
    if (!compress_section(section, compressed, flags, crc)) return false;

    out.write(section.tag.data(), static_cast<std::streamsize>(section.tag.size()));
    write_u32(out, flags);
    write_u64(out, section.length());
    write_u64(out, static_cast<std::uint64_t>(compressed.size()));
    write_u32(out, crc);
    write_u32(out, 0);
    if (!compressed.empty()) {
        out.write(reinterpret_cast<const char *>(compressed.data()),
                  static_cast<std::streamsize>(compressed.size()));
    }
    return out.good();
}

bool restore_section(const Tag &tag, const std::vector<std::uint8_t> &raw) {
    if (tag == kTagMeta) {
        return true;
    }
    if (tag == kTagPs2Ram) {
        if (raw.size() != ps2::kPs2RamSize) return false;
        std::memcpy(ps2::ps2_ram, raw.data(), raw.size());
        return true;
    }
    if (tag == kTagGameRam) {
        if (g_game_ram == nullptr || raw.size() != g_game_size) return false;
        std::memcpy(g_game_ram, raw.data(), raw.size());
        return true;
    }
    if (tag == kTagGsVram) {
        if (g_gs_vram == nullptr || raw.size() != g_gs_size) return false;
        std::memcpy(g_gs_vram, raw.data(), raw.size());
        return true;
    }
    if (tag == kTagGfxCache) {
#if !defined(PS2_STATE_NO_GIF)
        return ps2::gif::LoadTextureCacheState(raw.data(), raw.size());
#else
        return true;
#endif
    }
    if (tag == kTagAudio) {
        return ps2::audio::System().LoadState(raw.data(), raw.size());
    }
    if (tag == kTagOverlay) {
        if (g_dispatcher == nullptr) return true;
        return g_dispatcher->LoadState(raw.data(), raw.size());
    }
    return true;  // seccion desconocida: compatibilidad futura
}

}  // namespace

void SetStorageDirectory(const std::string &directory) {
    g_storage_dir = directory;
}

void SetGameMemory(std::uint8_t *ram, std::uint32_t size) {
    g_game_ram = ram;
    g_game_size = ram != nullptr ? size : 0;
}

void SetGsVram(std::uint8_t *vram, std::uint32_t size) {
    g_gs_vram = vram;
    g_gs_size = vram != nullptr ? size : 0;
}

void SetOverlayDispatcher(OverlayDispatcher *dispatcher) {
    g_dispatcher = dispatcher;
}

std::string ResolveQuickStatePath(const char *filename) {
    if (filename == nullptr || filename[0] == '\0') return {};
    fs::path path(filename);
    if (path.is_absolute()) return path.string();
#if defined(_WIN32)
    if (filename[1] == ':') return path.string();
#endif
    if (g_storage_dir.empty()) return path.string();
    return (fs::path(g_storage_dir) / path).string();
}

const std::string &LastError() {
    return g_last_error;
}

bool SaveQuickState(const char *filename) {
    g_last_error.clear();
    const std::string output_path = ResolveQuickStatePath(filename);
    if (output_path.empty()) {
        set_error("SaveQuickState: nombre de archivo vacio");
        return false;
    }

    const fs::path output(output_path);
    std::error_code error;
    if (!output.parent_path().empty()) {
        fs::create_directories(output.parent_path(), error);
        if (error) {
            set_error("SaveQuickState: no se puede crear el directorio: " +
                      error.message());
            return false;
        }
    }

    std::vector<Section> sections;
    sections.reserve(7);

    const std::string meta = "Onimusha2 quick state v1\n" + timestamp_text();
    Section meta_section;
    meta_section.tag = kTagMeta;
    meta_section.owned.assign(meta.begin(), meta.end());
    sections.push_back(std::move(meta_section));

    Section ps2_section;
    ps2_section.tag = kTagPs2Ram;
    ps2_section.data = ps2::ps2_ram;
    ps2_section.size = ps2::kPs2RamSize;
    sections.push_back(ps2_section);

    if (g_game_ram != nullptr && g_game_size != 0) {
        Section section;
        section.tag = kTagGameRam;
        section.data = g_game_ram;
        section.size = g_game_size;
        sections.push_back(section);
    }
    if (g_gs_vram != nullptr && g_gs_size != 0) {
        Section section;
        section.tag = kTagGsVram;
        section.data = g_gs_vram;
        section.size = g_gs_size;
        sections.push_back(section);
    }
#if !defined(PS2_STATE_NO_GIF)
    if (ps2::gif::VramSize() != 0) {
        Section section;
        section.tag = kTagGfxCache;
        ps2::gif::SaveTextureCacheState(section.owned);
        sections.push_back(std::move(section));
    }
#endif
    {
        Section section;
        section.tag = kTagAudio;
        ps2::audio::System().SaveState(section.owned);
        sections.push_back(std::move(section));
    }
    if (g_dispatcher != nullptr) {
        Section section;
        section.tag = kTagOverlay;
        g_dispatcher->SaveState(section.owned);
        sections.push_back(std::move(section));
    }

    const std::string temporary_path = output_path + ".tmp";
    std::ofstream out(temporary_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        set_error("SaveQuickState: no se puede abrir " + temporary_path);
        return false;
    }

    out.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
    write_u32(out, kVersion);
    write_u32(out, static_cast<std::uint32_t>(sections.size()));
    for (const Section &section : sections) {
        if (!write_section(out, section)) {
            out.close();
            fs::remove(temporary_path, error);
            set_error("SaveQuickState: fallo escribiendo seccion");
            return false;
        }
    }
    out.flush();
    out.close();
    if (!out) {
        fs::remove(temporary_path, error);
        set_error("SaveQuickState: fallo al cerrar el archivo");
        return false;
    }

    // En Android/POSIX rename reemplaza el destino de forma atomica. En
    // Windows falla si el destino existe, asi que se reintenta tras borrarlo.
    fs::rename(temporary_path, output, error);
    if (error) {
        error.clear();
        fs::remove(output, error);
        error.clear();
        fs::rename(temporary_path, output, error);
    }
    if (error) {
        const std::string message = error.message();
        error.clear();
        fs::remove(temporary_path, error);
        set_error("SaveQuickState: no se puede renombrar: " + message);
        return false;
    }

    const auto size = fs::file_size(output, error);
    STATE_LOGI("guardado %s (%llu bytes, %zu secciones)", output_path.c_str(),
               static_cast<unsigned long long>(error ? 0 : size),
               sections.size());
    return true;
}

bool LoadQuickState(const char *filename) {
    g_last_error.clear();
    const std::string input_path = ResolveQuickStatePath(filename);
    if (input_path.empty()) {
        set_error("LoadQuickState: nombre de archivo vacio");
        return false;
    }

    std::ifstream in(input_path, std::ios::binary);
    if (!in.is_open()) {
        set_error("LoadQuickState: no se puede abrir " + input_path);
        return false;
    }

    std::array<char, 8> magic{};
    in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!in || magic != kMagic) {
        set_error("LoadQuickState: cabecera magica invalida");
        return false;
    }

    std::uint32_t version = 0;
    std::uint32_t count = 0;
    if (!read_u32(in, version) || !read_u32(in, count)) {
        set_error("LoadQuickState: cabecera truncada");
        return false;
    }
    if (version != kVersion || count > 64u) {
        set_error("LoadQuickState: version/secciones no soportadas");
        return false;
    }

    bool saw_ps2_ram = false;

    // Se valida y descomprime todo el archivo antes de tocar la memoria del
    // juego. Asi un archivo truncado o corrupto no deja el estado a medias.
    struct PendingSection {
        Tag tag{};
        std::vector<std::uint8_t> raw;
    };
    std::vector<PendingSection> pending;
    pending.reserve(count);

    for (std::uint32_t index = 0; index < count; ++index) {
        Tag tag{};
        in.read(tag.data(), static_cast<std::streamsize>(tag.size()));
        std::uint32_t flags = 0;
        std::uint32_t crc = 0;
        std::uint64_t raw_size = 0;
        std::uint64_t compressed_size = 0;
        std::uint32_t reserved = 0;
        if (!in || !read_u32(in, flags) || !read_u64(in, raw_size) ||
            !read_u64(in, compressed_size) || !read_u32(in, crc) ||
            !read_u32(in, reserved)) {
            set_error("LoadQuickState: seccion truncada");
            return false;
        }
        if (raw_size > kMaxSection || compressed_size > kMaxSection) {
            set_error("LoadQuickState: seccion demasiado grande");
            return false;
        }

        std::vector<std::uint8_t> compressed(
            static_cast<std::size_t>(compressed_size));
        if (compressed_size != 0) {
            in.read(reinterpret_cast<char *>(compressed.data()),
                    static_cast<std::streamsize>(compressed.size()));
        }
        if (!in) {
            set_error("LoadQuickState: datos de seccion truncados");
            return false;
        }

        PendingSection section;
        section.tag = tag;
        if (!decompress_section(compressed, raw_size, flags, crc,
                                section.raw)) {
            set_error("LoadQuickState: no se pudo descomprimir una seccion");
            return false;
        }

        if (tag == kTagPs2Ram) {
            saw_ps2_ram = true;
            if (section.raw.size() != ps2::kPs2RamSize) {
                set_error("LoadQuickState: PS2R con tamano incorrecto");
                return false;
            }
        }
        pending.push_back(std::move(section));
    }

    if (!saw_ps2_ram) {
        set_error("LoadQuickState: falta la seccion PS2R");
        return false;
    }

    for (const PendingSection &section : pending) {
        if (!restore_section(section.tag, section.raw)) {
            set_error("LoadQuickState: fallo restaurando una seccion");
            return false;
        }
    }

    STATE_LOGI("cargado %s (%u secciones)", input_path.c_str(), count);
    return true;
}

}  // namespace ps2::state
