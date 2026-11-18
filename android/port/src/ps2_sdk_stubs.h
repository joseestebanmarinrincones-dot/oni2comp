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
