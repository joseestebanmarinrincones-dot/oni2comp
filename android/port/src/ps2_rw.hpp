#pragma once

// Apertura de archivos de PS2 con SDL_RWops.
//
//   SDL_RWops *rw = PS2_RWFromFile("cdrom0:\\IMAGE\\FILE.BIN;1", "rb");
//
// Limpia el volumen cdrom0:, cambia '\' por '/', quita el sufijo ISO ;1 y
// busca el archivo en SDL_AndroidGetExternalStoragePath() (y en los roots de
// port_fs), sin distinguir mayúsculas/minúsculas.

#include <SDL_rwops.h>

namespace port {

SDL_RWops *RWFromPS2File(const char *path, const char *mode = "rb");

}  // namespace port

// Versión con enlace C, por si el código descompilado la declara así.
extern "C" SDL_RWops *PS2_RWFromFile(const char *path, const char *mode);
