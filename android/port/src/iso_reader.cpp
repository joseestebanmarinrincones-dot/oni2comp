#include "iso_reader.hpp"

#include <SDL_error.h>
#include <SDL_log.h>
#include <SDL_system.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

#if defined(_WIN32)
#include <io.h>
#else
#include <cerrno>
#include <unistd.h>
#endif

namespace {
constexpr std::size_t kChunk = 1u << 20;  // 1 MiB por lectura

#if !defined(_WIN32)
// SDL_RWops sobre un fd ya abierto. Usa pread para no depender del offset
// compartido del descriptor (algunos DocumentProvider lo reutilizan).
struct FdStream {
    int fd = -1;
    Sint64 size = 0;
    Sint64 position = 0;
};

Sint64 SDLCALL fd_stream_size(SDL_RWops *context) {
    auto *stream = static_cast<FdStream *>(context->hidden.unknown.data1);
    return stream != nullptr ? stream->size : 0;
}

Sint64 SDLCALL fd_stream_seek(SDL_RWops *context, Sint64 offset, int whence) {
    auto *stream = static_cast<FdStream *>(context->hidden.unknown.data1);
    if (stream == nullptr) {
        SDL_Error(SDL_EFSEEK);
        return -1;
    }

    Sint64 base = 0;
    switch (whence) {
    case RW_SEEK_SET: base = 0; break;
    case RW_SEEK_CUR: base = stream->position; break;
    case RW_SEEK_END: base = stream->size; break;
    default:
        SDL_Error(SDL_EFSEEK);
        return -1;
    }

    if (offset == std::numeric_limits<Sint64>::min()) {
        SDL_Error(SDL_EFSEEK);
        return -1;
    }
    if (offset < 0) {
        if (base < -offset) {
            SDL_Error(SDL_EFSEEK);
            return -1;
        }
    } else if (offset > stream->size - base) {
        SDL_Error(SDL_EFSEEK);
        return -1;
    }

    const Sint64 target = base + offset;
    if (target < 0 || target > stream->size) {
        SDL_Error(SDL_EFSEEK);
        return -1;
    }
    stream->position = target;
    return target;
}

size_t SDLCALL fd_stream_read(SDL_RWops *context, void *ptr, size_t size,
                              size_t maxnum) {
    auto *stream = static_cast<FdStream *>(context->hidden.unknown.data1);
    if (stream == nullptr || ptr == nullptr || size == 0 || maxnum == 0) {
        return 0;
    }
    if (stream->position >= stream->size) return 0;
    if (size > std::numeric_limits<size_t>::max() / maxnum) {
        SDL_Error(SDL_EFREAD);
        return 0;
    }

    size_t wanted = size * maxnum;
    const Sint64 remaining = stream->size - stream->position;
    if (static_cast<std::uint64_t>(wanted) >
        static_cast<std::uint64_t>(remaining)) {
        wanted = static_cast<size_t>(remaining);
    }

    ssize_t got = -1;
    do {
        got = ::pread(stream->fd, ptr, wanted,
                      static_cast<off_t>(stream->position));
    } while (got < 0 && errno == EINTR);

    if (got <= 0) {
        if (got < 0) SDL_Error(SDL_EFREAD);
        return 0;
    }
    stream->position += static_cast<Sint64>(got);
    return static_cast<size_t>(got) / size;
}

size_t SDLCALL fd_stream_write(SDL_RWops *, const void *, size_t, size_t) {
    SDL_Error(SDL_EFWRITE);
    return 0;
}

int SDLCALL fd_stream_close(SDL_RWops *context) {
    if (context == nullptr) return 0;
    auto *stream = static_cast<FdStream *>(context->hidden.unknown.data1);
    if (stream != nullptr) {
        if (stream->fd >= 0) ::close(stream->fd);
        delete stream;
    }
    SDL_FreeRW(context);
    return 0;
}
#endif  // !_WIN32

}  // namespace

IsoReader::~IsoReader() {
    Close();
}

IsoReader::IsoReader(IsoReader &&other) noexcept
    : rw_(other.rw_),
      size_(other.size_),
      path_(std::move(other.path_)),
      last_error_(std::move(other.last_error_)) {
    other.rw_ = nullptr;
    other.size_ = 0;
}

IsoReader &IsoReader::operator=(IsoReader &&other) noexcept {
    if (this == &other) return *this;
    Close();
    rw_ = other.rw_;
    size_ = other.size_;
    path_ = std::move(other.path_);
    last_error_ = std::move(other.last_error_);
    other.rw_ = nullptr;
    other.size_ = 0;
    return *this;
}

void IsoReader::set_error(const std::string &message) {
    last_error_ = message;
    SDL_Log("IsoReader: %s", last_error_.c_str());
}

bool IsoReader::accept_open(const std::string &opened_path) {
    size_ = SDL_RWsize(rw_);
    if (size_ < 0) {
        set_error("SDL_RWsize falló para " + opened_path + ": " + SDL_GetError());
        SDL_RWclose(rw_);
        rw_ = nullptr;
        size_ = 0;
        return false;
    }
    path_ = opened_path;
    last_error_.clear();
    SDL_Log("IsoReader: abierto %s (%lld bytes, %lld sectores de 2048)",
            path_.c_str(), static_cast<long long>(size_),
            static_cast<long long>(SectorCount()));
    return true;
}

