# Generates android/port/src/ps2_sdk_stubs.h and .cpp from the Ghidra CSV.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts/generate_ps2_sdk_stubs.ps1
#   powershell ... -Csv <csv> -OutDir <android/port/src>
#
# The CSV is produced by scripts/ghidra/IdentifyPs2Sdk.java.

param(
    [string]$Csv = "",
    [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($Csv)) {
    $Csv = Join-Path $root "games\onimusha2\disasm\sdk_report\ps2_sdk_functions.csv"
}
if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $OutDir = Join-Path $root "android\port\src"
}
if (-not (Test-Path $Csv)) {
    throw "No existe el CSV: $Csv"
}
New-Item -ItemType Directory -Force $OutDir | Out-Null

$rows = Import-Csv $Csv
if (-not $rows -or $rows.Count -eq 0) {
    throw "El CSV no contiene funciones: $Csv"
}

function Convert-Library([string]$name) {
    switch ($name) {
        "crt"       { return "Crt" }
        "libgraph"  { return "LibGraph" }
        "libdma"    { return "LibDma" }
        "libvu0"    { return "LibVu0" }
        "libkernel" { return "LibKernel" }
        "libc"      { return "LibC" }
        "libcdvd"   { return "LibCdvd" }
        "libmc"     { return "LibMc" }
        "libsd"     { return "LibSd" }
        "libpad2"   { return "LibPad2" }
        "libipu"    { return "LibIpu" }
        default     { return "Unknown" }
    }
}

function Sanitize([string]$text) {
    if ($null -eq $text) { return "" }
    return ($text -replace '\\', '/' -replace '"', "'" -replace "`r", " " -replace "`n", " ")
}

$header = @'
#pragma once

// Tabla de funciones SDK de PS2 identificadas en SLUS_203.93.
//
// Generado por scripts/generate_ps2_sdk_stubs.ps1 a partir de
// games/onimusha2/disasm/sdk_report/ps2_sdk_functions.csv.
// No editar a mano: vuelve a ejecutar IdentifyPs2Sdk.java y el generador.
//
// `native == true` significa que este port intercepta la funcion con una
// implementacion C++/SDL2 (o un stub controlado). `native == false` significa
// que la funcion esta identificada como SDK, pero por ahora se deja al codigo
// recompilado o al runtime (libvu0/libkernel/libc/crt).
//
// Uso en el despachador:
//
//   std::uint32_t result = 0;
//   if (ps2::sdk::CallRet(address, a0, a1, a2, a3, &result)) {
//       return result;
//   }
//
// Para enlazar una funcion concreta a una implementacion nativa:
//
//   ps2::sdk::SetHook([](const ps2::sdk::Function &fn,
//                        std::uint32_t a0, std::uint32_t a1,
//                        std::uint32_t a2, std::uint32_t a3,
//                        std::uint32_t *out) -> bool {
//       if (fn.address == 0x00100B28u) {
//           // sceGsSyncV -> SDL_GL_SwapWindow
//           *out = 0;
//           return true;
//       }
//       return false;
//   });
//
// El hook global se prueba antes que el hook por libreria. Si ninguno
// responde, se aplica el valor por defecto de la libreria (0 para
// libgraph/libdma/libcdvd/libmc/libsd/libpad2).

#include <cstddef>
#include <cstdint>

