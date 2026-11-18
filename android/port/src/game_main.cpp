#include "game_main.hpp"

#include "crash_handler.hpp"
#include "game_stubs.h"
#include "gs_render_pipeline.hpp"
#include "ps2_audio_hle.hpp"
#include "ps2_cd_stream.hpp"
#include "ps2_globals.h"
#include "ps2_gs_sync.hpp"
#include "ps2_hle.hpp"
#include "ps2_memory_card.hpp"
#include "ps2_sdk_stubs.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_set>
#include <vector>

#if !defined(GAME_MAIN_NO_SDL)
#include <SDL.h>
#endif

#if defined(__ANDROID__)
#include <android/log.h>
#define GAME_TAG "OnimushaGame"
#define GAME_LOGI(...) __android_log_print(ANDROID_LOG_INFO, GAME_TAG, __VA_ARGS__)
#define GAME_LOGW(...) __android_log_print(ANDROID_LOG_WARN, GAME_TAG, __VA_ARGS__)
#define GAME_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, GAME_TAG, __VA_ARGS__)
#else
#define GAME_LOGI(...) do { std::fprintf(stdout, __VA_ARGS__); std::fputc('\n', stdout); } while (0)
#define GAME_LOGW(...) do { std::fprintf(stderr, __VA_ARGS__); std::fputc('\n', stderr); } while (0)
#define GAME_LOGE(...) do { std::fprintf(stderr, __VA_ARGS__); std::fputc('\n', stderr); } while (0)
#endif

#if defined(__GNUC__) || defined(__clang__)
#define GAME_WEAK __attribute__((weak))
#else
#define GAME_WEAK
#endif

// main.cpp define la version fuerte (HUD del InputManager). En pruebas de
// PC queda vacia.
GAME_WEAK void port_draw_overlay() {}

