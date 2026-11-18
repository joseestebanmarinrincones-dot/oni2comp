#pragma once

// HLE de la Memory Card de PS2.
//
// El ejecutable de Onimusha 2 lleva libmc/libmcman estatico y llama a:
//
//   0x0010E288  sceMcInit
//   0x0010EDC0  sceMcGetInfo
//   0x0010E5E8  sceMcOpen
//   0x0010E758  sceMcClose
//   0x0010E810  sceMcSeek
//   0x0010E978  sceMcRead
//   0x0010EA98  sceMcWrite
//   0x0010EC88  sceMcSync
//   0x0010E720  sceMcMkDir
//   0x0010EF48  sceMcGetDir
//   0x0010F128  sceMcChDir
//   0x0010F278  sceMcFormat
//   0x0010F340  sceMcDelete
//   0x0010F860  sceMcUnformat
//   0x0010F950  sceMcGetEntSpace
//
// Todas las operaciones (menos Init) son asincronas en libmc: la llamada
// devuelve 0 si el RPC fue aceptado y el resultado real se recoge con
// sceMcSync(). Esta clase reproduce ese contrato aunque ejecute el trabajo
// de forma sincrona: el resultado queda pendiente hasta el siguiente Sync.
//
// El almacenamiento va a <base>/memcard0/. La base la pasa main.cpp desde
// SDL_AndroidGetInternalStoragePath(); el header no depende de SDL.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace ps2::mc {

// Codigos de libmc-common.h.
constexpr int kResSucceed = 0;
constexpr int kResChangedCard = -1;
constexpr int kResNoFormat = -2;
constexpr int kResFullDevice = -3;
constexpr int kResNoEntry = -4;
constexpr int kResDeniedPermit = -5;
constexpr int kResNotEmpty = -6;
constexpr int kResUpLimitHandle = -7;
constexpr int kResFailReplace = -8;

// Numeros de funcion que libmc devuelve por el puntero cmd de mcSync.
constexpr int kFuncNone = 0x00;
constexpr int kFuncGetInfo = 0x01;
constexpr int kFuncOpen = 0x02;
constexpr int kFuncClose = 0x03;
constexpr int kFuncSeek = 0x04;
constexpr int kFuncRead = 0x05;
constexpr int kFuncWrite = 0x06;
constexpr int kFuncFlush = 0x0A;
constexpr int kFuncMkDir = 0x0B;
constexpr int kFuncChDir = 0x0C;
constexpr int kFuncGetDir = 0x0D;
constexpr int kFuncSetInfo = 0x0E;
constexpr int kFuncDelete = 0x0F;
constexpr int kFuncFormat = 0x10;
constexpr int kFuncUnformat = 0x11;
constexpr int kFuncGetEnt = 0x12;
constexpr int kFuncRename = 0x13;
constexpr int kFuncChgPriority = 0x14;
constexpr int kFuncEraseBlock = 0x5A;
constexpr int kFuncReadPage = 0x5B;
constexpr int kFuncWritePage = 0x5C;

constexpr int kTypeNoCard = 0;
constexpr int kTypePS1 = 1;
constexpr int kTypePS2 = 2;

// Bits de modo de sceMcOpen (newlib/PS2).
constexpr int kOpenRead = 0x0001;
constexpr int kOpenWrite = 0x0002;
constexpr int kOpenCreate = 0x0200;
constexpr int kOpenTruncate = 0x0400;
constexpr int kOpenCreateDir = 0x0040;

class Ps2MemoryCard {
public:
    Ps2MemoryCard();
    ~Ps2MemoryCard();

    Ps2MemoryCard(const Ps2MemoryCard &) = delete;
    Ps2MemoryCard &operator=(const Ps2MemoryCard &) = delete;

    // Crea <base_path>/memcard0. Es idempotente.
    bool Initialize(const std::string &base_path);
    void Shutdown();
    bool IsInitialized() const;
    std::string CardPath() const;

    // RAM EE para leer nombres y escribir resultados (game::Ram()).
    void SetEeMemory(std::uint8_t *ram, std::uint32_t size);

    // Operaciones libmc. Init devuelve el resultado directamente; el resto
    // devuelve 0 si la operacion fue aceptada y deja el resultado pendiente
    // para Sync().
    int Init(int type);
    int GetInfo(int port, int slot, std::uint32_t type_addr,
                std::uint32_t free_addr, std::uint32_t format_addr);
    int Open(int port, int slot, std::uint32_t name_addr, int mode);
    int Close(int fd);
    int Seek(int fd, int offset, int origin);
    int Read(int fd, std::uint32_t buffer_addr, int size);
    int Write(int fd, std::uint32_t buffer_addr, int size);
    int Flush(int fd);
    int MkDir(int port, int slot, std::uint32_t name_addr);
    int ChDir(int port, int slot, std::uint32_t name_addr,
              std::uint32_t current_addr);
    int GetDir(int port, int slot, std::uint32_t name_addr, unsigned mode,
               int maxent, std::uint32_t table_addr);
    int Delete(int port, int slot, std::uint32_t name_addr);
    int Format(int port, int slot);
    int Unformat(int port, int slot);
    int GetEntSpace(int port, int slot, std::uint32_t path_addr);
    int Rename(int port, int slot, std::uint32_t old_addr,
               std::uint32_t new_addr);
    int Sync(int mode, std::uint32_t cmd_addr, std::uint32_t result_addr);

    int PendingCommand() const;
    int PendingResult() const;

    // Estado interno (definido en ps2_memory_card.cpp).
    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

Ps2MemoryCard &System();

// Memoria EE compartida para todas las operaciones.
void SetEeMemory(std::uint8_t *ram, std::uint32_t size);

// Despacho por direccion de libmc. Devuelve true si la direccion pertenece
// a una funcion de Memory Card.
bool IsMemoryCardAddress(std::uint32_t address);
bool Invoke(std::uint32_t address, std::uint32_t a0, std::uint32_t a1,
            std::uint32_t a2, std::uint32_t a3, std::uint32_t *result);

// GetDir necesita 6 argumentos; se expone aparte para pruebas o para un
// dispatcher ampliado.
int GetDirEx(int port, int slot, std::uint32_t name_addr, unsigned mode,
             int maxent, std::uint32_t table_addr);

}  // namespace ps2::mc
