// Prueba del punto de enlace recompilado:
// una definicion fuerte de CallRecompiled/CallRecompiledRet debe sustituir al
// stub debil sin perder la cola, la tabla de estados ni el VSync.
//
// Compila con: -DGAME_MAIN_NO_SDL

#include "game_main.hpp"

#include <cstdint>
#include <cstdio>

namespace game {
int recompiled_calls = 0;
int recompiled_ret_calls = 0;
bool saw_audio_recompiled = false;

void CallRecompiled(std::uint32_t address, std::uint32_t, std::uint32_t,
                    std::uint32_t, std::uint32_t) {
    ++recompiled_calls;
    if (address >= 0x0025C650u && address < 0x0025C950u) {
        saw_audio_recompiled = true;
    }
    if (address == 0x00125740u) {
        std::printf("override: handler de estado 1 llamado\n");
    }
}

std::uint32_t CallRecompiledRet(std::uint32_t, std::uint32_t a0, std::uint32_t,
                                std::uint32_t, std::uint32_t) {
    ++recompiled_ret_calls;
    return a0 + 1u;
}
}  // namespace game

int main() {
    if (!game::Initialize(nullptr, "overlays")) return 1;
    if (game::recompiled_calls == 0) return 2;
    if (game::recompiled_ret_calls == 0) return 3;
    if (game::ReadU32(game::kQueueTail) != 0x0018477A8u) return 4;
    if (game::saw_audio_recompiled) return 7;   // libsd debe quedar interceptado
    if (game::CallPS2Ret(0x0025C650u) != 0) return 8;
    if (!game::Frame(nullptr)) return 5;
    if (game::CurrentLoadState() != 1) return 6;

    std::printf("PASS override: calls=%d ret=%d estado=%u\n",
                game::recompiled_calls, game::recompiled_ret_calls,
                game::CurrentLoadState());
    return 0;
}