namespace game {
namespace {

// ---------------------------------------------------------------------------
// RAM emulada
// ---------------------------------------------------------------------------

std::vector<std::uint8_t> owned_ram;
std::uint8_t *ram_pointer = nullptr;
std::uint32_t ram_bytes = 0;

SDL_Window *window_pointer = nullptr;
bool exit_requested = false;
bool shutdown_logged = false;
bool bad_access_logged = false;
std::uint8_t last_logged_state = 0xFF;

int requested_overlay_index = -1;
std::uint32_t requested_overlay_base = 0;
std::uint32_t requested_overlay_stamp = 0;
std::string requested_overlay_name;
bool requested_overlay_pending = false;
std::uint64_t gs_csr = 0;
std::uint64_t gs_display1 = 0;
std::uint64_t gs_display2 = 0;

std::unordered_set<std::uint32_t> logged_calls;
std::unordered_set<std::uint32_t> logged_returns;

// Blob estatico del ELF original en 0x002C4040: DMA END tag (qwc=14, id=7)
// seguido de 14 qwords de datos GIF. En la PS2 forma parte del segmento de
// datos del ejecutable; el port lo copia a la RAM emulada para que
// FUN_0014A340/sceDmaSend entregue el paquete real al hook.
constexpr std::uint8_t kStaticGifPacket[240] = {
    0x0E, 0x00, 0x00, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0D, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x49, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x80, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x47, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x7F, 0x02, 0x00, 0x00, 0xDF, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x7F, 0x02, 0x00, 0x00, 0xDF, 0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

std::uint8_t *ram_ptr(std::uint32_t address, std::uint32_t size) {
    if (!ram_pointer) return nullptr;
    if (address > ram_bytes) return nullptr;
    if (size > ram_bytes - address) return nullptr;
    return ram_pointer + address;
}

void log_bad_access(std::uint32_t address, std::uint32_t size, bool write) {
    if (bad_access_logged) return;
    bad_access_logged = true;
    GAME_LOGE("acceso %s fuera de RAM: 0x%08X (%u bytes), RAM=%u",
              write ? "de escritura" : "de lectura", address, size, ram_bytes);
}

// ---------------------------------------------------------------------------
// Funciones PS2 simples que si se traducen aqui
// ---------------------------------------------------------------------------

void flush_cache() {
    // FUN_001039E0 (FlushCache de PS2). En ARM64 no hay cache EE que limpiar.
}

void build_dma_tags(std::uint32_t address, int count) {
    // FUN_00146D30: lista de tags DMA.
    for (int i = 0; i < count; ++i) {
        WriteU32(address, (address + 4u) & 0x0FFFFFFFu);
        address += 4;
    }
    WriteU32(address, 0xF0000000u);
}

void setup_callback_slot(std::uint32_t index, std::uint32_t callback) {
    // FUN_00155130: tabla de callbacks de 0x3C bytes en 0x005D5510.
    index &= 0xFFu;
    const std::uint32_t offset = index * 0x3Cu;
    const std::uint32_t base = 0x005D5510u + offset;
    const std::uint32_t previous = ReadU32(0x005D5508u + offset);
    const std::uint32_t field = ReadU16(0x005D5504u + offset);

    WriteU32(base + 0x00, callback);
    WriteU32(base + 0x04, previous);
    WriteU32(base + 0x08, field);
    WriteU32(base + 0x10, index + 0x10u);
    WriteU32(base + 0x0C, 0x001850000u);   // gp de la PS2
    WriteU8(base + 0x06, 2);
}

void initialize_loader_queue() {
    // FUN_00124100 / FUN_00124260: cola de 8 entradas de 0x18 bytes.
    if (std::uint8_t *queue = ram_ptr(0x001847790u, 0xC0)) {
        std::memset(queue, 0, 0xC0);
    }
    if (std::uint8_t *context = ram_ptr(kLoaderContext, 0x88)) {
        std::memset(context, 0, 0x88);   // FUN_001172B8(0x18479F0, 0x1847A78)
    }
    WriteU32(kQueueTail, 0x001847790u);
    WriteU32(kQueueHead, 0x001847790u);
    WriteU32(kQueueFlag, 0);
    WriteU32(kLoaderContext, 0);
    WriteU8(kLoadState, 0);
}

bool enqueue_load_request(std::uint8_t type, std::uint16_t file_index,
                          std::uint32_t offset_sectors, std::uint8_t flags) {
    // FUN_00124430. La tabla ISO completa esta en 0x002B4660; de momento se
    // embebe la entrada de arranque 0x301 que usa FUN_00144680.
    const std::uint32_t tail = ReadU32(kQueueTail);
    if (tail < 0x001847790u || tail + 0x18u > 0x001847850u) {
        GAME_LOGE("cola de carga llena (tail=0x%08X)", tail);
        return false;
    }

    std::uint32_t lba = 0;
    std::uint32_t size = 0;
    if (file_index == 0x0301u) {
        lba = 0x001596C7u;
        size = 0x0011B800u;
    } else {
        GAME_LOGE("tabla ISO no embebida: indice 0x%04X", file_index);
        return false;
    }

    if (offset_sectors != 0) flags |= 1u;
    const std::uint64_t skip = static_cast<std::uint64_t>(offset_sectors) * 0x800u;
    if (skip > size) {
        GAME_LOGE("offset 0x%X mayor que el archivo 0x%X", offset_sectors, size);
        return false;
    }

    WriteU8(tail + 0x00, type);
    WriteU8(tail + 0x01, flags);
    WriteU16(tail + 0x02, file_index);
    WriteU32(tail + 0x04, lba + offset_sectors);
    WriteU32(tail + 0x14, static_cast<std::uint32_t>(size - skip));
    WriteU32(kQueueTail, tail + 0x18u);
    WriteU32(kQueueFlag, ReadU32(kQueueFlag) | 1u);
    GAME_LOGI("cola: type=%u file=0x%04X lba=0x%08X size=0x%X",
              type, file_index, lba + offset_sectors, static_cast<std::uint32_t>(size - skip));
    return true;
}

void pop_load_request() {
    // FUN_001242A0: consume la primera entrada y actualiza el estado.
    if ((ReadU32(kQueueFlag) & 1u) == 0) return;
    std::uint8_t *base = ram_ptr(0x001847790u, 0xC0);
    if (!base) return;

    for (int i = 0; i < 7; ++i) {
        std::memcpy(base + i * 0x18, base + (i + 1) * 0x18, 0x18);
    }
    base[7 * 0x18] = 0;

    const std::uint32_t tail = ReadU32(kQueueTail);
    if (tail >= 0x001847790u + 0x18u) WriteU32(kQueueTail, tail - 0x18u);
    WriteU32(kLoaderContext, 0);

    const std::uint8_t next = ReadU8(kQueueHead);
    WriteU8(kLoadState, next);
    if (next == 0) WriteU32(kQueueFlag, ReadU32(kQueueFlag) & ~1u);
}

void log_running_state(std::uint32_t estado) {
#if !defined(GAME_MAIN_NO_SDL)
    SDL_Log("Ejecutando estado: 0x%08X", estado);
#else
    GAME_LOGI("Ejecutando estado: 0x%08X", estado);
#endif
}

void load_state_idle() {
    // FUN_00124DC0 (estado 0): si hay peticion, copia su tipo al estado y
    // ejecuta el handler de la tabla.
    const std::uint32_t head = ReadU32(kQueueHead);
    if (!head) return;
    const std::uint8_t type = ReadU8(head);
    if (type == 0) return;
    if (type >= 24) {
        GAME_LOGE("peticion con estado invalido %u", type);
        return;
    }
    const std::uint32_t handler = kLoadStateTable[type];
    if (!handler) {
        GAME_LOGE("peticion con handler nulo (estado %u)", type);
        return;
    }
    WriteU8(kLoadState, type);
    last_logged_state = type;
    const std::uint32_t estado = handler;
    log_running_state(estado);
    GAME_LOGI("cargador: peticion type=%u -> estado %u, handler 0x%08X",
              type, type, handler);
    CallPS2(handler, kLoaderContext);
}

void dispatch_load_state() {
    // FUN_00124D90: jr a la tabla 0x002BA0E0.
    const std::uint8_t state = ReadU8(kLoadState);
    if (state >= 24) {
        GAME_LOGE("estado de carga fuera de tabla: %u", state);
        WriteU8(kLoadState, 0);
        return;
    }
    const std::uint32_t handler = kLoadStateTable[state];
    if (!handler) {
        GAME_LOGE("estado de carga %u sin handler (nulo)", state);
        WriteU8(kLoadState, 0);
        return;
    }
    if (state != last_logged_state) {
        last_logged_state = state;
        const std::uint32_t estado = handler;
        log_running_state(estado);
        GAME_LOGI("cargador: estado %u -> 0x%08X", state, handler);
    }
    if (state == 0) {
        load_state_idle();
        return;
    }
    CallPS2(handler, kLoaderContext);
}

void port_vsync() {
    // Sustituto de SONY_libgraph_00100b28 / sceGsSyncPath.
    // En Android no hay DMA/VIF/VU1 real: completamos el path y marcamos
    // VSINT + iGpffff9130 en ps2_ram para que el bucle no se bloquee.
    ps2::gs::CompletePath();

#if !defined(GAME_MAIN_NO_SDL)
    // Fondo 2D -> depth map del juego -> modelos 3D, en ese orden.
    ps2::render::ExecuteFrame();
    // HUD de entrada (main.cpp define la version fuerte).
    port_draw_overlay();
    if (window_pointer) SDL_GL_SwapWindow(window_pointer);
#else
    (void)window_pointer;
#endif

    gs_csr |= (1ull << 13);          // VSINT (modelo del port)
    WriteU32(0x001849130u, 1);       // iGpffff9130 para game::Ram()
}

}  // namespace

const std::uint32_t kLoadStateTable[24] = {
    0x00124DC0u,   // 0: idle / nueva peticion
    0x00125740u,   // 1: script / carga desde CD
    0x00125740u,   // 2
    0x00124E10u,   // 3: carga a destino
    0x00124E10u,   // 4
    0x00125050u,   // 5
    0x00125050u,   // 6
    0x00125130u,   // 7
    0x001251E0u,   // 8
    0x002915A0u,   // 9: arranque de escena/juego
    0x00125290u,   // 10: streaming
    0x00125440u,   // 11
    0x00125540u,   // 12
    0x00000000u,   // 13: nulo en el binario
    0x00000000u,   // 14: nulo
    0x00000000u,   // 15: nulo
    0x00125C30u,   // 16
    0x00125C50u,   // 17
    0x00125C60u,   // 18
    0x00125D60u,   // 19
    0x001264A0u,   // 20
    0x00126760u,   // 21
    0x001267C0u,   // 22
    0x00126880u,   // 23
};

std::uint8_t *Ram() { return ram_pointer; }
std::uint32_t RamSize() { return ram_bytes; }

bool AttachRam(std::uint8_t *ram, std::uint32_t size) {
    if (!ram) return false;
    if (size < 0x001847A78u) return false;
    owned_ram.clear();
    owned_ram.shrink_to_fit();
    ram_pointer = ram;
    ram_bytes = size;
    return true;
}

std::uint8_t ReadU8(std::uint32_t address) {
    if (const std::uint8_t *pointer = ram_ptr(address, 1)) return *pointer;
    log_bad_access(address, 1, false);
    return 0;
}

std::uint16_t ReadU16(std::uint32_t address) {
    if (const std::uint8_t *pointer = ram_ptr(address, 2)) {
        std::uint16_t value = 0;
        std::memcpy(&value, pointer, sizeof(value));
        return value;
    }
    log_bad_access(address, 2, false);
    return 0;
}

std::uint32_t ReadU32(std::uint32_t address) {
    if (const std::uint8_t *pointer = ram_ptr(address, 4)) {
        std::uint32_t value = 0;
        std::memcpy(&value, pointer, sizeof(value));
        return value;
    }
    log_bad_access(address, 4, false);
    return 0;
}

void WriteU8(std::uint32_t address, std::uint8_t value) {
    if (std::uint8_t *pointer = ram_ptr(address, 1)) {
        *pointer = value;
        return;
    }
    log_bad_access(address, 1, true);
}

void WriteU16(std::uint32_t address, std::uint16_t value) {
    if (std::uint8_t *pointer = ram_ptr(address, 2)) {
        std::memcpy(pointer, &value, sizeof(value));
        return;
    }
    log_bad_access(address, 2, true);
}

void WriteU32(std::uint32_t address, std::uint32_t value) {
    if (std::uint8_t *pointer = ram_ptr(address, 4)) {
        std::memcpy(pointer, &value, sizeof(value));
        return;
    }
    log_bad_access(address, 4, true);
}

bool EnqueueLoadRequest(std::uint8_t type, std::uint16_t file_index,
                        std::uint32_t offset_sectors, std::uint8_t flags) {
    return enqueue_load_request(type, file_index, offset_sectors, flags);
}

void PopLoadRequest() { pop_load_request(); }

void RequestOverlayJump(int index, std::uint32_t base, std::uint32_t stamp,
                        const std::string &name) {
    requested_overlay_index = index;
    requested_overlay_base = base;
    requested_overlay_stamp = stamp;
    requested_overlay_name = name;
    requested_overlay_pending = true;
    GAME_LOGI("overlay jump solicitado: #%d %s base=0x%08X stamp=0x%08X "
              "(loader state=%u)",
              index, name.c_str(), base, stamp, ReadU8(kLoadState));
}

bool ConsumeOverlayJumpRequest(int &index, std::uint32_t &base,
                               std::uint32_t &stamp, std::string &name) {
    if (!requested_overlay_pending) return false;
    index = requested_overlay_index;
    base = requested_overlay_base;
    stamp = requested_overlay_stamp;
    name = requested_overlay_name;
    requested_overlay_pending = false;
    return true;
}

std::uint8_t CurrentLoadState() { return ReadU8(kLoadState); }

std::uint32_t LoadStateHandler(std::uint8_t state) {
    return state < 24 ? kLoadStateTable[state] : 0;
}

void RequestExit() { exit_requested = true; }
bool ExitRequested() { return exit_requested; }

void CallPS2(std::uint32_t address, std::uint32_t a0, std::uint32_t a1,
             std::uint32_t a2, std::uint32_t a3) {
    // Traza del orden de llamadas de main. Los stubs no nativos se registran
    // solos al ejecutarse; aqui se registran los nativos/SDK.
    stubs::LogCall(address);

    // libsd/sceSdRemote: init y TransToIOP se aplican al Ps2AudioSystem;
    // el resto del bloque sigue como stub para no bloquear sin IOP.
    // El bloque libsd de SLUS_203.93 es 0x0025C650..0x0025C94F.
    // 0x0025C610..0x0025C64F son helpers del juego y 0x0025C950 ya es
    // codigo de juego.
    if (ps2::audio::IsStubbedAddress(address)) {
        ps2::audio::Invoke(address, a0, a1, a2, a3, nullptr);
        return;
    }

    // Memory Card: libmc es asincrono, pero el trabajo se hace al instante y
    // sceMcSync devuelve el resultado pendiente.
    if (ps2::mc::IsMemoryCardAddress(address)) {
        ps2::mc::Invoke(address, a0, a1, a2, a3, nullptr);
        return;
    }

    switch (address) {
    case 0x001018A0u:   // sceDmaSend(channel, source)
        ps2::hle::DmaSend(a0, a1);
        return;
    case 0x00101DF0u:   // sceDmaSync(channel, mode, timeout)
        ps2::hle::DmaSync(a0, a1, a2);
        return;
    case 0x0010DE10u:   // sceCdStStart(lba, mode)
        ps2::cd::sceCdStStart(a0, a1);
        return;
    case 0x0010DE40u:   // sceCdStStop()
        ps2::cd::sceCdStStop();
        return;
    case 0x0010DE70u:   // sceCdStSeek(lba)
        ps2::cd::sceCdStSeek(a0);
        return;
    case 0x0010DEA8u: { // sceCdStRead(sectors, dest, mode, &err)
        int err = 0;
        ps2::cd::sceCdStRead(a0, a1, a2, &err);
        if (a3 != 0) PS2_MEM_WRITE<std::int32_t>(a3, err);
        return;
    }
    case 0x001039E0u:   // FlushCache
        flush_cache();
        return;
    case 0x00146D30u:   // lista de tags DMA
        build_dma_tags(a0, static_cast<int>(a1));
        return;
    case 0x00155130u:   // tabla de callbacks
        setup_callback_slot(a0, a1);
        return;
    case 0x00124100u:   // inicializacion de la cola del cargador
    case 0x00124260u:
        initialize_loader_queue();
        return;
    case 0x00160A30u:   // wrapper de sceMcInit (FUN_00149BF0)
        if (ps2::mc::System().IsInitialized()) {
            ps2::mc::System().Init(0);
        }
        return;
    case 0x00124430u:   // encolar peticion
        enqueue_load_request(static_cast<std::uint8_t>(a0),
                             static_cast<std::uint16_t>(a1), a2,
                             static_cast<std::uint8_t>(a3));
        return;
    case 0x001242A0u:   // consumir peticion
        pop_load_request();
        return;
    case 0x00124D90u:   // despachador de estados
        dispatch_load_state();
        return;
    case 0x00124DC0u:   // estado 0
        load_state_idle();
        return;
    case 0x0014A2E0u: {
        // FUN_0014A2E0: tabla de canales DMA (FUN_00101550 en 0x002AE6C0).
        // 0x18263E8 queda apuntando a GIF (0x1000A000), que es lo que usa
        // FUN_0014A340 cada frame.
        CallRecompiled(0x001002F0u);
        CallRecompiled(0x00101578u, 1);
        CallRecompiled(0x00102CD0u);
        static constexpr std::uint32_t kDmaChannels[10] = {
            0x10008000u, 0x10009000u, 0x1000A000u, 0x1000B000u, 0x1000B400u,
            0x1000C000u, 0x1000C400u, 0x1000C800u, 0x1000D000u, 0x1000D400u,
        };
        for (int i = 0; i < 10; ++i) {
            WriteU32(0x0018263E0u + static_cast<std::uint32_t>(i) * 4u,
                     kDmaChannels[i]);
        }
        return;
    }
    case 0x0014A340u: {
        // FUN_0014A340: sceDmaSend del paquete de 0x2C4040.
        const std::uint32_t channel = ReadU32(0x0018263E8u);
        ps2::hle::DmaSync(channel, 0, 0);
        ps2::hle::DmaSend(channel, a0);
        return;
    }
    case 0x0014A390u:   // callback de VBlank; el puerto lo emula
        return;
    default:
        break;
    }

    // Funciones SDK identificadas en Ghidra (libgraph, libdma, libcdvd,
    // libmc, libsd, libpad2): no se ejecuta su codigo descompilado. El
    // despachador aplica la HLE nativa o el stub de la libreria.
    if (ps2::sdk::IsNativeAddress(address)) {
        ps2::sdk::Call(address, a0, a1, a2, a3, nullptr);
        return;
    }

    CallRecompiled(address, a0, a1, a2, a3);
}

GAME_WEAK void CallRecompiled(std::uint32_t address, std::uint32_t a0,
                              std::uint32_t a1, std::uint32_t a2,
                              std::uint32_t a3) {
    ps2::crash::SetLastFunction(address);
    if (logged_calls.insert(address).second) {
        GAME_LOGI("PS2 0x%08X(a0=0x%08X, a1=0x%08X, a2=0x%08X, a3=0x%08X) sin traducir",
                  address, a0, a1, a2, a3);
    }
}

std::uint32_t CallPS2Ret(std::uint32_t address, std::uint32_t a0,
                         std::uint32_t a1, std::uint32_t a2,
                         std::uint32_t a3) {
    stubs::LogCall(address);

    if (ps2::audio::IsStubbedAddress(address)) {
        std::uint32_t result = 0;
        ps2::audio::Invoke(address, a0, a1, a2, a3, &result);
        return result;
    }
    if (ps2::mc::IsMemoryCardAddress(address)) {
        std::uint32_t result = 0;
        ps2::mc::Invoke(address, a0, a1, a2, a3, &result);
        return result;
    }
    if (address == 0x001018A0u) {
        ps2::hle::DmaSend(a0, a1);
        return 0;
    }
    if (address == 0x00101DF0u) {
        return static_cast<std::uint32_t>(
            ps2::hle::DmaSync(a0, a1, a2));
    }
    if (address == 0x0010DE10u) return ps2::cd::sceCdStStart(a0, a1);
    if (address == 0x0010DE40u) return ps2::cd::sceCdStStop();
    if (address == 0x0010DE70u) return ps2::cd::sceCdStSeek(a0);
    if (address == 0x0010DEA8u) {
        int err = 0;
        const int result = ps2::cd::sceCdStRead(a0, a1, a2, &err);
        if (a3 != 0) PS2_MEM_WRITE<std::int32_t>(a3, err);
        return static_cast<std::uint32_t>(result);
    }
    if (address == 0x00124250u) return 1;   // FUN_00124250: return 1
    if (address == 0x001039E0u) {
        flush_cache();
        return 0;
    }

    if (ps2::sdk::IsNativeAddress(address)) {
        std::uint32_t result = 0;
        ps2::sdk::CallRet(address, a0, a1, a2, a3, &result);
        return result;
    }

    return CallRecompiledRet(address, a0, a1, a2, a3);
}

GAME_WEAK std::uint32_t CallRecompiledRet(std::uint32_t address, std::uint32_t a0,
                                          std::uint32_t a1, std::uint32_t a2,
                                          std::uint32_t a3) {
    ps2::crash::SetLastFunction(address);
    if (logged_returns.insert(address).second) {
        GAME_LOGI("PS2 ret 0x%08X(a0=0x%08X, a1=0x%08X, a2=0x%08X, a3=0x%08X) = 0",
                  address, a0, a1, a2, a3);
    }
    return 0;
}

bool Initialize(SDL_Window *window, const std::string &overlay_dir) {
    if (!ram_pointer) {
        owned_ram.assign(kTotalRamSize, 0);
        ram_pointer = owned_ram.data();
        ram_bytes = kTotalRamSize;
    } else {
        std::memset(ram_pointer, 0, ram_bytes);
    }

    window_pointer = window;
    exit_requested = false;
    shutdown_logged = false;
    bad_access_logged = false;
    last_logged_state = 0xFF;
    gs_csr = 0;
    gs_display1 = 0;
    gs_display2 = 0;
    logged_calls.clear();
    logged_returns.clear();

    // Enlaza sceGsSyncV (libgraph, 0x00100B28) con SDL_GL_SwapWindow a traves
    // de la tabla SDK. El caso ya no esta en el switch de CallPS2().
    ps2::sdk::SetHook([](const ps2::sdk::Function &function, std::uint32_t,
                         std::uint32_t, std::uint32_t, std::uint32_t,
                         std::uint32_t *result) -> bool {
        if (function.address == 0x00100B28u) {
            port_vsync();
            if (result != nullptr) *result = 0;
            return true;
        }
        return false;
    });

    GAME_LOGI("init: RAM %u MB + ventana alta %u MB, overlays=%s",
              kMainRamSize >> 20, kHighWindowSize >> 20, overlay_dir.c_str());

    // Carga del blob estatico del ejecutable (segmento de datos del ELF).
    if (std::uint8_t *packet = ram_ptr(0x002C4040u, sizeof(kStaticGifPacket))) {
        std::memcpy(packet, kStaticGifPacket, sizeof(kStaticGifPacket));
    }
    // El parser GIF lee la cadena DMA desde ps2_ram.
    if (0x002C4040u + sizeof(kStaticGifPacket) <= ps2::kPs2RamSize) {
        std::memcpy(ps2::ps2_ram + 0x002C4040u, kStaticGifPacket,
                    sizeof(kStaticGifPacket));
    }

    // ------------------------------------------------------------------
    // FUN_00149BF0, en el mismo orden.
    // ------------------------------------------------------------------
    CallPS2(0x00123F50u);
    CallPS2(0x00123FF0u, 0x002DCC10u);
    CallPS2(0x00123FF0u, 0x002DCC20u);
    CallPS2(0x00123FF0u, 0x002DCC30u);
    for (int i = 0; i < 2; ++i) CallPS2(0x00123FF0u, 0x002DCC40u);
    CallPS2(0x00123FF0u, 0x002DCC50u);
    CallPS2(0x00123FF0u, 0x002DCC60u);
    CallPS2(0x00123FF0u, 0x002DCC70u);
    CallPS2(0x00123FF0u, 0x002DCC80u);
    CallPS2(0x00123FF0u, 0x002DCC90u);
    CallPS2(0x00123FF0u, 0x002DCCA0u);
    CallPS2(0x00122A50u);
    CallPS2(0x00124060u);
    CallPS2(0x0014A2E0u);
    CallPS2(0x00100150u, 0, 1, 2, 1);

    WriteU8(0x001849414u, 1);       // uGpffff9414
    WriteU8(0x001849460u, 0);       // uGpffff9460
    WriteU16(0x0018493FCu, 0);      // sGpffff93fc
    WriteU16(0x0018493F8u, 0);      // sGpffff93f8
    CallPS2(0x00149E50u);

    WriteU32(0x0018493E0u, 0x3F800000u);
    WriteU8(0x0018493F0u, 0);
    WriteU8(0x0018493ECu, 0);
    WriteU8(0x0018493E8u, 0);
    WriteU8(0x0018493E4u, 0);
    CallPS2(0x00149F20u, 1);
    CallPS2(0x00100E40u, 0x0014A390u);

    WriteU8(0x00172A378u, 0);
    WriteU8(0x00172A379u, 0);
    WriteU8(0x00172A37Au, 0);
    WriteU8(0x00172A37Bu, 0);
    WriteU8(0x00172A37Cu, 0);
    WriteU8(0x0018493D4u, 0);
    WriteU8(0x00172A37Du, 0);
    WriteU8(0x0018493DCu, 0);

    CallPS2(0x0017B0E0u);
    CallPS2(0x00123FF0u, 0x002DCCB0u);
    CallPS2(0x0025C650u);
    CallPS2(0x00124100u);       // cola del cargador
    CallPS2(0x001700D0u);
    CallPS2(0x00160A30u);
    CallPS2(0x0013CE50u);
    CallPS2(0x001A7DD0u);
    CallPS2(0x00128240u);
    WriteU32(0x00184939Cu, 0);
    CallPS2(0x0015DE50u);
    CallPS2(0x00161BE0u);
    CallPS2(0x00183A60u);
    CallPS2(0x0014E170u);
    CallPS2(0x0019AE30u);
    CallPS2(0x00135730u);
    CallPS2(0x00197D00u);
    CallPS2(0x00154F10u);
    CallPS2(0x0011AB50u);
    CallPS2(0x0019DB30u);
    CallPS2(0x00120B90u);

    flush_cache();

    for (std::uint32_t i = 0; i < 0x1000u; ++i) {
        WriteU32(0x00898C50u + i * 4u, CallPS2Ret(0x00117520u, i));
    }

    WriteU32(0x0018492D0u, 0xB416A1E6u);
    WriteU32(0x0018492CCu, 0xB5DCA7F6u);
    WriteU32(0x0018492C8u, 0xC738AFEAu);

    // ------------------------------------------------------------------
    // Antes del bucle de FUN_001496D0:
    //   0x17E0350 = 0; FUN_00155130(0, 0x144630);
    // La hebra 0x144630 ejecuta FUN_00144680 y encola la peticion 0x301.
    // ------------------------------------------------------------------
    WriteU32(0x0017E0350u, 0);
    CallPS2(0x00155130u, 0, 0x00144630u);
    if (!EnqueueLoadRequest(1, 0x0301u, 0, 0)) {
        GAME_LOGE("no se pudo encolar la peticion inicial 0x301");
    }

    GAME_LOGI("init completada: estado=%u, cola tail=0x%08X head=0x%08X",
              CurrentLoadState(), ReadU32(kQueueTail), ReadU32(kQueueHead));
    return true;
}

bool Frame(SDL_Window *window) {
    if (!ram_pointer) return false;
    window_pointer = window;

    // 001496FC: REG_GS_CSR |= 8; iGpffff9130 = 0.
    gs_csr |= 8ull;
    WriteU32(0x001849130u, 0);

    // 00149710: si cGpffff93d4, desplaza seis flags 7 bits.
    if (ReadU8(0x0018493D4u) != 0) {
        WriteU8(0x0018493D4u, 0);
        const std::uint32_t flags[6] = {
            0x00172A378u, 0x00172A379u, 0x00172A37Au,
            0x00172A37Bu, 0x00172A37Cu, 0x00172A37Du,
        };
        for (std::uint32_t address : flags) {
            // lbu + sra 7: el byte se zero-extiende, asi que el resultado
            // es 0 o 1 (no 0xFF).
            const std::uint8_t value = ReadU8(address);
            WriteU8(address, static_cast<std::uint8_t>(value >> 7));
        }
    }

    // 00149798: FlushCache.
    flush_cache();

    // 001497A0: si no se esta cargando, prepara display y draw envs.
    if (ReadU8(0x0018493DCu) == 0) {
        const std::uint8_t buffer = ReadU8(0x001849460u);
        CallPS2(0x001014B8u, 0x001826410u, buffer);

        const std::int32_t y = static_cast<std::int16_t>(ReadU16(0x0018493F8u));
        const std::int32_t x = static_cast<std::int16_t>(ReadU16(0x0018493FCu));
        const std::uint64_t display =
            (static_cast<std::uint64_t>(static_cast<std::uint32_t>(y + 0x32)) << 12) |
            static_cast<std::uint32_t>(x * 4 + 0x27C) |
            0x1BF9FF02000000ull;
        gs_display1 = display;
        gs_display2 = display;
        if (ReadU16(kFrameCounter) == 0) {
            GAME_LOGI("GS_DISPLAY1=0x%016llX GS_DISPLAY2=0x%016llX (x=%d y=%d buffer=%u)",
                      static_cast<unsigned long long>(gs_display1),
                      static_cast<unsigned long long>(gs_display2), x, y, buffer);
        }

        CallPS2(0x00100EE0u, buffer == 0 ? 0x0018265E0u : 0x001826470u,
                0x800, 0x800, ReadU8(0x001849414u));
        CallPS2(0x00101148u, buffer == 0 ? 0x001826660u : 0x0018264F0u,
                0x800, 0x800, ReadU8(0x001849414u));
    }

    // 001498B8: subsistemas de inicio de frame.
    CallPS2(0x0014A340u, 0x002C4040u);
    CallPS2(0x001701B0u);
    CallPS2(0x00146D30u, 0x0018223E0u, 0x0FFF);
    CallPS2(0x00146D30u, 0x0018223A0u, 0x000F);
    CallPS2(0x00146D30u, 0x001822360u, 0x000F);
    WriteU32(0x00184945Cu, 0x0017E2360u);
    WriteU32(0x001849458u, 0x0017E0360u);

    if (ReadU8(0x0008ACE60u) != 0) CallPS2(0x00188730u);

    CallPS2(0x00150EF0u);
    CallPS2(0x0019AE20u);
    CallPS2(0x0017C2B0u);
    CallPS2(0x00155000u);
    CallPS2(0x0013CCB0u);
    CallPS2(0x0017D5D0u);

    // 00149958: maquina de estados del cargador (tabla 0x002BA0E0).
    dispatch_load_state();

    CallPS2(0x0019AE90u);
    CallPS2(0x00150F80u);
    CallPS2(0x0015FC20u, 0x40);
    CallPS2(0x001858E0u);
    CallPS2(0x00154110u);

    if ((ReadU8(0x00172A37Bu) & 1u) != 0) CallPS2(0x00146D60u, 0x0018223E0u, 1);
    if ((ReadU8(0x00172A37Du) & 1u) != 0) CallPS2(0x001519C0u);
    if ((ReadU8(0x00172A37Cu) & 1u) != 0) CallPS2(0x0015BDB0u);

    CallPS2(0x00161BB0u);

    // 001499F8: sceGsSyncV -> SDL_GL_SwapWindow.
    port_vsync();

    CallPS2(0x0015FC20u, 0x80);

    const std::uint8_t mode = ReadU8(0x001849418u);
    if (mode == 3) {
        CallPS2(0x00197E50u, 1);
    } else if (mode == 4) {
        CallPS2(0x00197E50u, 2);
    }

    if (ReadU8(0x0018493DCu) == 0) {
        if ((ReadU8(0x00172A37Bu) & 1u) == 0) {
            CallPS2(0x00146D60u, 0x0018223C0u, 1);
        } else {
            CallPS2(0x00146D60u, 0x0018223A0u, 1);
        }
    }

    if ((ReadU8(0x00172A37Cu) & 1u) != 0) CallPS2(0x0015BDB0u);
    if (ReadU8(0x0018493DCu) == 0) CallPS2(0x0015DBF0u);

    CallPS2(0x00185D90u);
    WriteU16(kFrameCounter, static_cast<std::uint16_t>(ReadU16(kFrameCounter) + 1));

    const std::uint16_t frame = ReadU16(kFrameCounter);
    if (frame != 0 && (frame % 300u) == 0) {
        GAME_LOGI("latido: frame=%u estado=%u cola=0x%08X 9460=%u 9414=%u",
                  frame, CurrentLoadState(), ReadU32(kQueueTail),
                  ReadU8(0x001849460u), ReadU8(0x001849414u));
    }

    // 00149ACC: el original, si cGpffff93dc == 0, entra en la ruta de
    // apagado y nunca vuelve. El port sigue presentando mientras SDL viva,
    // sin tocar 93DC ni la logica del juego.
    if (ReadU8(0x0018493DCu) == 0 && !shutdown_logged) {
        shutdown_logged = true;
        GAME_LOGW("93DC==0: cargador nativo inactivo; el original entraria en "
                  "apagado. El port continua el bucle.");
    }

    // 00149AE0: espera a iGpffff9130. port_vsync ya lo puso a 1.
    if (ReadU32(0x001849130u) == 0) {
        WriteU32(0x001849130u, 1);
    }

    WriteU8(0x001849460u, static_cast<std::uint8_t>(ReadU8(0x001849460u) ^ 1u));
    WriteU8(0x001849414u, static_cast<std::uint8_t>((gs_csr >> 13) & 1ull));

    return !exit_requested;
}

}  // namespace game
