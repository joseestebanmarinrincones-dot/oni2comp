#pragma once

// Globals absolutos de Onimusha 2 (SLUS_203.93) mapeados dentro de
// ps2::ps2_ram[32 * 1024 * 1024].
//
// ps2_ram cubre 0x00000000..0x01FFFFFF. Todas las direcciones de este
// archivo estan dentro de ese rango. Las direcciones de hardware
// (0x10000000..0x12001FFF) NO se mapean aqui: ps2_mem las intercepta y las
// redirige al shadow de registros.
//
// Uso:
//
//   uRam017e0350 = 0;
//   iGpffff9130 = 1;
//   g_frame_flags[0] = 1;
//   g_dma_channels[2] = 0x1000A000u;
//   std::memcpy(g_static_gif_packet, packet, 240);
//
// Los macros expanden a lvalues tipados sobre ps2_ram. Las direcciones de
// los escalares de main estan alineadas a su tipo. Para bloques sin tipo
// conocido se expone un puntero uint8_t*.

#include "ps2_mem.hpp"

#include <cstddef>
#include <cstdint>

namespace ps2::globals {

constexpr std::uint32_t kRamSize = ps2::kPs2RamSize;

// Acceso tipado sin comprobacion de limites. La direccion debe estar dentro
// de ps2_ram y alineada a alignof(T).
template <typename T>
inline T *ptr(std::uint32_t address) {
    return reinterpret_cast<T *>(ps2::ps2_ram
        + static_cast<std::size_t>(address));
}

template <typename T>
inline const T *cptr(std::uint32_t address) {
    return reinterpret_cast<const T *>(ps2::ps2_ram
        + static_cast<std::size_t>(address));
}

template <typename T>
inline T &ref(std::uint32_t address) {
    return *ptr<T>(address);
}

}  // namespace ps2::globals

// Puntero base a RAM PS2. No comprueba limites.
#define PS2_RAM_OFFSET(address) \
    (::ps2::ps2_ram + static_cast<std::size_t>(address))

#define PS2_RAM_PTR(type, address) \
    (::ps2::globals::ptr<type>(static_cast<std::uint32_t>(address)))
#define PS2_RAM_CPTR(type, address) \
    (::ps2::globals::cptr<type>(static_cast<std::uint32_t>(address)))
#define PS2_RAM_REF(type, address) \
    (::ps2::globals::ref<type>(static_cast<std::uint32_t>(address)))

#define PS2_RAM_U8(address)  PS2_RAM_REF(std::uint8_t,  (address))
#define PS2_RAM_S8(address)  PS2_RAM_REF(std::int8_t,   (address))
#define PS2_RAM_U16(address) PS2_RAM_REF(std::uint16_t, (address))
#define PS2_RAM_S16(address) PS2_RAM_REF(std::int16_t,  (address))
#define PS2_RAM_U32(address) PS2_RAM_REF(std::uint32_t, (address))
#define PS2_RAM_S32(address) PS2_RAM_REF(std::int32_t,  (address))
#define PS2_RAM_U64(address) PS2_RAM_REF(std::uint64_t, (address))
#define PS2_RAM_S64(address) PS2_RAM_REF(std::int64_t,  (address))

// ---------------------------------------------------------------------------
// FUN_001496D0 (main): escalares del bucle principal
// ---------------------------------------------------------------------------

#define uRam017e0350 PS2_RAM_U32(0x0017E0350u)  // estado hebra cargador
#define iGpffff9130  PS2_RAM_U32(0x001849130u)  // flag VSync
#define cGpffff93d4  PS2_RAM_U8 (0x0018493D4u)  // habilita shift de flags
#define cGpffff93dc  PS2_RAM_U8 (0x0018493DCu)  // 0 = frame normal
#define bGpffff9460  PS2_RAM_U8 (0x001849460u)  // toggle buffer/field
#define sGpffff93f8  PS2_RAM_S16(0x0018493F8u)  // display Y
#define sGpffff93fc  PS2_RAM_S16(0x0018493FCu)  // display X
#define bGpffff9414  PS2_RAM_U8 (0x001849414u)  // VSINT/field
#define uGpffff945c  PS2_RAM_U32(0x00184945Cu)
#define uGpffff9458  PS2_RAM_U32(0x001849458u)
#define cRam008ace60 PS2_RAM_U8 (0x0008ACE60u)
#define cGpffff9418  PS2_RAM_U8 (0x001849418u)  // modo 3/4
#define sGpffff941c  PS2_RAM_U16(0x00184941Cu)  // contador de frames

