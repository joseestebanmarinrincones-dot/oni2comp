// Prueba de room_overlay.cpp sin SDL ni GL.
// Compila con: clang++ -std=c++17 -O2 -I src tests/room_overlay_test.cpp
//              src/room_overlay.cpp

#include "room_overlay.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <unordered_map>
#include <vector>

namespace {

struct FakePacket {
    std::unordered_map<std::uint32_t, std::uint16_t> u16;
    std::unordered_map<std::uint32_t, std::uint32_t> u32;
    onimusha2::room::PacketCallback callback = onimusha2::room::PacketCallback::None;
};

struct FakeServices final : onimusha2::room::RoomServices {
    bool ready = true;
    std::uint8_t phase = 0;
    std::unordered_map<std::uint32_t, std::uint8_t> mem8;
    std::unordered_map<std::uint32_t, std::uint32_t> mem32;
    std::map<std::uint32_t, FakePacket> packets;
    std::vector<std::uint32_t> calls;
    std::uint32_t next_packet = 0x1000;
    int allocations = 0;
    onimusha2::room::Variant registered_variant{};
    onimusha2::room::FrameFunction registered_frame = nullptr;

    bool input_ready() override { return ready; }
    std::uint8_t boot_phase() override { return phase; }

    std::uint8_t read_u8(std::uint32_t address) override {
        const auto it = mem8.find(address);
        return it == mem8.end() ? 0 : it->second;
    }
    std::uint32_t read_u32(std::uint32_t address) override {
        const auto it = mem32.find(address);
        return it == mem32.end() ? 0 : it->second;
    }
    void write_u8(std::uint32_t address, std::uint8_t value) override {
        mem8[address] = value;
    }
    void write_u32(std::uint32_t address, std::uint32_t value) override {
        mem32[address] = value;
    }
    void call(std::uint32_t address, std::uint32_t, std::uint32_t, std::uint32_t) override {
        calls.push_back(address);
    }

    onimusha2::room::PacketHandle allocate_packet(std::uint64_t, std::uint64_t,
                                                  std::uint32_t) override {
        ++allocations;
        return next_packet++;
    }
    void set_packet_u16(onimusha2::room::PacketHandle packet, std::uint32_t offset,
                        std::uint16_t value) override {
        packets[packet].u16[offset] = value;
    }
    void set_packet_u32(onimusha2::room::PacketHandle packet, std::uint32_t offset,
                        std::uint32_t value) override {
        packets[packet].u32[offset] = value;
    }
    void set_packet_callback(onimusha2::room::PacketHandle packet,
                             onimusha2::room::PacketCallback callback) override {
        packets[packet].callback = callback;
    }
    void set_frame_callback(onimusha2::room::Variant variant,
                            onimusha2::room::FrameFunction function) override {
        registered_variant = variant;
        registered_frame = function;
    }

    std::uint32_t packet_u32(onimusha2::room::PacketHandle packet, std::uint32_t offset) const {
        const auto packet_it = packets.find(packet);
        if (packet_it == packets.end()) return 0;
        const auto field = packet_it->second.u32.find(offset);
        return field == packet_it->second.u32.end() ? 0 : field->second;
    }
    std::uint16_t packet_u16(onimusha2::room::PacketHandle packet, std::uint32_t offset) const {
        const auto packet_it = packets.find(packet);
        if (packet_it == packets.end()) return 0;
        const auto field = packet_it->second.u16.find(offset);
        return field == packet_it->second.u16.end() ? 0 : field->second;
    }
};

void set_name(onimusha2::room::Context &context, const char *name) {
    std::memcpy(context.name(), name, 5);
}

bool has_call(const FakeServices &services, std::uint32_t address) {
    for (std::uint32_t call : services.calls) {
        if (call == address) return true;
    }
    return false;
}

}  // namespace

