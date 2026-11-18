#pragma once

// Traduccion C++17 de los overlays de arranque de Onimusha 2 que comparten la
// base 0x01F90000:
//
//   Room010.bin (stamp 86124295) -> evento E000C -> secuencia de logos
//   Room000.bin (stamp D3325583) -> evento E000D -> menu principal
//
// Se traduce la logica de control: init, comprobacion del evento, dispatcher y
// maquina de estados. Las funciones graficas de los paquetes GS
// (FUN_01f92460 y FUN_01f927d0) se dejan como hooks de RoomServices.
//
// El contexto es el area de trabajo de la room en RAM PS2. Solo se usan estos
// offsets, verificados en el desensamblado:
//
//   +0x010  u32  argumento de entrada (Room000)
//   +0x014  s8   enable/fase de la room
//   +0x070  u8[5] nombre del evento ("E000C" / "E000D")
//   +0x6BC  u32  recurso 0 (Room000 usa 0x6BC,0x6C0,0x6C4)
//   +0x6CC  u32  recurso 1
//   +0x6D4  u32  recurso 2
//   +0x784  u8   estado de la maquina
//   +0x788  u32  paquete A (Room010)
//   +0x78C  u32  paquete B (Room010)
//   +0x790  u32  paquete C (Room010)
//   +0x794  u32  paquete D (Room010)

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace onimusha2::room {

enum class Variant : std::uint8_t {
    Room010Logos,   // Room010.bin, evento E000C
    Room000Menu,    // Room000.bin, evento E000D
};

enum class PacketCallback : std::uint32_t {
    None = 0,
    LogoA = 0x01F92460u,   // FUN_01f92460
    LogoB = 0x01F927D0u,   // FUN_01f927d0
};

using PacketHandle = std::uint32_t;

struct Context {
    static constexpr std::size_t kSize = 0x800;

    std::array<std::uint8_t, kSize> bytes{};

    std::uint8_t read8(std::size_t offset) const {
        return offset < bytes.size() ? bytes[offset] : 0;
    }
    std::uint16_t read16(std::size_t offset) const {
        if (offset + 2 > bytes.size()) return 0;
        std::uint16_t value = 0;
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
        return value;
    }
    std::uint32_t read32(std::size_t offset) const {
        if (offset + 4 > bytes.size()) return 0;
        std::uint32_t value = 0;
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
        return value;
    }
    void write8(std::size_t offset, std::uint8_t value) {
        if (offset < bytes.size()) bytes[offset] = value;
    }
    void write16(std::size_t offset, std::uint16_t value) {
        if (offset + 2 <= bytes.size()) std::memcpy(bytes.data() + offset, &value, sizeof(value));
    }
    void write32(std::size_t offset, std::uint32_t value) {
        if (offset + 4 <= bytes.size()) std::memcpy(bytes.data() + offset, &value, sizeof(value));
    }

    std::int8_t enable() const { return static_cast<std::int8_t>(read8(0x014)); }
    void set_enable(std::int8_t value) { write8(0x014, static_cast<std::uint8_t>(value)); }

    std::uint32_t arg10() const { return read32(0x010); }
    void set_arg10(std::uint32_t value) { write32(0x010, value); }

    const std::uint8_t *name() const { return bytes.data() + 0x070; }
    std::uint8_t *name() { return bytes.data() + 0x070; }

    std::uint32_t resource(std::size_t index) const {
        return index < 3 ? read32(0x6BC + index * 4) : 0;
    }
    void set_resource(std::size_t index, std::uint32_t value) {
        if (index < 3) write32(0x6BC + index * 4, value);
    }
    std::uint32_t resource6cc() const { return read32(0x6CC); }
    void set_resource6cc(std::uint32_t value) { write32(0x6CC, value); }
    std::uint32_t resource6d4() const { return read32(0x6D4); }
    void set_resource6d4(std::uint32_t value) { write32(0x6D4, value); }

    std::uint8_t state() const { return read8(0x784); }
    void set_state(std::uint8_t value) { write8(0x784, value); }

    PacketHandle packet_a() const { return read32(0x788); }
    PacketHandle packet_b() const { return read32(0x78C); }
    PacketHandle packet_c() const { return read32(0x790); }
    PacketHandle packet_d() const { return read32(0x794); }
    void set_packet_a(PacketHandle value) { write32(0x788, value); }
    void set_packet_b(PacketHandle value) { write32(0x78C, value); }
    void set_packet_c(PacketHandle value) { write32(0x790, value); }
    void set_packet_d(PacketHandle value) { write32(0x794, value); }

    void clear() { bytes.fill(0); }
};

struct Context;
class RoomServices;
using FrameFunction = void (*)(Context &, RoomServices &);

class RoomServices {
public:
    virtual ~RoomServices() = default;

    // FUN_00188BD0(0x80) != 0
    virtual bool input_ready() = 0;

    // cRam017de832
    virtual std::uint8_t boot_phase() = 0;

    // Memoria PS2 para tablas y globales del ejecutable principal.
    virtual std::uint8_t read_u8(std::uint32_t address) = 0;
    virtual std::uint32_t read_u32(std::uint32_t address) = 0;
    virtual void write_u8(std::uint32_t address, std::uint8_t value) = 0;
    virtual void write_u32(std::uint32_t address, std::uint32_t value) = 0;

    // Llamada a una funcion del ejecutable principal.
    virtual void call(std::uint32_t address, std::uint32_t a0 = 0,
                      std::uint32_t a1 = 0, std::uint32_t a2 = 0) = 0;

    // FUN_00137450: reserva un paquete GS y devuelve su direccion PS2.
    virtual PacketHandle allocate_packet(std::uint64_t tag, std::uint64_t arg,
                                         std::uint32_t flags = 0) = 0;

    virtual void set_packet_u16(PacketHandle packet, std::uint32_t offset,
                                std::uint16_t value) = 0;
    virtual void set_packet_u32(PacketHandle packet, std::uint32_t offset,
                                std::uint32_t value) = 0;
    virtual void set_packet_callback(PacketHandle packet, PacketCallback callback) = 0;

    // Sustituye el puntero 0x0062B840 por la entrada nativa correspondiente.
    virtual void set_frame_callback(Variant variant, FrameFunction function) = 0;
};

class Room010Overlay {
public:
    // Equivale a llamar a FUN_01f92080 y despues FUN_01f920d0.
    static void Initialize(Context &context, RoomServices &services);

    // FUN_01f92080
    static void InitializeResources(RoomServices &services);

    // FUN_01f920d0
    static void InitializeCallbacks(Context &context, RoomServices &services);

    // Wrapper FUN_01f92110 + dispatcher FUN_01f92160.
    static void Frame(Context &context, RoomServices &services);

private:
    static void allocate_packets(Context &context, RoomServices &services);
};

class Room000Overlay {
public:
    // FUN_01f920d0, que internamente llama a FUN_01f92080.
    static void Initialize(Context &context, RoomServices &services);

    // FUN_01f92080
    static void InitializeResources(RoomServices &services);

    // FUN_01f920d0
    static void InitializeCallbacks(Context &context, RoomServices &services);

    // Wrapper FUN_01f92170 + maquina FUN_01f921d0.
    static void Frame(Context &context, RoomServices &services);

private:
    static void setup_state1(Context &context, RoomServices &services);
    static void setup_state2(Context &context, RoomServices &services);
    static void resolve_descriptors(RoomServices &services, std::uint32_t base,
                                    PacketHandle packet);
};

}  // namespace onimusha2::room
