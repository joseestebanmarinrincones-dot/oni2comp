// Prueba de SaveQuickState/LoadQuickState: ida y vuelta de ps2_ram, RAM del
// juego, gs_vram, estado de audio y registros de OverlayDispatcher.
//
// Compila sin zlib ni cache GIF para poder ejecutarse en PC con MinGW:
//   clang++ -std=c++17 -O2 -Isrc -DPS2_STATE_NO_ZLIB=1 -DPS2_STATE_NO_GIF=1 `
//     tests/quick_state_test.cpp src/quick_state.cpp src/ps2_mem.cpp `
//     src/ps2_audio_system.cpp src/overlay_dispatcher.cpp `
//     -o quick_state_test.exe
//   .\quick_state_test.exe

#include "overlay_dispatcher.hpp"
#include "ps2_audio_system.hpp"
#include "ps2_mem.hpp"
#include "quick_state.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void check(bool condition, const char *name) {
    std::printf("  %-58s %s\n", name, condition ? "PASS" : "FAIL");
    if (!condition) ++failures;
}

std::vector<std::uint8_t> make_pattern(std::size_t size, std::uint32_t seed) {
    std::vector<std::uint8_t> data(size);
    std::uint32_t state = seed;
    for (std::uint8_t &value : data) {
        state = state * 1664525u + 1013904223u;
        value = static_cast<std::uint8_t>(state >> 24);
    }
    return data;
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

struct SectionRecord {
    std::string tag;
    std::uint64_t payload_offset = 0;
    std::vector<std::uint8_t> payload;
};

std::vector<SectionRecord> read_state_file(const std::string &path) {
    std::vector<SectionRecord> sections;
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return sections;

    std::array<char, 8> magic{};
    in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!in || magic != std::array<char, 8>{'Q', 'P', 'S', '2',
                                            'Q', 'S', 'T', '1'}) {
        return sections;
    }

    std::uint32_t version = 0;
    std::uint32_t count = 0;
    if (!read_u32(in, version) || !read_u32(in, count)) return sections;
    if (version != 1 || count > 64u) return sections;

    for (std::uint32_t i = 0; i < count; ++i) {
        SectionRecord section;
        std::array<char, 4> tag{};
        in.read(tag.data(), static_cast<std::streamsize>(tag.size()));
        std::uint32_t flags = 0;
        std::uint32_t crc = 0;
        std::uint32_t reserved = 0;
        std::uint64_t raw_size = 0;
        std::uint64_t compressed_size = 0;
        if (!in || !read_u32(in, flags) || !read_u64(in, raw_size) ||
            !read_u64(in, compressed_size) || !read_u32(in, crc) ||
            !read_u32(in, reserved)) {
            return {};
        }
        section.tag.assign(tag.data(), tag.size());
        section.payload_offset =
            static_cast<std::uint64_t>(in.tellg());
        section.payload.resize(static_cast<std::size_t>(compressed_size));
        if (compressed_size != 0) {
            in.read(reinterpret_cast<char *>(section.payload.data()),
                    static_cast<std::streamsize>(section.payload.size()));
        }
        if (!in) return {};
        sections.push_back(std::move(section));
    }
    return sections;
}

const SectionRecord *find_section(const std::vector<SectionRecord> &sections,
                                  const char *tag) {
    for (const SectionRecord &section : sections) {
        if (section.tag == tag) return &section;
    }
    return nullptr;
}

bool same_section(const std::vector<SectionRecord> &a,
                  const std::vector<SectionRecord> &b, const char *tag) {
    const SectionRecord *left = find_section(a, tag);
    const SectionRecord *right = find_section(b, tag);
    if (left == nullptr || right == nullptr) return false;
    return left->payload == right->payload;
}

