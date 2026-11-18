#pragma once

// Estado de un mando de PS2 y zonas táctiles invisibles.
//
// No depende de SDL: main.cpp le pasa los dedos activos con coordenadas
// normalizadas (0..1) y la función actualiza los bits una vez por fotograma.
//
// Distribución en pantalla (horizontal):
//
//   +--------------------------+--------------------------+
//   |                          |  L1              R1      |
//   |        CRUCETA           |        TRIÁNGULO          |
//   |   (mitad izquierda)      |  CUADRADO       CÍRCULO   |
//   |                          |        CROSS              |
//   |                          |          START            |
//   +--------------------------+--------------------------+

#include <cstddef>
#include <cstdint>

namespace port {

enum class PadButton : std::uint16_t {
    Up       = 1u << 0,
    Down     = 1u << 1,
    Left     = 1u << 2,
    Right    = 1u << 3,
    Triangle = 1u << 4,
    Circle   = 1u << 5,
    Cross    = 1u << 6,
    Square   = 1u << 7,
    L1       = 1u << 8,
    R1       = 1u << 9,
    Start    = 1u << 10,
    L2       = 1u << 11,
    R2       = 1u << 12,
    L3       = 1u << 13,
    R3       = 1u << 14,
    Select   = 1u << 15,
};

constexpr std::uint16_t mask(PadButton button) {
    return static_cast<std::uint16_t>(button);
}

struct PadState {
    std::uint16_t buttons = 0;

    // Sticks normalizados: -1 = izquierda/arriba, +1 = derecha/abajo.
    // Se convierten a 0..255 en el reporte de scePadRead.
    float left_x = 0.0f;
    float left_y = 0.0f;
    float right_x = 0.0f;
    float right_y = 0.0f;

    void clear() {
        buttons = 0;
        left_x = 0.0f;
        left_y = 0.0f;
        right_x = 0.0f;
        right_y = 0.0f;
    }

    void set(PadButton button, bool down = true) {
        if (down) buttons = static_cast<std::uint16_t>(buttons | mask(button));
        else buttons = static_cast<std::uint16_t>(buttons & ~mask(button));
    }

    bool pressed(PadButton button) const {
        return (buttons & mask(button)) != 0;
    }

    // Formato activo-bajo, por si el código del juego espera el estado del
    // mando de PS2 tal cual (pulsado = 0).
    std::uint16_t active_low() const {
        return static_cast<std::uint16_t>(~buttons);
    }
};

struct TouchPoint {
    bool active = false;
    std::uint64_t id = 0;
    float x = 0.0f;   // normalizado 0..1
    float y = 0.0f;
};

inline constexpr std::size_t kMaxTouches = 8;

// Actualiza pad.buttons desde los dedos activos. Llamar una vez por fotograma,
// antes de Game_Update(). Las zonas son invisibles.
void update_touch_pad(PadState& pad, const TouchPoint* points, std::size_t count);

}  // namespace port
