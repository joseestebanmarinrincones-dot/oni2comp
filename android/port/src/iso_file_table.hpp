#pragma once

// Constantes de la tabla de archivos/LBA encontrada en SLUS_203.93.
// Ver games/onimusha2/disasm/ISO_FILE_TABLE.md y iso_file_table.csv.

#include <cstdint>

namespace onimusha2 {

struct IsoFileEntry {
    std::uint32_t lba;
    std::uint32_t size;
};

inline constexpr std::uint32_t kIsoFileTableVaddr = 0x002B4660;
inline constexpr std::uint32_t kIsoFileTableElfOffset = 0x001B8060;
inline constexpr std::uint32_t kIsoFileTableCount = 1416;
inline constexpr std::uint32_t kIsoFileTableEnd = 0x002B72A0;

inline constexpr std::uint32_t kSceneIndexTableVaddr = 0x002B42B8;
inline constexpr std::uint32_t kSceneIndexTableCount = 388;
inline constexpr std::uint32_t kBootIndexTableVaddr = 0x002B45D0;
inline constexpr std::uint32_t kBootIndexTableCount = 8;

}  // namespace onimusha2
