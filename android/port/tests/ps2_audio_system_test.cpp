// Prueba del decodificador PS2 ADPCM y del mezclador Ps2AudioSystem sin SDL.
// Compila con:
//   clang++ -std=c++17 -O2 -Isrc tests/ps2_audio_system_test.cpp `
//     src/ps2_audio_system.cpp -o audio_system_test.exe

#include "ps2_audio_system.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char *name) {
    std::printf("  %-52s %s\n", name, condition ? "PASS" : "FAIL");
    if (!condition) ++failures;
}

bool nearly(std::int16_t value, int expected, int tolerance = 1) {
    return std::abs(static_cast<int>(value) - expected) <= tolerance;
}

std::array<std::uint8_t, 16> constant_block(std::uint8_t shift,
                                            std::uint8_t filter,
                                            std::uint8_t flags,
                                            std::uint8_t data) {
    std::array<std::uint8_t, 16> block{};
    block[0] = static_cast<std::uint8_t>((filter << 4) | (shift & 0x0F));
    block[1] = flags;
    std::memset(block.data() + 2, data, 14);
    return block;
}

void test_decoder() {
    std::printf("\n=== Decodificador PS2 ADPCM ===\n");

    std::uint8_t block[16]{};
    block[0] = 0x0C;  // shift 12, filter 0
    block[1] = 0x00;
    block[2] = 0x21;
    block[3] = 0x43;

    const auto decoded = ps2::audio::Ps2AudioSystem::DecodeBlock(block);
    check(decoded.shift == 12 && decoded.filter == 0,
          "cabecera shift/filter");
    check(decoded.flags == 0, "flags");
    check(nearly(decoded.samples[0], 1) && nearly(decoded.samples[1], 2) &&
              nearly(decoded.samples[2], 3) && nearly(decoded.samples[3], 4),
          "nibbles bajos primero (filtro 0)");

    block[0] = 0x1C;  // shift 12, filter 1
    const auto filtered = ps2::audio::Ps2AudioSystem::DecodeBlock(block);
    check(nearly(filtered.samples[0], 1), "filtro 1 muestra 0");
    check(nearly(filtered.samples[1], 3), "filtro 1 prediccion 60");
    check(nearly(filtered.samples[2], 6), "filtro 1 tercera muestra");

    block[0] = 0x2C;  // shift 12, filter 2
    const auto filtered2 = ps2::audio::Ps2AudioSystem::DecodeBlock(block);
    check(nearly(filtered2.samples[0], 1), "filtro 2 muestra 0");
    check(nearly(filtered2.samples[1], 4), "filtro 2 prediccion 115");
    check(nearly(filtered2.samples[2], 9), "filtro 2 prediccion -52");

    block[1] = 0x07;
    const auto flags = ps2::audio::Ps2AudioSystem::DecodeBlock(block);
    check(flags.flags == 7, "loop start/loop/loop end");

    // La prediccion se conserva entre bloques.
    int hist1 = 0;
    int hist2 = 0;
    block[0] = 0x1C;
    block[1] = 0;
    std::int16_t out[28]{};
    ps2::audio::Ps2AudioSystem::DecodeBlock(block, out, hist1, hist2);
    check(nearly(out[0], 1) && hist1 == out[27], "historial tras bloque");
}

void test_voice_mix() {
    std::printf("\n=== Mezcla de voces SPU2 ===\n");

    ps2::audio::Ps2AudioSystem audio;
    audio.Reset();

    const auto block = constant_block(0, 0, 0, 0x11);  // 4096 por muestra
    audio.WriteSpu2(0x1000, block.data(), block.size());

    using namespace ps2::audio;
    audio.SetAddr(sd::Voice(0, 0) | sd::kVAddrSsa, 0x1000);
    audio.SetParam(sd::Voice(0, 0) | sd::kVParamVoll, 0x4000);
    audio.SetParam(sd::Voice(0, 0) | sd::kVParamVolr, 0x4000);
    audio.SetParam(sd::Voice(0, 0) | sd::kVParamPitch, 0x1000);
    audio.SetSwitch(sd::kSwitchKon, 1u);

    std::vector<std::int16_t> pcm(28 * 2, 0);
    audio.RenderPcm(pcm.data(), 28);
    check(nearly(pcm[0], 4096, 2) && nearly(pcm[1], 4096, 2),
          "voz mono centrada a L/R");
    check(nearly(pcm[20], 4096, 2), "voz sigue en la muestra 10");

    audio.KeyOff(0);
    check(!audio.IsVoiceActive(0), "key off sin ADSR desactiva");

    // Paneo: solo izquierda.
    audio.Reset();
    audio.WriteSpu2(0x1000, block.data(), block.size());
    audio.SetAddr(sd::Voice(0, 1) | sd::kVAddrSsa, 0x1000);
    audio.SetParam(sd::Voice(0, 1) | sd::kVParamVoll, 0x4000);
    audio.SetParam(sd::Voice(0, 1) | sd::kVParamVolr, 0x0000);
    audio.SetSwitch(sd::kSwitchKon, 1u << 1);
    audio.RenderPcm(pcm.data(), 8);
    check(nearly(pcm[0], 4096, 2) && pcm[1] == 0, "paneo izquierdo");

    // Loop: flags loop start + loop + loop end.
    audio.Reset();
    const auto loop_block = constant_block(0, 0, 0x07, 0x11);
    audio.WriteSpu2(0x2000, loop_block.data(), loop_block.size());
    audio.SetAddr(sd::Voice(0, 2) | sd::kVAddrSsa, 0x2000);
    audio.SetParam(sd::Voice(0, 2) | sd::kVParamVoll, 0x4000);
    audio.SetParam(sd::Voice(0, 2) | sd::kVParamVolr, 0x4000);
    audio.SetSwitch(sd::kSwitchKon, 1u << 2);
    std::vector<std::int16_t> loop_pcm(120 * 2, 0);
    audio.RenderPcm(loop_pcm.data(), 120);
    check(audio.IsVoiceActive(2), "loop mantiene la voz activa");
    check(nearly(loop_pcm[100 * 2], 4096, 2), "loop sigue sonando");
}

