#pragma once

// Lector de la imagen ISO del juego para Android.
//
// Usa SDL_RWops y Sint64 en todas las posiciones, así que funciona con
// archivos mayores de 2 GB (disc.iso de 3,78 GB).
//
//   IsoReader iso;
//   if (iso.Open("disc.iso")) {
//       std::vector<std::uint8_t> sector(IsoReader::kSectorSize);
//       if (iso.ReadSectors(16, 1, sector.data())) { ... }
//   }
//
// Si la ruta no es absoluta, la busca en SDL_AndroidGetExternalStoragePath();
// si falla, prueba SDL_AndroidGetInternalStoragePath() y por último la ruta
// tal cual.

#include <SDL_rwops.h>

#include <cstddef>
#include <cstdint>
#include <string>

class IsoReader {
public:
    static constexpr std::uint32_t kSectorSize = 2048;  // DVD/ISO 9660

    IsoReader() = default;
    ~IsoReader();

    IsoReader(const IsoReader &) = delete;
    IsoReader &operator=(const IsoReader &) = delete;
    IsoReader(IsoReader &&other) noexcept;
    IsoReader &operator=(IsoReader &&other) noexcept;

    // Abre disc.iso. Devuelve false y deja LastError() con el motivo.
    bool Open(const std::string &path = "disc.iso");

    // Abre la imagen desde un descriptor de Android (ACTION_OPEN_DOCUMENT).
    // Toma posesión del fd: lo cierra al fallar o en Close(). `label` es solo
    // para logs (por ejemplo, el nombre visible del documento).
    bool OpenFd(int fd, const std::string &label = "disc.iso (fd)");

    void Close();

    bool IsOpen() const { return rw_ != nullptr; }
    explicit operator bool() const { return IsOpen(); }

    // Tamaño total y número de sectores completos. Sint64 (>2 GB).
    Sint64 Size() const { return size_; }
    Sint64 SectorCount() const { return size_ >= 0 ? size_ / kSectorSize : 0; }
    const std::string &Path() const { return path_; }
    const std::string &LastError() const { return last_error_; }

    // Lee count sectores de 2048 bytes desde el LBA indicado.
    // buffer debe tener count * 2048 bytes.
    bool ReadSectors(std::uint32_t lba, std::uint32_t count, void *buffer);

    // Lectura por offset absoluto (64 bits). Útil para tablas del ISO.
    bool ReadBytes(Sint64 offset, void *buffer, std::size_t bytes);

private:
    bool accept_open(const std::string &opened_path);
    bool read_at(Sint64 offset, void *buffer, std::size_t bytes);
    void set_error(const std::string &message);

    SDL_RWops *rw_ = nullptr;
    Sint64 size_ = 0;
    std::string path_;
    std::string last_error_;
};
