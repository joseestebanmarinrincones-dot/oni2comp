#pragma once

// Traduccion C++17 de la funcion main de Onimusha 2 (SLUS_203.93):
//
//   FUN_00149BF0  inicializacion
//   FUN_001496D0  bucle principal (una iteracion = Frame)
//   FUN_00124D90  despachador de estados de carga (tabla 0x002BA0E0)
//
// El punto donde la PS2 llama a sceGsSyncV (SONY_libgraph_00100b28) se
// sustituye por SDL_GL_SwapWindow. El resto del orden de llamadas, flags,
// contadores y transiciones se conserva.
//
// Las funciones de subsistema que aun no tienen traduccion nativa pasan por
// CallPS2(); el stub por defecto las registra una vez y continua. Una
// definicion fuerte de CallPS2() puede sustituirlo para enlazar el codigo
// recompilado.

#include <cstdint>
#include <string>

struct SDL_Window;

namespace game {

// Tamano total del bloque: 32 MB de RAM principal + 4 MB de ventana alta
// (0x02000000..0x023FFFFF), igual que OverlayManager.
constexpr std::uint32_t kMainRamSize = 32u << 20;
constexpr std::uint32_t kHighWindowBase = 0x02000000u;
constexpr std::uint32_t kHighWindowSize = 4u << 20;
constexpr std::uint32_t kTotalRamSize = kMainRamSize + kHighWindowSize;

// Direcciones absolutas de los globales usados por main. gp = 0x1850000.
constexpr std::uint32_t kFrameCounter = 0x00184941Cu;   // u16
constexpr std::uint32_t kLoadState = 0x0018479F4u;      // u8
constexpr std::uint32_t kLoaderContext = 0x0018479F0u;  // struct base
constexpr std::uint32_t kQueueHead = 0x001849488u;      // request array base
constexpr std::uint32_t kQueueTail = 0x001849484u;      // next free entry
constexpr std::uint32_t kQueueFlag = 0x001849478u;      // bit 0 = pending

// Tabla exacta leida de 0x002BA0E0 (24 entradas; 13..15 son nulos).
extern const std::uint32_t kLoadStateTable[24];

// Reserva RAM, traduce FUN_00149BF0 y deja la peticion de arranque 0x301 en
// la cola, igual que FUN_00144680 -> FUN_00124360.
bool Initialize(SDL_Window *window, const std::string &overlay_dir);

// Una iteracion completa de FUN_001496D0. Incluye SDL_GL_SwapWindow en el
// lugar de sceGsSyncV. Devuelve false solo si se pidio salir al port.
bool Frame(SDL_Window *window);

void RequestExit();
bool ExitRequested();

// RAM emulada.
std::uint8_t *Ram();
std::uint32_t RamSize();
bool AttachRam(std::uint8_t *ram, std::uint32_t size);

// Acceso little-endian con comprobacion de limites.
std::uint8_t ReadU8(std::uint32_t address);
std::uint16_t ReadU16(std::uint32_t address);
std::uint32_t ReadU32(std::uint32_t address);
void WriteU8(std::uint32_t address, std::uint8_t value);
void WriteU16(std::uint32_t address, std::uint16_t value);
void WriteU32(std::uint32_t address, std::uint32_t value);

// Estado del cargador para diagnostico.
std::uint8_t CurrentLoadState();
std::uint32_t LoadStateHandler(std::uint8_t state);

// Cola de peticiones del cargador (FUN_00124430 / FUN_001242A0).
bool EnqueueLoadRequest(std::uint8_t type, std::uint16_t file_index,
                        std::uint32_t offset_sectors = 0,
                        std::uint8_t flags = 0);
void PopLoadRequest();

// El menu de depuracion puede pedir cargar/ejecutar un overlay. El loader
// nativo consume esta peticion cuando su estado 1 este traducido; mientras
// tanto queda como diagnostico visible en el propio menu.
void RequestOverlayJump(int index, std::uint32_t base, std::uint32_t stamp,
                        const std::string &name);
bool ConsumeOverlayJumpRequest(int &index, std::uint32_t &base,
                               std::uint32_t &stamp, std::string &name);

// Despachador interno: primero atiende cola, tabla, DMA y VSync; para el
// resto llama a CallRecompiled().
void CallPS2(std::uint32_t address, std::uint32_t a0 = 0,
             std::uint32_t a1 = 0, std::uint32_t a2 = 0,
             std::uint32_t a3 = 0);
std::uint32_t CallPS2Ret(std::uint32_t address, std::uint32_t a0 = 0,
                         std::uint32_t a1 = 0, std::uint32_t a2 = 0,
                         std::uint32_t a3 = 0);

// Punto de enganche para el codigo recompilado. El stub debil registra una
// vez cada direccion. Una definicion fuerte lo sustituye sin perder la cola
// ni el VSync del despachador interno.
void CallRecompiled(std::uint32_t address, std::uint32_t a0 = 0,
                    std::uint32_t a1 = 0, std::uint32_t a2 = 0,
                    std::uint32_t a3 = 0);
std::uint32_t CallRecompiledRet(std::uint32_t address, std::uint32_t a0 = 0,
                                std::uint32_t a1 = 0, std::uint32_t a2 = 0,
                                std::uint32_t a3 = 0);

}  // namespace game