// Los seis flags de 0x00149710. En el original son bytes; el nombre del
// pseudocodigo se conserva.
#define bRam0172a378 PS2_RAM_U8(0x00172A378u)
#define bRam0172a379 PS2_RAM_U8(0x00172A379u)
#define bRam0172a37a PS2_RAM_U8(0x00172A37Au)
#define bRam0172a37b PS2_RAM_U8(0x00172A37Bu)
#define bRam0172a37c PS2_RAM_U8(0x00172A37Cu)
#define bRam0172a37d PS2_RAM_U8(0x00172A37Du)

#define g_frame_flags PS2_RAM_PTR(std::uint8_t, 0x00172A378u)  // 6 bytes

// ---------------------------------------------------------------------------
// FUN_001496D0: bloques de datos pasados a funciones hijas
// ---------------------------------------------------------------------------

// Display/draw environments del juego.
#define g_draw_env_main  PS2_RAM_PTR(std::uint8_t, 0x001826410u)
#define g_draw_env_even  PS2_RAM_PTR(std::uint8_t, 0x0018265E0u)
#define g_draw_env_odd   PS2_RAM_PTR(std::uint8_t, 0x001826470u)
#define g_draw_env2_even PS2_RAM_PTR(std::uint8_t, 0x001826660u)
#define g_draw_env2_odd  PS2_RAM_PTR(std::uint8_t, 0x0018264F0u)

// Paquete GIF estatico de 240 bytes (0x002C4040).
#define g_static_gif_packet PS2_RAM_PTR(std::uint8_t, 0x002C4040u)
constexpr std::uint32_t kStaticGifPacketSize = 240u;

// Listas de tags DMA construidas por FUN_00146D30.
#define g_dma_tags_display PS2_RAM_PTR(std::uint32_t, 0x0018223E0u)
#define g_dma_tags_0f      PS2_RAM_PTR(std::uint32_t, 0x0018223A0u)
#define g_dma_tags_0f2     PS2_RAM_PTR(std::uint32_t, 0x001822360u)
#define g_dma_tags_other   PS2_RAM_PTR(std::uint32_t, 0x0018223C0u)

// Punteros que main guarda en los globals uGpffff945c / uGpffff9458.
#define g_ptr_17e2360 PS2_RAM_U32(0x0017E2360u)
#define g_ptr_17e0360 PS2_RAM_U32(0x0017E0360u)

// ---------------------------------------------------------------------------
// Cargador (FUN_00124100 / FUN_001242A0 / FUN_00124D90)
// ---------------------------------------------------------------------------

// Entrada de 0x18 bytes de la cola de peticiones.
struct LoaderQueueEntry {
    std::uint8_t type;
    std::uint8_t flags;
    std::uint16_t file_index;
    std::uint32_t lba;
    std::uint32_t field_08;
    std::uint32_t field_0c;
    std::uint32_t destination;
    std::uint32_t remaining;
};
static_assert(sizeof(LoaderQueueEntry) == 0x18,
              "LoaderQueueEntry debe medir 0x18 bytes");

#define g_loader_queue  PS2_RAM_PTR(LoaderQueueEntry, 0x001847790u) // 8 entradas
#define g_loader_context PS2_RAM_PTR(std::uint8_t, 0x0018479F0u)   // 0x88 bytes
#define g_load_state    PS2_RAM_U8 (0x0018479F4u)                  // contexto+4
#define g_queue_flag    PS2_RAM_U32(0x001849478u)
#define g_queue_tail    PS2_RAM_U32(0x001849484u)
#define g_queue_head    PS2_RAM_U32(0x001849488u)

// Tablas del cargador.
#define g_iso_file_table   PS2_RAM_CPTR(std::uint8_t,  0x002B4660u)
#define g_load_state_table PS2_RAM_CPTR(std::uint32_t, 0x002BA0E0u) // 24
#define g_opcode_table_a   PS2_RAM_CPTR(std::uint32_t, 0x002BA120u) // 128
#define g_opcode_table_b   PS2_RAM_CPTR(std::uint32_t, 0x002BA1B0u) // 128
#define g_thread_table     PS2_RAM_CPTR(std::uint32_t, 0x002C3CB8u) // 2

// ---------------------------------------------------------------------------
// FUN_00149BF0: init de display/draw y constantes
// ---------------------------------------------------------------------------

#define uGpffff9410 PS2_RAM_U8 (0x001849410u)
#define sGpffff9404 PS2_RAM_U16(0x001849404u)
#define sGpffff9400 PS2_RAM_U16(0x001849400u)
#define sGpffff940c PS2_RAM_U16(0x00184940Cu)
#define sGpffff9408 PS2_RAM_U16(0x001849408u)
#define bGpffff93f4 PS2_RAM_U8 (0x0018493F4u)

