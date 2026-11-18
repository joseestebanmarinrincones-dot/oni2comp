#include "ps2_mem.hpp"

#include <algorithm>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>

#if defined(__ANDROID__)
#include <android/log.h>
#define PS2HW_TAG "PS2HW"
#define PS2HW_LOGI(...) __android_log_print(ANDROID_LOG_INFO, PS2HW_TAG, __VA_ARGS__)
#define PS2HW_LOGW(...) __android_log_print(ANDROID_LOG_WARN, PS2HW_TAG, __VA_ARGS__)
#else
#define PS2HW_LOGI(...) do { \
    std::fprintf(stdout, "[PS2HW] "); \
    std::fprintf(stdout, __VA_ARGS__); \
    std::fputc('\n', stdout); \
    std::fflush(stdout); \
} while (0)
#define PS2HW_LOGW(...) do { \
    std::fprintf(stderr, "[PS2HW] "); \
    std::fprintf(stderr, __VA_ARGS__); \
    std::fputc('\n', stderr); \
    std::fflush(stderr); \
} while (0)
#endif

namespace ps2 {

// Array global de 32 MB, alineado a cache line. Es la RAM principal emulada.
alignas(64) std::uint8_t ps2_ram[kPs2RamSize] = {};

namespace {

std::unordered_map<std::uint32_t, std::uint8_t> g_hw_shadow;
std::unordered_set<std::uint32_t> g_bad_addresses;
bool g_hw_logging = true;
Ps2Memory::HardwareWriteHook g_hw_write_hook = nullptr;
Ps2Memory::HardwareReadHook g_hw_read_hook = nullptr;

const char *kind_name(Ps2Memory::HwKind kind) {
    switch (kind) {
    case Ps2Memory::HwKind::Dma: return "DMA";
    case Ps2Memory::HwKind::Gs: return "GS";
    case Ps2Memory::HwKind::Intc: return "INTC";
    case Ps2Memory::HwKind::Timer: return "TIMER";
    case Ps2Memory::HwKind::Other: return "HW";
    default: return "?";
    }
}

void log_bad_access(std::uint32_t address, std::size_t size) {
    if (g_bad_addresses.insert(address).second) {
        PS2HW_LOGW("acceso fuera de mapa: 0x%08X (%u bytes)",
                   address, static_cast<unsigned>(size));
    }
}

void log_hardware_write(const Ps2Memory::HardwareAccess &access) {
    PS2HW_LOGI("WRITE%u %s 0x%08X = 0x%016llX",
               static_cast<unsigned>(access.size) * 8u,
               kind_name(access.kind), access.address,
               static_cast<unsigned long long>(access.value));
}

void write_hardware(std::uint32_t address, const void *data, std::size_t size) {
    const auto *bytes = static_cast<const std::uint8_t *>(data);

    std::uint64_t wide = 0;
    const std::size_t copy = std::min<std::size_t>(size, sizeof(wide));
    std::memcpy(&wide, bytes, copy);

    for (std::size_t i = 0; i < size; ++i) {
        g_hw_shadow[address + static_cast<std::uint32_t>(i)] = bytes[i];
    }

    Ps2Memory::HardwareAccess access;
    access.address = address;
    access.value = wide;
    access.size = static_cast<std::uint8_t>(std::min<std::size_t>(size, 255));
    access.kind = Ps2Memory::Classify(address);

    if (g_hw_logging) log_hardware_write(access);
    if (g_hw_write_hook) g_hw_write_hook(access);
}

void read_hardware(std::uint32_t address, void *out, std::size_t size) {
    Ps2Memory::HardwareAccess access;
    access.address = address;
    access.size = static_cast<std::uint8_t>(std::min<std::size_t>(size, 255));
    access.kind = Ps2Memory::Classify(address);

    if (g_hw_read_hook && g_hw_read_hook(access, out)) return;

    auto *bytes = static_cast<std::uint8_t *>(out);
    for (std::size_t i = 0; i < size; ++i) {
        const auto it = g_hw_shadow.find(address + static_cast<std::uint32_t>(i));
        bytes[i] = (it == g_hw_shadow.end()) ? 0 : it->second;
    }
}

}  // namespace

void Ps2Memory::Reset() {
    std::memset(ps2_ram, 0, sizeof(ps2_ram));
    g_hw_shadow.clear();
    g_bad_addresses.clear();
    PS2HW_LOGI("RAM 32 MB y shadow de hardware reseteados");
}

std::uint32_t Ps2Memory::PhysicalAddress(std::uint32_t address) {
    if (address >= 0x20000000u && address < 0x40000000u) {
        return address - 0x20000000u;   // espejo uncached de RAM
    }
    if (address >= 0x80000000u && address < 0xA0000000u) {
        return address - 0x80000000u;   // KSEG0
    }
    if (address >= 0xA0000000u && address < 0xC0000000u) {
        return address - 0xA0000000u;   // KSEG1
    }
    return address;
}

bool Ps2Memory::IsRamAddress(std::uint32_t address) {
    return PhysicalAddress(address) < kRamSize;
}

bool Ps2Memory::IsHardwareAddress(std::uint32_t address) {
    const std::uint32_t physical = PhysicalAddress(address);
    return physical >= kHwBegin && physical < kHwEnd;
}

Ps2Memory::HwKind Ps2Memory::Classify(std::uint32_t address) {
    const std::uint32_t physical = PhysicalAddress(address);

    if (physical >= 0x12000000u && physical < 0x12002000u) {
        return HwKind::Gs;
    }
    if (physical >= 0x10008000u && physical < 0x1000E100u) {
        return HwKind::Dma;
    }
    if (physical >= 0x1000F000u && physical < 0x1000F100u) {
        return HwKind::Intc;
    }
    if (physical >= 0x10000000u && physical < 0x100000C0u) {
        return HwKind::Timer;
    }
    if (physical >= kHwBegin && physical < kHwEnd) {
        return HwKind::Other;
    }
    return HwKind::None;
}

void Ps2Memory::SetHardwareLogging(bool enabled) {
    g_hw_logging = enabled;
}

bool Ps2Memory::HardwareLogging() {
    return g_hw_logging;
}

void Ps2Memory::SetHardwareWriteHook(HardwareWriteHook hook) {
    g_hw_write_hook = hook;
}

void Ps2Memory::SetHardwareReadHook(HardwareReadHook hook) {
    g_hw_read_hook = hook;
}

void Ps2Memory::ReadBytes(std::uint32_t address, void *out, std::size_t size) {
    if (!out || size == 0) return;

    const std::uint32_t physical = PhysicalAddress(address);
    if (physical < kRamSize) {
        if (size <= kRamSize - physical) {
            std::memcpy(out, ps2_ram + physical, size);
        } else {
            std::memset(out, 0, size);
            log_bad_access(physical, size);
        }
        return;
    }

    if (physical >= kHwBegin && physical < kHwEnd) {
        if (size <= kHwEnd - physical) {
            read_hardware(physical, out, size);
        } else {
            std::memset(out, 0, size);
            log_bad_access(physical, size);
        }
        return;
    }

    std::memset(out, 0, size);
    log_bad_access(physical, size);
}

void Ps2Memory::WriteBytes(std::uint32_t address, const void *data, std::size_t size) {
    if (!data || size == 0) return;

    const std::uint32_t physical = PhysicalAddress(address);
    if (physical < kRamSize) {
        if (size <= kRamSize - physical) {
            std::memcpy(ps2_ram + physical, data, size);
        } else {
            log_bad_access(physical, size);
        }
        return;
    }

    if (physical >= kHwBegin && physical < kHwEnd) {
        if (size <= kHwEnd - physical) {
            write_hardware(physical, data, size);
        } else {
            log_bad_access(physical, size);
        }
        return;
    }

    log_bad_access(physical, size);
}

}  // namespace ps2
