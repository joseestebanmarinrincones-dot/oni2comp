#include "ps2_cd_stream.hpp"

#include "ps2_mem.hpp"

#include <cstddef>
#include <cstdint>

namespace ps2::cd {
namespace {

Reader g_reader;
std::uint32_t g_stream_lba = 0;

constexpr std::uint32_t kSectorSize = 2048;

}  // namespace

void SetReader(Reader reader) {
    g_reader = std::move(reader);
}

void ClearReader() {
    g_reader = nullptr;
}

void SetStreamLba(std::uint32_t lba) {
    g_stream_lba = lba;
}

std::uint32_t StreamLba() {
    return g_stream_lba;
}

bool ReadDvdSectors(std::uint32_t lba, std::uint32_t count,
                    std::uint32_t destination) {
    if (!g_reader) return false;
    if (count == 0) return true;

    const std::uint32_t physical =
        Ps2Memory::PhysicalAddress(destination);
    const std::uint64_t bytes =
        static_cast<std::uint64_t>(count) * kSectorSize;

    if (physical >= kPs2RamSize) return false;
    if (bytes > kPs2RamSize - physical) return false;

    return g_reader(lba, count,
                    ps2_ram + static_cast<std::size_t>(physical));
}

int sceCdStStart(std::uint32_t lba, std::uint32_t mode) {
    (void)mode;
    g_stream_lba = lba;
    return 0;
}

int sceCdStStop() {
    g_stream_lba = 0;
    return 0;
}

int sceCdStSeek(std::uint32_t lba) {
    g_stream_lba = lba;
    return 0;
}

int sceCdStRead(std::uint32_t sectors, std::uint32_t destination,
                std::uint32_t mode, int *err) {
    (void)mode;
    if (err != nullptr) *err = 0;

    if (!ReadDvdSectors(g_stream_lba, sectors, destination)) {
        if (err != nullptr) *err = 1;
        return -1;
    }

    g_stream_lba += sectors;
    return 0;
}

std::int32_t FUN_00124C20(std::uint32_t sectors,
                          std::uint32_t destination) {
    int err = 0;
    const int result = sceCdStRead(sectors, destination, 0, &err);
    return (err == 0 && result == 0) ? 0 : -1;
}

}  // namespace ps2::cd