void test_quick_state() {
    std::printf("\n=== QuickState: guardar/cargar ===\n");

    const fs::path directory = fs::path("quick_state_test_tmp");
    std::error_code error;
    fs::remove_all(directory, error);
    fs::create_directories(directory, error);
    check(!error, "directorio temporal creado");

    ps2::state::SetStorageDirectory(directory.string());
    check(fs::path(ps2::state::ResolveQuickStatePath("quickstate.qs")) ==
              directory / "quickstate.qs",
          "ruta relativa resuelta contra el directorio interno");

    // Memoria del port.
    const std::vector<std::uint8_t> ps2_expected =
        make_pattern(ps2::kPs2RamSize, 0x1234u);
    std::memcpy(ps2::ps2_ram, ps2_expected.data(), ps2_expected.size());
    const std::vector<std::uint8_t> game_expected = make_pattern(64u * 1024u, 0x5678u);
    const std::vector<std::uint8_t> vram_expected =
        make_pattern(4u * 1024u * 1024u, 0x9ABCu);
    std::vector<std::uint8_t> game_buffer = game_expected;
    std::vector<std::uint8_t> vram_buffer = vram_expected;

    ps2::state::SetGameMemory(game_buffer.data(),
                              static_cast<std::uint32_t>(game_buffer.size()));
    ps2::state::SetGsVram(vram_buffer.data(),
                          static_cast<std::uint32_t>(vram_buffer.size()));

    // OverlayDispatcher.
    OverlayDispatcher dispatcher;
    ps2::state::SetOverlayDispatcher(&dispatcher);
    check(dispatcher.RegisterOverlay(0x02390000u, 0x8000u, [] {}),
          "overlay registrado");
    check(dispatcher.JumpToOverlay(0x02394000u), "salto dentro del overlay");
    check(dispatcher.TargetAddress() == 0x02394000u &&
              dispatcher.CurrentBase() == 0x02390000u,
          "target/base iniciales correctos");

    // Audio: RAM SPU2, registros, voz y stream.
    ps2::audio::Ps2AudioSystem &audio = ps2::audio::System();
    audio.Reset();
    const std::vector<std::uint8_t> spu_expected = make_pattern(64u, 0xDEADu);
    audio.WriteSpu2(0x2000u, spu_expected.data(), spu_expected.size());
    audio.SetTransferAddress(0x12345u);
    audio.SetVoicePitch(5, 0x0ABCu);
    audio.SetVoiceVolume(5, -111, 222);
    audio.SetMasterVolume(1000, -1000);
    audio.SetMuted(true);
    audio.KeyOn(5);
    const std::uint8_t adpcm[32] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    check(audio.QueueStream(adpcm, sizeof(adpcm)), "stream ADPCM encolado");
    audio.SetStreamPaused(true);
    check(audio.StreamQueuedBytes() == sizeof(adpcm),
          "bytes de stream disponibles");

    // Guardar.
    check(ps2::state::SaveQuickState("quickstate.qs"),
          "SaveQuickState devuelve true");
    check(ps2::state::LastError().empty(), "SaveQuickState sin error");
    check(fs::exists(directory / "quickstate.qs"), "archivo .qs creado");

    const std::vector<SectionRecord> first =
        read_state_file((directory / "quickstate.qs").string());
    check(!first.empty(), "contenedor legible");
    const SectionRecord *ps2_section = find_section(first, "PS2R");
    check(ps2_section != nullptr &&
              ps2_section->payload.size() == ps2::kPs2RamSize,
          "seccion PS2R con los 32 MB");
    check(find_section(first, "GAMR") != nullptr, "seccion GAMR presente");
    check(find_section(first, "GSVR") != nullptr, "seccion GSVR presente");
    check(find_section(first, "AUDS") != nullptr, "seccion AUDS presente");
    check(find_section(first, "OVDS") != nullptr, "seccion OVDS presente");

    const std::uintmax_t saved_size = fs::file_size(directory / "quickstate.qs", error);
    check(!error && saved_size >= ps2::kPs2RamSize, "tamano de archivo coherente");

    // Destruir el estado en memoria.
    std::memset(ps2::ps2_ram, 0xAA, ps2::kPs2RamSize);
    std::memset(game_buffer.data(), 0x55, game_buffer.size());
    std::memset(vram_buffer.data(), 0x77, vram_buffer.size());
    audio.Reset();
    dispatcher.Clear();
    check(dispatcher.TargetAddress() == 0u && dispatcher.OverlayCount() == 0u,
          "estado destruido antes de cargar");

    // Cargar.
    check(ps2::state::LoadQuickState("quickstate.qs"),
          "LoadQuickState devuelve true");
    check(ps2::state::LastError().empty(), "LoadQuickState sin error");

    check(std::memcmp(ps2::ps2_ram, ps2_expected.data(),
                      ps2::kPs2RamSize) == 0,
          "ps2_ram restaurada byte a byte");
    check(std::memcmp(game_buffer.data(), game_expected.data(),
                      game_expected.size()) == 0,
          "RAM del juego restaurada byte a byte");
    check(std::memcmp(vram_buffer.data(), vram_expected.data(),
                      vram_expected.size()) == 0,
          "gs_vram restaurada byte a byte");

    std::uint8_t spu_loaded[64]{};
    audio.ReadSpu2(0x2000u, spu_loaded, sizeof(spu_loaded));
    check(std::memcmp(spu_loaded, spu_expected.data(), sizeof(spu_loaded)) == 0,
          "SPU2 RAM restaurada");
    check(audio.TransferAddress() == 0x12345u, "direccion de transferencia");
    check(audio.GetParam(ps2::audio::sd::kParamMvoll) ==
              static_cast<std::uint16_t>(1000),
          "volumen master izquierdo");
    check(audio.GetParam(ps2::audio::sd::kParamMvolr) ==
              static_cast<std::uint16_t>(-1000),
          "volumen master derecho");
    check(audio.GetParam(static_cast<std::uint16_t>(
              ps2::audio::sd::Voice(0, 5) |
              ps2::audio::sd::kVParamPitch)) == 0x0ABCu,
          "pitch de voz");
    check(audio.GetParam(static_cast<std::uint16_t>(
              ps2::audio::sd::Voice(0, 5) |
              ps2::audio::sd::kVParamVoll)) ==
              static_cast<std::uint16_t>(-111),
          "volumen izquierdo de voz");
    check(audio.GetParam(static_cast<std::uint16_t>(
              ps2::audio::sd::Voice(0, 5) |
              ps2::audio::sd::kVParamVolr)) == 222,
          "volumen derecho de voz");
    check(audio.IsVoiceActive(5), "voz activa restaurada");
    check(audio.StreamQueuedBytes() == sizeof(adpcm),
          "stream restaurado");

    check(dispatcher.TargetAddress() == 0x02394000u,
          "target de overlay restaurado");
    check(dispatcher.CurrentBase() == 0x02390000u,
          "base de overlay restaurada");

    // Un segundo guardado debe producir las mismas secciones de estado.
    check(ps2::state::SaveQuickState("quickstate2.qs"),
          "segundo SaveQuickState");
    const std::vector<SectionRecord> second =
        read_state_file((directory / "quickstate2.qs").string());
    check(same_section(first, second, "PS2R"), "PS2R estable tras ida/vuelta");
    check(same_section(first, second, "GAMR"), "GAMR estable tras ida/vuelta");
    check(same_section(first, second, "GSVR"), "GSVR estable tras ida/vuelta");
    check(same_section(first, second, "AUDS"), "AUDS estable tras ida/vuelta");
    check(same_section(first, second, "OVDS"), "OVDS estable tras ida/vuelta");

    // Un archivo truncado a mitad de una seccion posterior no debe aplicar
    // las secciones anteriores (PS2R ya completa): el estado queda intacto.
    const SectionRecord *game_section = find_section(first, "GAMR");
    check(game_section != nullptr, "seccion GAMR localizada");
    if (game_section != nullptr) {
        const std::uint64_t cut = game_section->payload_offset + 10u;
        std::ifstream source((directory / "quickstate.qs").string(),
                             std::ios::binary);
        std::ofstream truncated((directory / "truncated.qs").string(),
                                std::ios::binary);
        std::vector<char> buffer(static_cast<std::size_t>(cut));
        source.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        truncated.write(buffer.data(),
                        static_cast<std::streamsize>(source.gcount()));
        truncated.close();
    }
    std::memset(ps2::ps2_ram, 0x11, ps2::kPs2RamSize);
    check(!ps2::state::LoadQuickState("truncated.qs"),
          "rechaza archivo truncado");
    check(ps2::ps2_ram[0] == 0x11 &&
              ps2::ps2_ram[ps2::kPs2RamSize - 1] == 0x11,
          "no aplica secciones parciales");

    // Errores esperados.
    {
        std::ofstream bad(directory / "bad.qs", std::ios::binary);
        bad.write("NOQPS2!!", 8);
    }
    check(!ps2::state::LoadQuickState("bad.qs"), "rechaza magia invalida");
    check(!ps2::state::LastError().empty(), "error descriptivo en magia invalida");
    check(!ps2::state::LoadQuickState("missing.qs"), "rechaza archivo inexistente");
    check(!ps2::state::LastError().empty(), "error descriptivo en archivo ausente");

    ps2::state::SetOverlayDispatcher(nullptr);
    ps2::state::SetGameMemory(nullptr, 0);
    ps2::state::SetGsVram(nullptr, 0);
    fs::remove_all(directory, error);
}

}  // namespace

int main() {
    std::printf("=== Pruebas de QuickState ===\n");
    test_quick_state();

    if (failures == 0) {
        std::printf("\nTodas las pruebas de QuickState pasaron.\n");
        return 0;
    }
    std::printf("\n%d prueba(s) de QuickState fallaron.\n", failures);
    return 1;
}
