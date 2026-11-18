#pragma once

// Capa de acceso a memoria PS2 para el codigo descompilado.
//
// RAM principal:
//   0x00000000 .. 0x01FFFFFF  -> ps2_ram[32 MB]
//
// Hardware interceptado:
//   0x10000000 .. 0x12001FFF  -> DMA, INTC, timers y GS
//   (GS empieza en 0x12000000; se incluye el bloque privilegiado completo)
//
// Las escrituras a hardware no tocan ps2_ram: se guardan en un shadow de
// registros y se registran en Logcat con el tag PS2HW. Las lecturas devuelven
// el ultimo valor escrito, salvo que se instale un HardwareReadHook.
//
// Uso con la sintaxis pedida:
//
//   uint32_t v = PS2_MEM_READ<uint32_t>(0x00100000);
//   PS2_MEM_WRITE<uint32_t>(0x00100000, v);
//   PS2_MEM_WRITE<uint64_t>(0x12001000, 0x123456789ABCDEF0ull);
//
// PS2_MEM_READ/PS2_MEM_WRITE son macros object-like que expanden al template;
// por eso funciona PS2_MEM_READ<T>(addr) y no hace falta pasar el tipo como
// argumento de macro.
//
// Direcciones KSEG0 (0x80000000+) y KSEG1 (0xA0000000+) se normalizan a su
// direccion fisica antes de resolver RAM/hardware.
//
// La ventana alta de overlays 0x02000000..0x023FFFFF NO esta en este array;
// la gestiona OverlayManager. El scratchpad 0x70000000 tampoco esta cubierto.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace ps2 {

inline constexpr std::uint32_t kPs2RamSize = 32u * 1024u * 1024u;

alignas(64) extern std::uint8_t ps2_ram[kPs2RamSize];

class Ps2Memory {
public:
    static constexpr std::uint32_t kRamBase = 0x00000000u;
    static constexpr std::uint32_t kRamSize = kPs2RamSize;
    static constexpr std::uint32_t kHwBegin = 0x10000000u;
    static constexpr std::uint32_t kHwEnd = 0x12002000u;

    enum class HwKind : std::uint8_t {
        None = 0,
        Dma,
        Gs,
        Intc,
        Timer,
        Other,
    };

    struct HardwareAccess {
        std::uint32_t address = 0;
        std::uint64_t value = 0;
        std::uint8_t size = 0;
        HwKind kind = HwKind::None;
    };

    using HardwareWriteHook = void (*)(const HardwareAccess &);
    using HardwareReadHook = bool (*)(const HardwareAccess &, void *out);

    static std::uint8_t *Ram() { return ps2_ram; }
    static std::uint32_t Size() { return kRamSize; }

    static void Reset();

    static bool IsRamAddress(std::uint32_t address);
    static bool IsHardwareAddress(std::uint32_t address);
    static std::uint32_t PhysicalAddress(std::uint32_t address);
    static HwKind Classify(std::uint32_t address);

    static void SetHardwareLogging(bool enabled);
    static bool HardwareLogging();
    static void SetHardwareWriteHook(HardwareWriteHook hook);
    static void SetHardwareReadHook(HardwareReadHook hook);

    static void ReadBytes(std::uint32_t address, void *out, std::size_t size);
    static void WriteBytes(std::uint32_t address, const void *data, std::size_t size);

    template <typename T>
    static T Read(std::uint32_t address) {
        static_assert(std::is_trivially_copyable<T>::value,
                      "PS2_MEM_READ necesita un tipo trivially copyable");
        T value{};
        ReadBytes(address, &value, sizeof(T));
        return value;
    }

    template <typename T>
    static void Write(std::uint32_t address, T value) {
        static_assert(std::is_trivially_copyable<T>::value,
                      "PS2_MEM_WRITE necesita un tipo trivially copyable");
        WriteBytes(address, &value, sizeof(T));
    }
};

namespace mem {

template <typename T>
inline T read(std::uint32_t address) {
    return Ps2Memory::Read<T>(address);
}

template <typename T>
inline void write(std::uint32_t address, T value) {
    Ps2Memory::Write<T>(address, value);
}

inline void read_bytes(std::uint32_t address, void *out, std::size_t size) {
    Ps2Memory::ReadBytes(address, out, size);
}

inline void write_bytes(std::uint32_t address, const void *data, std::size_t size) {
    Ps2Memory::WriteBytes(address, data, size);
}

}  // namespace mem
}  // namespace ps2

// Sintaxis pedida: PS2_MEM_READ<T>(addr), PS2_MEM_WRITE<T>(addr, val).
#define PS2_MEM_READ ps2::mem::read
#define PS2_MEM_WRITE ps2::mem::write

// Alternativa C-style si se prefiere pasar el tipo como argumento.
#define PS2_MEM_READ_T(type, addr) ps2::mem::read<type>(addr)
#define PS2_MEM_WRITE_T(type, addr, value) ps2::mem::write<type>(addr, value)
