#include "game_stubs.h"
#include "game_main.hpp"
#include "crash_handler.hpp"

#include <cstdio>

#if defined(GAME_MAIN_NO_SDL)
#define STUB_LOG(...) do { \
    std::fprintf(stdout, "[GAME_STUB] "); \
    std::fprintf(stdout, __VA_ARGS__); \
    std::fputc('\n', stdout); \
    std::fflush(stdout); \
} while (0)
#else
#include <SDL.h>
#define STUB_LOG(...) SDL_Log(__VA_ARGS__)
#endif

namespace game {
namespace {

bool g_logging = true;

const stubs::Entry *FindEntry(std::uint32_t address) {
    std::size_t low = 0;
    std::size_t high = stubs::kEntryCount;
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2;
        if (stubs::kEntries[middle].address < address) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    if (low < stubs::kEntryCount && stubs::kEntries[low].address == address) {
        return &stubs::kEntries[low];
    }
    return nullptr;
}

}  // namespace
std::uint32_t FUN_00100B28(std::uint32_t, std::uint32_t,
                             std::uint32_t, std::uint32_t) {
    STUB_LOG("FUN_00100B28");
    return 0u;
}

std::uint32_t FUN_00100EE0(std::uint32_t, std::uint32_t,
                             std::uint32_t, std::uint32_t) {
    STUB_LOG("FUN_00100EE0");
    return 0u;
}

std::uint32_t FUN_00101148(std::uint32_t, std::uint32_t,
                             std::uint32_t, std::uint32_t) {
    STUB_LOG("FUN_00101148");
    return 0u;
}

std::uint32_t FUN_001014B8(std::uint32_t, std::uint32_t,
                             std::uint32_t, std::uint32_t) {
    STUB_LOG("FUN_001014B8");
    return 0u;
}

std::uint32_t FUN_001039E0(std::uint32_t, std::uint32_t,
                             std::uint32_t, std::uint32_t) {
    STUB_LOG("FUN_001039E0");
    return 0u;
}

std::uint32_t FUN_00124D90(std::uint32_t, std::uint32_t,
                             std::uint32_t, std::uint32_t) {
    STUB_LOG("FUN_00124D90");
    return 0u;
}

std::uint32_t FUN_0013CCB0(std::uint32_t, std::uint32_t,
                             std::uint32_t, std::uint32_t) {
    STUB_LOG("FUN_0013CCB0");
    return 0u;
}

std::uint32_t FUN_00146D30(std::uint32_t, std::uint32_t,
                             std::uint32_t, std::uint32_t) {
    STUB_LOG("FUN_00146D30");
    return 0u;
}

std::uint32_t FUN_00146D60(std::uint32_t, std::uint32_t,
                             std::uint32_t, std::uint32_t) {
    STUB_LOG("FUN_00146D60");
    return 0u;
}

std::uint32_t FUN_0014A340(std::uint32_t, std::uint32_t,
                             std::uint32_t, std::uint32_t) {
    STUB_LOG("FUN_0014A340");
    return 0u;
}

std::uint32_t FUN_00150EF0(std::uint32_t, std::uint32_t,
                             std::uint32_t, std::uint32_t) {
    STUB_LOG("FUN_00150EF0");
    return 0u;
}

std::uint32_t FUN_00150F80(std::uint32_t, std::uint32_t,
                             std::uint32_t, std::uint32_t) {
    STUB_LOG("FUN_00150F80");
    return 0u;
}

std::uint32_t FUN_001519C0(std::uint32_t, std::uint32_t,
                             std::uint32_t, std::uint32_t) {
    STUB_LOG("FUN_001519C0");
    return 0u;
}

std::uint32_t FUN_00154110(std::uint32_t, std::uint32_t,
                             std::uint32_t, std::uint32_t) {
    STUB_LOG("FUN_00154110");
    return 0u;
}

std::uint32_t FUN_00155000(std::uint32_t, std::uint32_t,
                             std::uint32_t, std::uint32_t) {
    STUB_LOG("FUN_00155000");
    return 0u;
}

std::uint32_t FUN_00155130(std::uint32_t, std::uint32_t,
                             std::uint32_t, std::uint32_t) {
    STUB_LOG("FUN_00155130");
    return 0u;
}

std::uint32_t FUN_0015BDB0(std::uint32_t, std::uint32_t,
                             std::uint32_t, std::uint32_t) {
    STUB_LOG("FUN_0015BDB0");
    return 0u;
}

std::uint32_t FUN_0015DBF0(std::uint32_t, std::uint32_t,
                             std::uint32_t, std::uint32_t) {
    STUB_LOG("FUN_0015DBF0");
    return 0u;
}

std::uint32_t FUN_0015FC20(std::uint32_t, std::uint32_t,
                             std::uint32_t, std::uint32_t) {
    STUB_LOG("FUN_0015FC20");
    return 0u;
}

std::uint32_t FUN_00161BB0(std::uint32_t, std::uint32_t,
                             std::uint32_t, std::uint32_t) {
    STUB_LOG("FUN_00161BB0");
    return 0u;
}

std::uint32_t FUN_001701B0(std::uint32_t, std::uint32_t,
                             std::uint32_t, std::uint32_t) {
    STUB_LOG("FUN_001701B0");
    return 0u;
}

std::uint32_t FUN_0017C2B0(std::uint32_t, std::uint32_t,
                             std::uint32_t, std::uint32_t) {
    STUB_LOG("FUN_0017C2B0");
    return 0u;
}

std::uint32_t FUN_0017D5D0(std::uint32_t, std::uint32_t,
                             std::uint32_t, std::uint32_t) {
    STUB_LOG("FUN_0017D5D0");
    return 0u;
}

std::uint32_t FUN_001858E0(std::uint32_t, std::uint32_t,
                             std::uint32_t, std::uint32_t) {
    STUB_LOG("FUN_001858E0");
    return 0u;
}

std::uint32_t FUN_00185D90(std::uint32_t, std::uint32_t,
                             std::uint32_t, std::uint32_t) {
    STUB_LOG("FUN_00185D90");
    return 0u;
}

std::uint32_t FUN_00188730(std::uint32_t, std::uint32_t,
                             std::uint32_t, std::uint32_t) {
    STUB_LOG("FUN_00188730");
    return 0u;
}

std::uint32_t FUN_00197E50(std::uint32_t, std::uint32_t,
                             std::uint32_t, std::uint32_t) {
    STUB_LOG("FUN_00197E50");
    return 0u;
}

std::uint32_t FUN_0019AE20(std::uint32_t, std::uint32_t,
                             std::uint32_t, std::uint32_t) {
    STUB_LOG("FUN_0019AE20");
    return 0u;
}

std::uint32_t FUN_0019AE90(std::uint32_t, std::uint32_t,
                             std::uint32_t, std::uint32_t) {
    STUB_LOG("FUN_0019AE90");
    return 0u;
}

#ifndef GAME_STUBS_NO_INIT_STUB
void FUN_00149BF0() {
    STUB_LOG("FUN_00149BF0");
}
#endif

const stubs::Entry stubs::kEntries[] = {
    {0x00100B28u, "FUN_00100B28", &FUN_00100B28, true},  // libgraph/vsync
    {0x00100EE0u, "FUN_00100EE0", &FUN_00100EE0, true},  // libgraph
    {0x00101148u, "FUN_00101148", &FUN_00101148, true},  // libgraph
    {0x001014B8u, "FUN_001014B8", &FUN_001014B8, true},  // libgraph
    {0x001039E0u, "FUN_001039E0", &FUN_001039E0, true},  // FlushCache
    {0x00124D90u, "FUN_00124D90", &FUN_00124D90, true},  // loader dispatcher
    {0x0013CCB0u, "FUN_0013CCB0", &FUN_0013CCB0, false},
    {0x00146D30u, "FUN_00146D30", &FUN_00146D30, false},
    {0x00146D60u, "FUN_00146D60", &FUN_00146D60, false},
    {0x0014A340u, "FUN_0014A340", &FUN_0014A340, true},  // DMA send
    {0x00150EF0u, "FUN_00150EF0", &FUN_00150EF0, false},
    {0x00150F80u, "FUN_00150F80", &FUN_00150F80, false},
    {0x001519C0u, "FUN_001519C0", &FUN_001519C0, false},
    {0x00154110u, "FUN_00154110", &FUN_00154110, false},
    {0x00155000u, "FUN_00155000", &FUN_00155000, false},
    {0x00155130u, "FUN_00155130", &FUN_00155130, true},  // callback slot
    {0x0015BDB0u, "FUN_0015BDB0", &FUN_0015BDB0, false},
    {0x0015DBF0u, "FUN_0015DBF0", &FUN_0015DBF0, false},
    {0x0015FC20u, "FUN_0015FC20", &FUN_0015FC20, false},
    {0x00161BB0u, "FUN_00161BB0", &FUN_00161BB0, false},
    {0x001701B0u, "FUN_001701B0", &FUN_001701B0, false},
    {0x0017C2B0u, "FUN_0017C2B0", &FUN_0017C2B0, false},
    {0x0017D5D0u, "FUN_0017D5D0", &FUN_0017D5D0, false},
    {0x001858E0u, "FUN_001858E0", &FUN_001858E0, false},
    {0x00185D90u, "FUN_00185D90", &FUN_00185D90, false},
    {0x00188730u, "FUN_00188730", &FUN_00188730, false},
    {0x00197E50u, "FUN_00197E50", &FUN_00197E50, false},
    {0x0019AE20u, "FUN_0019AE20", &FUN_0019AE20, false},
    {0x0019AE90u, "FUN_0019AE90", &FUN_0019AE90, false},
};
const std::size_t stubs::kEntryCount =
    sizeof(stubs::kEntries) / sizeof(stubs::kEntries[0]);

namespace stubs {

bool Call(std::uint32_t address, std::uint32_t a0, std::uint32_t a1,
          std::uint32_t a2, std::uint32_t a3, std::uint32_t *result) {
    const Entry *entry = FindEntry(address);
    if (entry == nullptr || entry->native) return false;

    const std::uint32_t value = entry->function(a0, a1, a2, a3);
    if (result != nullptr) *result = value;
    return true;
}

bool Has(std::uint32_t address) {
    return FindEntry(address) != nullptr;
}

const char *NameOf(std::uint32_t address) {
    const Entry *entry = FindEntry(address);
    return entry != nullptr ? entry->name : "unknown";
}

void LogCall(std::uint32_t address) {
    // Se registra siempre, aunque el logging este apagado: el handler de
    // SIGSEGV usa esta direccion para escribir FUN_xxxxxxxx en crash_log.
    ps2::crash::SetLastFunction(address);

    if (!g_logging) return;

    // FUN_00117520 se llama 0x1000 veces durante la init; registrar solo la
    // primera para no inundar Logcat.
    if (address == 0x00117520u) {
        static bool logged = false;
        if (logged) return;
        logged = true;
    }

    const Entry *entry = FindEntry(address);
    if (entry != nullptr && !entry->native) {
        // El stub imprime su propio nombre al ejecutarse.
        return;
    }
    if (entry != nullptr) {
        STUB_LOG("MAIN -> 0x%08X %s (native)", address, entry->name);
    } else {
        STUB_LOG("MAIN -> 0x%08X (native/SDK)", address);
    }
}

void SetLogging(bool enabled) { g_logging = enabled; }
bool Logging() { return g_logging; }

}  // namespace stubs
}  // namespace game

#ifndef GAME_STUBS_NO_RECOMPILED_HOOK
namespace game {

void CallRecompiled(std::uint32_t address, std::uint32_t a0, std::uint32_t a1,
                    std::uint32_t a2, std::uint32_t a3) {
    ps2::crash::SetLastFunction(address);
    stubs::Call(address, a0, a1, a2, a3, nullptr);
}

std::uint32_t CallRecompiledRet(std::uint32_t address, std::uint32_t a0,
                                std::uint32_t a1, std::uint32_t a2,
                                std::uint32_t a3) {
    ps2::crash::SetLastFunction(address);
    std::uint32_t result = 0;
    stubs::Call(address, a0, a1, a2, a3, &result);
    return result;
}

}  // namespace game
#endif
