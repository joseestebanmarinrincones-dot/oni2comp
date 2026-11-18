// Prueba de los stubs/HLE de audio, sin SDL ni GL.
// Compila con: clang++ -std=c++17 -O2 -I src tests/ps2_audio_hle_test.cpp
//              src/ps2_audio_hle.cpp src/ps2_audio_system.cpp

#include "ps2_audio_hle.hpp"
#include "ps2_audio_system.hpp"

#include <cstdio>
#include <cstring>

int main() {
    using namespace ps2::audio;

    if (IsStubbedAddress(0x0025C610u)) return 1;   // thunk del juego
    if (IsStubbedAddress(0x0025C620u)) return 1;   // thread del juego
    if (IsStubbedAddress(0x0025C640u)) return 1;   // flag GP del juego
    if (!IsStubbedAddress(0x0025C650u)) return 2;
    if (!IsStubbedAddress(0x0025C94Fu)) return 3;
    if (IsStubbedAddress(0x0025C950u)) return 4;   // ya es codigo de juego
    if (IsStubbedAddress(0x0025D290u)) return 5;   // graficos/juego

    if (::sceSdRemoteInit() != 0) return 6;
    if (::sceSdRemote(0x8000) != 0) return 7;
    if (::sceSdTransToIOP(nullptr, nullptr, 0, 0) != 0) return 8;
    if (::sceSdCallBack(nullptr) != nullptr) return 9;
    if (::sceSdRemoteCallbackInit(0) != 0) return 10;
    if (::sceSdRemoteCallbackQuit() != 0) return 11;
    if (::sceSdInit(0) != 0) return 12;
    if (::sceSdSetParam(0, 0) != 0) return 13;
    if (::sceSdSetSwitch(0, 0) != 0) return 14;
    if (::sceSdVoiceTrans(0, 0, nullptr, 0) != 0) return 15;
    if (::sceSdBlockTrans(0, 0, nullptr, 0, 0) != 0) return 16;
    if (::sceSdStopTrans(0) != 0) return 17;
    if (Stub(0x0025C650u) != 0) return 18;

    // sceSdTransToIOP: 16 bytes desde direccion EE 0x10 a SPU2 0x1000.
    System().Reset();
    std::uint8_t ee[64]{};
    for (int i = 0; i < 64; ++i) ee[i] = static_cast<std::uint8_t>(i);
    SetEeMemory(ee, sizeof(ee));

    std::uint32_t result = 0xFFFFFFFFu;
    if (!Invoke(0x0025C6F8u, 0x10u, 0x1000u, 16u, 0u, &result)) return 19;
    if (result != 0) return 20;

    std::uint8_t spu[16]{};
    System().ReadSpu2(0x1000, spu, sizeof(spu));
    if (std::memcmp(spu, ee + 0x10, sizeof(spu)) != 0) return 21;
    if (System().TransferAddress() != 0x1010u) return 22;

    // El dispatcher remoto registra el comando y sigue devolviendo 0.
    if (!Invoke(0x0025C790u, 1u, 0x8010u, 0x0980u, 0u, &result)) return 23;
    if (result != 0) return 24;

    // Fuera del bloque libsd, Invoke no intercepta.
    if (Invoke(0x00100000u, 0, 0, 0, 0, &result)) return 25;

    SetEeMemory(nullptr, 0);
    System().Reset();

    std::printf("PASS audio: libsd HLE, ADPCM SPU2 y TransToIOP\n");
    return 0;
}
