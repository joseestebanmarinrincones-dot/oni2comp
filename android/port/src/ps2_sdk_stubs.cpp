#include "ps2_sdk_stubs.h"

#include <cstdio>

#if defined(__ANDROID__)
#include <android/log.h>
#define SDK_TAG "PS2SDK"
#define SDK_LOGI(...) __android_log_print(ANDROID_LOG_INFO, SDK_TAG, __VA_ARGS__)
#else
#define SDK_LOGI(...) do { \
    std::fprintf(stdout, "[PS2SDK] "); \
    std::fprintf(stdout, __VA_ARGS__); \
    std::fputc('\n', stdout); \
    std::fflush(stdout); \
} while (0)
#endif

namespace ps2::sdk {
namespace {

bool g_logging = true;
Hook g_hook = nullptr;
Hook g_library_hooks[static_cast<std::size_t>(Library::LibIpu) + 1] = {};

// Log una vez por direccion: las funciones de libgraph se llaman por frame.
std::uint32_t g_logged[256] = {};
std::size_t g_logged_count = 0;

bool already_logged(std::uint32_t address) {
    for (std::size_t i = 0; i < g_logged_count; ++i) {
        if (g_logged[i] == address) return true;
    }
    if (g_logged_count < (sizeof(g_logged) / sizeof(g_logged[0]))) {
        g_logged[g_logged_count++] = address;
    }
    return false;
}

std::size_t library_index(Library library) {
    const auto index = static_cast<std::size_t>(library);
    return index < (sizeof(g_library_hooks) / sizeof(g_library_hooks[0]))
        ? index : 0;
}

void log_stub(const Function &function, std::uint32_t result) {
    if (!g_logging || already_logged(function.address)) return;
    SDK_LOGI("stub 0x%08X %s (%s) -> 0x%08X",
             function.address, LibraryName(function.library),
             function.name, result);
}

}  // namespace

const Function kFunctions[] = {    // crt CONFIRMED: range 0x100008
    {0x00100008u, 172u, Library::Crt, 60, false, "entry"},
    // crt CONFIRMED: range 0x1000b8
    {0x001000B8u, 8u, Library::Crt, -1, false, "FUN_001000b8"},
    // crt CONFIRMED: range 0x1000d0
    {0x001000D0u, 60u, Library::Crt, -1, false, "FUN_001000d0"},
    // crt CONFIRMED: range 0x100110
    {0x00100110u, 32u, Library::Crt, -1, false, "FUN_00100110"},
    // crt CONFIRMED: range 0x100130
    {0x00100130u, 32u, Library::Crt, -1, false, "FUN_00100130"},
    // libgraph CONFIRMED: range 0x100150
    {0x00100150u, 400u, Library::LibGraph, -1, true, "FUN_00100150"},
    // libgraph CONFIRMED: range 0x1002e0
    {0x001002E0u, 12u, Library::LibGraph, -1, true, "FUN_001002e0"},
    // libgraph CONFIRMED: range 0x1002f0
    {0x001002F0u, 104u, Library::LibGraph, -1, true, "FUN_001002f0"},
    // libgraph CONFIRMED: string 'sceGsDefDispEnv:Not support displaymode for %d!!'
    {0x00100358u, 624u, Library::LibGraph, -1, true, "SONY_libgraph_00100358"},
    // libgraph CONFIRMED: range 0x1005c8
    {0x001005C8u, 188u, Library::LibGraph, -1, true, "FUN_001005c8"},
    // libgraph CONFIRMED: range 0x100688
    {0x00100688u, 200u, Library::LibGraph, -1, true, "FUN_00100688"},
    // libgraph CONFIRMED: range 0x100750
    {0x00100750u, 484u, Library::LibGraph, -1, true, "FUN_00100750"},
    // libgraph CONFIRMED: range 0x100938
    {0x00100938u, 260u, Library::LibGraph, -1, true, "FUN_00100938"},
    // libgraph CONFIRMED: string 'sceGsPutDrawEnv: DMA Ch.2 does not terminate'
    {0x00100A40u, 232u, Library::LibGraph, -1, true, "SONY_libgraph_00100a40"},
    // libgraph CONFIRMED: string 'sceGsSyncPath: GIF does not terminate'
    {0x00100B28u, 788u, Library::LibGraph, -1, true, "SONY_libgraph_00100b28"},
    // libgraph CONFIRMED: range 0x100e40
    {0x00100E40u, 160u, Library::LibGraph, -1, true, "FUN_00100e40"},
    // libgraph CONFIRMED: range 0x100ee0
    {0x00100EE0u, 136u, Library::LibGraph, -1, true, "FUN_00100ee0"},
    // libgraph CONFIRMED: range 0x100f68
    {0x00100F68u, 480u, Library::LibGraph, -1, true, "FUN_00100f68"},
    // libgraph CONFIRMED: range 0x101148
    {0x00101148u, 136u, Library::LibGraph, -1, true, "FUN_00101148"},
    // libgraph CONFIRMED: range 0x1011d0
    {0x001011D0u, 740u, Library::LibGraph, -1, true, "FUN_001011d0"},
    // libgraph CONFIRMED: range 0x1014b8
    {0x001014B8u, 92u, Library::LibGraph, -1, true, "FUN_001014b8"},
    // libgraph CONFIRMED: range 0x101518
    {0x00101518u, 52u, Library::LibGraph, -1, true, "FUN_00101518"},
    // libdma CONFIRMED: range 0x101550
    {0x00101550u, 40u, Library::LibDma, -1, true, "FUN_00101550"},
    // libdma CONFIRMED: range 0x101578
    {0x00101578u, 220u, Library::LibDma, -1, true, "FUN_00101578"},
    // libdma CONFIRMED: range 0x101668
    {0x00101668u, 472u, Library::LibDma, -1, true, "FUN_00101668"},
    // libdma CONFIRMED: string 'libdma: sync timeout'
    {0x001018A0u, 216u, Library::LibDma, -1, true, "SONY_libdma_001018a0"},
    // libdma CONFIRMED: string 'libdma: sync timeout'
    {0x00101978u, 228u, Library::LibDma, -1, true, "SONY_libdma_00101978"},
    // libdma CONFIRMED: string 'libdma: sync timeout'
    {0x00101A60u, 228u, Library::LibDma, -1, true, "SONY_libdma_00101a60"},
    // libdma CONFIRMED: string 'libdma: sync timeout'
    {0x00101B48u, 196u, Library::LibDma, -1, true, "SONY_libdma_00101b48"},
    // libdma CONFIRMED: string 'libdma: sync timeout'
    {0x00101C10u, 236u, Library::LibDma, -1, true, "SONY_libdma_00101c10"},
    // libdma CONFIRMED: string 'libdma: sync timeout'
    {0x00101D00u, 240u, Library::LibDma, -1, true, "SONY_libdma_00101d00"},
    // libdma CONFIRMED: string 'libdma: sync timeout'
    {0x00101DF0u, 196u, Library::LibDma, -1, true, "SONY_libdma_00101df0"},
    // libdma CONFIRMED: string 'libdma: sync timeout'
    {0x00101EB8u, 208u, Library::LibDma, -1, true, "SONY_libdma_00101eb8"},
    // libvu0 CANDIDATE: range 0x101fe8
    {0x00101FE8u, 44u, Library::LibVu0, -1, false, "FUN_00101fe8"},
    // libvu0 CANDIDATE: range 0x102018
    {0x00102018u, 68u, Library::LibVu0, -1, false, "FUN_00102018"},
    // libvu0 CANDIDATE: range 0x102060
    {0x00102060u, 28u, Library::LibVu0, -1, false, "FUN_00102060"},
    // libvu0 CANDIDATE: range 0x102080
    {0x00102080u, 36u, Library::LibVu0, -1, false, "FUN_00102080"},
    // libvu0 CANDIDATE: range 0x1020a8
    {0x001020A8u, 60u, Library::LibVu0, -1, false, "FUN_001020a8"},
    // libvu0 CANDIDATE: range 0x1020e8
    {0x001020E8u, 68u, Library::LibVu0, -1, false, "FUN_001020e8"},
    // libvu0 CANDIDATE: range 0x102130
    {0x00102130u, 108u, Library::LibVu0, -1, false, "FUN_00102130"},
    // libvu0 CANDIDATE: range 0x1021c0
    {0x001021C0u, 32u, Library::LibVu0, -1, false, "FUN_001021c0"},
    // libvu0 CANDIDATE: range 0x1021e0
    {0x001021E0u, 40u, Library::LibVu0, -1, false, "FUN_001021e0"},
    // libvu0 CANDIDATE: range 0x102208
    {0x00102208u, 20u, Library::LibVu0, -1, false, "FUN_00102208"},
    // libvu0 CANDIDATE: range 0x102220
    {0x00102220u, 20u, Library::LibVu0, -1, false, "FUN_00102220"},
    // libvu0 CANDIDATE: range 0x102238
    {0x00102238u, 20u, Library::LibVu0, -1, false, "FUN_00102238"},
    // libvu0 CANDIDATE: range 0x102250
    {0x00102250u, 24u, Library::LibVu0, -1, false, "FUN_00102250"},
    // libvu0 CANDIDATE: range 0x102268
    {0x00102268u, 44u, Library::LibVu0, -1, false, "FUN_00102268"},
    // libvu0 CANDIDATE: range 0x102298
    {0x00102298u, 12u, Library::LibVu0, -1, false, "FUN_00102298"},
    // libvu0 CANDIDATE: range 0x1022a8
    {0x001022A8u, 36u, Library::LibVu0, -1, false, "FUN_001022a8"},
    // libvu0 CANDIDATE: range 0x1022e0
    {0x001022E0u, 16u, Library::LibVu0, -1, false, "FUN_001022e0"},
    // libvu0 CANDIDATE: range 0x102310
    {0x00102310u, 40u, Library::LibVu0, -1, false, "FUN_00102310"},
    // libvu0 CANDIDATE: range 0x102338
    {0x00102338u, 116u, Library::LibVu0, -1, false, "FUN_00102338"},
    // libvu0 CANDIDATE: range 0x1023b0
    {0x001023B0u, 164u, Library::LibVu0, -1, false, "FUN_001023b0"},
    // libvu0 CANDIDATE: range 0x102458
    {0x00102458u, 168u, Library::LibVu0, -1, false, "FUN_00102458"},
    // libvu0 CANDIDATE: range 0x102500
    {0x00102500u, 168u, Library::LibVu0, -1, false, "FUN_00102500"},
    // libvu0 CANDIDATE: range 0x1025a8
    {0x001025A8u, 80u, Library::LibVu0, -1, false, "FUN_001025a8"},
    // libvu0 CANDIDATE: range 0x1025f8
    {0x001025F8u, 36u, Library::LibVu0, -1, false, "FUN_001025f8"},
    // libvu0 CANDIDATE: range 0x102620
    {0x00102620u, 176u, Library::LibVu0, -1, false, "FUN_00102620"},
    // libvu0 CANDIDATE: range 0x1026d0
    {0x001026D0u, 192u, Library::LibVu0, -1, false, "FUN_001026d0"},
    // libvu0 CANDIDATE: range 0x102790
    {0x00102790u, 104u, Library::LibVu0, -1, false, "FUN_00102790"},
    // libvu0 CANDIDATE: range 0x1027f8
    {0x001027F8u, 260u, Library::LibVu0, -1, false, "FUN_001027f8"},
    // libvu0 CANDIDATE: range 0x102900
    {0x00102900u, 400u, Library::LibVu0, -1, false, "FUN_00102900"},
    // libvu0 CANDIDATE: range 0x102ae8
    {0x00102AE8u, 68u, Library::LibVu0, -1, false, "FUN_00102ae8"},
    // libvu0 CANDIDATE: range 0x102b30
    {0x00102B30u, 28u, Library::LibVu0, -1, false, "FUN_00102b30"},
    // libvu0 CANDIDATE: range 0x102b50
    {0x00102B50u, 44u, Library::LibVu0, -1, false, "FUN_00102b50"},
    // libvu0 CANDIDATE: range 0x102b80
    {0x00102B80u, 24u, Library::LibVu0, -1, false, "FUN_00102b80"},
    // libvu0 CANDIDATE: range 0x102cd0
    {0x00102CD0u, 92u, Library::LibVu0, -1, false, "FUN_00102cd0"},
    // libc CANDIDATE: range 0x102d30
    {0x00102D30u, 100u, Library::LibC, -1, false, "FUN_00102d30"},
    // libc CANDIDATE: range 0x102d98
    {0x00102D98u, 100u, Library::LibC, -1, false, "FUN_00102d98"},
    // libc CANDIDATE: range 0x102ee8
    {0x00102EE8u, 336u, Library::LibC, -1, false, "FUN_00102ee8"},
    // libc CANDIDATE: range 0x103038
    {0x00103038u, 104u, Library::LibC, -1, false, "FUN_00103038"},
    // libc CANDIDATE: range 0x1030a0
    {0x001030A0u, 100u, Library::LibC, -1, false, "FUN_001030a0"},
    // libc CANDIDATE: range 0x103108
    {0x00103108u, 568u, Library::LibC, -1, false, "FUN_00103108"},
    // libkernel CONFIRMED: syscall 0x2
    {0x00103360u, 16u, Library::LibKernel, 2, false, "SetGsCrt"},
    // libkernel CONFIRMED: syscall 0x4
    {0x00103380u, 16u, Library::LibKernel, 4, false, "_Exit"},
    // libkernel CONFIRMED: syscall 0x6
    {0x001033A0u, 16u, Library::LibKernel, 6, false, "_LoadExecPS2"},
    // libkernel CONFIRMED: syscall 0xD
    {0x00103410u, 16u, Library::LibKernel, 13, false, "SetVTLBRefillHandler"},
    // libkernel CONFIRMED: syscall 0x10
    {0x00103440u, 16u, Library::LibKernel, 16, false, "AddIntcHandler"},
    // libkernel CONFIRMED: syscall 0x11
    {0x00103460u, 16u, Library::LibKernel, 17, false, "RemoveIntcHandler"},
    // libkernel CONFIRMED: syscall 0x12
    {0x00103470u, 16u, Library::LibKernel, 18, false, "AddDmacHandler"},
    // libkernel CONFIRMED: syscall 0x13
    {0x00103490u, 16u, Library::LibKernel, 19, false, "RemoveDmacHandler"},
    // libkernel CONFIRMED: syscall 0x14
    {0x001034A0u, 16u, Library::LibKernel, 20, false, "_EnableIntc"},
    // libkernel CONFIRMED: syscall 0x15
    {0x001034B0u, 16u, Library::LibKernel, 21, false, "_DisableIntc"},
    // libkernel CONFIRMED: syscall 0x16
    {0x001034C0u, 16u, Library::LibKernel, 22, false, "_EnableDmac"},
    // libkernel CONFIRMED: syscall 0x17
    {0x001034D0u, 16u, Library::LibKernel, 23, false, "_DisableDmac"},
    // libkernel CONFIRMED: syscall 0xFC
    {0x001034E0u, 16u, Library::LibKernel, 252, false, "SetAlarm"},
    // libkernel CONFIRMED: syscall ?
    {0x00103500u, 16u, Library::LibKernel, -26, false, "_iEnableIntc"},
    // libkernel CONFIRMED: syscall ?
    {0x00103510u, 16u, Library::LibKernel, -27, false, "_iDisableIntc"},
    // libkernel CONFIRMED: syscall ?
    {0x00103520u, 16u, Library::LibKernel, -28, false, "_iEnableDmac"},
    // libkernel CONFIRMED: syscall ?
    {0x00103530u, 16u, Library::LibKernel, -29, false, "_iDisableDmac"},
    // libkernel CONFIRMED: syscall 0x20
    {0x00103560u, 16u, Library::LibKernel, 32, false, "CreateThread"},
    // libkernel CONFIRMED: syscall 0x21
    {0x00103570u, 16u, Library::LibKernel, 33, false, "DeleteThread"},
    // libkernel CONFIRMED: syscall 0x22
    {0x00103580u, 16u, Library::LibKernel, 34, false, "_StartThread"},
    // libkernel CONFIRMED: syscall 0x24
    {0x001035A0u, 16u, Library::LibKernel, 36, false, "ExitDeleteThread"},
    // libkernel CONFIRMED: syscall 0x25
    {0x001035B0u, 16u, Library::LibKernel, 37, false, "TerminateThread"},
    // libkernel CONFIRMED: syscall 0x29
    {0x001035F0u, 16u, Library::LibKernel, 41, false, "ChangeThreadPriority"},
    // libkernel CONFIRMED: syscall 0x2B
    {0x00103610u, 16u, Library::LibKernel, 43, false, "RotateThreadReadyQueue"},
    // libkernel CONFIRMED: syscall 0x2F
    {0x00103650u, 16u, Library::LibKernel, 47, false, "GetThreadId"},
    // libkernel CONFIRMED: syscall 0x30
    {0x00103660u, 16u, Library::LibKernel, 48, false, "ReferThreadStatus"},
    // libkernel CONFIRMED: syscall 0x32
    {0x00103680u, 16u, Library::LibKernel, 50, false, "SleepThread"},
    // libkernel CONFIRMED: syscall 0x33
    {0x00103690u, 16u, Library::LibKernel, 51, false, "WakeupThread"},
    // libkernel CONFIRMED: syscall ?
    {0x001036A0u, 16u, Library::LibKernel, -52, false, "_iWakeupThread"},
    // libkernel CONFIRMED: syscall 0x37
    {0x001036D0u, 16u, Library::LibKernel, 55, false, "SuspendThread"},
    // libkernel CONFIRMED: syscall ?
    {0x001036E0u, 16u, Library::LibKernel, -56, false, "_iSuspendThread"},
    // libkernel CONFIRMED: syscall 0x3E
    {0x00103740u, 16u, Library::LibKernel, 62, false, "EndOfHeap"},
    // libkernel CONFIRMED: syscall 0x40
    {0x00103760u, 16u, Library::LibKernel, 64, false, "CreateSema"},
    // libkernel CONFIRMED: syscall 0x41
    {0x00103770u, 16u, Library::LibKernel, 65, false, "DeleteSema"},
    // libkernel CONFIRMED: syscall 0x42
    {0x00103780u, 16u, Library::LibKernel, 66, false, "SignalSema"},
    // libkernel CONFIRMED: syscall ?
    {0x00103790u, 16u, Library::LibKernel, -67, false, "iSignalSema"},
    // libkernel CONFIRMED: syscall 0x44
    {0x001037A0u, 16u, Library::LibKernel, 68, false, "WaitSema"},
    // libkernel CONFIRMED: syscall 0x45
    {0x001037B0u, 16u, Library::LibKernel, 69, false, "PollSema"},
    // libkernel CONFIRMED: syscall 0x4A
    {0x00103800u, 16u, Library::LibKernel, 74, false, "SetOsdConfigParam"},
    // libkernel CONFIRMED: syscall 0x4B
    {0x00103810u, 16u, Library::LibKernel, 75, false, "GetOsdConfigParam"},
    // libkernel CONFIRMED: syscall 0x64
    {0x001039E0u, 16u, Library::LibKernel, 100, false, "FlushCache"},
    // libkernel CONFIRMED: syscall 0x6B
    {0x00103A30u, 16u, Library::LibKernel, 107, false, "sceSifStopDma"},
    // libkernel CONFIRMED: syscall 0x71
    {0x00103AA0u, 16u, Library::LibKernel, 113, false, "GsPutIMR"},
    // libkernel CONFIRMED: syscall 0x73
    {0x00103AD0u, 16u, Library::LibKernel, 115, false, "SetVSyncFlag"},
    // libkernel CONFIRMED: syscall 0x76
    {0x00103B00u, 16u, Library::LibKernel, 118, false, "sceSifDmaStat"},
    // libkernel CONFIRMED: syscall 0x77
    {0x00103B20u, 16u, Library::LibKernel, 119, false, "sceSifSetDma"},
    // libkernel CONFIRMED: syscall ?
    {0x00103B30u, 16u, Library::LibKernel, -119, false, "isceSifSetDma"},
    // libkernel CONFIRMED: syscall 0x78
    {0x00103B40u, 16u, Library::LibKernel, 120, false, "sceSifSetDChain"},
    // libkernel CONFIRMED: syscall ?
    {0x00103B50u, 16u, Library::LibKernel, -120, false, "isceSifSetDChain"},
    // libkernel CONFIRMED: syscall 0x79
    {0x00103B60u, 16u, Library::LibKernel, 121, false, "sceSifSetReg"},
    // libkernel CONFIRMED: syscall 0x7A
    {0x00103B70u, 16u, Library::LibKernel, 122, false, "sceSifGetReg"},
    // libkernel CONFIRMED: syscall 0x7B
    {0x00103B80u, 16u, Library::LibKernel, 123, false, "_ExecOSD"},
    // libkernel CONFIRMED: syscall 0x7C
    {0x00103B90u, 16u, Library::LibKernel, 124, false, "Deci2Call"},
    // libkernel CONFIRMED: syscall 0x7F
    {0x00103BC0u, 16u, Library::LibKernel, 127, false, "GetMemorySize"},
    // libkernel CONFIRMED: syscall 0x82
    {0x00103BD0u, 16u, Library::LibKernel, 130, false, "_InitTLB"},
    // libkernel CANDIDATE: range 0x103be0
    {0x00103BE0u, 12u, Library::LibKernel, -1, false, "FUN_00103be0"},
    // libkernel CANDIDATE: range 0x103bf0
    {0x00103BF0u, 144u, Library::LibKernel, -1, false, "FUN_00103bf0"},
    // libkernel CANDIDATE: range 0x103c80
    {0x00103C80u, 164u, Library::LibKernel, -1, false, "FUN_00103c80"},
    // libkernel CANDIDATE: range 0x103d28
    {0x00103D28u, 124u, Library::LibKernel, -1, false, "FUN_00103d28"},
    // libkernel CANDIDATE: range 0x103da8
    {0x00103DA8u, 116u, Library::LibKernel, -1, false, "FUN_00103da8"},
    // libkernel CANDIDATE: range 0x103e20
    {0x00103E20u, 40u, Library::LibKernel, -1, false, "FUN_00103e20"},
    // libkernel CANDIDATE: range 0x103e48
    {0x00103E48u, 8u, Library::LibKernel, -1, false, "FUN_00103e48"},
    // libkernel CANDIDATE: range 0x103e58
    {0x00103E58u, 8u, Library::LibKernel, -1, false, "FUN_00103e58"},
    // libkernel CANDIDATE: range 0x103e60
    {0x00103E60u, 172u, Library::LibKernel, -1, false, "FUN_00103e60"},
    // libkernel CANDIDATE: range 0x103f10
    {0x00103F10u, 8u, Library::LibKernel, -1, false, "FUN_00103f10"},
    // libkernel CANDIDATE: range 0x103f18
    {0x00103F18u, 20u, Library::LibKernel, -1, false, "FUN_00103f18"},
    // libkernel CANDIDATE: range 0x103f38
    {0x00103F38u, 40u, Library::LibKernel, -1, false, "FUN_00103f38"},
    // libkernel CANDIDATE: range 0x103f60
    {0x00103F60u, 40u, Library::LibKernel, -1, false, "FUN_00103f60"},
    // libkernel CANDIDATE: range 0x103f88
    {0x00103F88u, 40u, Library::LibKernel, -1, false, "FUN_00103f88"},
    // libkernel CANDIDATE: range 0x103fb0
    {0x00103FB0u, 164u, Library::LibKernel, -1, false, "FUN_00103fb0"},
    // libkernel CANDIDATE: range 0x104058
    {0x00104058u, 124u, Library::LibKernel, -1, false, "FUN_00104058"},
    // libkernel CANDIDATE: range 0x1040d8
    {0x001040D8u, 20u, Library::LibKernel, -1, false, "FUN_001040d8"},
    // libkernel CANDIDATE: range 0x1040f0
    {0x001040F0u, 164u, Library::LibKernel, -1, false, "FUN_001040f0"},
    // libkernel CANDIDATE: range 0x104198
    {0x00104198u, 124u, Library::LibKernel, -1, false, "FUN_00104198"},
    // libkernel CANDIDATE: range 0x104230
    {0x00104230u, 104u, Library::LibKernel, -1, false, "FUN_00104230"},
    // libkernel CANDIDATE: range 0x104298
    {0x00104298u, 104u, Library::LibKernel, -1, false, "FUN_00104298"},
    // libkernel CANDIDATE: range 0x104300
    {0x00104300u, 104u, Library::LibKernel, -1, false, "FUN_00104300"},
    // libkernel CANDIDATE: range 0x104368
    {0x00104368u, 104u, Library::LibKernel, -1, false, "FUN_00104368"},
    // libkernel CANDIDATE: range 0x1043d0
    {0x001043D0u, 32u, Library::LibKernel, -1, false, "FUN_001043d0"},
    // libkernel CANDIDATE: range 0x1043f0
    {0x001043F0u, 32u, Library::LibKernel, -1, false, "FUN_001043f0"},
    // libkernel CANDIDATE: range 0x104410
    {0x00104410u, 32u, Library::LibKernel, -1, false, "FUN_00104410"},
    // libkernel CANDIDATE: range 0x104430
    {0x00104430u, 32u, Library::LibKernel, -1, false, "FUN_00104430"},
    // libkernel CONFIRMED: syscall 0x74
    {0x00104450u, 16u, Library::LibKernel, 116, false, "RFU116_SetSyscall"},
    // libkernel CONFIRMED: syscall 0x5A
    {0x00104460u, 16u, Library::LibKernel, 90, false, "FUN_00104460"},
    // libkernel CONFIRMED: syscall 0x5B
    {0x001044A8u, 16u, Library::LibKernel, 91, false, "RFU091"},
    // libkernel CANDIDATE: range 0x1044b8
    {0x001044B8u, 212u, Library::LibKernel, -1, false, "FUN_001044b8"},
    // libkernel CONFIRMED: syscall ?
    {0x00104740u, 148u, Library::LibKernel, -47, false, "FUN_00104740"},
    // libkernel CONFIRMED: syscall ?
    {0x00104858u, 152u, Library::LibKernel, -47, false, "FUN_00104858"},
    // libkernel CONFIRMED: syscall 0x5A
    {0x0010BAA0u, 16u, Library::LibKernel, 90, false, "FUN_0010baa0"},
    // libkernel CONFIRMED: syscall 0x5B
    {0x0010BAE8u, 16u, Library::LibKernel, 91, false, "RFU091"},
    // libkernel CONFIRMED: syscall 0x74
    {0x0010BAF8u, 16u, Library::LibKernel, 116, false, "RFU116_SetSyscall"},
    // libkernel CONFIRMED: syscall 0x56
    {0x0010BBE0u, 16u, Library::LibKernel, 86, false, "RFU086_WaitEvnetFlag"},
    // libkernel CONFIRMED: syscall 0x83
    {0x0010C300u, 16u, Library::LibKernel, 131, false, "FUN_0010c300"},
    // libkernel CONFIRMED: syscall 0x74
    {0x0010C350u, 16u, Library::LibKernel, 116, false, "RFU116_SetSyscall"},
    // libkernel CONFIRMED: syscall 0x74
    {0x0010C3C0u, 16u, Library::LibKernel, 116, false, "RFU116_SetSyscall"},
    // libkernel CONFIRMED: syscall 0x5A
    {0x0010C3D0u, 16u, Library::LibKernel, 90, false, "FUN_0010c3d0"},
    // libkernel CONFIRMED: syscall 0x5B
    {0x0010C418u, 16u, Library::LibKernel, 91, false, "RFU091"},
    // libc CONFIRMED: SONY name SONY_libc_0010c490
    {0x0010C490u, 172u, Library::LibC, -1, false, "SONY_libc_0010c490"},
    // libc CANDIDATE: range 0x10c540
    {0x0010C540u, 8u, Library::LibC, -1, false, "thunk_FUN_0010bc80"},
    // libc CANDIDATE: range 0x10c5a8
    {0x0010C5A8u, 72u, Library::LibC, -1, false, "FUN_0010c5a8"},
    // libc CANDIDATE: range 0x10c5f0
    {0x0010C5F0u, 40u, Library::LibC, -1, false, "FUN_0010c5f0"},
    // libc CANDIDATE: range 0x10c618
    {0x0010C618u, 56u, Library::LibC, -1, false, "FUN_0010c618"},
    // libc CANDIDATE: range 0x10c650
    {0x0010C650u, 36u, Library::LibC, -1, false, "FUN_0010c650"},
    // libc CANDIDATE: range 0x10c678
    {0x0010C678u, 104u, Library::LibC, -1, false, "FUN_0010c678"},
    // libc CANDIDATE: range 0x10c6e0
    {0x0010C6E0u, 84u, Library::LibC, -1, false, "FUN_0010c6e0"},
    // libc CANDIDATE: range 0x10c738
    {0x0010C738u, 160u, Library::LibC, -1, false, "FUN_0010c738"},
    // libcdvd CONFIRMED: string 'sceCdCbfunc= %d sceCdCbfunc_num= %d'
    {0x0010C7D8u, 188u, Library::LibCdvd, -1, true, "SONY_libcdvd_0010c7d8"},
    // libcdvd CONFIRMED: range 0x10ca10
    {0x0010CA10u, 148u, Library::LibCdvd, -1, true, "FUN_0010ca10"},
    // libcdvd CONFIRMED: range 0x10caa8
    {0x0010CAA8u, 128u, Library::LibCdvd, -1, true, "FUN_0010caa8"},
    // libcdvd CONFIRMED: range 0x10cb28
    {0x0010CB28u, 108u, Library::LibCdvd, -1, true, "FUN_0010cb28"},
    // libcdvd CONFIRMED: range 0x10cb98
    {0x0010CB98u, 60u, Library::LibCdvd, -1, true, "FUN_0010cb98"},
    // libcdvd CONFIRMED: range 0x10cbd8
    {0x0010CBD8u, 100u, Library::LibCdvd, -1, true, "FUN_0010cbd8"},
    // libcdvd CONFIRMED: string 'Libcdvd bind err CdSearchFile'
    {0x0010CC40u, 764u, Library::LibCdvd, -1, true, "SONY_libcdvd_0010cc40"},
    // libcdvd CONFIRMED: string 'Libcdvd bind err N CMD'
    {0x0010CF40u, 364u, Library::LibCdvd, -1, true, "SONY_libcdvd_0010cf40"},
    // libcdvd CONFIRMED: range 0x10d0b0
    {0x0010D0B0u, 152u, Library::LibCdvd, -1, true, "FUN_0010d0b0"},
    // libcdvd CONFIRMED: range 0x10d148
    {0x0010D148u, 160u, Library::LibCdvd, -1, true, "FUN_0010d148"},
    // libcdvd CONFIRMED: range 0x10d1e8
    {0x0010D1E8u, 108u, Library::LibCdvd, -1, true, "FUN_0010d1e8"},
    // libcdvd CONFIRMED: string 'Libcdvd bind err S cmd'
    {0x0010D258u, 364u, Library::LibCdvd, -1, true, "SONY_libcdvd_0010d258"},
    // libcdvd CONFIRMED: string 'Libcdvd bind err %d CD_Init %d'
    {0x0010D3C8u, 736u, Library::LibCdvd, -1, true, "SONY_libcdvd_0010d3c8"},
    // libcdvd CONFIRMED: string 'Libcdvd bind err CdDiskReady'
    {0x0010D6A8u, 500u, Library::LibCdvd, -1, true, "SONY_libcdvd_0010d6a8"},
    // libcdvd CONFIRMED: range 0x10d8a0
    {0x0010D8A0u, 196u, Library::LibCdvd, -1, true, "FUN_0010d8a0"},
    // libcdvd CONFIRMED: range 0x10d968
    {0x0010D968u, 172u, Library::LibCdvd, -1, true, "FUN_0010d968"},
    // libcdvd CONFIRMED: range 0x10da18
    {0x0010DA18u, 152u, Library::LibCdvd, -1, true, "FUN_0010da18"},
    // libcdvd CONFIRMED: range 0x10dab0
    {0x0010DAB0u, 152u, Library::LibCdvd, -1, true, "FUN_0010dab0"},
    // libcdvd CONFIRMED: range 0x10db48
    {0x0010DB48u, 184u, Library::LibCdvd, -1, true, "FUN_0010db48"},
    // libcdvd CONFIRMED: range 0x10dc00
    {0x0010DC00u, 184u, Library::LibCdvd, -1, true, "FUN_0010dc00"},
    // libcdvd CONFIRMED: range 0x10dcb8
    {0x0010DCB8u, 236u, Library::LibCdvd, -1, true, "FUN_0010dcb8"},
    // libcdvd CONFIRMED: range 0x10dda8
    {0x0010DDA8u, 44u, Library::LibCdvd, -1, true, "FUN_0010dda8"},
    // libcdvd CONFIRMED: range 0x10ddd8
    {0x0010DDD8u, 52u, Library::LibCdvd, -1, true, "FUN_0010ddd8"},
    // libcdvd CONFIRMED: range 0x10de10
    {0x0010DE10u, 44u, Library::LibCdvd, -1, true, "FUN_0010de10"},
    // libcdvd CONFIRMED: range 0x10de40
    {0x0010DE40u, 44u, Library::LibCdvd, -1, true, "FUN_0010de40"},
    // libcdvd CONFIRMED: range 0x10de70
    {0x0010DE70u, 56u, Library::LibCdvd, -1, true, "FUN_0010de70"},
    // libcdvd CONFIRMED: string 'sceCdStRead BLK Read Ended'
    {0x0010DEA8u, 392u, Library::LibCdvd, -1, true, "SONY_libcdvd_0010dea8"},
    // libcdvd CONFIRMED: string 'sceCdStPause call'
    {0x0010E030u, 80u, Library::LibCdvd, -1, true, "SONY_libcdvd_0010e030"},
    // libcdvd CONFIRMED: string 'sceCdStResume call'
    {0x0010E080u, 84u, Library::LibCdvd, -1, true, "SONY_libcdvd_0010e080"},
    // libcdvd CONFIRMED: string 'sceCdStStat call'
    {0x0010E0D8u, 72u, Library::LibCdvd, -1, true, "SONY_libcdvd_0010e0d8"},
    // libcdvd CONFIRMED: range 0x10e120
    {0x0010E120u, 124u, Library::LibCdvd, -1, true, "FUN_0010e120"},
    // libmc CONFIRMED: string 'libmc: too old release of mcman.irx'
    {0x0010E288u, 432u, Library::LibMc, -1, true, "SONY_libmc_0010e288"},
    // libmc CONFIRMED: range 0x10e440
    {0x0010E440u, 48u, Library::LibMc, -1, true, "FUN_0010e440"},
    // libmc CONFIRMED: range 0x10e470
    {0x0010E470u, 180u, Library::LibMc, -1, true, "FUN_0010e470"},
    // libsd CONFIRMED: string 'sceSdRemoteInit() RPC bind error!'
    {0x0025C650u, 164u, Library::LibSd, -1, true, "SONY_libsd_0025c650"},
    // libsd CONFIRMED: range 0x25c6f8
    {0x0025C6F8u, 132u, Library::LibSd, -1, true, "FUN_0025c6f8"},
    // libsd CONFIRMED: range 0x25c790
    {0x0025C790u, 440u, Library::LibSd, -1, true, "FUN_0025c790"},
    // libpad2 CONFIRMED: string '[libdbc.a = %d.%d, dbcman.irx = %d.%d]'
    {0x00285398u, 404u, Library::LibPad2, -1, true, "SONY_libpad2_00285398"},
    // libpad2 CONFIRMED: string 'sceDbcSetWorkAddr: rpc error'
    {0x00285538u, 108u, Library::LibPad2, -1, true, "SONY_libpad2_00285538"},
    // libpad2 CONFIRMED: string 'sceDbcCreateSocket: rpc error'
    {0x002855A8u, 200u, Library::LibPad2, -1, true, "SONY_libpad2_002855a8"},
    // libpad2 CONFIRMED: string 'sceDbcDeleteSocket: rpc error'
    {0x00285670u, 112u, Library::LibPad2, -1, true, "SONY_libpad2_00285670"},
    // libpad2 CONFIRMED: string 'sceDbcGetDepNumber: rpc error'
    {0x002856E0u, 328u, Library::LibPad2, -1, true, "SONY_libpad2_002856e0"},
    // libpad2 CONFIRMED: string 'sceDbcInitSocket: rpc error'
    {0x00285828u, 112u, Library::LibPad2, -1, true, "SONY_libpad2_00285828"},
    // libpad2 CONFIRMED: string 'sceDbcResetSocket: rpc error'
    {0x00285898u, 112u, Library::LibPad2, -1, true, "SONY_libpad2_00285898"},
    // libpad2 CONFIRMED: string 'sceDbcGetDeviceStatus: rpc error'
    {0x00285908u, 112u, Library::LibPad2, -1, true, "SONY_libpad2_00285908"},
    // libpad2 CONFIRMED: string 'sceDbcSRData: rpc error'
    {0x00285978u, 328u, Library::LibPad2, -1, true, "SONY_libpad2_00285978"},
    // libpad2 CONFIRMED: string 'sceDbcSendData: rpc error'
    {0x00285AC0u, 228u, Library::LibPad2, -1, true, "SONY_libpad2_00285ac0"},
    // libpad2 CONFIRMED: string 'sceDbcSendData2: rpc error'
    {0x00285BA8u, 224u, Library::LibPad2, -1, true, "SONY_libpad2_00285ba8"},
    // libpad2 CONFIRMED: string 'sceDbcReceiveData: rpc error'
    {0x00285C88u, 236u, Library::LibPad2, -1, true, "SONY_libpad2_00285c88"},
    // libpad2 CONFIRMED: range 0x285d78
    {0x00285D78u, 40u, Library::LibPad2, -1, true, "FUN_00285d78"},
};

const std::size_t kFunctionCount = sizeof(kFunctions) / sizeof(kFunctions[0]);

const char *LibraryName(Library library) {
    switch (library) {
    case Library::Crt:       return "crt";
    case Library::LibGraph:  return "libgraph";
    case Library::LibDma:    return "libdma";
    case Library::LibVu0:    return "libvu0";
    case Library::LibKernel: return "libkernel";
    case Library::LibC:      return "libc";
    case Library::LibCdvd:   return "libcdvd";
    case Library::LibMc:     return "libmc";
    case Library::LibSd:     return "libsd";
    case Library::LibPad2:   return "libpad2";
    case Library::LibIpu:    return "libipu";
    default:                 return "unknown";
    }
}

const Function *Find(std::uint32_t address) {
    std::size_t low = 0;
    std::size_t high = kFunctionCount;
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2;
        if (kFunctions[middle].address < address) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    if (low < kFunctionCount && kFunctions[low].address == address) {
        return &kFunctions[low];
    }
    return nullptr;
}

const Function *FindContaining(std::uint32_t address) {
    std::size_t low = 0;
    std::size_t high = kFunctionCount;
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2;
        if (kFunctions[middle].address <= address) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    if (low == 0) return nullptr;
    const Function &function = kFunctions[low - 1];
    const std::uint32_t end = function.address + function.size;
    return (address >= function.address && address < end) ? &function : nullptr;
}

bool IsSdkAddress(std::uint32_t address) {
    return FindContaining(address) != nullptr;
}

bool IsNativeAddress(std::uint32_t address) {
    const Function *function = FindContaining(address);
    return function != nullptr && function->native;
}

Library LibraryOf(std::uint32_t address) {
    const Function *function = FindContaining(address);
    return function != nullptr ? function->library : Library::Unknown;
}

void SetHook(Hook hook) { g_hook = hook; }

void SetLibraryHook(Library library, Hook hook) {
    g_library_hooks[library_index(library)] = hook;
}

void ClearHooks() {
    g_hook = nullptr;
    for (std::size_t i = 0; i < (sizeof(g_library_hooks) / sizeof(g_library_hooks[0])); ++i) {
        g_library_hooks[i] = nullptr;
    }
}

bool Call(std::uint32_t address, std::uint32_t a0, std::uint32_t a1,
          std::uint32_t a2, std::uint32_t a3, std::uint32_t *result) {
    const Function *function = FindContaining(address);
    if (function == nullptr || !function->native) return false;

    std::uint32_t value = 0;
    if (g_hook != nullptr && g_hook(*function, a0, a1, a2, a3, &value)) {
        if (result != nullptr) *result = value;
        return true;
    }
    const Hook libraryHook = g_library_hooks[library_index(function->library)];
    if (libraryHook != nullptr && libraryHook(*function, a0, a1, a2, a3, &value)) {
        if (result != nullptr) *result = value;
        return true;
    }

    switch (function->library) {
    case Library::LibGraph:
    case Library::LibDma:
    case Library::LibCdvd:
    case Library::LibMc:
    case Library::LibSd:
    case Library::LibPad2:
        value = 0;
        break;
    default:
        return false;
    }

    log_stub(*function, value);
    if (result != nullptr) *result = value;
    return true;
}

bool CallRet(std::uint32_t address, std::uint32_t a0, std::uint32_t a1,
             std::uint32_t a2, std::uint32_t a3, std::uint32_t *result) {
    return Call(address, a0, a1, a2, a3, result);
}

void SetLogging(bool enabled) { g_logging = enabled; }
bool Logging() { return g_logging; }

}  // namespace ps2::sdk
