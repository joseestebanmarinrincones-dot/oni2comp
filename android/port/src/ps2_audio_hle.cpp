#include "ps2_audio_hle.hpp"

#include "ps2_audio_system.hpp"

#include <cstdio>
#include <unordered_set>

#if defined(__ANDROID__)
#include <android/log.h>
#define AUDIO_TAG "OnimushaAudio"
#define AUDIO_LOGI(...) __android_log_print(ANDROID_LOG_INFO, AUDIO_TAG, __VA_ARGS__)
#else
#define AUDIO_LOGI(...) do { std::fprintf(stdout, __VA_ARGS__); std::fputc('\n', stdout); } while (0)
#endif

namespace ps2::audio {
namespace {

std::unordered_set<std::uint32_t> logged_addresses;
std::unordered_set<std::uint32_t> logged_remote_commands;

const std::uint8_t *ee_memory = nullptr;
std::uint32_t ee_memory_size = 0;

const std::uint8_t *resolve_ee_pointer(std::uint32_t address,
                                       std::uint32_t size) {
    if (ee_memory != nullptr && address < ee_memory_size &&
        size <= ee_memory_size - address) {
        return ee_memory + address;
    }
    return reinterpret_cast<const std::uint8_t *>(
        static_cast<std::uintptr_t>(address));
}

void log_remote(std::uint32_t arg0, std::uint32_t command,
                std::uint32_t a2, std::uint32_t a3) {
    if (logged_remote_commands.size() >= 24) return;
    if (!logged_remote_commands.insert(command).second) return;
    AUDIO_LOGI("sceSdRemote(arg0=%u cmd=0x%04X a2=0x%08X a3=0x%08X)",
               arg0, command, a2, a3);
}

}  // namespace

bool IsStubbedAddress(std::uint32_t address) {
    return address >= kLibSdBegin && address < kLibSdEnd;
}

int Stub(std::uint32_t address) {
    if (logged_addresses.insert(address).second) {
        AUDIO_LOGI("audio stub 0x%08X -> 0 (libsd sin handler nativo)", address);
    }
    return 0;
}

void ResetLog() {
    logged_addresses.clear();
    logged_remote_commands.clear();
}

void SetEeMemory(const std::uint8_t *ram, std::uint32_t size) {
    ee_memory = ram;
    ee_memory_size = size;
}

bool Invoke(std::uint32_t address, std::uint32_t a0, std::uint32_t a1,
            std::uint32_t a2, std::uint32_t a3, std::uint32_t *result) {
    if (!IsStubbedAddress(address)) return false;

    std::uint32_t value = 0;
    switch (address) {
    case 0x0025C650u:  // sceSdRemoteInit -> abrir SDL audio
        ps2::audio::System().Initialize();
        value = 0;
        break;
    case 0x0025C6F8u: {  // sceSdTransToIOP(eeaddr, iopaddr, size, blocking)
        const std::uint8_t *source = resolve_ee_pointer(a0, a2);
        if (source != nullptr && a2 != 0) {
            ps2::audio::System().SetTransferAddress(a1);
            ps2::audio::System().UploadVoices(source, a2);
            AUDIO_LOGI("TransToIOP: EE=0x%08X SPU2=0x%05X size=%u",
                       a0, a1 & 0x1FFFFFu, a2);
        }
        value = 0;
        break;
    }
    case 0x0025C790u:  // sceSdRemote(arg0, cmd, ...)
        log_remote(a0, a1, a2, a3);
        value = 0;
        break;
    default:
        value = static_cast<std::uint32_t>(Stub(address));
        break;
    }

    if (result != nullptr) *result = value;
    return true;
}

bool StreamAdpcm(const void *data, std::size_t bytes, bool loop) {
    Ps2AudioSystem &system = ps2::audio::System();
    if (loop) system.SetStreamLoop(true);
    return system.QueueStream(data, bytes);
}

}  // namespace ps2::audio

extern "C" {

int sceSdRemoteInit(void) {
    ps2::audio::System().Initialize();
    return 0;
}

int sceSdRemote(int arg0, ...) {
    AUDIO_LOGI("sceSdRemote(arg0=%d) por simbolo C", arg0);
    return ps2::audio::Stub(0x0025C790u);
}

int sceSdTransToIOP(void *eeaddr, void *iopaddr, unsigned int size,
                    unsigned int blocking) {
    (void)blocking;
    if (eeaddr != nullptr && size != 0) {
        const auto address =
            static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(eeaddr));
        const std::uint8_t *source =
            ps2::audio::resolve_ee_pointer(address, size);
        ps2::audio::System().SetTransferAddress(
            static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(iopaddr)));
        ps2::audio::System().UploadVoices(source, size);
    }
    return 0;
}

void *sceSdCallBack(void *endfunc) {
    (void)endfunc;
    return nullptr;
}

int sceSdRemoteCallbackInit(int pri) {
    (void)pri;
    return 0;
}

int sceSdRemoteCallbackQuit(void) {
    return 0;
}

int sceSdInit(int flag) {
    (void)flag;
    ps2::audio::System().Initialize();
    return 0;
}

int sceSdSetParam(unsigned short entry, unsigned short value) {
    ps2::audio::System().SetParam(entry, value);
    return 0;
}

int sceSdGetParam(unsigned short entry) {
    return static_cast<int>(ps2::audio::System().GetParam(entry));
}

int sceSdSetSwitch(unsigned short entry, unsigned int value) {
    ps2::audio::System().SetSwitch(entry, value);
    return 0;
}

int sceSdGetSwitch(unsigned short entry) {
    return static_cast<int>(ps2::audio::System().GetSwitch(entry));
}

int sceSdSetAddr(unsigned short entry, unsigned int value) {
    ps2::audio::System().SetAddr(entry, value);
    return 0;
}

int sceSdGetAddr(unsigned short entry) {
    return static_cast<int>(ps2::audio::System().GetAddr(entry));
}

int sceSdSetCoreAttr(unsigned short entry, unsigned short value) {
    ps2::audio::System().SetCoreAttr(entry, value);
    return 0;
}

int sceSdGetCoreAttr(unsigned short entry) {
    return static_cast<int>(ps2::audio::System().GetCoreAttr(entry));
}

int sceSdVoiceTrans(short core, unsigned char mode, void *mem,
                    unsigned int size) {
    (void)core;
    if ((mode & 0x0F) == 0 && mem != nullptr && size != 0) {
        const auto address =
            static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(mem));
        const std::uint8_t *source =
            ps2::audio::resolve_ee_pointer(address, size);
        ps2::audio::System().UploadVoices(source, size);
    }
    return 0;
}

int sceSdBlockTrans(short core, unsigned char mode, void *mem,
                    unsigned int size, unsigned short arg) {
    (void)core;
    (void)arg;
    if ((mode & 0x0F) == 0 && mem != nullptr && size != 0) {
        const auto address =
            static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(mem));
        const std::uint8_t *source =
            ps2::audio::resolve_ee_pointer(address, size);
        ps2::audio::System().UploadBlock(source, size);
    }
    return 0;
}

int sceSdVoiceTransStatus(short core, short flag) {
    (void)core;
    (void)flag;
    return 1;  // transferencia sincrona completada
}

int sceSdBlockTransStatus(short core, short flag) {
    (void)core;
    (void)flag;
    return 1;
}

int sceSdStopTrans(short core) {
    (void)core;
    return 0;
}

}  // extern "C"
