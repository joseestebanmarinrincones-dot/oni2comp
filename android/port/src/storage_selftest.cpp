#include "storage_selftest.hpp"

#include "iso_reader.hpp"
#include "overlay_manager.hpp"

#include <SDL_log.h>
#include <SDL_system.h>

#include <cstdint>
#include <string>
#include <vector>

void port_storage_self_test() {
    const char *external = SDL_AndroidGetExternalStoragePath();
    std::string base = external ? external : "";
    if (!base.empty() && base.back() != '/') base += '/';

    SDL_Log("SELFTEST external=%s", base.c_str());

    // 1) ISO completa (si disc.iso está en el directorio externo).
    IsoReader iso;
    if (iso.Open("disc.iso")) {
        SDL_Log("SELFTEST ISO size=%lld sectors=%lld",
                static_cast<long long>(iso.Size()),
                static_cast<long long>(iso.SectorCount()));
        std::vector<std::uint8_t> pvd(IsoReader::kSectorSize);
        if (iso.ReadSectors(16, 1, pvd.data())) {
            SDL_Log("SELFTEST PVD=%02X %.5s", pvd[0], pvd.data() + 1);
        } else {
            SDL_Log("SELFTEST PVD read failed: %s", iso.LastError().c_str());
        }
    } else {
        SDL_Log("SELFTEST ISO open failed: %s", iso.LastError().c_str());
    }

    // 2) Overlays capturados.
    OverlayManager manager;
    const std::string overlay_dir = base + "overlays";
    if (!manager.Initialize(overlay_dir)) {
        SDL_Log("SELFTEST overlay init failed: %s", manager.LastError().c_str());
        return;
    }
    SDL_Log("SELFTEST overlays=%zu aligned=%d",
            manager.Overlays().size(),
            reinterpret_cast<std::uintptr_t>(manager.MainRam()) % OverlayManager::kAlignment == 0);

    if (manager.Load("01F64000_856903D1")) {
        const std::uint8_t *p = manager.Resolve(0x01F64000);
        SDL_Log("SELFTEST main 0x01F64000 ptr=%p bytes=%02X%02X%02X%02X",
                static_cast<const void *>(p), p ? p[0] : 0, p ? p[1] : 0,
                p ? p[2] : 0, p ? p[3] : 0);
    } else {
        SDL_Log("SELFTEST main load failed: %s", manager.LastError().c_str());
    }

    if (manager.Load("02390000_07DEACE6")) {
        const std::uint8_t *p = manager.Resolve(0x02390000);
        SDL_Log("SELFTEST high 0x02390000 ptr=%p bytes=%02X%02X%02X%02X",
                static_cast<const void *>(p), p ? p[0] : 0, p ? p[1] : 0,
                p ? p[2] : 0, p ? p[3] : 0);
    } else {
        SDL_Log("SELFTEST high load failed: %s", manager.LastError().c_str());
    }

    SDL_Log("SELFTEST done loaded=%zu", manager.LoadedCount());
}
