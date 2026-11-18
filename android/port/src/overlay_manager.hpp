#pragma once

// Gestor de overlays capturados de la RAM de PS2.
//
// Reserva un bloque alineado de 32 MB que hace de RAM principal
// (0x00000000..0x01FFFFFF) y carga cada .bin en su dirección virtual, tomada
// del nombre:
//
//     01F64000_856903D1.bin  ->  base 0x01F64000, stamp 0x856903D1
//
// Los overlays de 0x02390000..0x023FFFFF están por encima de los 32 MB de RAM
// principal. Para no falsear direcciones, el gestor reserva además una ventana
// alta de 4 MB en 0x02000000..0x023FFFFF. Resolve() devuelve el puntero host
// correcto para ambas zonas.
//
// Es C++17 y no depende de SDL: el host llama a Initialize() con la carpeta
// donde estén los .bin.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class OverlayManager {
public:
    static constexpr std::uint32_t kMainRamSize = 32u << 20;   // 0x02000000
    static constexpr std::uint32_t kHighBase = 0x02000000u;    // justo tras la RAM
    static constexpr std::uint32_t kHighSize = 4u << 20;       // 0x00400000
    static constexpr std::size_t kAlignment = 4096;

    struct Overlay {
        std::string name;       // sin .bin: 01F64000_856903D1
        std::string path;       // ruta completa del .bin
        std::uint32_t base = 0;
        std::uint32_t stamp = 0;
        std::uint32_t size = 0;
        bool loaded = false;

        std::uint32_t End() const { return base + size; }
        bool Contains(std::uint32_t address) const {
            return address >= base && address < End();
        }
    };

    OverlayManager();
    ~OverlayManager();

    OverlayManager(const OverlayManager &) = delete;
    OverlayManager &operator=(const OverlayManager &) = delete;
    OverlayManager(OverlayManager &&) noexcept = default;
    OverlayManager &operator=(OverlayManager &&) noexcept = default;

    // Escanea overlay_dir, reserva RAM y ventana alta, y deja todo a cero.
    bool Initialize(const std::string &overlay_dir);

    // Carga por nombre (con o sin .bin), por base/stamp o por dirección.
    bool Load(const std::string &name);
    bool LoadByBase(std::uint32_t base, std::uint32_t stamp = 0);
    bool LoadForAddress(std::uint32_t address);

    // Búsqueda sin cargar.
    const Overlay *FindByName(const std::string &name) const;
    const Overlay *FindByBase(std::uint32_t base, std::uint32_t stamp = 0) const;

    // Puntero host para una dirección PS2; nullptr si no está mapeada.
    std::uint8_t *Resolve(std::uint32_t address);
    const std::uint8_t *Resolve(std::uint32_t address) const;

    std::uint8_t *MainRam() { return ram_.get(); }
    const std::uint8_t *MainRam() const { return ram_.get(); }
    std::uint8_t *HighWindow() { return high_.get(); }
    const std::uint8_t *HighWindow() const { return high_.get(); }

    const std::vector<Overlay> &Overlays() const { return overlays_; }
    std::size_t LoadedCount() const;
    const std::string &LastError() const { return last_error_; }

    // Pone a cero RAM y ventana alta y marca todo como no cargado.
    void Clear();

private:
    struct AlignedDeleter {
        void operator()(std::uint8_t *pointer) const noexcept;
    };
    using Block = std::unique_ptr<std::uint8_t, AlignedDeleter>;

    bool allocate_blocks();
    bool read_file(const Overlay &overlay, std::vector<std::uint8_t> &data) const;
    void set_error(const std::string &message);

    Block ram_;
    Block high_;
    std::vector<Overlay> overlays_;
    std::string last_error_;
};
