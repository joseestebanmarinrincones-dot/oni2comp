#pragma once

// Instantaneas rapidas del port: ps2_ram, RAM del juego, gs_vram, cache de
// texturas y estado de Ps2AudioSystem/OverlayDispatcher.
//
// El archivo usa un contenedor binario versionado con secciones comprimidas
// por separado con zlib. Las funciones resuelven nombres relativos contra el
// directorio interno configurado con SetStorageDirectory().

#include <cstddef>
#include <cstdint>
#include <string>

class OverlayDispatcher;

namespace ps2::state {

// Directorio interno de la app (p. ej. files/). Se llama desde main.cpp.
void SetStorageDirectory(const std::string &directory);

// Memoria adicional que no vive en ps2_ram.
void SetGameMemory(std::uint8_t *ram, std::uint32_t size);
void SetGsVram(std::uint8_t *vram, std::uint32_t size);

// OverlayDispatcher activo (opcional). Save/Load conservan sus handlers.
void SetOverlayDispatcher(OverlayDispatcher *dispatcher);

// Guarda/carga una instantanea. `filename` puede ser absoluto o relativo al
// directorio configurado. Devuelve false y actualiza LastError() si falla.
bool SaveQuickState(const char *filename);
bool LoadQuickState(const char *filename);

std::string ResolveQuickStatePath(const char *filename);
const std::string &LastError();

}  // namespace ps2::state
