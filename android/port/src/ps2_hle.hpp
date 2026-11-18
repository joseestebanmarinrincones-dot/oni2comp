#pragma once

// Capa HLE (High Level Emulation) para las llamadas de sistema de PS2 que usa
// el codigo desensamblado:
//
//   sceCdRead   -> IsoReader (sectores de 2048 bytes)
//   scePadRead  -> estado del mando virtual actualizado por SDL2
//   sceDmaSend  -> hook para paquetes GIF/VIF hacia OpenGL ES 3.0
//
// La capa no depende de SDL en el .cpp: el host le pasa el estado de los dedos
// (port::TouchPoint) o el port::PadState que main.cpp actualiza desde SDL.
//
// Uso tipico en main.cpp:
//
//   ps2::hle::AttachIsoReader(&g_iso);
//   ps2::hle::SetPadState(&g_pad);
//   ps2::hle::AttachMemory(game::Ram(), game::RamSize());
//   ps2::hle::SetDmaSendHook([](const ps2::hle::DmaTransfer &dma) {
//       // recorrer dma.tags y pasar TagData()/TagBytes() al renderer
//   });
//
// Las funciones sce* tienen enlace C para que el codigo recompilado las llame
// por su nombre. Solo cubren las llamadas indicadas; las libs Sony originales
// deben excluirse del enlace.

#include "ps2_pad.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ps2::hle {

// ---------------------------------------------------------------------------
// CDVD
// ---------------------------------------------------------------------------

constexpr std::uint32_t kCdSectorSize = 2048;

// El lector se entrega como callback para no acoplar la HLE a IsoReader.
// AttachIsoReader() crea el callback con cualquier clase que tenga IsOpen()
// y ReadSectors(uint32_t, uint32_t, void*).
using CdReadCallback =
    std::function<bool(std::uint32_t lba, std::uint32_t sectors, void *buffer)>;

void SetCdReader(CdReadCallback callback);

template <typename Reader>
void AttachIsoReader(Reader *reader) {
    if (!reader) {
        SetCdReader({});
        return;
    }
    SetCdReader([reader](std::uint32_t lba, std::uint32_t sectors, void *buffer) {
        return reader->IsOpen() && reader->ReadSectors(lba, sectors, buffer);
    });
}

// Lectura sincrona: devuelve true si el callback leyo todos los sectores.
bool CdRead(std::uint32_t lba, std::uint32_t sectors, void *buffer);

struct CdStats {
    std::uint64_t reads = 0;
    std::uint64_t sectors = 0;
    std::uint64_t bytes = 0;
    std::uint32_t last_lba = 0;
    std::uint32_t last_sectors = 0;
};

const CdStats &GetCdStats();
void ResetCdStats();
const std::string &LastCdError();

// ---------------------------------------------------------------------------
// Pad
// ---------------------------------------------------------------------------

// Usa uno de los dos:
//   SetPadState(&g_pad): main.cpp ya actualiza g_pad desde SDL2.
//   UpdatePadFromTouches(...): la HLE llama a port::update_touch_pad.
void SetPadState(const port::PadState *pad);
void UpdatePadFromTouches(const port::TouchPoint *points, std::size_t count);

// Word de botones en formato PS2, activo a bajo.
std::uint16_t PadButtonsActiveLow(const port::PadState &pad);

// Relleno de los 32 bytes que devuelve scePadRead.
void FillPadReport(std::uint8_t out[32]);

// ---------------------------------------------------------------------------
// DMA / GIF / VIF
// ---------------------------------------------------------------------------

enum class DmaChannel : std::uint8_t {
    Vif0 = 0,
    Vif1 = 1,
    Gif = 2,
    Ipu0 = 3,
    Ipu1 = 4,
    Sif0 = 5,
    Sif1 = 6,
    Sif2 = 7,
    Spr0 = 8,
    Spr1 = 9,
    Unknown = 0xFF,
};

const char *DmaChannelName(DmaChannel channel);

// Un tag de la cadena DMA. Los IDs son los de source chain:
//   0 REFE, 1 CNT, 2 NEXT, 3 REF, 4 REFS, 5 CALL, 6 RET, 7 END.
// Para END/CNT/NEXT/CALL/RET los datos empiezan en tag_address + 16.
// Para REF/REFE/REFS empiezan en data_address (campo ADDR del tag).
struct DmaTag {
    std::uint32_t tag_address = 0;
    std::uint32_t data_address = 0;
    std::uint32_t qwords = 0;
    std::uint8_t id = 0;
    std::uint8_t pce = 0;
    bool irq = false;
    bool spr = false;
    bool data_valid = false;
};

struct DmaTransfer {
    DmaChannel channel = DmaChannel::Unknown;
    std::uint32_t channel_address = 0;
    std::uint32_t source = 0;
    bool chain = true;
    bool parsed = false;
    std::vector<DmaTag> tags;
};

// El hook se llama en el hilo del bucle del juego. Debe copiar lo que necesite
// antes de volver; el DmaTransfer solo es valido durante la llamada.
using DmaSendHook = std::function<void(const DmaTransfer &)>;

void SetDmaSendHook(DmaSendHook hook);
void ClearDmaSendHook();

// RAM principal emulada para resolver las direcciones PS2 de los tags.
void AttachMemory(std::uint8_t *ram, std::uint32_t size);
std::uint8_t *Memory();
std::uint32_t MemorySize();

// Recorre una cadena DMA en RAM. Devuelve false si no hay cadena valida.
bool WalkDmaChain(std::uint32_t source, std::vector<DmaTag> &tags,
                  std::size_t max_tags = 4096);

const std::uint8_t *TagData(const DmaTag &tag);
std::uint32_t TagBytes(const DmaTag &tag);

// Equivalente a sceDmaSend/sceDmaSync con direcciones PS2 de 32 bits.
void DmaSend(std::uint32_t channel_address, std::uint32_t source_address);
int DmaSync(std::uint32_t channel_address, int mode, int timeout);

const DmaTransfer &LastDmaTransfer();

}  // namespace ps2::hle

// ---------------------------------------------------------------------------
// ABI C para el codigo recompilado
// ---------------------------------------------------------------------------

extern "C" {

// libcdvd. Solo se soportan sectores de 2048 bytes; el modo se valida si no
// es nulo (mode[2] == 0).
int sceCdRead(std::uint32_t lbn, std::uint32_t sectors, void *buffer, void *mode);
int sceCdSync(int mode);       // 0 = completado (lectura sincrona)
int sceCdGetError();           // 0 = sin error

// libpad. Reporte de 32 bytes en formato Sony (analogico, botones activo-bajo).
int scePadRead(int port, int slot, std::uint8_t *data);
int scePadInit(int mode);
int scePadPortOpen(int port, int slot, void *pad_area);
int scePadPortClose(int port, int slot);
int scePadEnd(void);
int scePadGetState(int port, int slot);
int scePadInfoMode(int port, int slot, int info_mode, int index);
int scePadInfoPressMode(int port, int slot);
int scePadSetMainMode(int port, int slot, int mode, int lock);
int scePadEnterPressMode(int port, int slot);
int scePadExitPressMode(int port, int slot);
int scePadSetActDirect(int port, int slot, const unsigned char *act);
int scePadSetActAlign(int port, int slot, const unsigned char *act);

// libdma. sceDmaSend usa source chain (TADR = address).
void sceDmaSend(void *channel, void *address);
int sceDmaSync(void *channel, int mode, int timeout);

}  // extern "C"
