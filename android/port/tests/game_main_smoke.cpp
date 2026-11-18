// Prueba de humo de game_main.cpp sin SDL ni GL.
// Compila con: -DGAME_MAIN_NO_SDL

#include "game_main.hpp"

#include <cstdint>
#include <cstdio>

int main() {
    if (!game::Initialize(nullptr, "overlays")) {
        std::fprintf(stderr, "Initialize fallo\n");
        return 1;
    }

    if (game::ReadU32(game::kQueueTail) != 0x0018477A8u) {
        std::fprintf(stderr, "tail inesperado: 0x%08X\n", game::ReadU32(game::kQueueTail));
        return 2;
    }
    if (game::ReadU32(game::kQueueHead) != 0x001847790u) {
        std::fprintf(stderr, "head inesperado: 0x%08X\n", game::ReadU32(game::kQueueHead));
        return 3;
    }
    if (game::ReadU8(0x001847790u) != 1) {
        std::fprintf(stderr, "tipo de peticion inesperado\n");
        return 4;
    }
    if (game::ReadU16(0x001847792u) != 0x0301u) {
        std::fprintf(stderr, "indice de archivo inesperado\n");
        return 5;
    }
    if (game::ReadU32(0x001847794u) != 0x001596C7u) {
        std::fprintf(stderr, "LBA inesperado: 0x%08X\n", game::ReadU32(0x001847794u));
        return 6;
    }
    if (game::ReadU32(0x0018477A4u) != 0x0011B800u) {
        std::fprintf(stderr, "tamano inesperado: 0x%08X\n", game::ReadU32(0x0018477A4u));
        return 7;
    }
    if (game::CurrentLoadState() != 0) {
        std::fprintf(stderr, "estado inicial inesperado: %u\n", game::CurrentLoadState());
        return 8;
    }

    if (!game::Frame(nullptr)) {
        std::fprintf(stderr, "Frame 1 devolvio false\n");
        return 9;
    }
    if (game::CurrentLoadState() != 1) {
        std::fprintf(stderr, "estado tras Frame 1: %u\n", game::CurrentLoadState());
        return 10;
    }

    for (int i = 0; i < 10; ++i) {
        if (!game::Frame(nullptr)) {
            std::fprintf(stderr, "Frame %d devolvio false\n", i + 2);
            return 11;
        }
    }
    if (game::ReadU16(game::kFrameCounter) != 11u) {
        std::fprintf(stderr, "contador de frames: %u\n", game::ReadU16(game::kFrameCounter));
        return 12;
    }

    std::printf("PASS: cola 0x301, estado 0->1, 11 frames, tail=0x%08X\n",
                game::ReadU32(game::kQueueTail));
    return 0;
}