namespace ps2::sdk {

enum class Library : std::uint8_t {
    Unknown = 0,
    Crt,
    LibGraph,
    LibDma,
    LibVu0,
    LibKernel,
    LibC,
    LibCdvd,
    LibMc,
    LibSd,
    LibPad2,
    LibIpu,
};

struct Function {
    std::uint32_t address;
    std::uint32_t size;      // bytes del cuerpo en Ghidra
    Library library;
    std::int16_t syscall;    // -1 = no es syscall, -2 = numero desconocido
    bool native;             // true = interceptada por este port
    const char *name;        // nombre conservado de Ghidra
};

extern const Function kFunctions[];
extern const std::size_t kFunctionCount;

// Busca por direccion exacta o por direccion contenida en el cuerpo.
const Function *Find(std::uint32_t address);
const Function *FindContaining(std::uint32_t address);

bool IsSdkAddress(std::uint32_t address);
bool IsNativeAddress(std::uint32_t address);
Library LibraryOf(std::uint32_t address);
const char *LibraryName(Library library);

using Hook = bool (*)(const Function &function, std::uint32_t a0,
                      std::uint32_t a1, std::uint32_t a2, std::uint32_t a3,
                      std::uint32_t *result);

void SetHook(Hook hook);
void SetLibraryHook(Library library, Hook hook);
void ClearHooks();

// Devuelve true si la direccion es una funcion SDK nativa y fue atendida.
// `result` recibe el valor de retorno de 32 bits (0 por defecto).
bool Call(std::uint32_t address, std::uint32_t a0, std::uint32_t a1,
          std::uint32_t a2, std::uint32_t a3, std::uint32_t *result);
bool CallRet(std::uint32_t address, std::uint32_t a0, std::uint32_t a1,
             std::uint32_t a2, std::uint32_t a3, std::uint32_t *result);

void SetLogging(bool enabled);
bool Logging();

}  // namespace ps2::sdk
'@

$sourcePreamble = @'
#include "ps2_sdk_stubs.h"

#include <cstdio>

#if defined(__ANDROID__)
#include <android/log.h>
#define SDK_TAG "PS2SDK"
#define SDK_LOGI(...) __android_log_print(ANDROID_LOG_INFO, SDK_TAG, __VA_ARGS__)
#else
#define SDK_LOGI(...) do { \
    std::fprintf(stdout, "[PS2SDK] "); \
    std::fprintf(stdout, __VA_ARGS__); \
    std::fputc('\n', stdout); \
    std::fflush(stdout); \
} while (0)
#endif

