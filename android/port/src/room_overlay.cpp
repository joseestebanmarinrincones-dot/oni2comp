#include "room_overlay.hpp"

#include <cstring>

namespace onimusha2::room {
namespace {

// Globales del ejecutable principal usados por los overlays (gp = 0x1850000).
constexpr std::uint32_t kGp932C = 0x00184932Cu;
constexpr std::uint32_t kGp9328 = 0x001849328u;
constexpr std::uint32_t kGp9308 = 0x001849308u;
constexpr std::uint32_t kGp9320 = 0x001849320u;
constexpr std::uint32_t kGp9344 = 0x001849344u;
constexpr std::uint32_t kGp935C = 0x00184935Cu;
constexpr std::uint32_t kGp9358 = 0x001849358u;
constexpr std::uint32_t kGp9368 = 0x001849368u;
constexpr std::uint32_t kGp9364 = 0x001849364u;
constexpr std::uint32_t kGp92E4 = 0x0018492E4u;
constexpr std::uint32_t kGp9310 = 0x001849310u;

constexpr std::uint32_t kRoomMode = 0x0017DE84Cu;
constexpr std::uint32_t kEventFlag = 0x0017E7DAu;
constexpr std::uint32_t kRoom000Table = 0x0017DC0E0u;

constexpr std::uint64_t kPacketTagRoom010Base = 0x40007020022ull;
constexpr std::uint64_t kPacketTagRoom010Stream = 0x4000FF0022ull;
constexpr std::uint64_t kPacketTagRoom000 = 0x40004020022ull;
constexpr std::uint64_t kPacketArgA = 0x12C000000000000ull;
constexpr std::uint64_t kPacketArgB = 0xFED4000000000000ull;

constexpr std::uint8_t kRoom010Name[5] = {'E', '0', '0', '0', 'C'};
constexpr std::uint8_t kRoom000Name[5] = {'E', '0', '0', '0', 'D'};

void set_packet_state(RoomServices &services, PacketHandle packet, std::uint32_t state) {
    // Las esperas del original escriben en el paquete; si el recurso no se
    // creo, no se debe tocar la direccion 0.
    if (packet != 0) services.set_packet_u32(packet, 0x98, state);
}

void set_packet_u16_if(RoomServices &services, PacketHandle packet,
                       std::uint32_t offset, std::uint16_t value) {
    if (packet != 0) services.set_packet_u16(packet, offset, value);
}

void set_packet_u32_if(RoomServices &services, PacketHandle packet,
                       std::uint32_t offset, std::uint32_t value) {
    if (packet != 0) services.set_packet_u32(packet, offset, value);
}

}  // namespace

// ---------------------------------------------------------------------------
// Room010: secuencia de logos (evento E000C)
// ---------------------------------------------------------------------------

void Room010Overlay::InitializeResources(RoomServices &services) {
    // FUN_01f92080
    services.write_u32(kGp932C, 0x01F92E50u);
    services.write_u32(kGp9328, 0x01F92D60u);
    services.write_u32(kGp9308, 0x01F93630u);
    services.write_u32(kGp9320, 0);
    services.write_u8(kEventFlag, 0);
    services.call(0x001A6240u, 0x01F93670u, 0);
    services.call(0x00178EE0u, 1);
}

void Room010Overlay::InitializeCallbacks(Context &, RoomServices &services) {
    // FUN_01f920d0
    services.write_u32(kGp92E4, 8);
    services.write_u32(kGp9310, 0x01F93580u);
    services.call(0x001A8D40u, 0x01F93610u);
    services.set_frame_callback(Variant::Room010Logos, &Room010Overlay::Frame);
}

void Room010Overlay::Initialize(Context &context, RoomServices &services) {
    InitializeResources(services);
    InitializeCallbacks(context, services);
}

void Room010Overlay::allocate_packets(Context &context, RoomServices &services) {
    // FUN_00137450(0x40007020022, 0, 0): paquete base. El original no guarda
    // el puntero; solo limpia ctx+0x788.
    PacketHandle packet = services.allocate_packet(kPacketTagRoom010Base, 0, 0);
    if (packet != 0) {
        set_packet_u16_if(services, packet, 0x18, 0);
        context.set_packet_a(0);
    }

    const std::uint32_t resource0 = context.resource(0);
    if (resource0 != 0) {
        packet = services.allocate_packet(kPacketTagRoom010Stream, kPacketArgB, 0);
        if (packet != 0) {
            services.set_packet_callback(packet, PacketCallback::LogoA);
            set_packet_u16_if(services, packet, 0x18, 0);
            set_packet_u32_if(services, packet, 0x90, resource0);
            set_packet_u32_if(services, packet, 0x98, 0);
            context.set_packet_b(packet);
        }
    }

    const std::uint32_t resource1 = context.resource6cc();
    if (resource1 != 0) {
        packet = services.allocate_packet(kPacketTagRoom010Stream, kPacketArgA, 0);
        if (packet != 0) {
            services.set_packet_callback(packet, PacketCallback::LogoB);
            set_packet_u16_if(services, packet, 0x18, 0);
            set_packet_u16_if(services, packet, 0x1C, 0xFFFFu);
            set_packet_u32_if(services, packet, 0x90, resource1);
            set_packet_u32_if(services, packet, 0x98, 7);
            context.set_packet_c(packet);
        }
    }

    const std::uint32_t resource2 = context.resource6d4();
    if (resource2 != 0) {
        packet = services.allocate_packet(kPacketTagRoom010Stream, kPacketArgB, 0);
        if (packet != 0) {
            services.set_packet_callback(packet, PacketCallback::LogoB);
            set_packet_u16_if(services, packet, 0x18, 0);
            set_packet_u32_if(services, packet, 0x90, resource2);
            set_packet_u32_if(services, packet, 0x98, 0);
            context.set_packet_d(packet);
        }
    }

    context.set_state(static_cast<std::uint8_t>(context.state() + 1));
}

void Room010Overlay::Frame(Context &context, RoomServices &services) {
    // Wrapper FUN_01f92110: FUN_00188BD0(0x80) y nombre "E000C".
    if (!services.input_ready()) return;
    if (std::memcmp(context.name(), kRoom010Name, sizeof(kRoom010Name)) != 0) return;

    switch (context.state()) {
    case 0:
        context.set_packet_d(0);
        context.set_packet_c(0);
        context.set_packet_b(0);
        context.set_packet_a(0);
        context.set_state(1);
        [[fallthrough]];
    case 1:
        if (context.enable() == 0) return;
        allocate_packets(context, services);
        return;

    case 2:
        if (services.boot_phase() != 1) return;
        set_packet_state(services, context.packet_b(), 0);
        set_packet_state(services, context.packet_c(), 0);
        context.set_state(3);
        return;

    case 3:
        if (services.boot_phase() != 2) return;
        set_packet_state(services, context.packet_d(), 7);
        context.set_state(4);
        return;

    case 4:
        if (services.boot_phase() != 3) return;
        set_packet_state(services, context.packet_d(), 0);
        context.set_state(5);
        return;

    case 5:
        if (services.boot_phase() != 4) return;
        context.set_state(6);
        return;

    case 6:
        if (services.boot_phase() != 5) return;
        context.set_state(7);
        return;

    case 7:
        if (services.boot_phase() != 6) return;
        set_packet_state(services, context.packet_b(), 0x15);
        context.set_state(8);
        return;

    case 8:
        if (services.boot_phase() != 7) return;
        set_packet_state(services, context.packet_b(), 0);
        context.set_state(9);
        return;

    default:
        return;
    }
}

// ---------------------------------------------------------------------------
// Room000: menu principal (evento E000D)
// ---------------------------------------------------------------------------

void Room000Overlay::InitializeResources(RoomServices &services) {
    // FUN_01f92080
    const std::uint8_t mode = services.read_u8(kRoomMode);
    services.write_u32(kGp9344, mode == 2 ? 0x01F92640u : 0x01F92540u);
    services.write_u8(kGp935C, 0);
    services.write_u8(kGp9358, 0);
    services.write_u32(kGp9368, 0);
    services.write_u32(kGp9364, 0);
}

void Room000Overlay::InitializeCallbacks(Context &, RoomServices &services) {
    // FUN_01f920d0
    services.write_u32(kGp932C, 0x01F92950u);
    services.write_u32(kGp9328, 0x01F92800u);
    services.write_u32(kGp9308, 0x01F92F10u);
    services.write_u32(kGp9320, 0);
    InitializeResources(services);
    services.call(0x001A6240u, 0x01F92F80u, 0);
    services.call(0x00178EE0u, 1);
    services.call(0x00135C30u, 0x01F92F40u);
    services.call(0x001A8D40u, 0x01F92EF0u);
    services.write_u32(kGp92E4, 8);
    services.write_u32(kGp9310, 0x01F92E80u);
    services.set_frame_callback(Variant::Room000Menu, &Room000Overlay::Frame);
}

void Room000Overlay::Initialize(Context &context, RoomServices &services) {
    InitializeCallbacks(context, services);
}

void Room000Overlay::resolve_descriptors(RoomServices &services, std::uint32_t base,
                                         PacketHandle packet) {
    // Bloque comun de FUN_01f921d0: resuelve tres punteros a partir de la
    // tabla +0x24C0 usando los indices de +0x24BD y de la estructura +0x24C4.
    const std::uint32_t array = services.read_u32(base + 0x24C0u);
    const std::int32_t index =
        static_cast<std::int8_t>(services.read_u8(base + 0x24BDu));
    const std::uint32_t first =
        services.read_u32(array + static_cast<std::uint32_t>(index) * 4u);
    set_packet_u32_if(services, packet, 0x98, first);

    const std::uint32_t descriptor = services.read_u32(base + 0x24C4u);

    std::uint32_t pointer = services.read_u32(descriptor + 0x10u);
    std::uint32_t sub_index = services.read_u8(pointer);
    const std::uint32_t second = services.read_u32(array + sub_index * 4u);
    set_packet_u32_if(services, packet, 0x9C, second);

    pointer = services.read_u32(descriptor + 0x14u);
    sub_index = services.read_u8(pointer);
    const std::uint32_t third = services.read_u32(array + sub_index * 4u);
    set_packet_u32_if(services, packet, 0xA0, third);
}

void Room000Overlay::setup_state1(Context &context, RoomServices &services) {
    // FUN_00137450(0x40004020022, 0, 0) + tabla global 0x17DC0E0.
    PacketHandle packet = services.allocate_packet(kPacketTagRoom000, 0, 0);
    if (packet != 0) {
        set_packet_u16_if(services, packet, 0x18, 0);
        set_packet_u32_if(services, packet, 0x90, kRoom000Table);
        set_packet_u32_if(services, packet, 0x94, 0);
        resolve_descriptors(services, kRoom000Table, packet);
    }

    const std::uint32_t resource = context.resource6cc();
    if (resource != 0) {
        packet = services.allocate_packet(kPacketTagRoom000, 0, 0);
        if (packet != 0) {
            set_packet_u16_if(services, packet, 0x18, 0);
            set_packet_u32_if(services, packet, 0x90, resource);
            set_packet_u32_if(services, packet, 0x94, 0);
            set_packet_u32_if(services, packet, 0x98, 0x0064FF0Cu);
            set_packet_u32_if(services, packet, 0x9C, 0x0032FF0Eu);
            set_packet_u32_if(services, packet, 0xA0, 0x0032FF13u);
        }
    }
}

void Room000Overlay::setup_state2(Context &context, RoomServices &services) {
    for (std::size_t i = 0; i < 3; ++i) {
        const std::uint32_t resource = context.resource(i);
        if (resource == 0) return;   // el original espera, no salta el recurso
        PacketHandle packet = services.allocate_packet(kPacketTagRoom000, 0, 0);
        if (packet != 0) {
            set_packet_u16_if(services, packet, 0x18, 0);
            set_packet_u32_if(services, packet, 0x90, resource);
            set_packet_u32_if(services, packet, 0x94, 0);
            resolve_descriptors(services, resource, packet);
        }
    }
}

void Room000Overlay::Frame(Context &context, RoomServices &services) {
    // Wrapper FUN_01f92170: FUN_00188BD0(0x80) y nombre "E000D".
    if (!services.input_ready()) return;
    if (std::memcmp(context.name(), kRoom000Name, sizeof(kRoom000Name)) != 0) return;

    switch (context.state()) {
    case 0:
        if (context.enable() == 0) return;
        context.set_state(1);
        [[fallthrough]];

    case 1:
        if (context.enable() != 1) return;
        if (static_cast<std::int32_t>(context.arg10()) < 0) return;
        setup_state1(context, services);
        context.set_state(2);
        return;

    case 2:
        if (context.enable() != 7) return;
        if (static_cast<std::int32_t>(context.arg10()) < 0) return;
        setup_state2(context, services);
        context.set_state(3);
        return;

    default:
        return;
    }
}

}  // namespace onimusha2::room
