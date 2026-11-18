#pragma once

// Resolución de rutas de consola para el port de Android.
//
// El código descompilado sigue llamando a fopen("cdrom0:\\DATA\\FILE.BIN;1").
// Esta capa quita el volumen, normaliza separadores y busca el archivo real
// bajo el directorio externo de la app, sin distinguir mayúsculas/minúsculas.
//
// Con PORTFS_LINK_WRAP el CMake enlaza con -Wl,--wrap=fopen, etc., así que el
// código original no se toca. Sin wrap se puede usar portfs::fopen
// directamente o un #define fopen portfs::fopen.

#include <cstdio>
#include <string>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>

namespace portfs {

// Añade los roots por defecto: SDL_AndroidGetExternalStoragePath(),
// SDL_AndroidGetInternalStoragePath(), PORT_FS_ROOT y subcarpetas data/CD/files.
void initialize();

// Añade una raíz adicional (por ejemplo la carpeta donde copiaste DATA/).
void add_root(const std::string& root);

// Borra la caché de rutas resueltas (tras copiar archivos nuevos).
void clear_cache();

// ¿La ruta usa un volumen de consola (cdrom0:, host0:, mc0:, ...)?
bool is_console_path(const std::string& path);

// Quita el volumen, cambia '\' por '/', elimina ";1" y normaliza.
// Devuelve vacío si la ruta intenta salir del root con "..".
std::string normalize(const std::string& path);

// Devuelve la ruta real bajo algún root, o vacío si no existe.
std::string resolve(const std::string& console_path);

// Versiones utilizables directamente (sin --wrap).
FILE* fopen(const char* path, const char* mode);
int open(const char* path, int flags, ...);
int stat(const char* path, struct stat* info);
int access(const char* path, int mode);
DIR* opendir(const char* path);

}  // namespace portfs
