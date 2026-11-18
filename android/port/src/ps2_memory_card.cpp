#include "ps2_memory_card.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <system_error>
#include <vector>

#if defined(__ANDROID__)
#include <android/log.h>
#define MC_TAG "OnimushaMC"
#define MC_LOGI(...) __android_log_print(ANDROID_LOG_INFO, MC_TAG, __VA_ARGS__)
#define MC_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, MC_TAG, __VA_ARGS__)
#else
#define MC_LOGI(...)                        \
    do {                                    \
        std::fprintf(stdout, "[MC] ");      \
        std::fprintf(stdout, __VA_ARGS__);  \
        std::fputc('\n', stdout);           \
        std::fflush(stdout);                \
    } while (0)
#define MC_LOGE(...)                        \
    do {                                    \
        std::fprintf(stderr, "[MC] ");      \
        std::fprintf(stderr, __VA_ARGS__);  \
        std::fputc('\n', stderr);           \
        std::fflush(stderr);                \
    } while (0)
#endif

namespace fs = std::filesystem;

namespace ps2::mc {
namespace {

constexpr std::size_t kMaxName = 1024;
constexpr std::size_t kMcEntrySize = 64;
constexpr std::uint32_t kCardSizeKb = 8192;  // 8 MB, como una MC PS2.

struct FileHandle {
    std::fstream stream;
    fs::path path;
    int mode = 0;
    bool readable = false;
    bool writable = false;
};

void put_u16(std::uint8_t *out, std::uint16_t value) {
    out[0] = static_cast<std::uint8_t>(value & 0xFF);
    out[1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
}

void put_u32(std::uint8_t *out, std::uint32_t value) {
    out[0] = static_cast<std::uint8_t>(value & 0xFF);
    out[1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    out[2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    out[3] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
}

std::time_t file_time_to_time_t(fs::file_time_type time) {
    using namespace std::chrono;
    const auto system_time =
        time_point_cast<system_clock::duration>(
            time - fs::file_time_type::clock::now() +
            system_clock::now());
    return system_clock::to_time_t(system_time);
}

void fill_mc_datetime(std::uint8_t *out, std::time_t time) {
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif
    out[0] = 0;
    out[1] = static_cast<std::uint8_t>(tm.tm_sec);
    out[2] = static_cast<std::uint8_t>(tm.tm_min);
    out[3] = static_cast<std::uint8_t>(tm.tm_hour);
    out[4] = static_cast<std::uint8_t>(tm.tm_mday);
    out[5] = static_cast<std::uint8_t>(tm.tm_mon + 1);
    put_u16(out + 6, static_cast<std::uint16_t>(tm.tm_year + 1900));
}

std::array<std::uint8_t, kMcEntrySize> make_entry(
    const fs::path &path, const std::string &name) {
    std::array<std::uint8_t, kMcEntrySize> entry{};
    std::error_code ec;
    std::time_t time = std::time(nullptr);
    const auto ftime = fs::last_write_time(path, ec);
    if (!ec) time = file_time_to_time_t(ftime);

    fill_mc_datetime(entry.data(), time);
    fill_mc_datetime(entry.data() + 8, time);

    if (fs::is_directory(path, ec)) {
        put_u16(entry.data() + 20, 0x8027);  // subdir + readable/writeable
    } else {
        put_u16(entry.data() + 20, 0x8097);  // file + closed
        if (!ec) {
            const auto size = fs::file_size(path, ec);
            if (!ec) {
                put_u32(entry.data() + 16,
                        static_cast<std::uint32_t>(size));
            }
        }
    }

    const std::size_t length = std::min<std::size_t>(name.size(), 31);
    std::memcpy(entry.data() + 32, name.data(), length);
    entry[32 + length] = 0;
    return entry;
}

bool wildcard_match(const std::string &pattern, const std::string &name) {
    std::size_t p = 0;
    std::size_t s = 0;
    std::size_t star = std::string::npos;
    std::size_t retry = 0;

    while (s < name.size()) {
        if (p < pattern.size() &&
            (pattern[p] == '?' ||
             std::tolower(static_cast<unsigned char>(pattern[p])) ==
                 std::tolower(static_cast<unsigned char>(name[s])))) {
            ++p;
            ++s;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            retry = s;
        } else if (star != std::string::npos) {
            p = star + 1;
            s = ++retry;
        } else {
            return false;
        }
    }

    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

}  // namespace

struct Ps2MemoryCard::Impl {
    fs::path base_path;
    bool initialized = false;
    std::uint8_t *ee = nullptr;
    std::uint32_t ee_size = 0;

    std::map<int, FileHandle> files;
    int next_fd = 3;
    std::array<std::string, 2> current_dir{};
    int pending_command = kFuncNone;
    int pending_result = 0;
    int open_logs = 0;
};

namespace {

fs::path card_root(const Ps2MemoryCard::Impl &impl, int port) {
    const int normalized = (port == 1) ? 1 : 0;
    return impl.base_path / ("memcard" + std::to_string(normalized));
}

bool valid_slot(int port, int slot) {
    return port >= 0 && port <= 1 && slot == 0;
}

void set_pending(Ps2MemoryCard::Impl &impl, int command, int result) {
    impl.pending_command = command;
    impl.pending_result = result;
}

std::uint8_t *ee_pointer(const Ps2MemoryCard::Impl &impl,
                         std::uint32_t address, std::size_t size) {
    if (impl.ee == nullptr) return nullptr;
    if (address >= impl.ee_size) return nullptr;
    if (size > impl.ee_size - address) return nullptr;
    return impl.ee + address;
}

std::string read_ee_string(const Ps2MemoryCard::Impl &impl,
                           std::uint32_t address,
                           std::size_t max_length = kMaxName) {
    if (address == 0 || impl.ee == nullptr || address >= impl.ee_size) {
        return {};
    }
    const std::size_t available = impl.ee_size - address;
    const std::size_t limit = std::min(max_length, available);
    const char *text = reinterpret_cast<const char *>(impl.ee + address);
    std::size_t length = 0;
    while (length < limit && text[length] != '\0') ++length;
    return std::string(text, length);
}

bool write_ee_bytes(Ps2MemoryCard::Impl &impl, std::uint32_t address,
                    const void *data, std::size_t size) {
    std::uint8_t *destination = ee_pointer(impl, address, size);
    if (destination == nullptr || data == nullptr) return false;
    std::memcpy(destination, data, size);
    return true;
}

bool write_ee_u32(Ps2MemoryCard::Impl &impl, std::uint32_t address,
                  std::uint32_t value) {
    return write_ee_bytes(impl, address, &value, sizeof(value));
}

std::optional<fs::path> resolve_path(const Ps2MemoryCard::Impl &impl,
                                     int port, const std::string &name,
                                     bool allow_empty = false) {
    if (!valid_slot(port, 0)) return std::nullopt;

    std::string normalized = name;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    const bool absolute = !normalized.empty() && normalized.front() == '/';
    while (!normalized.empty() && normalized.front() == '/') {
        normalized.erase(normalized.begin());
    }

    fs::path relative;
    if (normalized.empty()) {
        if (!allow_empty) return std::nullopt;
        if (absolute) {
            relative = fs::path(".");
        } else {
            relative = impl.current_dir[port].empty()
                           ? fs::path(".")
                           : fs::path(impl.current_dir[port]);
        }
    } else {
        relative = fs::path(normalized);
        if (!absolute && !impl.current_dir[port].empty()) {
            relative = fs::path(impl.current_dir[port]) / relative;
        }
    }

    relative = relative.lexically_normal();
    const std::string relative_text = relative.generic_string();
    if (relative_text == ".." ||
        relative_text.rfind("../", 0) == 0) {
        return std::nullopt;
    }

    const fs::path root = card_root(impl, port);
    const fs::path full = root / relative;

    std::error_code ec;
    fs::path root_abs = fs::weakly_canonical(root, ec);
    if (ec) root_abs = fs::absolute(root).lexically_normal();
    fs::path full_abs = fs::weakly_canonical(full, ec);
    if (ec) full_abs = fs::absolute(full).lexically_normal();

    const std::string root_text = root_abs.generic_string();
    const std::string full_text = full_abs.generic_string();
    if (full_text != root_text &&
        full_text.rfind(root_text + "/", 0) != 0) {
        return std::nullopt;
    }
    return full;
}

std::string relative_to_card(const Ps2MemoryCard::Impl &impl, int port,
                             const fs::path &path) {
    std::error_code ec;
    const fs::path relative = fs::relative(path, card_root(impl, port), ec);
    if (ec) return {};
    std::string text = relative.generic_string();
    if (text == ".") text.clear();
    return text;
}

std::uint32_t directory_used_bytes(const fs::path &root) {
    std::uint32_t total = 0;
    std::error_code ec;
    if (!fs::exists(root, ec)) return 0;
    for (fs::recursive_directory_iterator it(root, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) {
            ec.clear();
            continue;
        }
        const auto size = it->file_size(ec);
        if (!ec) total += static_cast<std::uint32_t>(size);
    }
    return total;
}

}  // namespace

Ps2MemoryCard::Ps2MemoryCard() : impl_(std::make_unique<Impl>()) {}

Ps2MemoryCard::~Ps2MemoryCard() {
    Shutdown();
}

bool Ps2MemoryCard::Initialize(const std::string &base_path) {
    if (base_path.empty()) {
        MC_LOGE("Initialize: ruta base vacia");
        return false;
    }

    Shutdown();
    impl_->base_path = fs::path(base_path);
    impl_->files.clear();
    impl_->next_fd = 3;
    impl_->current_dir = {};
    impl_->pending_command = kFuncNone;
    impl_->pending_result = 0;

    std::error_code ec;
    fs::create_directories(card_root(*impl_, 0), ec);
    if (ec) {
        MC_LOGE("Initialize: no se puede crear %s: %s",
                card_root(*impl_, 0).generic_string().c_str(),
                ec.message().c_str());
        return false;
    }

    impl_->initialized = true;
    MC_LOGI("memcard0=%s", card_root(*impl_, 0).generic_string().c_str());
    return true;
}

void Ps2MemoryCard::Shutdown() {
    for (auto &entry : impl_->files) {
        if (entry.second.stream.is_open()) {
            entry.second.stream.flush();
            entry.second.stream.close();
        }
    }
    impl_->files.clear();
    impl_->pending_command = kFuncNone;
    impl_->pending_result = 0;
    impl_->initialized = false;
}

bool Ps2MemoryCard::IsInitialized() const {
    return impl_->initialized;
}

std::string Ps2MemoryCard::CardPath() const {
    return card_root(*impl_, 0).generic_string();
}

void Ps2MemoryCard::SetEeMemory(std::uint8_t *ram, std::uint32_t size) {
    impl_->ee = ram;
    impl_->ee_size = size;
}

int Ps2MemoryCard::Init(int type) {
    (void)type;
    if (!impl_->initialized) {
        MC_LOGE("Init sin Initialize()");
        return -1;
    }
    std::error_code ec;
    fs::create_directories(card_root(*impl_, 0), ec);
    if (ec) {
        MC_LOGE("Init: %s", ec.message().c_str());
        return -1;
    }
    impl_->pending_command = kFuncNone;
    impl_->pending_result = 0;
    MC_LOGI("Init OK");
    return kResSucceed;
}

int Ps2MemoryCard::GetInfo(int port, int slot, std::uint32_t type_addr,
                           std::uint32_t free_addr,
                           std::uint32_t format_addr) {
    int result = kResSucceed;
    std::uint32_t type = kTypeNoCard;
    std::uint32_t free_kb = 0;
    std::uint32_t formatted = 0;

    if (!valid_slot(port, slot)) {
        result = kResDeniedPermit;
    } else {
        const fs::path root = card_root(*impl_, port);
        std::error_code ec;
        if (fs::exists(root, ec)) {
            type = kTypePS2;
            formatted = 1;
            const std::uint32_t used = directory_used_bytes(root);
            const std::uint32_t used_kb = (used + 1023u) / 1024u;
            free_kb = used_kb < kCardSizeKb ? kCardSizeKb - used_kb : 0;
        } else {
            result = kResNoFormat;
        }
    }

    bool ok = true;
    if (type_addr != 0) ok = write_ee_u32(*impl_, type_addr, type) && ok;
    if (free_addr != 0) ok = write_ee_u32(*impl_, free_addr, free_kb) && ok;
    if (format_addr != 0) {
        ok = write_ee_u32(*impl_, format_addr, formatted) && ok;
    }
    if (!ok) result = kResDeniedPermit;

    set_pending(*impl_, kFuncGetInfo, result);
    MC_LOGI("GetInfo port=%d slot=%d type=%u free=%uKB format=%u result=%d",
            port, slot, type, free_kb, formatted, result);
    return kResSucceed;
}

int Ps2MemoryCard::Open(int port, int slot, std::uint32_t name_addr,
                        int mode) {
    int result = kResSucceed;
    const bool create_dir = (mode & kOpenCreateDir) != 0;
    int command = create_dir ? kFuncMkDir : kFuncOpen;

    if (!valid_slot(port, slot)) {
        result = kResDeniedPermit;
    } else {
        const std::string name = read_ee_string(*impl_, name_addr);
        if (name.empty()) {
            result = kResDeniedPermit;
        } else {
            const auto full = resolve_path(*impl_, port, name);
            if (!full) {
                result = kResDeniedPermit;
            } else if (create_dir) {
                std::error_code ec;
                fs::create_directories(*full, ec);
                result = ec ? kResDeniedPermit : kResSucceed;
            } else {
                const bool create = (mode & kOpenCreate) != 0;
                const bool truncate = (mode & kOpenTruncate) != 0;
                const int access = mode & 0x03;
                bool readable = access == 0 || access == 2 || access == 3;
                bool writable = access == 1 || access == 2 || access == 3;
                if (!readable && !writable) readable = true;

                std::error_code ec;
                if (create) {
                    fs::create_directories(full->parent_path(), ec);
                }
                const bool exists = fs::exists(*full, ec);

                if (!exists && !create) {
                    result = kResNoEntry;
                } else {
                    if (!exists && create) {
                        // fstream con in|out no crea el archivo; se crea
                        // vacio primero y luego se abre en lectura/escritura.
                        std::ofstream touch(*full, std::ios::binary);
                        if (!touch.is_open()) {
                            result = kResDeniedPermit;
                        }
                    }

                    if (result == kResSucceed) {
                        std::ios::openmode open_mode = std::ios::binary;
                        if (readable && writable) {
                            open_mode |= std::ios::in | std::ios::out;
                        } else if (writable) {
                            open_mode |= std::ios::out;
                            if (!truncate) open_mode |= std::ios::in;
                        } else {
                            open_mode |= std::ios::in;
                        }
                        if (truncate) open_mode |= std::ios::trunc;

                        FileHandle handle;
                        handle.path = *full;
                        handle.mode = mode;
                        handle.readable = readable;
                        handle.writable = writable;
                        handle.stream.open(*full, open_mode);

                        if (!handle.stream.is_open()) {
                            result = kResDeniedPermit;
                        } else {
                            const int fd = impl_->next_fd++;
                            impl_->files.emplace(fd, std::move(handle));
                            result = fd;
                            if (impl_->open_logs < 16) {
                                ++impl_->open_logs;
                                MC_LOGI("Open %s mode=0x%X fd=%d",
                                        name.c_str(), mode, fd);
                            }
                        }
                    }
                }
            }
        }
    }

    set_pending(*impl_, command, result);
    return kResSucceed;
}

int Ps2MemoryCard::Close(int fd) {
    int result = kResNoEntry;
    const auto it = impl_->files.find(fd);
    if (it != impl_->files.end()) {
        if (it->second.stream.is_open()) {
            if (it->second.writable) it->second.stream.flush();
            it->second.stream.close();
        }
        impl_->files.erase(it);
        result = kResSucceed;
    }
    set_pending(*impl_, kFuncClose, result);
    return kResSucceed;
}

int Ps2MemoryCard::Seek(int fd, int offset, int origin) {
    int result = kResNoEntry;
    const auto it = impl_->files.find(fd);
    if (it != impl_->files.end()) {
        FileHandle &handle = it->second;
        handle.stream.clear();
        const std::ios_base::seekdir direction =
            origin == 0   ? std::ios_base::beg
            : origin == 1 ? std::ios_base::cur
                          : std::ios_base::end;
        bool ok = true;
        std::streampos position = -1;
        if (handle.readable) {
            handle.stream.clear();
            handle.stream.seekg(offset, direction);
            if (handle.stream.good()) {
                position = handle.stream.tellg();
            } else {
                ok = false;
            }
        }
        if (handle.writable) {
            handle.stream.clear();
            if (position >= 0) {
                handle.stream.seekp(position);
            } else {
                handle.stream.seekp(offset, direction);
                if (handle.stream.good()) {
                    position = handle.stream.tellp();
                }
            }
            if (!handle.stream.good()) ok = false;
        }
        if (!ok || position < 0) {
            result = kResDeniedPermit;
        } else {
            result = static_cast<int>(position);
        }
    }
    set_pending(*impl_, kFuncSeek, result);
    return kResSucceed;
}

int Ps2MemoryCard::Read(int fd, std::uint32_t buffer_addr, int size) {
    int result = kResNoEntry;
    if (size < 0) {
        result = kResDeniedPermit;
    } else {
        const auto it = impl_->files.find(fd);
        if (it != impl_->files.end()) {
            FileHandle &handle = it->second;
            if (!handle.readable) {
                result = kResDeniedPermit;
            } else if (size == 0) {
                result = 0;
            } else {
                std::vector<std::uint8_t> buffer(
                    static_cast<std::size_t>(size));
                handle.stream.clear();
                handle.stream.read(reinterpret_cast<char *>(buffer.data()),
                                   size);
                const std::streamsize got = handle.stream.gcount();
                if (handle.stream.bad()) {
                    result = kResDeniedPermit;
                } else if (got < 0) {
                    result = kResDeniedPermit;
                } else {
                    const std::size_t count =
                        static_cast<std::size_t>(got);
                    if (count > 0 &&
                        !write_ee_bytes(*impl_, buffer_addr, buffer.data(),
                                        count)) {
                        result = kResDeniedPermit;
                    } else {
                        result = static_cast<int>(count);
                    }
                }
            }
            MC_LOGI("Read fd=%d size=%d result=%d", fd, size, result);
        }
    }
    set_pending(*impl_, kFuncRead, result);
    return kResSucceed;
}

int Ps2MemoryCard::Write(int fd, std::uint32_t buffer_addr, int size) {
    int result = kResNoEntry;
    if (size < 0) {
        result = kResDeniedPermit;
    } else {
        const auto it = impl_->files.find(fd);
        if (it != impl_->files.end()) {
            FileHandle &handle = it->second;
            if (!handle.writable) {
                result = kResDeniedPermit;
            } else if (size == 0) {
                result = 0;
            } else {
                const std::uint8_t *source =
                    ee_pointer(*impl_, buffer_addr,
                               static_cast<std::size_t>(size));
                if (source == nullptr) {
                    result = kResDeniedPermit;
                } else {
                    handle.stream.clear();
                    handle.stream.write(
                        reinterpret_cast<const char *>(source), size);
                    if (!handle.stream.good()) {
                        result = kResFullDevice;
                    } else {
                        result = size;
                    }
                }
            }
            MC_LOGI("Write fd=%d size=%d result=%d", fd, size, result);
        }
    }
    set_pending(*impl_, kFuncWrite, result);
    return kResSucceed;
}

int Ps2MemoryCard::Flush(int fd) {
    int result = kResNoEntry;
    const auto it = impl_->files.find(fd);
    if (it != impl_->files.end()) {
        it->second.stream.flush();
        result = it->second.stream.good() ? kResSucceed
                                          : kResDeniedPermit;
    }
    set_pending(*impl_, kFuncFlush, result);
    return kResSucceed;
}

int Ps2MemoryCard::MkDir(int port, int slot, std::uint32_t name_addr) {
    int result = kResDeniedPermit;
    if (valid_slot(port, slot)) {
        const std::string name = read_ee_string(*impl_, name_addr);
        const auto full = resolve_path(*impl_, port, name);
        if (full) {
            std::error_code ec;
            if (fs::create_directories(*full, ec)) {
                result = kResSucceed;
            } else {
                result = fs::is_directory(*full, ec) ? kResSucceed
                                                     : kResDeniedPermit;
            }
        }
    }
    set_pending(*impl_, kFuncMkDir, result);
    MC_LOGI("MkDir result=%d", result);
    return kResSucceed;
}

int Ps2MemoryCard::ChDir(int port, int slot, std::uint32_t name_addr,
                         std::uint32_t current_addr) {
    int result = kResDeniedPermit;
    if (valid_slot(port, slot)) {
        const std::string name = read_ee_string(*impl_, name_addr);
        const auto full = resolve_path(*impl_, port, name, true);
        std::error_code ec;
        if (full && fs::is_directory(*full, ec)) {
            impl_->current_dir[port] = relative_to_card(*impl_, port, *full);
            result = kResSucceed;
        } else {
            result = kResNoEntry;
        }

        if (current_addr != 0) {
            std::string current = "/" + impl_->current_dir[port];
            if (current.size() >= kMaxName) current.resize(kMaxName - 1);
            if (!write_ee_bytes(*impl_, current_addr, current.c_str(),
                                current.size() + 1)) {
                result = kResDeniedPermit;
            }
        }
    }
    set_pending(*impl_, kFuncChDir, result);
    return kResSucceed;
}

int Ps2MemoryCard::GetDir(int port, int slot, std::uint32_t name_addr,
                          unsigned mode, int maxent,
                          std::uint32_t table_addr) {
    int result = kResNoEntry;
    int count = 0;

    if (!valid_slot(port, slot)) {
        result = kResDeniedPermit;
    } else if (mode != 0) {
        result = 0;  // no hay segunda pagina
    } else if (maxent <= 0 || table_addr == 0) {
        result = 0;
    } else {
        const std::string name = read_ee_string(*impl_, name_addr);
        std::string directory;
        std::string pattern = "*";

        if (!name.empty() && name != "/") {
            const auto full = resolve_path(*impl_, port, name);
            std::error_code ec;
            if (full && fs::is_directory(*full, ec)) {
                directory = name;
            } else {
                std::string normalized = name;
                std::replace(normalized.begin(), normalized.end(), '\\', '/');
                const std::size_t slash = normalized.find_last_of('/');
                if (slash == std::string::npos) {
                    directory.clear();
                    pattern = normalized;
                } else {
                    directory = normalized.substr(0, slash);
                    pattern = normalized.substr(slash + 1);
                }
                if (pattern.empty()) pattern = "*";
            }
        }

        const auto base = resolve_path(*impl_, port, directory, true);
        std::error_code ec;
        if (!base || !fs::is_directory(*base, ec)) {
            result = kResNoEntry;
        } else {
            std::vector<fs::directory_entry> entries;
            for (fs::directory_iterator it(*base, ec), end;
                 !ec && it != end; it.increment(ec)) {
                entries.push_back(*it);
            }
            std::sort(entries.begin(), entries.end(),
                      [](const fs::directory_entry &a,
                         const fs::directory_entry &b) {
                          return a.path().filename().generic_string() <
                                 b.path().filename().generic_string();
                      });

            for (const fs::directory_entry &entry : entries) {
                if (count >= maxent) break;
                const std::string entry_name =
                    entry.path().filename().generic_string();
                if (!wildcard_match(pattern, entry_name)) continue;

                const auto record = make_entry(entry.path(), entry_name);
                if (!write_ee_bytes(
                        *impl_,
                        table_addr + static_cast<std::uint32_t>(count) *
                                          kMcEntrySize,
                        record.data(), record.size())) {
                    result = kResDeniedPermit;
                    count = 0;
                    break;
                }
                ++count;
            }
            if (result != kResDeniedPermit) result = count;
        }
    }

    set_pending(*impl_, kFuncGetDir, result);
    MC_LOGI("GetDir mode=%u maxent=%d count=%d result=%d", mode, maxent,
            count, result);
    return kResSucceed;
}

int Ps2MemoryCard::Delete(int port, int slot, std::uint32_t name_addr) {
    int result = kResNoEntry;
    if (valid_slot(port, slot)) {
        const std::string name = read_ee_string(*impl_, name_addr);
        const auto full = resolve_path(*impl_, port, name);
        std::error_code ec;
        if (full && fs::exists(*full, ec)) {
            if (fs::is_directory(*full, ec) &&
                !fs::is_empty(*full, ec)) {
                result = kResNotEmpty;
            } else if (fs::remove(*full, ec)) {
                result = kResSucceed;
            } else {
                result = kResDeniedPermit;
            }
        }
    }
    set_pending(*impl_, kFuncDelete, result);
    MC_LOGI("Delete result=%d", result);
    return kResSucceed;
}

int Ps2MemoryCard::Format(int port, int slot) {
    int result = kResDeniedPermit;
    if (valid_slot(port, slot)) {
        for (auto &entry : impl_->files) {
            if (entry.second.stream.is_open()) {
                entry.second.stream.close();
            }
        }
        impl_->files.clear();
        impl_->current_dir[port].clear();

        std::error_code ec;
        fs::remove_all(card_root(*impl_, port), ec);
        fs::create_directories(card_root(*impl_, port), ec);
        result = ec ? kResDeniedPermit : kResSucceed;
    }
    set_pending(*impl_, kFuncFormat, result);
    MC_LOGI("Format result=%d", result);
    return kResSucceed;
}

int Ps2MemoryCard::Unformat(int port, int slot) {
    int result = kResDeniedPermit;
    if (valid_slot(port, slot)) {
        std::error_code ec;
        fs::remove_all(card_root(*impl_, port), ec);
        fs::create_directories(card_root(*impl_, port), ec);
        impl_->current_dir[port].clear();
        result = ec ? kResDeniedPermit : kResSucceed;
    }
    set_pending(*impl_, kFuncUnformat, result);
    return kResSucceed;
}

int Ps2MemoryCard::GetEntSpace(int port, int slot,
                               std::uint32_t path_addr) {
    int result = kResDeniedPermit;
    if (valid_slot(port, slot)) {
        const std::string name = read_ee_string(*impl_, path_addr);
        const auto full = resolve_path(*impl_, port, name, true);
        std::error_code ec;
        if (full && fs::is_directory(*full, ec)) {
            result = 100;  // entradas libres simuladas
        }
    }
    set_pending(*impl_, kFuncGetEnt, result);
    return kResSucceed;
}

int Ps2MemoryCard::Rename(int port, int slot, std::uint32_t old_addr,
                          std::uint32_t new_addr) {
    int result = kResNoEntry;
    if (valid_slot(port, slot)) {
        const std::string old_name = read_ee_string(*impl_, old_addr);
        const std::string new_name = read_ee_string(*impl_, new_addr);
        const auto old_path = resolve_path(*impl_, port, old_name);
        const auto new_path = resolve_path(*impl_, port, new_name);
        if (old_path && new_path) {
            std::error_code ec;
            if (!fs::exists(*old_path, ec)) {
                result = kResNoEntry;
            } else {
                fs::rename(*old_path, *new_path, ec);
                result = ec ? kResDeniedPermit : 1;
            }
        }
    }
    set_pending(*impl_, kFuncRename, result);
    return kResSucceed;
}

int Ps2MemoryCard::Sync(int mode, std::uint32_t cmd_addr,
                        std::uint32_t result_addr) {
    (void)mode;
    if (impl_->pending_command == kFuncNone) return -1;

    if (cmd_addr != 0) {
        write_ee_u32(*impl_, cmd_addr,
                     static_cast<std::uint32_t>(impl_->pending_command));
    }
    if (result_addr != 0) {
        write_ee_u32(*impl_, result_addr,
                     static_cast<std::uint32_t>(impl_->pending_result));
    }

    const int command = impl_->pending_command;
    const int result = impl_->pending_result;
    impl_->pending_command = kFuncNone;
    impl_->pending_result = 0;
    MC_LOGI("Sync cmd=0x%02X result=%d", command, result);
    return 1;
}

int Ps2MemoryCard::PendingCommand() const {
    return impl_->pending_command;
}

int Ps2MemoryCard::PendingResult() const {
    return impl_->pending_result;
}

Ps2MemoryCard &System() {
    static Ps2MemoryCard instance;
    return instance;
}

void SetEeMemory(std::uint8_t *ram, std::uint32_t size) {
    System().SetEeMemory(ram, size);
}

bool IsMemoryCardAddress(std::uint32_t address) {
    switch (address) {
    case 0x0010E288u:
    case 0x0010EDC0u:
    case 0x0010E5E8u:
    case 0x0010E758u:
    case 0x0010E810u:
    case 0x0010E978u:
    case 0x0010EA98u:
    case 0x0010EC88u:
    case 0x0010E720u:
    case 0x0010EF48u:
    case 0x0010F128u:
    case 0x0010F278u:
    case 0x0010F340u:
    case 0x0010F860u:
    case 0x0010F950u:
        return true;
    default:
        return false;
    }
}

bool Invoke(std::uint32_t address, std::uint32_t a0, std::uint32_t a1,
            std::uint32_t a2, std::uint32_t a3, std::uint32_t *result) {
    if (!IsMemoryCardAddress(address)) return false;

    Ps2MemoryCard &mc = System();
    int value = 0;
    switch (address) {
    case 0x0010E288u:
        value = mc.Init(static_cast<int>(a0));
        break;
    case 0x0010EDC0u:
        value = mc.GetInfo(static_cast<int>(a0), static_cast<int>(a1),
                           a2, a3, 0);
        break;
    case 0x0010E5E8u:
        value = mc.Open(static_cast<int>(a0), static_cast<int>(a1), a2,
                        static_cast<int>(a3));
        break;
    case 0x0010E758u:
        value = mc.Close(static_cast<int>(a0));
        break;
    case 0x0010E810u:
        value = mc.Seek(static_cast<int>(a0), static_cast<int>(a1),
                        static_cast<int>(a2));
        break;
    case 0x0010E978u:
        value = mc.Read(static_cast<int>(a0), a1, static_cast<int>(a2));
        break;
    case 0x0010EA98u:
        value = mc.Write(static_cast<int>(a0), a1, static_cast<int>(a2));
        break;
    case 0x0010EC88u:
        value = mc.Sync(static_cast<int>(a0), a1, a2);
        break;
    case 0x0010E720u:
        value = mc.MkDir(static_cast<int>(a0), static_cast<int>(a1), a2);
        break;
    case 0x0010EF48u:
        // CallPS2 solo entrega 4 argumentos; maxent/table llegan en t0/t1.
        // Se deja la operacion aceptada con 0 entradas hasta ampliar el
        // dispatcher. La API GetDir() completa existe para pruebas.
        value = mc.GetDir(static_cast<int>(a0), static_cast<int>(a1), a2,
                          a3, 0, 0);
        break;
    case 0x0010F128u:
        value = mc.ChDir(static_cast<int>(a0), static_cast<int>(a1), a2, a3);
        break;
    case 0x0010F278u:
        value = mc.Format(static_cast<int>(a0), static_cast<int>(a1));
        break;
    case 0x0010F340u:
        value = mc.Delete(static_cast<int>(a0), static_cast<int>(a1), a2);
        break;
    case 0x0010F860u:
        value = mc.Unformat(static_cast<int>(a0), static_cast<int>(a1));
        break;
    case 0x0010F950u:
        value = mc.GetEntSpace(static_cast<int>(a0), static_cast<int>(a1),
                               a2);
        break;
    default:
        return false;
    }

    if (result != nullptr) *result = static_cast<std::uint32_t>(value);
    return true;
}

int GetDirEx(int port, int slot, std::uint32_t name_addr, unsigned mode,
             int maxent, std::uint32_t table_addr) {
    return System().GetDir(port, slot, name_addr, mode, maxent, table_addr);
}

}  // namespace ps2::mc
