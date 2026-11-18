# Generates android/port/src/game_stubs.h/.cpp for the FUN_ calls in main.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts/generate_game_stubs.ps1
#
# The list is curated from FUN_001496D0 (main) plus its direct init call.
# `native=$true` means the function already has a native case in game_main.cpp;
# its stub is declared but GameStubCall does not intercept it.

param(
    [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $OutDir = Join-Path $root "android\port\src"
}
New-Item -ItemType Directory -Force $OutDir | Out-Null

$entries = @(
    [pscustomobject]@{ Address = 0x00100B28; Name = "FUN_00100B28"; Native = $true;  Note = "libgraph/vsync" },
    [pscustomobject]@{ Address = 0x00100EE0; Name = "FUN_00100EE0"; Native = $true;  Note = "libgraph" },
    [pscustomobject]@{ Address = 0x00101148; Name = "FUN_00101148"; Native = $true;  Note = "libgraph" },
    [pscustomobject]@{ Address = 0x001014B8; Name = "FUN_001014B8"; Native = $true;  Note = "libgraph" },
    [pscustomobject]@{ Address = 0x001039E0; Name = "FUN_001039E0"; Native = $true;  Note = "FlushCache" },
    [pscustomobject]@{ Address = 0x00124D90; Name = "FUN_00124D90"; Native = $true;  Note = "loader dispatcher" },
    [pscustomobject]@{ Address = 0x0013CCB0; Name = "FUN_0013CCB0"; Native = $false; Note = "" },
    [pscustomobject]@{ Address = 0x00146D30; Name = "FUN_00146D30"; Native = $false; Note = "" },
    [pscustomobject]@{ Address = 0x00146D60; Name = "FUN_00146D60"; Native = $false; Note = "" },
    [pscustomobject]@{ Address = 0x0014A340; Name = "FUN_0014A340"; Native = $true;  Note = "DMA send" },
    [pscustomobject]@{ Address = 0x00150EF0; Name = "FUN_00150EF0"; Native = $false; Note = "" },
    [pscustomobject]@{ Address = 0x00150F80; Name = "FUN_00150F80"; Native = $false; Note = "" },
    [pscustomobject]@{ Address = 0x001519C0; Name = "FUN_001519C0"; Native = $false; Note = "" },
    [pscustomobject]@{ Address = 0x00154110; Name = "FUN_00154110"; Native = $false; Note = "" },
    [pscustomobject]@{ Address = 0x00155000; Name = "FUN_00155000"; Native = $false; Note = "" },
    [pscustomobject]@{ Address = 0x00155130; Name = "FUN_00155130"; Native = $true;  Note = "callback slot" },
    [pscustomobject]@{ Address = 0x0015BDB0; Name = "FUN_0015BDB0"; Native = $false; Note = "" },
    [pscustomobject]@{ Address = 0x0015DBF0; Name = "FUN_0015DBF0"; Native = $false; Note = "" },
    [pscustomobject]@{ Address = 0x0015FC20; Name = "FUN_0015FC20"; Native = $false; Note = "" },
    [pscustomobject]@{ Address = 0x00161BB0; Name = "FUN_00161BB0"; Native = $false; Note = "" },
    [pscustomobject]@{ Address = 0x001701B0; Name = "FUN_001701B0"; Native = $false; Note = "" },
    [pscustomobject]@{ Address = 0x0017C2B0; Name = "FUN_0017C2B0"; Native = $false; Note = "" },
    [pscustomobject]@{ Address = 0x0017D5D0; Name = "FUN_0017D5D0"; Native = $false; Note = "" },
    [pscustomobject]@{ Address = 0x001858E0; Name = "FUN_001858E0"; Native = $false; Note = "" },
    [pscustomobject]@{ Address = 0x00185D90; Name = "FUN_00185D90"; Native = $false; Note = "" },
    [pscustomobject]@{ Address = 0x00188730; Name = "FUN_00188730"; Native = $false; Note = "" },
    [pscustomobject]@{ Address = 0x00197E50; Name = "FUN_00197E50"; Native = $false; Note = "" },
    [pscustomobject]@{ Address = 0x0019AE20; Name = "FUN_0019AE20"; Native = $false; Note = "" },
    [pscustomobject]@{ Address = 0x0019AE90; Name = "FUN_0019AE90"; Native = $false; Note = "" }
)

$header = @'
#pragma once

// Stubs de las funciones FUN_ invocadas desde FUN_001496D0 (main).
// Generado por scripts/generate_game_stubs.ps1.
//
// Cada stub escribe su nombre con SDL_Log y devuelve 0. El despachador
// game::stubs::Call() se usa desde CallRecompiled()/CallRecompiledRet()
// para que main siga llamando por direccion y el orden quede en Logcat.
//
// `native=true` significa que la direccion ya esta atendida por un caso de
// game_main.cpp o por ps2_sdk_stubs; su stub se declara pero Call() no lo
// intercepta.

#include <cstddef>
#include <cstdint>

namespace game {

'@

$decls = New-Object System.Text.StringBuilder
foreach ($entry in $entries) {
    [void]$decls.AppendLine(("std::uint32_t {0}(std::uint32_t a0 = 0, std::uint32_t a1 = 0," -f $entry.Name))
    [void]$decls.AppendLine(("             std::uint32_t a2 = 0, std::uint32_t a3 = 0);"))
}
[void]$decls.AppendLine("void FUN_00149BF0();")

$header += $decls.ToString()
$header += @'
namespace stubs {

using StubFn = std::uint32_t (*)(std::uint32_t, std::uint32_t,
                                 std::uint32_t, std::uint32_t);

struct Entry {
    std::uint32_t address;
    const char *name;
    StubFn function;
    bool native;
};

extern const Entry kEntries[];
extern const std::size_t kEntryCount;

bool Call(std::uint32_t address, std::uint32_t a0, std::uint32_t a1,
          std::uint32_t a2, std::uint32_t a3, std::uint32_t *result);
bool Has(std::uint32_t address);
const char *NameOf(std::uint32_t address);

// Log de una llamada de main. Los stubs no nativos se registran solos;
// esta funcion registra los nativos/SDK para conservar el orden completo.
void LogCall(std::uint32_t address);
void SetLogging(bool enabled);
bool Logging();

}  // namespace stubs
}  // namespace game
'@

$source = @'
#include "game_stubs.h"
#include "game_main.hpp"

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

'@

$defs = New-Object System.Text.StringBuilder
foreach ($entry in $entries) {
    [void]$defs.AppendLine(("std::uint32_t {0}(std::uint32_t, std::uint32_t," -f $entry.Name))
    [void]$defs.AppendLine("                             std::uint32_t, std::uint32_t) {")
    [void]$defs.AppendLine(("    STUB_LOG(`"{0}`");" -f $entry.Name))
    [void]$defs.AppendLine("    return 0u;")
    [void]$defs.AppendLine("}")
    [void]$defs.AppendLine("")
}
[void]$defs.AppendLine("#ifndef GAME_STUBS_NO_INIT_STUB")
[void]$defs.AppendLine("void FUN_00149BF0() {")
[void]$defs.AppendLine('    STUB_LOG("FUN_00149BF0");')
[void]$defs.AppendLine("}")
[void]$defs.AppendLine("#endif")
[void]$defs.AppendLine("")

$table = New-Object System.Text.StringBuilder
[void]$table.AppendLine("const stubs::Entry stubs::kEntries[] = {")
foreach ($entry in $entries) {
    $native = if ($entry.Native) { "true" } else { "false" }
    $comment = if ([string]::IsNullOrWhiteSpace($entry.Note)) { "" } else { "  // " + $entry.Note }
    [void]$table.AppendLine(("    {{0x{0:X8}u, `"{1}`", &{1}, {2}}},{3}" -f $entry.Address, $entry.Name, $native, $comment))
}
[void]$table.AppendLine("};")
[void]$table.AppendLine("const std::size_t stubs::kEntryCount =")
[void]$table.AppendLine("    sizeof(stubs::kEntries) / sizeof(stubs::kEntries[0]);")
[void]$table.AppendLine("")

$source += $defs.ToString() + $table.ToString()
$source += @'
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
    stubs::Call(address, a0, a1, a2, a3, nullptr);
}

std::uint32_t CallRecompiledRet(std::uint32_t address, std::uint32_t a0,
                                std::uint32_t a1, std::uint32_t a2,
                                std::uint32_t a3) {
    std::uint32_t result = 0;
    stubs::Call(address, a0, a1, a2, a3, &result);
    return result;
}

}  // namespace game
#endif
'@

$headerPath = Join-Path $OutDir "game_stubs.h"
$sourcePath = Join-Path $OutDir "game_stubs.cpp"
Set-Content -Path $headerPath -Value $header -Encoding UTF8
Set-Content -Path $sourcePath -Value $source -Encoding UTF8

$nativeCount = ($entries | Where-Object { $_.Native }).Count
Write-Output ("game_stubs: {0} entradas, {1} stubs, {2} nativas -> {3}" -f $entries.Count, ($entries.Count - $nativeCount), $nativeCount, $OutDir)
