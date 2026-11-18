#pragma once

// Manejador de crash POSIX para Android/Linux.
//
// Instala sigaction(SIGSEGV) (y SIGBUS/SIGILL/SIGFPE/SIGABRT) con una pila
// alternativa. Antes de reenviar la senal al sistema escribe crash_log.txt con
// la ultima direccion FUN_xxxxxxxx despachada por game_main / game_stubs.
//
// La escritura usa open/write/close y formateo manual para ser async-signal-
// safe. En Windows (pruebas de PC) se usa un fallback con stdio.

#include <cstdint>
#include <string>

namespace ps2::crash {

// Instala los manejadores. `log_path` debe ser una ruta escribible; en
// Android main.cpp pasa <internal storage>/crash_log.txt.
void Install(const char *log_path);

// Restaura los manejadores anteriores.
void Shutdown();

// Ultima funcion PS2 ejecutada. Se llama desde stubs::LogCall y desde
// CallRecompiled/CallRecompiledRet.
void SetLastFunction(std::uint32_t address);
std::uint32_t LastFunction();

// Escribe una entrada de crash. Es publica para pruebas y para usar desde
// otros manejadores; en POSIX es async-signal-safe.
void WriteCrashLog(int signal_number, std::uintptr_t pc,
                   std::uintptr_t fault_address,
                   std::uint32_t last_function);

// Ruta actual del log (para Logcat/diagnostico).
const char *LogPath();

}  // namespace ps2::crash
