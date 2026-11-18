#pragma once

// Lectura de sectores del DVD/ISO conectada a IsoReader.
//
// En SLUS_203.93 el loader usa la API de streaming de libcdvd:
//
//   0x0010DE10  sceCdStStart(lba, mode)   (FUN_00125740 lo llama con el LBA)
//   0x0010DE40  sceCdStStop()
//   0x0010DE70  sceCdStSeek()
//   0x0010DEA8  sceCdStRead(sectors, destination, mode, &err)
//
// FUN_00124C20 es el wrapper del juego que llama a sceCdStRead y maneja
// reintentos/estado. Aqui se implementa de forma sincrona: cada lectura
// copia los sectores reales de disc.iso a ps2_ram[destination].
//
// destination puede venir con alias:
//   0x00000000..0x01FFFFFF  RAM directa
//   0x20000000..0x3FFFFFFF  espejo uncached
//   0x80000000..0x9FFFFFFF  KSEG0
//   0xA0000000..0xBFFFFFFF  KSEG1
// Ps2Memory::PhysicalAddress() normaliza todos ellos.

#include <cstdint>
#include <functional>
#include <utility>

namespace ps2::cd {

// Devuelve true si copio count sectores de 2048 bytes en buffer.
using Reader = std::function<bool(std::uint32_t lba, std::uint32_t count,
                                  void *buffer)>;

void SetReader(Reader reader);
void ClearReader();

// Adaptador directo para IsoReader:
//
//   ps2::cd::AttachIsoReader(&g_iso);
//
// Expande a g_iso.IsOpen() && g_iso.ReadSectors(lba, count, buffer).
template <typename IsoReaderT>
void AttachIsoReader(IsoReaderT *reader) {
    if (reader == nullptr) {
        ClearReader();
        return;
    }
    SetReader([reader](std::uint32_t lba, std::uint32_t count, void *buffer) {
        return reader->IsOpen() && reader->ReadSectors(lba, count, buffer);
    });
}

void SetStreamLba(std::uint32_t lba);
std::uint32_t StreamLba();

// Copia count sectores de 2048 bytes desde el ISO a ps2_ram[destination].
// Devuelve false si no hay lector, el rango se sale de ps2_ram o falla la
// lectura.
bool ReadDvdSectors(std::uint32_t lba, std::uint32_t count,
                    std::uint32_t destination);

// Traduccion de la API de streaming de libcdvd.
int sceCdStStart(std::uint32_t lba, std::uint32_t mode);
int sceCdStStop();
int sceCdStSeek(std::uint32_t lba);
int sceCdStRead(std::uint32_t sectors, std::uint32_t destination,
                std::uint32_t mode, int *err);

// Traduccion simplificada de FUN_00124C20. Devuelve 0 si la lectura se
// completo, -1 si fallo.
std::int32_t FUN_00124C20(std::uint32_t sectors, std::uint32_t destination);

}  // namespace ps2::cd
