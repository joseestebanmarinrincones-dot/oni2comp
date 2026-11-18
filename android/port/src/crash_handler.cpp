#include "crash_handler.hpp"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if !defined(_WIN32)
#include <fcntl.h>
#include <ucontext.h>
#include <unistd.h>
#endif

namespace ps2::crash {
namespace {

constexpr std::size_t kMaxPath = 512;
#if !defined(_WIN32)
constexpr std::size_t kAltStackBytes = 64 * 1024;
#endif

char g_log_path[kMaxPath] = "crash_log.txt";
std::atomic<std::uint32_t> g_last_function{0};
static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
              "El handler necesita atomic<uint32_t> lock-free");

#if !defined(_WIN32)
volatile sig_atomic_t g_in_handler = 0;
bool g_installed = false;
struct sigaction g_old_actions[5] = {};
int g_old_signals[5] = {};
int g_old_count = 0;
alignas(16) unsigned char g_alt_stack[kAltStackBytes] = {};
#else
volatile sig_atomic_t g_in_handler = 0;
bool g_installed = false;
#endif

const char *signal_name(int signal_number) {
    switch (signal_number) {
    case SIGSEGV: return "SIGSEGV";
#if defined(SIGBUS)
    case SIGBUS: return "SIGBUS";
#endif
#if defined(SIGILL)
    case SIGILL: return "SIGILL";
#endif
#if defined(SIGFPE)
    case SIGFPE: return "SIGFPE";
#endif
    case SIGABRT: return "SIGABRT";
    default: return "SIGNAL";
    }
}

#if !defined(_WIN32)
void append_char(char *&cursor, char *end, char value) {
    if (cursor < end) *cursor++ = value;
}

void append_text(char *&cursor, char *end, const char *text) {
    while (text != nullptr && *text != '\0') {
        append_char(cursor, end, *text++);
    }
}

void append_uint(char *&cursor, char *end, unsigned int value) {
    char digits[16];
    int count = 0;
    if (value == 0) {
        append_char(cursor, end, '0');
        return;
    }
    while (value != 0 && count < static_cast<int>(sizeof(digits))) {
        digits[count++] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    }
    while (count > 0) append_char(cursor, end, digits[--count]);
}

void append_hex64(char *&cursor, char *end, std::uint64_t value) {
    static constexpr char kHex[] = "0123456789abcdef";
    for (int shift = 60; shift >= 0; shift -= 4) {
        append_char(cursor, end,
                    kHex[(value >> static_cast<unsigned>(shift)) & 0xFu]);
    }
}

void append_hex32(char *&cursor, char *end, std::uint32_t value) {
    static constexpr char kHex[] = "0123456789abcdef";
    for (int shift = 28; shift >= 0; shift -= 4) {
        append_char(cursor, end,
                    kHex[(value >> static_cast<unsigned>(shift)) & 0xFu]);
    }
}

void write_crash_log_posix(int signal_number, std::uintptr_t pc,
                           std::uintptr_t fault_address,
                           std::uint32_t last_function) {
    char buffer[512];
    char *cursor = buffer;
    char *end = buffer + sizeof(buffer) - 1;

    append_text(cursor, end, "\n=== Onimusha crash ===\n");
    append_text(cursor, end, "signal=");
    append_text(cursor, end, signal_name(signal_number));
    append_char(cursor, end, '(');
    append_uint(cursor, end, static_cast<unsigned>(signal_number));
    append_text(cursor, end, ")\n");

    append_text(cursor, end, "last_function=0x");
    append_hex32(cursor, end, last_function);
    append_text(cursor, end, " FUN_");
    append_hex32(cursor, end, last_function);
    append_char(cursor, end, '\n');

    append_text(cursor, end, "pc=0x");
    append_hex64(cursor, end, static_cast<std::uint64_t>(pc));
    append_char(cursor, end, '\n');

    append_text(cursor, end, "fault_addr=0x");
    append_hex64(cursor, end, static_cast<std::uint64_t>(fault_address));
    append_char(cursor, end, '\n');
    append_text(cursor, end, "===\n");
    *cursor = '\0';

    const std::size_t length =
        static_cast<std::size_t>(cursor - buffer);
    const int fd = ::open(g_log_path,
                          O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd >= 0) {
        (void)::write(fd, buffer, length);
        (void)::close(fd);
    } else {
        (void)::write(STDERR_FILENO, buffer, length);
    }
}

void signal_handler(int signal_number, siginfo_t *info, void *context) {
    if (g_in_handler != 0) {
        _exit(128 + signal_number);
    }
    g_in_handler = 1;

    const int saved_errno = errno;
    std::uintptr_t pc = 0;
    std::uintptr_t fault_address = 0;

    if (info != nullptr) {
        if (signal_number == SIGSEGV || signal_number == SIGBUS ||
            signal_number == SIGILL || signal_number == SIGFPE) {
            fault_address = reinterpret_cast<std::uintptr_t>(info->si_addr);
        }
    }

    if (context != nullptr) {
        const auto *ucontext = static_cast<const ucontext_t *>(context);
#if defined(__aarch64__)
        pc = static_cast<std::uintptr_t>(ucontext->uc_mcontext.pc);
#elif defined(__arm__)
        pc = static_cast<std::uintptr_t>(ucontext->uc_mcontext.arm_pc);
#elif defined(__x86_64__)
        pc = static_cast<std::uintptr_t>(
            ucontext->uc_mcontext.gregs[REG_RIP]);
#elif defined(__i386__)
        pc = static_cast<std::uintptr_t>(
            ucontext->uc_mcontext.gregs[REG_EIP]);
#endif
    }

    const std::uint32_t last_function =
        g_last_function.load(std::memory_order_relaxed);
    write_crash_log_posix(signal_number, pc, fault_address, last_function);

    errno = saved_errno;

    // Restaurar SIG_DFL y reenviar la senal para que Android genere el
    // tombstone/ANR normal. _exit es solo el ultimo recurso.
    struct sigaction default_action {};
    default_action.sa_handler = SIG_DFL;
    sigemptyset(&default_action.sa_mask);
    (void)::sigaction(signal_number, &default_action, nullptr);
    (void)::kill(::getpid(), signal_number);
    _exit(128 + signal_number);
}
#else
void win_signal_handler(int signal_number) {
    if (g_in_handler != 0) {
        std::_Exit(128 + signal_number);
    }
    g_in_handler = 1;
    const std::uint32_t last_function =
        g_last_function.load(std::memory_order_relaxed);
    WriteCrashLog(signal_number, 0, 0, last_function);
    std::signal(signal_number, SIG_DFL);
    std::raise(signal_number);
}
#endif

}  // namespace

