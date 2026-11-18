#include "ps2_pad.hpp"

#include <algorithm>
#include <cmath>

namespace port {
namespace {

struct ActionZone {
    PadButton button;
    float cx, cy;   // centro normalizado
    float hw, hh;   // semiancho y semialto
};

// Zonas de la mitad derecha. Se solapan lo mínimo; si un dedo cae en varias,
// gana la de centro más cercano.
constexpr ActionZone kActionZones[] = {
    {PadButton::Triangle, 0.75f, 0.235f, 0.075f, 0.095f},
    {PadButton::Cross,    0.75f, 0.765f, 0.075f, 0.095f},
    {PadButton::Square,   0.60f, 0.500f, 0.080f, 0.130f},
    {PadButton::Circle,   0.90f, 0.500f, 0.080f, 0.130f},
    {PadButton::L1,       0.56f, 0.100f, 0.060f, 0.070f},
    {PadButton::R1,       0.94f, 0.100f, 0.060f, 0.070f},
    // Un poco por encima del borde inferior: Android reserva esa franja para
    // el gesto de navegacion y se comeria el toque.
    {PadButton::Start,    0.75f, 0.890f, 0.075f, 0.050f},
};

void update_dpad(PadState& pad, float x, float y) {
    // Centro de la mitad izquierda; el radio horizontal es 0.25 y el vertical
    // 0.5. La zona muerta evita pulsaciones al apoyar el dedo.
    constexpr float cx = 0.25f;
    constexpr float cy = 0.50f;
    constexpr float dead = 0.18f;

    const float dx = (x - cx) / 0.25f;
    const float dy = (y - cy) / 0.50f;

    if (dx < -dead) pad.set(PadButton::Left);
    if (dx > dead) pad.set(PadButton::Right);
    if (dy < -dead) pad.set(PadButton::Up);
    if (dy > dead) pad.set(PadButton::Down);
}

void update_actions(PadState& pad, float x, float y) {
    bool found = false;
    PadButton best = PadButton::Start;
    float best_distance = 0.0f;

    for (const ActionZone& zone : kActionZones) {
        const float nx = (x - zone.cx) / zone.hw;
        const float ny = (y - zone.cy) / zone.hh;
        if (std::fabs(nx) > 1.0f || std::fabs(ny) > 1.0f) continue;

        const float distance = nx * nx + ny * ny;
        if (!found || distance < best_distance) {
            found = true;
            best = zone.button;
            best_distance = distance;
        }
    }

    if (found) pad.set(best);
}

}  // namespace

void update_touch_pad(PadState& pad, const TouchPoint* points, std::size_t count) {
    pad.clear();
    if (!points) return;

    for (std::size_t i = 0; i < count; ++i) {
        const TouchPoint& point = points[i];
        if (!point.active) continue;

        const float x = std::clamp(point.x, 0.0f, 1.0f);
        const float y = std::clamp(point.y, 0.0f, 1.0f);

        if (x < 0.5f) update_dpad(pad, x, y);
        else update_actions(pad, x, y);
    }
}

}  // namespace port