int main() {
    using namespace onimusha2::room;

    // ------------------------------------------------------------------ Room010
    {
        Context ctx;
        ctx.clear();
        ctx.set_enable(1);
        ctx.set_resource(0, 0x200);
        ctx.set_resource6cc(0x300);
        ctx.set_resource6d4(0x400);
        set_name(ctx, "E000C");

        FakeServices services;
        services.next_packet = 0x1000;
        Room010Overlay::Initialize(ctx, services);
        if (services.registered_variant != Variant::Room010Logos) return 1;
        if (services.registered_frame == nullptr) return 2;
        if (!has_call(services, 0x001A6240u) || !has_call(services, 0x00178EE0u) ||
            !has_call(services, 0x001A8D40u)) return 3;

        Room010Overlay::Frame(ctx, services);
        if (ctx.state() != 2) return 4;
        if (services.allocations != 4) return 5;
        if (ctx.packet_a() != 0) return 6;
        if (ctx.packet_b() == 0 || ctx.packet_c() == 0 || ctx.packet_d() == 0) return 7;
        if (services.packet_u32(ctx.packet_b(), 0x90) != 0x200) return 8;
        if (services.packet_u32(ctx.packet_c(), 0x90) != 0x300) return 9;
        if (services.packet_u32(ctx.packet_d(), 0x90) != 0x400) return 10;
        if (services.packet_u16(ctx.packet_c(), 0x1C) != 0xFFFF) return 11;
        if (services.packets[ctx.packet_b()].callback != PacketCallback::LogoA) return 12;
        if (services.packets[ctx.packet_c()].callback != PacketCallback::LogoB) return 13;

        services.phase = 1;
        Room010Overlay::Frame(ctx, services);
        if (ctx.state() != 3) return 14;
        if (services.packet_u32(ctx.packet_b(), 0x98) != 0) return 15;
        if (services.packet_u32(ctx.packet_c(), 0x98) != 0) return 16;

        services.phase = 2;
        Room010Overlay::Frame(ctx, services);
        if (ctx.state() != 4) return 17;
        if (services.packet_u32(ctx.packet_d(), 0x98) != 7) return 18;

        services.phase = 3;
        Room010Overlay::Frame(ctx, services);
        if (ctx.state() != 5) return 19;
        if (services.packet_u32(ctx.packet_d(), 0x98) != 0) return 20;

        services.phase = 4;
        Room010Overlay::Frame(ctx, services);
        if (ctx.state() != 6) return 21;
        services.phase = 5;
        Room010Overlay::Frame(ctx, services);
        if (ctx.state() != 7) return 22;

        services.phase = 6;
        Room010Overlay::Frame(ctx, services);
        if (ctx.state() != 8) return 23;
        if (services.packet_u32(ctx.packet_b(), 0x98) != 0x15) return 24;

        services.phase = 7;
        Room010Overlay::Frame(ctx, services);
        if (ctx.state() != 9) return 25;
        if (services.packet_u32(ctx.packet_b(), 0x98) != 0) return 26;

        // El nombre de evento no coincide: no debe ejecutar la maquina.
        Context wrong;
        wrong.clear();
        wrong.set_enable(1);
        wrong.set_state(0);
        set_name(wrong, "E000D");
        const int before = services.allocations;
        Room010Overlay::Frame(wrong, services);
        if (wrong.state() != 0 || services.allocations != before) return 27;
    }

    // ------------------------------------------------------------------ Room000
    {
        Context ctx;
        ctx.clear();
        ctx.set_enable(1);
        ctx.set_arg10(0);
        ctx.set_resource(0, 0x500);
        ctx.set_resource(1, 0x504);
        ctx.set_resource(2, 0x508);
        ctx.set_resource6cc(0x50C);
        set_name(ctx, "E000D");

        FakeServices services;
        services.next_packet = 0x2000;
        Room000Overlay::Initialize(ctx, services);
        if (services.registered_variant != Variant::Room000Menu) return 28;
        if (services.registered_frame == nullptr) return 29;
        if (!has_call(services, 0x00135C30u) || !has_call(services, 0x001A8D40u)) return 30;

        Room000Overlay::Frame(ctx, services);
        if (ctx.state() != 2) return 31;
        if (services.allocations != 2) return 32;
        if (services.packet_u32(0x2001, 0x90) != 0x50C) return 33;
        if (services.packet_u32(0x2001, 0x98) != 0x0064FF0Cu) return 34;
        if (services.packet_u32(0x2001, 0x9C) != 0x0032FF0Eu) return 35;
        if (services.packet_u32(0x2001, 0xA0) != 0x0032FF13u) return 36;

        ctx.set_enable(7);
        Room000Overlay::Frame(ctx, services);
        if (ctx.state() != 3) return 37;
        if (services.allocations != 5) return 38;
        if (services.packet_u32(0x2002, 0x90) != 0x500) return 39;
        if (services.packet_u32(0x2003, 0x90) != 0x504) return 40;
        if (services.packet_u32(0x2004, 0x90) != 0x508) return 41;
    }

    std::printf("PASS rooms: Room010 E000C 0->9, Room000 E000D 0->3, "
                "4+2+3 paquetes\n");
    return 0;
}