void Install(const char *log_path) {
    if (log_path != nullptr && log_path[0] != '\0') {
        std::strncpy(g_log_path, log_path, sizeof(g_log_path) - 1);
        g_log_path[sizeof(g_log_path) - 1] = '\0';
    }

#if !defined(_WIN32)
    if (g_installed) return;

#if defined(__linux__)
    stack_t stack {};
    stack.ss_sp = g_alt_stack;
    stack.ss_size = sizeof(g_alt_stack);
    stack.ss_flags = 0;
    (void)::sigaltstack(&stack, nullptr);
#endif

    const int signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
    struct sigaction action {};
    action.sa_sigaction = &signal_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART;
    for (int signal_number : signals) {
        struct sigaction old_action {};
        if (::sigaction(signal_number, &action, &old_action) == 0) {
            if (g_old_count <
                static_cast<int>(sizeof(g_old_actions) /
                                 sizeof(g_old_actions[0]))) {
                g_old_actions[g_old_count] = old_action;
                g_old_signals[g_old_count] = signal_number;
                ++g_old_count;
            }
        }
    }
    g_installed = true;
#else
    if (g_installed) return;
    std::signal(SIGSEGV, win_signal_handler);
    std::signal(SIGABRT, win_signal_handler);
    g_installed = true;
#endif
}

void Shutdown() {
#if !defined(_WIN32)
    if (!g_installed) return;
    for (int index = 0; index < g_old_count; ++index) {
        (void)::sigaction(g_old_signals[index], &g_old_actions[index],
                          nullptr);
    }
    g_old_count = 0;
#else
    if (!g_installed) return;
    std::signal(SIGSEGV, SIG_DFL);
    std::signal(SIGABRT, SIG_DFL);
#endif
    g_installed = false;
}

void SetLastFunction(std::uint32_t address) {
    g_last_function.store(address, std::memory_order_relaxed);
}

std::uint32_t LastFunction() {
    return g_last_function.load(std::memory_order_relaxed);
}

void WriteCrashLog(int signal_number, std::uintptr_t pc,
                   std::uintptr_t fault_address,
                   std::uint32_t last_function) {
#if !defined(_WIN32)
    write_crash_log_posix(signal_number, pc, fault_address, last_function);
#else
    std::FILE *file = std::fopen(g_log_path, "a");
    const bool fallback = (file == nullptr);
    if (fallback) file = stderr;
    if (file != nullptr) {
        std::fprintf(file,
                     "\n=== Onimusha crash ===\n"
                     "signal=%s(%d)\n"
                     "last_function=0x%08x FUN_%08x\n"
                     "pc=0x%016llx\n"
                     "fault_addr=0x%016llx\n"
                     "===\n",
                     signal_name(signal_number), signal_number,
                     last_function, last_function,
                     static_cast<unsigned long long>(pc),
                     static_cast<unsigned long long>(fault_address));
        if (!fallback) std::fclose(file);
    }
#endif
}

const char *LogPath() {
    return g_log_path;
}

}  // namespace ps2::crash
