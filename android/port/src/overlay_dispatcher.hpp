#pragma once

// Despachador de overlays recompilados.
//
// Mapa principal, tal como se pidió:
//     std::unordered_map<uint32_t, std::function<void()>>
// base virtual -> función nativa que ejecuta ese overlay.
//
// Además guarda el tamaño de cada ventana para resolver saltos dinámicos a
// direcciones internas (no solo a la base) y un mapa de entradas exactas para
// funciones concretas o variantes que comparten base (01F90000 A/B).
//
// El handler se ejecuta sin argumentos; dentro puede consultar TargetAddress()
// para saber la dirección PS2 exacta a la que saltó el código base.
//
// Ejemplo:
//     OverlayDispatcher dispatcher;
//     dispatcher.SetLoader([&](uint32_t a) { return overlays.LoadForAddress(a); });
//     dispatcher.RegisterOverlay(0x01F64000, 0x28000, [&] {
//         NativeEnter(0x01F64000, dispatcher.TargetAddress());
//     });
//     ...
//     if (dispatcher.JumpToOverlay(c.pc)) return; // salto dinámico interceptado

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class OverlayDispatcher {
public:
    using Handler = std::function<void()>;
    using Loader = std::function<bool(std::uint32_t)>;

    // Registra una ventana completa. Devuelve false si la base ya existe.
    bool RegisterOverlay(std::uint32_t base, std::uint32_t size, Handler handler);

    // Entrada exacta (función interna o variante con la misma base).
    // Tiene prioridad sobre la búsqueda por ventana.
    bool RegisterExact(std::uint32_t ps2_address, Handler handler);

    // Callback opcional para asegurar que la RAM tiene cargado el overlay
    // antes de ejecutar el handler. Normalmente:
    //     [&](uint32_t a) { return overlays.LoadForAddress(a); }
    void SetLoader(Loader loader) { loader_ = std::move(loader); }

    // Intercepta un salto/call dinámico. Devuelve true si lo atendió.
    bool JumpToOverlay(std::uint32_t ps2_address);

    std::uint32_t TargetAddress() const { return target_; }
    std::uint32_t CurrentBase() const { return current_base_; }
    std::size_t OverlayCount() const { return handlers_.size(); }
    std::size_t ExactCount() const { return entries_.size(); }
    const std::string &LastError() const { return last_error_; }

    // Instantanea de los registros de estado. Los handlers (codigo) no se
    // serializan: al cargar se conservan los ya registrados en la instancia.
    void SaveState(std::vector<std::uint8_t> &out) const;
    bool LoadState(const std::uint8_t *data, std::size_t size);

    void Clear();

    // Mapa solicitado, expuesto solo para consulta/diagnóstico.
    const std::unordered_map<std::uint32_t, Handler> &Handlers() const { return handlers_; }

private:
    void set_error(const std::string &message);

    std::unordered_map<std::uint32_t, Handler> handlers_;       // base -> handler
    std::unordered_map<std::uint32_t, std::uint32_t> sizes_;    // base -> tamaño
    std::unordered_map<std::uint32_t, Handler> entries_;        // dirección exacta
    Loader loader_;
    std::uint32_t target_ = 0;
    std::uint32_t current_base_ = 0;
    std::string last_error_;
};
