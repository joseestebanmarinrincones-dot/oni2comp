#include "port_fs.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string_view>
#include <system_error>
#include <unordered_map>

#include <fcntl.h>
#include <unistd.h>

#if defined(PORTFS_LINK_WRAP)
extern "C" {
FILE *__real_fopen(const char *, const char *);
int __real_open(const char *, int, ...);
int __real_stat(const char *, struct stat *);
int __real_access(const char *, int);
DIR *__real_opendir(const char *);
}
#endif

#if defined(__ANDROID__)
#include <SDL_system.h>
#include <android/log.h>
#define PORTFS_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "PortFS", __VA_ARGS__)
#else
#define PORTFS_LOGI(...) ((void)0)
#endif

namespace portfs {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view kVolumes[] = {
    "cdrom0", "cdrom1", "cdrom", "cdfs", "dvdrom",
    "host0", "host", "mc0", "mc1", "pfs0",
    "mass", "usb0", "usb1", "rom0", "rom1",
};

std::mutex g_mutex;
std::vector<std::string> g_roots;
std::unordered_map<std::string, std::string> g_cache;
std::once_flag g_once;

bool ascii_iequals(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        const unsigned char a = static_cast<unsigned char>(left[i]);
        const unsigned char b = static_cast<unsigned char>(right[i]);
        if (std::tolower(a) != std::tolower(b)) return false;
    }
    return true;
}

std::string lower_ascii(std::string_view text) {
    std::string out(text);
    for (char &c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

void add_root_unlocked(std::string root) {
    while (root.size() > 1 && (root.back() == '/' || root.back() == '\\')) root.pop_back();
    if (root.empty()) return;
    for (const std::string &existing : g_roots) {
        if (existing == root) return;
    }
    g_roots.push_back(std::move(root));
    g_cache.clear();
}

void initialize_roots() {
    std::lock_guard<std::mutex> lock(g_mutex);

    if (const char *env = std::getenv("PORT_FS_ROOT"); env && *env) {
        std::string list(env);
        std::size_t start = 0;
        while (start <= list.size()) {
            const std::size_t end = list.find(';', start);
            const std::string part = list.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!part.empty()) add_root_unlocked(part);
            if (end == std::string::npos) break;
            start = end + 1;
        }
    }

#if defined(__ANDROID__)
    const char *external = SDL_AndroidGetExternalStoragePath();
    const char *internal = SDL_AndroidGetInternalStoragePath();
    if (external && *external) {
        const std::string base(external);
        add_root_unlocked(base);
        add_root_unlocked(base + "/data");
        add_root_unlocked(base + "/DATA");
        add_root_unlocked(base + "/CD");
        add_root_unlocked(base + "/files");
    }
    if (internal && *internal) add_root_unlocked(internal);
#endif

    if (g_roots.empty()) {
        std::error_code ec;
        const auto cwd = fs::current_path(ec);
        if (!ec) add_root_unlocked(cwd.string());
    }

    for (const std::string &root : g_roots) PORTFS_LOGI("root: %s", root.c_str());
}

std::vector<std::string> components(const std::string &relative) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= relative.size(); ++i) {
        if (i != relative.size() && relative[i] != '/') continue;
        std::string part = relative.substr(start, i - start);
        start = i + 1;
        if (part.empty() || part == ".") continue;
        if (part == "..") return {};
        parts.push_back(std::move(part));
    }
    return parts;
}

std::string resolve_in_root(const std::string &root, const std::string &relative) {
    std::error_code ec;
    fs::path current(root);

    for (const std::string &part : components(relative)) {
        const fs::path exact = current / part;
        if (fs::exists(exact, ec)) {
            current = exact;
            continue;
        }

        bool found = false;
        fs::directory_iterator iterator(current, fs::directory_options::skip_permission_denied, ec);
        if (ec) return {};
        for (const fs::directory_entry &entry : iterator) {
            const std::string name = entry.path().filename().string();
            if (ascii_iequals(name, part)) {
                current = entry.path();
                found = true;
                break;
            }
        }
        if (!found) return {};
    }

    if (fs::is_regular_file(current, ec)) return current.string();
    return {};
}

FILE *real_fopen(const char *path, const char *mode) {
#if defined(PORTFS_LINK_WRAP)
    return __real_fopen(path, mode);
#else
    return std::fopen(path, mode);
#endif
}

int real_open(const char *path, int flags, mode_t mode) {
#if defined(PORTFS_LINK_WRAP)
    return __real_open(path, flags, mode);
#else
    return ::open(path, flags, mode);
#endif
}

int real_stat(const char *path, struct stat *info) {
#if defined(PORTFS_LINK_WRAP)
    return __real_stat(path, info);
#else
    return ::stat(path, info);
#endif
}

int real_access(const char *path, int mode) {
#if defined(PORTFS_LINK_WRAP)
    return __real_access(path, mode);
#else
    return ::access(path, mode);
#endif
}

DIR *real_opendir(const char *path) {
#if defined(PORTFS_LINK_WRAP)
    return __real_opendir(path);
#else
    return ::opendir(path);
#endif
}

}  // namespace

void initialize() {
    std::call_once(g_once, initialize_roots);
}

void add_root(const std::string &root) {
    initialize();
    std::lock_guard<std::mutex> lock(g_mutex);
    add_root_unlocked(root);
}

