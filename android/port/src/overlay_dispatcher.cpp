#include "overlay_dispatcher.hpp"

#include <cstdio>
#include <cstring>
#include <exception>
#include <utility>

namespace {

void put_u32(std::vector<std::uint8_t> &out, std::uint32_t value) {
    std::uint8_t bytes[4];
    std::memcpy(bytes, &value, sizeof(value));
    out.insert(out.end(), bytes, bytes + 4);
}

bool get_u32(const std::uint8_t *data, std::size_t size, std::size_t &offset,
             std::uint32_t &value) {
    if (offset + 4 > size) return false;
    std::memcpy(&value, data + offset, 4);
    offset += 4;
    return true;
}

}  // namespace

void OverlayDispatcher::set_error(const std::string &message) {
    last_error_ = message;
    std::fprintf(stderr, "OverlayDispatcher: %s\n", last_error_.c_str());
}

bool OverlayDispatcher::RegisterOverlay(std::uint32_t base, std::uint32_t size, Handler handler) {
    if (!handler) {
        set_error("RegisterOverlay con handler vacío");
        return false;
    }
    if (size == 0) {
        set_error("RegisterOverlay con tamaño cero");
        return false;
    }
    if (handlers_.count(base) != 0) {
        set_error("ya hay un overlay registrado en esa base");
        return false;
    }
    const std::uint64_t end = static_cast<std::uint64_t>(base) + size;
    if (end > 0x1'0000'0000ull) {
        set_error("la ventana del overlay se sale del espacio de 32 bits");
        return false;
    }

    handlers_.emplace(base, std::move(handler));
    sizes_.emplace(base, size);
    std::fprintf(stderr, "OverlayDispatcher: base 0x%08X size 0x%X registrada\n", base, size);
    return true;
}

bool OverlayDispatcher::RegisterExact(std::uint32_t ps2_address, Handler handler) {
    if (!handler) {
        set_error("RegisterExact con handler vacío");
        return false;
    }
    entries_[ps2_address] = std::move(handler);
    return true;
}

bool OverlayDispatcher::JumpToOverlay(std::uint32_t ps2_address) {
    last_error_.clear();
    target_ = ps2_address;
    current_base_ = 0;

    // Ventana que contiene la dirección. Si hay solapes, gana la base más
    // alta (la captura más específica, p. ej. 02394000 sobre 02390000).
    bool found = false;
    std::uint32_t best_base = 0;
    for (const auto &[base, size] : sizes_) {
        if (ps2_address < base) continue;
        const std::uint32_t offset = ps2_address - base;
        if (offset >= size) continue;
        if (!found || base > best_base) {
            found = true;
            best_base = base;
        }
    }
    if (found) current_base_ = best_base;

    // 1) Entrada exacta (prioridad: variantes y funciones internas).
    if (const auto exact = entries_.find(ps2_address); exact != entries_.end()) {
        if (loader_ && !loader_(ps2_address)) {
            set_error("el loader no pudo preparar la dirección exacta");
            return false;
        }
        try {
            exact->second();
            return true;
        } catch (const std::exception &error) {
            set_error(std::string("excepción en handler exacto: ") + error.what());
            return false;
        }
    }

    // 2) Handler de la ventana.
    if (!found) {
        set_error("ningún overlay contiene la dirección");
        return false;
    }

    const auto handler = handlers_.find(best_base);
    if (handler == handlers_.end()) {
        set_error("ventana sin handler registrado");
        return false;
    }

    if (loader_ && !loader_(ps2_address)) {
        set_error("el loader no pudo cargar el overlay");
        return false;
    }

    try {
        handler->second();
        return true;
    } catch (const std::exception &error) {
        set_error(std::string("excepción en handler de overlay: ") + error.what());
        return false;
    }
}

void OverlayDispatcher::SaveState(std::vector<std::uint8_t> &out) const {
    put_u32(out, target_);
    put_u32(out, current_base_);
    put_u32(out, static_cast<std::uint32_t>(last_error_.size()));
    out.insert(out.end(), last_error_.begin(), last_error_.end());
    put_u32(out, static_cast<std::uint32_t>(sizes_.size()));
    for (const auto &pair : sizes_) {
        put_u32(out, pair.first);
        put_u32(out, pair.second);
    }
}

bool OverlayDispatcher::LoadState(const std::uint8_t *data, std::size_t size) {
    if (data == nullptr || size == 0) return false;
    std::size_t offset = 0;
    std::uint32_t target = 0;
    std::uint32_t current_base = 0;
    std::uint32_t error_size = 0;
    if (!get_u32(data, size, offset, target) ||
        !get_u32(data, size, offset, current_base) ||
        !get_u32(data, size, offset, error_size)) {
        return false;
    }
    if (offset + error_size > size) return false;
    std::string error(reinterpret_cast<const char *>(data + offset),
                      error_size);
    offset += error_size;

    std::uint32_t count = 0;
    if (!get_u32(data, size, offset, count) || count > 100000u) return false;

    std::unordered_map<std::uint32_t, std::uint32_t> sizes;
    sizes.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint32_t base = 0;
        std::uint32_t bytes = 0;
        if (!get_u32(data, size, offset, base) ||
            !get_u32(data, size, offset, bytes)) {
            return false;
        }
        sizes[base] = bytes;
    }

    target_ = target;
    current_base_ = current_base;
    last_error_ = std::move(error);
    sizes_ = std::move(sizes);
    // handlers_ y entries_ son codigo ya registrado; se conservan.
    return true;
}

void OverlayDispatcher::Clear() {
    handlers_.clear();
    sizes_.clear();
    entries_.clear();
    loader_ = nullptr;
    target_ = 0;
    current_base_ = 0;
    last_error_.clear();
}
