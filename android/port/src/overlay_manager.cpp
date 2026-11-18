#include "overlay_manager.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace {

void *allocate_aligned(std::size_t size, std::size_t alignment) {
#if defined(_WIN32)
    return _aligned_malloc(size, alignment);
#else
    void *pointer = nullptr;
    if (posix_memalign(&pointer, alignment, size) != 0) return nullptr;
    return pointer;
#endif
}

void free_aligned(void *pointer) {
#if defined(_WIN32)
    _aligned_free(pointer);
#else
    std::free(pointer);
#endif
}

bool is_hex8(const std::string &text, std::size_t offset) {
    if (offset + 8 > text.size()) return false;
    for (std::size_t i = 0; i < 8; ++i) {
        if (!std::isxdigit(static_cast<unsigned char>(text[offset + i]))) return false;
    }
    return true;
}

std::uint32_t parse_hex8(const std::string &text, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        const char c = text[offset + i];
        const unsigned digit = (c >= '0' && c <= '9') ? static_cast<unsigned>(c - '0')
                             : (c >= 'a' && c <= 'f') ? static_cast<unsigned>(c - 'a' + 10)
                             : static_cast<unsigned>(c - 'A' + 10);
        value = (value << 4) | digit;
    }
    return value;
}

std::string strip_bin(std::string name) {
    if (name.size() > 4) {
        const std::string suffix = name.substr(name.size() - 4);
        if (suffix == ".bin" || suffix == ".BIN") name.resize(name.size() - 4);
    }
    return name;
}

}  // namespace

void OverlayManager::AlignedDeleter::operator()(std::uint8_t *pointer) const noexcept {
    free_aligned(pointer);
}

OverlayManager::OverlayManager() = default;
OverlayManager::~OverlayManager() = default;

void OverlayManager::set_error(const std::string &message) {
    last_error_ = message;
    std::fprintf(stderr, "OverlayManager: %s\n", last_error_.c_str());
}

bool OverlayManager::allocate_blocks() {
    if (!ram_) {
        ram_.reset(static_cast<std::uint8_t *>(allocate_aligned(kMainRamSize, kAlignment)));
        if (!ram_) {
            set_error("no se pudo reservar la RAM principal de 32 MB");
            return false;
        }
    }
    if (!high_) {
        high_.reset(static_cast<std::uint8_t *>(allocate_aligned(kHighSize, kAlignment)));
        if (!high_) {
            set_error("no se pudo reservar la ventana alta de 4 MB");
            return false;
        }
    }
    std::memset(ram_.get(), 0, kMainRamSize);
    std::memset(high_.get(), 0, kHighSize);
    return true;
}

bool OverlayManager::Initialize(const std::string &overlay_dir) {
    last_error_.clear();
    overlays_.clear();

    if (!allocate_blocks()) return false;

    std::error_code error;
    if (!fs::is_directory(overlay_dir, error)) {
        set_error("no existe la carpeta de overlays: " + overlay_dir);
        return false;
    }

    for (const fs::directory_entry &entry : fs::directory_iterator(overlay_dir, error)) {
        if (error) break;
        if (!entry.is_regular_file(error)) continue;
        const fs::path path = entry.path();
        if (path.extension() != ".bin" && path.extension() != ".BIN") continue;

        const std::string stem = path.stem().string();
        // 01F64000_856903D1
        if (stem.size() < 17 || stem[8] != '_') continue;
        if (!is_hex8(stem, 0) || !is_hex8(stem, 9)) continue;

        Overlay overlay;
        overlay.name = stem;
        overlay.path = path.string();
        overlay.base = parse_hex8(stem, 0);
        overlay.stamp = parse_hex8(stem, 9);
        overlay.size = static_cast<std::uint32_t>(fs::file_size(path, error));
        if (error) {
            error.clear();
            continue;
        }
        overlays_.push_back(std::move(overlay));
    }

    if (error) {
        set_error("error leyendo la carpeta de overlays: " + error.message());
        return false;
    }
    if (overlays_.empty()) {
        set_error("no hay archivos .bin de overlay en " + overlay_dir);
        return false;
    }

    std::sort(overlays_.begin(), overlays_.end(), [](const Overlay &a, const Overlay &b) {
        if (a.base != b.base) return a.base < b.base;
        return a.stamp < b.stamp;
    });

    std::fprintf(stderr, "OverlayManager: %zu overlays, RAM %u MB en %p, ventana alta %u MB\n",
                 overlays_.size(), kMainRamSize >> 20,
                 static_cast<void *>(ram_.get()), kHighSize >> 20);
    return true;
}