void clear_cache() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_cache.clear();
}

bool is_console_path(const std::string &path) {
    const std::size_t colon = path.find(':');
    if (colon == std::string::npos || colon == 0 || colon > 16) return false;
    const std::string prefix = lower_ascii(std::string_view(path).substr(0, colon));
    for (std::string_view volume : kVolumes) {
        if (prefix == volume) return true;
    }
    return false;
}

std::string normalize(const std::string &path) {
    std::string text = path;
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    text = text.substr(first, last - first + 1);
    std::replace(text.begin(), text.end(), '\\', '/');

    if (is_console_path(text)) {
        const std::size_t colon = text.find(':');
        text.erase(0, colon + 1);
    }

    while (!text.empty() && text.front() == '/') text.erase(text.begin());

    // ISO9660: FILE.BIN;1 -> FILE.BIN
    const std::size_t semicolon = text.find_last_of(';');
    if (semicolon != std::string::npos && semicolon + 1 < text.size()) {
        bool version = true;
        for (std::size_t i = semicolon + 1; i < text.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(text[i]))) { version = false; break; }
        }
        if (version) text.erase(semicolon);
    }

    const std::vector<std::string> parts = components(text);
    if (parts.empty()) return {};
    std::string out;
    for (const std::string &part : parts) {
        if (!out.empty()) out += '/';
        out += part;
    }
    return out;
}

std::string resolve(const std::string &console_path) {
    if (!is_console_path(console_path)) return {};
    initialize();
    const std::string relative = normalize(console_path);
    if (relative.empty()) return {};

    std::lock_guard<std::mutex> lock(g_mutex);
    if (const auto cached = g_cache.find(relative); cached != g_cache.end()) return cached->second;

    for (const std::string &root : g_roots) {
        const std::string found = resolve_in_root(root, relative);
        if (!found.empty()) {
            g_cache[relative] = found;
            PORTFS_LOGI("%s -> %s", console_path.c_str(), found.c_str());
            return found;
        }
    }

    g_cache[relative] = {};
    PORTFS_LOGI("no encontrado: %s (normalizado: %s)", console_path.c_str(), relative.c_str());
    return {};
}

FILE *fopen(const char *path, const char *mode) {
    if (!path) {
        errno = EINVAL;
        return nullptr;
    }
    if (!is_console_path(path)) return real_fopen(path, mode);
    const std::string resolved = resolve(path);
    if (resolved.empty()) {
        errno = ENOENT;
        return nullptr;
    }
    return real_fopen(resolved.c_str(), mode);
}

int open(const char *path, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode = static_cast<mode_t>(va_arg(args, int));
        va_end(args);
    }
    if (!path) {
        errno = EINVAL;
        return -1;
    }
    if (!is_console_path(path)) return real_open(path, flags, mode);
    const std::string resolved = resolve(path);
    if (resolved.empty()) {
        errno = ENOENT;
        return -1;
    }
    return real_open(resolved.c_str(), flags, mode);
}

int stat(const char *path, struct stat *info) {
    if (!path || !info) {
        errno = EINVAL;
        return -1;
    }
    if (!is_console_path(path)) return real_stat(path, info);
    const std::string resolved = resolve(path);
    if (resolved.empty()) {
        errno = ENOENT;
        return -1;
    }
    return real_stat(resolved.c_str(), info);
}

int access(const char *path, int mode) {
    if (!path) {
        errno = EINVAL;
        return -1;
    }
    if (!is_console_path(path)) return real_access(path, mode);
    const std::string resolved = resolve(path);
    if (resolved.empty()) {
        errno = ENOENT;
        return -1;
    }
    return real_access(resolved.c_str(), mode);
}

DIR *opendir(const char *path) {
    if (!path) {
        errno = EINVAL;
        return nullptr;
    }
    if (!is_console_path(path)) return real_opendir(path);
    const std::string resolved = resolve(path);
    if (resolved.empty()) {
        errno = ENOENT;
        return nullptr;
    }
    return real_opendir(resolved.c_str());
}

}  // namespace portfs

// ---------------------------------------------------------------------------
// Intercepción a nivel de enlace: CMake añade -Wl,--wrap=fopen, etc.
// El código original sigue llamando a fopen() y el linker lo redirige aquí.
// ---------------------------------------------------------------------------
#if defined(PORTFS_LINK_WRAP)

extern "C" FILE *__wrap_fopen(const char *path, const char *mode) {
    return portfs::fopen(path, mode);
}

extern "C" int __wrap_open(const char *path, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode = static_cast<mode_t>(va_arg(args, int));
        va_end(args);
    }
    if (!path) {
        errno = EINVAL;
        return -1;
    }
    if (!portfs::is_console_path(path)) {
        return __real_open(path, flags, mode);
    }
    const std::string resolved = portfs::resolve(path);
    if (resolved.empty()) {
        errno = ENOENT;
        return -1;
    }
    return __real_open(resolved.c_str(), flags, mode);
}

extern "C" int __wrap_stat(const char *path, struct stat *info) {
    return portfs::stat(path, info);
}

extern "C" int __wrap_access(const char *path, int mode) {
    return portfs::access(path, mode);
}

extern "C" DIR *__wrap_opendir(const char *path) {
    return portfs::opendir(path);
}

#endif