#define DAT_002c40f0 PS2_RAM_U64(0x002C40F0u)
#define DAT_002c4100 PS2_RAM_U64(0x002C4100u)
#define uRam01826700 PS2_RAM_U64(0x001826700u)
#define uRam01826590 PS2_RAM_U64(0x001826590u)

#define uGpffff93e0 PS2_RAM_U32(0x0018493E0u)
#define uGpffff93f0 PS2_RAM_U8 (0x0018493F0u)
#define uGpffff93ec PS2_RAM_U8 (0x0018493ECu)
#define uGpffff93e8 PS2_RAM_U8 (0x0018493E8u)
#define uGpffff93e4 PS2_RAM_U8 (0x0018493E4u)

#define uGpffff939c PS2_RAM_U32(0x00184939Cu)
#define uGpffff92d0 PS2_RAM_U32(0x0018492D0u)
#define uGpffff92cc PS2_RAM_U32(0x0018492CCu)
#define uGpffff92c8 PS2_RAM_U32(0x0018492C8u)

// Tabla de 0x1000 entradas generada por FUN_00117520.
#define g_table_898c50 PS2_RAM_PTR(std::uint32_t, 0x000898C50u)

// ---------------------------------------------------------------------------
// DMA
// ---------------------------------------------------------------------------

#define g_dma_channels        PS2_RAM_PTR(std::uint32_t, 0x0018263E0u) // 10
#define g_current_dma_channel PS2_RAM_U32(0x0018263E8u)

// ---------------------------------------------------------------------------
// Nombres de modulos IOP (FUN_00123FF0 / FUN_00124060)
// ---------------------------------------------------------------------------

#define g_module_path    PS2_RAM_CPTR(char, 0x002B9EC0u) // "cdrom0:\\MODULES\\"
#define g_module_path2   PS2_RAM_CPTR(char, 0x002B9FE0u)
#define g_module_config  PS2_RAM_CPTR(std::uint8_t, 0x002B9FC0u)
#define g_snddrvcd_name  PS2_RAM_CPTR(char, 0x002DC3C0u) // "SNDDRVCD.IRX"

#define g_module_sio2man PS2_RAM_CPTR(char, 0x002DCC10u)
#define g_module_sio2d   PS2_RAM_CPTR(char, 0x002DCC20u)
#define g_module_dbcman  PS2_RAM_CPTR(char, 0x002DCC30u)
#define g_module_ds2u    PS2_RAM_CPTR(char, 0x002DCC40u)
#define g_module_mcman   PS2_RAM_CPTR(char, 0x002DCC50u)
#define g_module_mcserv  PS2_RAM_CPTR(char, 0x002DCC60u)
#define g_module_libsd   PS2_RAM_CPTR(char, 0x002DCC70u)
#define g_module_modhsyn PS2_RAM_CPTR(char, 0x002DCC80u)
#define g_module_modmidi PS2_RAM_CPTR(char, 0x002DCC90u)
#define g_module_modmsin PS2_RAM_CPTR(char, 0x002DCCA0u)
#define g_module_sdrdrv  PS2_RAM_CPTR(char, 0x002DCCB0u)

// ---------------------------------------------------------------------------
// Punteros a codigo usados por main (no son datos de ps2_ram)
// ---------------------------------------------------------------------------

constexpr std::uint32_t kVBlankCallback = 0x0014A390u;    // FUN_00100E40
constexpr std::uint32_t kLoaderThreadCallback = 0x00144630u; // FUN_00155130

// ---------------------------------------------------------------------------
// Hardware GS: fuera de ps2_ram
// ---------------------------------------------------------------------------

// No usar PS2_RAM_PTR con estas direcciones. ps2_mem las intercepta.
constexpr std::uint32_t kGsCsr      = 0x12001000u;
constexpr std::uint32_t kGsDisplay1 = 0x12000080u;
constexpr std::uint32_t kGsDisplay2 = 0x12000090u;

// ---------------------------------------------------------------------------
// Comprobaciones de rango
// ---------------------------------------------------------------------------

static_assert(0x001849488u < ps2::globals::kRamSize,
              "globals de main fuera de ps2_ram");
static_assert(0x002C4040u + kStaticGifPacketSize <= ps2::globals::kRamSize,
              "paquete GIF fuera de ps2_ram");
static_assert(0x002DCC10u < ps2::globals::kRamSize,
              "nombres de modulos fuera de ps2_ram");
static_assert(0x000898C50u + 0x1000u * 4u <= ps2::globals::kRamSize,
              "tabla 0x898C50 fuera de ps2_ram");