void test_stream() {
    std::printf("\n=== Streaming ADPCM ===\n");

    ps2::audio::Ps2AudioSystem audio;
    audio.Reset();
    const auto block = constant_block(0, 0, 0, 0x11);

    check(audio.QueueStream(block.data(), block.size()), "QueueStream 1 bloque");
    check(audio.QueueStream(block.data(), block.size()), "QueueStream 2 bloques");
    check(!audio.QueueStream(block.data(), block.size() + 3),
          "rechaza bloques incompletos");

    std::vector<std::int16_t> pcm(28 * 2, 0);
    audio.RenderPcm(pcm.data(), 28);
    check(nearly(pcm[0], 4096, 2) && nearly(pcm[1], 4096, 2),
          "stream mezclado a L/R");

    audio.ClearStream();
    std::vector<std::int16_t> silence(8 * 2, 123);
    audio.RenderPcm(silence.data(), 8);
    check(silence[0] == 0 && silence[1] == 0, "ClearStream silencia");

    audio.SetStreamLoop(true);
    check(audio.QueueStream(block.data(), block.size()), "stream en loop");
    std::vector<std::int16_t> loop_pcm(120 * 2, 0);
    audio.RenderPcm(loop_pcm.data(), 120);
    check(nearly(loop_pcm[100 * 2], 4096, 2), "loop de stream");
}

void test_registers_and_transfer() {
    std::printf("\n=== Registros libsd y transferencias ===\n");

    ps2::audio::Ps2AudioSystem audio;
    audio.Reset();
    using namespace ps2::audio;

    audio.SetParam(sd::Voice(0, 5) | sd::kVParamVoll, 0x1234);
    check(audio.GetParam(sd::Voice(0, 5) | sd::kVParamVoll) == 0x1234,
          "SetParam/GetParam VOLL");
    audio.SetParam(sd::Voice(1, 23) | sd::kVParamPitch, 0x2000);
    check(audio.GetParam(sd::Voice(1, 23) | sd::kVParamPitch) == 0x2000,
          "SetParam voz core 1");
    audio.SetParam(sd::kParamMvoll, 0x2000);
    check(audio.GetParam(sd::kParamMvoll) == 0x2000, "volumen maestro");

    audio.SetSwitch(sd::kSwitchKon, 1u << 4);
    check((audio.GetSwitch(sd::kSwitchKon) & (1u << 4)) != 0,
          "KON enmascara voz");
    audio.SetSwitch(sd::kSwitchKoff, 1u << 4);
    check((audio.GetSwitch(sd::kSwitchKon) & (1u << 4)) == 0,
          "KOFF limpia voz");

    const auto block = constant_block(0, 0, 0, 0x11);
    audio.SetAddr(sd::kAddrTsa, 0x3000);
    audio.UploadVoices(block.data(), block.size());
    check(audio.TransferAddress() == 0x3010, "TSA avanza tras upload");
    std::uint8_t readback[16]{};
    audio.ReadSpu2(0x3000, readback, sizeof(readback));
    check(std::memcmp(readback, block.data(), block.size()) == 0,
          "banco visible en RAM SPU2");

    audio.WriteSpu2(static_cast<std::uint32_t>(
                        ps2::audio::Ps2AudioSystem::kSpu2RamSize - 4),
                    block.data(), 8);
    audio.ReadSpu2(static_cast<std::uint32_t>(
                       ps2::audio::Ps2AudioSystem::kSpu2RamSize - 4),
                   readback, 8);
    check(readback[0] == block[0] && readback[4] == block[4],
          "transferencia con wrap de 2 MB");
}

}  // namespace

int main() {
    test_decoder();
    test_voice_mix();
    test_stream();
    test_registers_and_transfer();

    std::printf("\nFALLOS: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