const OverlayManager::Overlay *OverlayManager::FindByName(const std::string &name) const {
    const std::string wanted = strip_bin(name);
    for (const Overlay &overlay : overlays_) {
        if (overlay.name == wanted) return &overlay;
    }
    return nullptr;
}

const OverlayManager::Overlay *OverlayManager::FindByBase(std::uint32_t base,
                                                          std::uint32_t stamp) const {
    for (const Overlay &overlay : overlays_) {
        if (overlay.base == base && (stamp == 0 || overlay.stamp == stamp)) return &overlay;
    }
    return nullptr;
}

bool OverlayManager::read_file(const Overlay &overlay,
                               std::vector<std::uint8_t> &data) const {
    std::ifstream file(overlay.path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    const std::streamoff size = file.tellg();
    if (size <= 0 || size > static_cast<std::streamoff>(std::numeric_limits<std::uint32_t>::max())) {
        return false;
    }
    data.resize(static_cast<std::size_t>(size));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(size));
    return static_cast<bool>(file) || file.eof();
}

bool OverlayManager::Load(const std::string &name) {
    if (!ram_ || !high_) {
        set_error("Load sin Initialize()");
        return false;
    }
    const Overlay *found = FindByName(name);
    if (!found) {
        set_error("overlay no encontrado: " + name);
        return false;
    }

    std::vector<std::uint8_t> data;
    if (!read_file(*found, data)) {
        set_error("no se pudo leer: " + found->path);
        return false;
    }

    const std::uint64_t begin = found->base;
    const std::uint64_t end = begin + data.size();
    std::uint8_t *destination = nullptr;

    if (begin < kMainRamSize && end <= kMainRamSize) {
        destination = ram_.get() + begin;
    } else if (begin >= kHighBase && end <= static_cast<std::uint64_t>(kHighBase) + kHighSize) {
        destination = high_.get() + (begin - kHighBase);
    } else {
        set_error("el overlay " + found->name + " no cabe en la RAM/ventana mapeada");
        return false;
    }

    std::memcpy(destination, data.data(), data.size());

    Overlay *mutable_overlay = const_cast<Overlay *>(found);
    mutable_overlay->size = static_cast<std::uint32_t>(data.size());
    mutable_overlay->loaded = true;

    std::fprintf(stderr, "OverlayManager: cargado %s (%zu bytes) en 0x%08X%s\n",
                 found->name.c_str(), data.size(), found->base,
                 begin >= kHighBase ? " [ventana alta]" : "");
    return true;
}

bool OverlayManager::LoadByBase(std::uint32_t base, std::uint32_t stamp) {
    const Overlay *found = FindByBase(base, stamp);
    if (!found) {
        set_error(stamp ? "overlay no encontrado para base/stamp"
                        : "overlay no encontrado para la base indicada");
        return false;
    }
    if (stamp == 0) {
        // Una base puede tener dos variantes (01F90000 A/B): no adivines.
        std::size_t matches = 0;
        for (const Overlay &overlay : overlays_) if (overlay.base == base) ++matches;
        if (matches > 1) {
            set_error("base ambigua; usa el nombre o el stamp");
            return false;
        }
    }
    return Load(found->name);
}

bool OverlayManager::LoadForAddress(std::uint32_t address) {
    const Overlay *best = nullptr;
    for (const Overlay &overlay : overlays_) {
        if (!overlay.Contains(address)) continue;
        if (!best || overlay.base > best->base) best = &overlay;
    }
    if (!best) {
        set_error("ningún overlay contiene la dirección indicada");
        return false;
    }
    return Load(best->name);
}

std::uint8_t *OverlayManager::Resolve(std::uint32_t address) {
    if (address < kMainRamSize) return ram_ ? ram_.get() + address : nullptr;
    if (address >= kHighBase && address < static_cast<std::uint64_t>(kHighBase) + kHighSize) {
        return high_ ? high_.get() + (address - kHighBase) : nullptr;
    }
    return nullptr;
}

const std::uint8_t *OverlayManager::Resolve(std::uint32_t address) const {
    return const_cast<OverlayManager *>(this)->Resolve(address);
}

std::size_t OverlayManager::LoadedCount() const {
    std::size_t count = 0;
    for (const Overlay &overlay : overlays_) if (overlay.loaded) ++count;
    return count;
}

void OverlayManager::Clear() {
    if (ram_) std::memset(ram_.get(), 0, kMainRamSize);
    if (high_) std::memset(high_.get(), 0, kHighSize);
    for (Overlay &overlay : overlays_) overlay.loaded = false;
}