bool IsoReader::Open(const std::string &path) {
    Close();
    last_error_.clear();

    const std::string requested = path.empty() ? "disc.iso" : path;

    // Ruta absoluta: se usa tal cual.
    if (!requested.empty() && requested.front() == '/') {
        rw_ = SDL_RWFromFile(requested.c_str(), "rb");
        if (rw_) return accept_open(requested);
    } else {
#if defined(__ANDROID__)
        // 1) Directorio externo de la app (donde está disc.iso).
        if (const char *external = SDL_AndroidGetExternalStoragePath(); external && *external) {
            std::string candidate(external);
            if (candidate.empty() || candidate.back() != '/') candidate += '/';
            candidate += requested;
            rw_ = SDL_RWFromFile(candidate.c_str(), "rb");
            if (rw_) return accept_open(candidate);
        }

        // 2) Directorio interno de la app.
        if (const char *internal = SDL_AndroidGetInternalStoragePath(); internal && *internal) {
            std::string candidate(internal);
            if (candidate.empty() || candidate.back() != '/') candidate += '/';
            candidate += requested;
            rw_ = SDL_RWFromFile(candidate.c_str(), "rb");
            if (rw_) return accept_open(candidate);
        }
#endif
        // 3) Ruta relativa al directorio de trabajo.
        rw_ = SDL_RWFromFile(requested.c_str(), "rb");
        if (rw_) return accept_open(requested);
    }

    set_error("no se pudo abrir " + requested + ": " + SDL_GetError());
    return false;
}

bool IsoReader::OpenFd(int fd, const std::string &label) {
    Close();
    last_error_.clear();

#if defined(_WIN32)
    (void)label;
    if (fd >= 0) _close(fd);
    set_error("OpenFd no esta soportado en Windows");
    return false;
#else
    if (fd < 0) {
        set_error("OpenFd con descriptor invalido");
        return false;
    }

    const off_t end = ::lseek(fd, 0, SEEK_END);
    if (end <= 0) {
        const int error_number = errno;
        ::close(fd);
        set_error("el descriptor no es un archivo seekable: " +
                  std::string(std::strerror(error_number)));
        return false;
    }
    if (end < static_cast<off_t>(kSectorSize)) {
        ::close(fd);
        set_error("el archivo del descriptor es demasiado pequeno para un ISO");
        return false;
    }

    SDL_RWops *rw = SDL_AllocRW();
    auto *stream = new (std::nothrow) FdStream;
    if (rw == nullptr || stream == nullptr) {
        delete stream;
        if (rw != nullptr) SDL_FreeRW(rw);
        ::close(fd);
        set_error("sin memoria para el lector por descriptor");
        return false;
    }

    stream->fd = fd;
    stream->size = static_cast<Sint64>(end);
    stream->position = 0;

    std::memset(rw, 0, sizeof(*rw));
    rw->size = fd_stream_size;
    rw->seek = fd_stream_seek;
    rw->read = fd_stream_read;
    rw->write = fd_stream_write;
    rw->close = fd_stream_close;
    rw->type = SDL_RWOPS_UNKNOWN;
    rw->hidden.unknown.data1 = stream;

    rw_ = rw;
    return accept_open(label.empty() ? "disc.iso (fd)" : label);
#endif
}

void IsoReader::Close() {
    if (rw_) {
        SDL_RWclose(rw_);
        rw_ = nullptr;
    }
    size_ = 0;
}

bool IsoReader::ReadBytes(Sint64 offset, void *buffer, std::size_t bytes) {
    if (!rw_) {
        set_error("ReadBytes sin archivo abierto");
        return false;
    }
    if (!buffer || bytes == 0) {
        set_error("ReadBytes con buffer nulo o tamaño cero");
        return false;
    }
    if (offset < 0 || offset > size_) {
        set_error("ReadBytes: offset fuera del archivo");
        return false;
    }
    if (static_cast<std::uint64_t>(bytes) > static_cast<std::uint64_t>(size_ - offset)) {
        set_error("ReadBytes: la lectura pasa del final del archivo");
        return false;
    }
    return read_at(offset, buffer, bytes);
}

bool IsoReader::read_at(Sint64 offset, void *buffer, std::size_t bytes) {
    const Sint64 position = SDL_RWseek(rw_, offset, RW_SEEK_SET);
    if (position < 0 || position != offset) {
        set_error("SDL_RWseek falló: " + std::string(SDL_GetError()));
        return false;
    }

    auto *out = static_cast<std::uint8_t *>(buffer);
    std::size_t done = 0;
    while (done < bytes) {
        const std::size_t want = std::min(kChunk, bytes - done);
        const std::size_t got = SDL_RWread(rw_, out + done, 1, want);
        if (got == 0) {
            set_error("lectura corta en offset " + std::to_string(offset + static_cast<Sint64>(done)));
            return false;
        }
        done += got;
    }
    return true;
}

bool IsoReader::ReadSectors(std::uint32_t lba, std::uint32_t count, void *buffer) {
    if (!rw_) {
        set_error("ReadSectors sin archivo abierto");
        return false;
    }
    if (count == 0) return true;
    if (!buffer) {
        set_error("ReadSectors con buffer nulo");
        return false;
    }

    const std::uint64_t offset = static_cast<std::uint64_t>(lba) * kSectorSize;
    const std::uint64_t bytes = static_cast<std::uint64_t>(count) * kSectorSize;

    if (offset > static_cast<std::uint64_t>(size_) ||
        bytes > static_cast<std::uint64_t>(size_) - offset) {
        set_error("ReadSectors fuera del ISO");
        return false;
    }
    if (bytes > std::numeric_limits<std::size_t>::max()) {
        set_error("ReadSectors: tamaño mayor que size_t");
        return false;
    }

    return ReadBytes(static_cast<Sint64>(offset), buffer, static_cast<std::size_t>(bytes));
}