namespace ps2::sdk {
namespace {

bool g_logging = true;
Hook g_hook = nullptr;
Hook g_library_hooks[static_cast<std::size_t>(Library::LibIpu) + 1] = {};

// Log una vez por direccion: las funciones de libgraph se llaman por frame.
std::uint32_t g_logged[256] = {};
std::size_t g_logged_count = 0;

bool already_logged(std::uint32_t address) {
    for (std::size_t i = 0; i < g_logged_count; ++i) {
        if (g_logged[i] == address) return true;
    }
    if (g_logged_count < (sizeof(g_logged) / sizeof(g_logged[0]))) {
        g_logged[g_logged_count++] = address;
    }
    return false;
}

std::size_t library_index(Library library) {
    const auto index = static_cast<std::size_t>(library);
    return index < (sizeof(g_library_hooks) / sizeof(g_library_hooks[0]))
        ? index : 0;
}

void log_stub(const Function &function, std::uint32_t result) {
    if (!g_logging || already_logged(function.address)) return;
    SDK_LOGI("stub 0x%08X %s (%s) -> 0x%08X",
             function.address, LibraryName(function.library),
             function.name, result);
}

}  // namespace

const Function kFunctions[] = {
'@

$sourceBody = @'
};

const std::size_t kFunctionCount = sizeof(kFunctions) / sizeof(kFunctions[0]);

const char *LibraryName(Library library) {
    switch (library) {
    case Library::Crt:       return "crt";
    case Library::LibGraph:  return "libgraph";
    case Library::LibDma:    return "libdma";
    case Library::LibVu0:    return "libvu0";
    case Library::LibKernel: return "libkernel";
    case Library::LibC:      return "libc";
    case Library::LibCdvd:   return "libcdvd";
    case Library::LibMc:     return "libmc";
    case Library::LibSd:     return "libsd";
    case Library::LibPad2:   return "libpad2";
    case Library::LibIpu:    return "libipu";
    default:                 return "unknown";
    }
}

const Function *Find(std::uint32_t address) {
    std::size_t low = 0;
    std::size_t high = kFunctionCount;
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2;
        if (kFunctions[middle].address < address) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    if (low < kFunctionCount && kFunctions[low].address == address) {
        return &kFunctions[low];
    }
    return nullptr;
}

const Function *FindContaining(std::uint32_t address) {
    std::size_t low = 0;
    std::size_t high = kFunctionCount;
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2;
        if (kFunctions[middle].address <= address) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    if (low == 0) return nullptr;
    const Function &function = kFunctions[low - 1];
    const std::uint32_t end = function.address + function.size;
    return (address >= function.address && address < end) ? &function : nullptr;
}

bool IsSdkAddress(std::uint32_t address) {
    return FindContaining(address) != nullptr;
}

bool IsNativeAddress(std::uint32_t address) {
    const Function *function = FindContaining(address);
    return function != nullptr && function->native;
}

Library LibraryOf(std::uint32_t address) {
    const Function *function = FindContaining(address);
    return function != nullptr ? function->library : Library::Unknown;
}

void SetHook(Hook hook) { g_hook = hook; }

void SetLibraryHook(Library library, Hook hook) {
    g_library_hooks[library_index(library)] = hook;
}

void ClearHooks() {
    g_hook = nullptr;
    for (std::size_t i = 0; i < (sizeof(g_library_hooks) / sizeof(g_library_hooks[0])); ++i) {
        g_library_hooks[i] = nullptr;
    }
}

bool Call(std::uint32_t address, std::uint32_t a0, std::uint32_t a1,
          std::uint32_t a2, std::uint32_t a3, std::uint32_t *result) {
    const Function *function = FindContaining(address);
    if (function == nullptr || !function->native) return false;

    std::uint32_t value = 0;
    if (g_hook != nullptr && g_hook(*function, a0, a1, a2, a3, &value)) {
        if (result != nullptr) *result = value;
        return true;
    }
    const Hook libraryHook = g_library_hooks[library_index(function->library)];
    if (libraryHook != nullptr && libraryHook(*function, a0, a1, a2, a3, &value)) {
        if (result != nullptr) *result = value;
        return true;
    }

    switch (function->library) {
    case Library::LibGraph:
    case Library::LibDma:
    case Library::LibCdvd:
    case Library::LibMc:
    case Library::LibSd:
    case Library::LibPad2:
        value = 0;
        break;
    default:
        return false;
    }

    log_stub(*function, value);
    if (result != nullptr) *result = value;
    return true;
}

bool CallRet(std::uint32_t address, std::uint32_t a0, std::uint32_t a1,
             std::uint32_t a2, std::uint32_t a3, std::uint32_t *result) {
    return Call(address, a0, a1, a2, a3, result);
}

void SetLogging(bool enabled) { g_logging = enabled; }
bool Logging() { return g_logging; }

}  // namespace ps2::sdk
'@

$table = New-Object System.Text.StringBuilder
$countNative = 0
foreach ($row in $rows) {
    $library = Convert-Library $row.library
    if ($row.native -eq "yes") { $countNative++ }
    $address = [Convert]::ToUInt32($row.address, 16)
    $size = [Convert]::ToUInt32($row.size)
    $syscall = [Convert]::ToInt32($row.syscall)
    $native = if ($row.native -eq "yes") { "true" } else { "false" }
    $name = Sanitize $row.name
    $evidence = Sanitize $row.evidence
    $confidence = Sanitize $row.confidence
    [void]$table.AppendLine(("    // {0} {1}: {2}" -f $row.library, $confidence, $evidence))
    [void]$table.AppendLine(("    {{0x{0:X8}u, {1}u, Library::{2}, {3}, {4}, `"{5}`"}}," -f $address, $size, $library, $syscall, $native, $name))
}

$headerPath = Join-Path $OutDir "ps2_sdk_stubs.h"
$sourcePath = Join-Path $OutDir "ps2_sdk_stubs.cpp"
Set-Content -Path $headerPath -Value $header -Encoding UTF8
$source = $sourcePreamble + $table.ToString() + $sourceBody
Set-Content -Path $sourcePath -Value $source -Encoding UTF8

Write-Output ("ps2_sdk_stubs: {0} funciones, {1} nativas -> {2}" -f $rows.Count, $countNative, $OutDir)
