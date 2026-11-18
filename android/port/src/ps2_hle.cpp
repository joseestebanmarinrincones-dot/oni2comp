#include "ps2_hle.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <utility>

#if defined(__ANDROID__)
#include <android/log.h>
#define HLE_TAG "OnimushaHLE"
#define HLE_LOGI(...) __android_log_print(ANDROID_LOG_INFO, HLE_TAG, __VA_ARGS__)
#define HLE_LOGW(...) __android_log_print(ANDROID_LOG_WARN, HLE_TAG, __VA_ARGS__)
#define HLE_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, HLE_TAG, __VA_ARGS__)
#else
#define HLE_LOGI(...) do { std::fprintf(stdout, __VA_ARGS__); std::fputc('\n', stdout); } while (0)
#define HLE_LOGW(...) do { std::fprintf(stderr, __VA_ARGS__); std::fputc('\n', stderr); } while (0)
#define HLE_LOGE(...) do { std::fprintf(stderr, __VA_ARGS__); std::fputc('\n', stderr); } while (0)
#endif

namespace ps2::hle {
namespace {

// ---------------------------------------------------------------------------
// CDVD
// ---------------------------------------------------------------------------

CdReadCallback cd_reader;
CdStats cd_stats{};
std::string cd_error;
bool cd_logged = false;

// ---------------------------------------------------------------------------
// Pad
// ---------------------------------------------------------------------------

const port::PadState *pad_source = nullptr;
port::PadState owned_pad;

const port::PadState &current_pad() {
    return pad_source ? *pad_source : owned_pad;
}

// ---------------------------------------------------------------------------
// DMA
// ---------------------------------------------------------------------------

std::uint8_t *dma_ram = nullptr;
std::uint32_t dma_ram_bytes = 0;
DmaSendHook dma_hook;
DmaTransfer last_transfer;
bool dma_logged = false;

bool read_u64(std::uint32_t address, std::uint64_t &value) {
    if (!dma_ram || address > dma_ram_bytes || 8u > dma_ram_bytes - address) {
        return false;
    }
    std::memcpy(&value, dma_ram + address, sizeof(value));
    return true;
}

DmaChannel channel_from_address(std::uint32_t address) {
    switch (address & 0xFFFFFF00u) {
    case 0x10008000u: return DmaChannel::Vif0;
    case 0x10009000u: return DmaChannel::Vif1;
    case 0x1000A000u: return DmaChannel::Gif;
    case 0x1000B000u: return DmaChannel::Ipu0;
    case 0x1000B400u: return DmaChannel::Ipu1;
    case 0x1000C000u: return DmaChannel::Sif0;
    case 0x1000C400u: return DmaChannel::Sif1;
    case 0x1000C800u: return DmaChannel::Sif2;
    case 0x1000D000u: return DmaChannel::Spr0;
    case 0x1000D400u: return DmaChannel::Spr1;
    default: return DmaChannel::Unknown;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// CDVD
// ---------------------------------------------------------------------------

void SetCdReader(CdReadCallback callback) {
    cd_reader = std::move(callback);
    cd_error.clear();
}

void ResetCdStats() {
    cd_stats = {};
    cd_logged = false;
}

const CdStats &GetCdStats() {
    return cd_stats;
}

const std::string &LastCdError() {
    return cd_error;
}

bool CdRead(std::uint32_t lba, std::uint32_t sectors, void *buffer) {
    cd_error.clear();

    if (sectors == 0) return true;
    if (!buffer) {
        cd_error = "sceCdRead con buffer nulo";
        HLE_LOGE("%s", cd_error.c_str());
        return false;
    }
    if (!cd_reader) {
        cd_error = "sceCdRead sin IsoReader conectado";
        HLE_LOGE("%s", cd_error.c_str());
        return false;
    }

    const std::uint64_t bytes =
        static_cast<std::uint64_t>(sectors) * kCdSectorSize;
    if (bytes > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        cd_error = "sceCdRead: tamano de lectura demasiado grande";
        HLE_LOGE("%s", cd_error.c_str());
        return false;
    }

    if (!cd_reader(lba, sectors, buffer)) {
        cd_error = "sceCdRead: IsoReader no pudo leer";
        HLE_LOGE("%s (lba=%u sectores=%u)", cd_error.c_str(), lba, sectors);
        return false;
    }

    cd_stats.reads++;
    cd_stats.sectors += sectors;
    cd_stats.bytes += bytes;
    cd_stats.last_lba = lba;
    cd_stats.last_sectors = sectors;

    if (!cd_logged) {
        cd_logged = true;
        HLE_LOGI("sceCdRead: lba=%u sectores=%u (%llu bytes)",
                 lba, sectors, static_cast<unsigned long long>(bytes));
    }
    return true;
}

// ---------------------------------------------------------------------------
// Pad
// ---------------------------------------------------------------------------

void SetPadState(const port::PadState *pad) {
    pad_source = pad;
}

void UpdatePadFromTouches(const port::TouchPoint *points, std::size_t count) {
    port::update_touch_pad(owned_pad, points, count);
    pad_source = &owned_pad;
}

std::uint16_t PadButtonsActiveLow(const port::PadState &pad) {
    // Bits de boton de PS2/PAD_*.
    constexpr std::uint16_t kUp = 0x0010;
    constexpr std::uint16_t kDown = 0x0040;
    constexpr std::uint16_t kLeft = 0x0080;
    constexpr std::uint16_t kRight = 0x0020;
    constexpr std::uint16_t kTriangle = 0x1000;
    constexpr std::uint16_t kCircle = 0x2000;
    constexpr std::uint16_t kCross = 0x4000;
    constexpr std::uint16_t kSquare = 0x8000;
    constexpr std::uint16_t kL1 = 0x0400;
    constexpr std::uint16_t kR1 = 0x0800;
    constexpr std::uint16_t kStart = 0x0008;
    constexpr std::uint16_t kL2 = 0x0100;
    constexpr std::uint16_t kR2 = 0x0200;
    constexpr std::uint16_t kL3 = 0x0002;
    constexpr std::uint16_t kR3 = 0x0004;
    constexpr std::uint16_t kSelect = 0x0001;

    std::uint16_t pressed = 0;
    if (pad.pressed(port::PadButton::Up)) pressed |= kUp;
    if (pad.pressed(port::PadButton::Down)) pressed |= kDown;
    if (pad.pressed(port::PadButton::Left)) pressed |= kLeft;
    if (pad.pressed(port::PadButton::Right)) pressed |= kRight;
    if (pad.pressed(port::PadButton::Triangle)) pressed |= kTriangle;
    if (pad.pressed(port::PadButton::Circle)) pressed |= kCircle;
    if (pad.pressed(port::PadButton::Cross)) pressed |= kCross;
    if (pad.pressed(port::PadButton::Square)) pressed |= kSquare;
    if (pad.pressed(port::PadButton::L1)) pressed |= kL1;
    if (pad.pressed(port::PadButton::R1)) pressed |= kR1;
    if (pad.pressed(port::PadButton::Start)) pressed |= kStart;
    if (pad.pressed(port::PadButton::L2)) pressed |= kL2;
    if (pad.pressed(port::PadButton::R2)) pressed |= kR2;
    if (pad.pressed(port::PadButton::L3)) pressed |= kL3;
    if (pad.pressed(port::PadButton::R3)) pressed |= kR3;
    if (pad.pressed(port::PadButton::Select)) pressed |= kSelect;

    return static_cast<std::uint16_t>(~pressed);
}

void FillPadReport(std::uint8_t out[32]) {
    if (!out) return;
    std::memset(out, 0, 32);

    // Formato Sony de 32 bytes: status 0 = OK, id 0x73 = DualShock analogico,
    // botones activo-bajo (byte alto primero), sticks centrados, sin presion.
    out[0] = 0x00;
    out[1] = 0x73;

    const std::uint16_t buttons = PadButtonsActiveLow(current_pad());
    out[2] = static_cast<std::uint8_t>(buttons >> 8);
    out[3] = static_cast<std::uint8_t>(buttons & 0xFF);

    const port::PadState &pad = current_pad();
    const auto axis_byte = [](float value) {
        const int mapped = static_cast<int>(128.0f + value * 128.0f);
        return static_cast<std::uint8_t>(std::clamp(mapped, 0, 255));
    };
    out[4] = axis_byte(pad.right_x);   // right x
    out[5] = axis_byte(pad.right_y);   // right y
    out[6] = axis_byte(pad.left_x);    // left x
    out[7] = axis_byte(pad.left_y);    // left y

    // 8..19: presion (0 = no pulsado). 20/21: word de menus.
    out[20] = out[2];
    out[21] = out[3];
}

// ---------------------------------------------------------------------------
// DMA
// ---------------------------------------------------------------------------

const char *DmaChannelName(DmaChannel channel) {
    switch (channel) {
    case DmaChannel::Vif0: return "VIF0";
    case DmaChannel::Vif1: return "VIF1";
    case DmaChannel::Gif: return "GIF";
    case DmaChannel::Ipu0: return "IPU0";
    case DmaChannel::Ipu1: return "IPU1";
    case DmaChannel::Sif0: return "SIF0";
    case DmaChannel::Sif1: return "SIF1";
    case DmaChannel::Sif2: return "SIF2";
    case DmaChannel::Spr0: return "SPR0";
    case DmaChannel::Spr1: return "SPR1";
    default: return "?";
    }
}

void SetDmaSendHook(DmaSendHook hook) {
    dma_hook = std::move(hook);
}

void ClearDmaSendHook() {
    dma_hook = nullptr;
}

void AttachMemory(std::uint8_t *ram, std::uint32_t size) {
    dma_ram = ram;
    dma_ram_bytes = ram ? size : 0;
}

std::uint8_t *Memory() {
    return dma_ram;
}

std::uint32_t MemorySize() {
    return dma_ram_bytes;
}

bool WalkDmaChain(std::uint32_t source, std::vector<DmaTag> &tags,
                  std::size_t max_tags) {
    tags.clear();
    if (!dma_ram || dma_ram_bytes == 0 || max_tags == 0) return false;

    std::uint32_t current = source;
    std::uint32_t returns[2] = {0, 0};
    int asp = 0;

    for (std::size_t index = 0; index < max_tags; ++index) {
        std::uint64_t qword = 0;
        if (!read_u64(current, qword)) {
            return !tags.empty();
        }
        // Un tag cero no es una cadena valida (paquete sin inicializar).
        if (qword == 0 || qword == ~0ull) {
            return !tags.empty();
        }

        const std::uint32_t qwc = static_cast<std::uint32_t>(qword & 0xFFFFu);
        const std::uint8_t pce = static_cast<std::uint8_t>((qword >> 26) & 0x3u);
        const std::uint8_t id = static_cast<std::uint8_t>((qword >> 28) & 0x7u);
        const bool irq = ((qword >> 31) & 1u) != 0;
        const std::uint32_t addr =
            static_cast<std::uint32_t>((qword >> 32) & 0x7FFFFFFFu);
        const bool spr = ((qword >> 63) & 1u) != 0;

        DmaTag tag;
        tag.tag_address = current;
        tag.qwords = qwc;
        tag.id = id;
        tag.pce = pce;
        tag.irq = irq;
        tag.spr = spr;

        // REF/REFE/REFS leen del campo ADDR. El resto lee detras del tag.
        if (id == 0 || id == 3 || id == 4) {
            tag.data_address = addr;
        } else {
            tag.data_address = current + 16u;
        }

        const std::uint64_t data_bytes = static_cast<std::uint64_t>(qwc) * 16u;
        if (data_bytes == 0) {
            tag.data_valid = true;
        } else if (!spr && tag.data_address <= dma_ram_bytes &&
                   data_bytes <= static_cast<std::uint64_t>(dma_ram_bytes - tag.data_address)) {
            tag.data_valid = true;
        }

        tags.push_back(tag);

        const auto advance = [](std::uint32_t base, std::uint32_t count,
                                std::uint32_t &next) -> bool {
            const std::uint64_t delta = 16ull + static_cast<std::uint64_t>(count) * 16ull;
            const std::uint64_t value = static_cast<std::uint64_t>(base) + delta;
            if (value > 0xFFFFFFFFull) return false;
            next = static_cast<std::uint32_t>(value);
            return true;
        };

        switch (id) {
        case 0:  // REFE: transfiere desde ADDR y termina
            return true;
        case 1:  // CNT: datos detras del tag; siguiente tag tras los datos
            if (!advance(current, qwc, current)) return false;
            break;
        case 2:  // NEXT: datos detras del tag; siguiente tag en ADDR
            if (addr == 0) return false;
            current = addr;
            break;
        case 3:  // REF
        case 4:  // REFS
            current += 16u;
            break;
        case 5: {  // CALL
            if (asp >= 2) return false;   // pila ASP llena
            std::uint32_t resume = 0;
            if (!advance(current, qwc, resume)) return false;
            returns[asp++] = resume;
            if (addr == 0) return false;
            current = addr;
            break;
        }
        case 6:  // RET
            if (asp == 0) return true;
            --asp;
            current = returns[asp];
            break;
        case 7:  // END: datos detras del tag y termina
            return true;
        default:
            return false;
        }

        if (current == 0) return false;
    }

    // Cadena demasiado larga: evita bucles infinitos.
    HLE_LOGW("WalkDmaChain: limite de %zu tags alcanzado", max_tags);
    return false;
}

const std::uint8_t *TagData(const DmaTag &tag) {
    if (!dma_ram || !tag.data_valid) return nullptr;
    return dma_ram + tag.data_address;
}

std::uint32_t TagBytes(const DmaTag &tag) {
    return tag.qwords * 16u;
}

void DmaSend(std::uint32_t channel_address, std::uint32_t source_address) {
    DmaTransfer transfer;
    transfer.channel_address = channel_address;
    transfer.channel = channel_from_address(channel_address);
    transfer.source = source_address;
    transfer.chain = true;
    transfer.parsed = WalkDmaChain(source_address, transfer.tags);
    last_transfer = transfer;

    if (dma_hook) {
        try {
            dma_hook(transfer);
        } catch (const std::exception &error) {
            HLE_LOGE("DmaSend hook lanzo una excepcion: %s", error.what());
        } catch (...) {
            HLE_LOGE("DmaSend hook lanzo una excepcion desconocida");
        }
        return;
    }

    if (!dma_logged) {
        dma_logged = true;
        HLE_LOGI("sceDmaSend: canal=%s (0x%08X) source=0x%08X tags=%zu parsed=%d",
                 DmaChannelName(transfer.channel), channel_address,
                 source_address, transfer.tags.size(),
                 transfer.parsed ? 1 : 0);
    }
}

int DmaSync(std::uint32_t channel_address, int mode, int timeout) {
    // La HLE procesa el paquete de forma sincrona; no hay DMAC que esperar.
    (void)channel_address;
    (void)mode;
    (void)timeout;
    return 0;
}

const DmaTransfer &LastDmaTransfer() {
    return last_transfer;
}

}  // namespace ps2::hle

// ---------------------------------------------------------------------------
// ABI C
// ---------------------------------------------------------------------------

extern "C" {

int sceCdRead(std::uint32_t lbn, std::uint32_t sectors, void *buffer, void *mode) {
    if (mode) {
        const auto *read_mode = static_cast<const std::uint8_t *>(mode);
        // sceCdRMode: trycount, spindlctrl, datapattern, pad.
        if (read_mode[2] != 0) {
            HLE_LOGE("sceCdRead: solo se soportan sectores de 2048 bytes (mode=%u)",
                     read_mode[2]);
            return 0;
        }
    }
    return ps2::hle::CdRead(lbn, sectors, buffer) ? 1 : 0;
}

int sceCdSync(int mode) {
    (void)mode;
    return 0;   // lectura sincrona: siempre completada
}

int sceCdGetError() {
    return 0;
}

int scePadRead(int port, int slot, std::uint8_t *data) {
    if (!data) return 0;
    if (port != 0 || slot != 0) {
        std::memset(data, 0, 32);
        data[0] = 0xFF;   // desconectado
        return 0;
    }
    ps2::hle::FillPadReport(data);
    return 1;
}

int scePadInit(int mode) {
    (void)mode;
    return 1;
}

int scePadPortOpen(int port, int slot, void *pad_area) {
    (void)pad_area;
    return (port == 0 && slot == 0) ? 1 : 0;
}

int scePadPortClose(int port, int slot) {
    return (port == 0 && slot == 0) ? 1 : 0;
}

int scePadEnd(void) {
    return 1;
}

int scePadGetState(int port, int slot) {
    // PAD_STATE_STABLE
    return (port == 0 && slot == 0) ? 6 : 0;
}

int scePadInfoMode(int port, int slot, int info_mode, int index) {
    (void)index;
    if (port != 0 || slot != 0) return 0;
    if (info_mode == 1) return 0x73;   // PAD_MODECURID: DualShock analogico
    return 7;                          // PAD_TYPE_DUALSHOCK
}

int scePadInfoPressMode(int port, int slot) {
    (void)port;
    (void)slot;
    return 0;   // sin presion analogica
}

int scePadSetMainMode(int port, int slot, int mode, int lock) {
    (void)mode;
    (void)lock;
    return (port == 0 && slot == 0) ? 1 : 0;
}

int scePadEnterPressMode(int port, int slot) {
    (void)port;
    (void)slot;
    return 1;
}

int scePadExitPressMode(int port, int slot) {
    (void)port;
    (void)slot;
    return 1;
}

int scePadSetActDirect(int port, int slot, const unsigned char *act) {
    (void)act;
    return (port == 0 && slot == 0) ? 1 : 0;
}

int scePadSetActAlign(int port, int slot, const unsigned char *act) {
    (void)act;
    return (port == 0 && slot == 0) ? 1 : 0;
}

void sceDmaSend(void *channel, void *address) {
    const auto channel_address =
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(channel) & 0xFFFFFFFFu);
    const auto source_address =
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(address) & 0xFFFFFFFFu);
    ps2::hle::DmaSend(channel_address, source_address);
}

int sceDmaSync(void *channel, int mode, int timeout) {
    const auto channel_address =
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(channel) & 0xFFFFFFFFu);
    return ps2::hle::DmaSync(channel_address, mode, timeout);
}

}  // extern "C"
