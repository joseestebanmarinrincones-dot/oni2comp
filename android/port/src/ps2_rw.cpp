#include "ps2_rw.hpp"

#include "port_fs.hpp"

#include <SDL_error.h>
#include <SDL_log.h>
#include <SDL_system.h>

#include <string>

namespace port {
namespace {

// Ruta directa bajo el directorio externo, por si port_fs todavía no tiene
// roots (por ejemplo si se llama antes de portfs::initialize()).
SDL_RWops *open_from_external(const std::string &relative, const char *mode) {
#if defined(__ANDROID__)
    const char *external = SDL_AndroidGetExternalStoragePath();
    if (!external || !*external) return nullptr;

    std::string full(external);
    if (full.empty() || full.back() != '/') full += '/';
    full += relative;
    return SDL_RWFromFile(full.c_str(), mode);
#else
    (void)relative;
    (void)mode;
    return nullptr;
#endif
}

}  // namespace

SDL_RWops *RWFromPS2File(const char *path, const char *mode) {
    if (!path || !*path) return nullptr;
    if (!mode || !*mode) mode = "rb";

    // Una ruta que no es de consola (por ejemplo /sdcard/... o assets/...) se
    // deja tal cual.
    if (!portfs::is_console_path(path)) {
        return SDL_RWFromFile(path, mode);
    }

    const std::string relative = portfs::normalize(path);
    if (relative.empty()) {
        SDL_Log("PS2_RWFromFile: ruta inválida: %s", path);
        return nullptr;
    }

    // portfs resuelve los roots, el case-insensitive y la caché.
    const std::string resolved = portfs::resolve(path);
    if (!resolved.empty()) {
        SDL_RWops *rw = SDL_RWFromFile(resolved.c_str(), mode);
        if (rw) return rw;
        SDL_Log("PS2_RWFromFile: SDL_RWFromFile falló: %s (%s)", resolved.c_str(), SDL_GetError());
    }

    // Fallback: directorio externo directo con la ruta ya normalizada.
    if (SDL_RWops *rw = open_from_external(relative, mode)) return rw;

    SDL_Log("PS2_RWFromFile: no encontrado: %s (normalizado: %s)", path, relative.c_str());
    return nullptr;
}

}  // namespace port

extern "C" SDL_RWops *PS2_RWFromFile(const char *path, const char *mode) {
    return port::RWFromPS2File(path, mode);
}
