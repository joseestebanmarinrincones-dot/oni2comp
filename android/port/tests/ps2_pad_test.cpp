// Prueba de las zonas tactiles del mando PS2.
// Compila con:
//   clang++ -std=c++17 -O2 -Isrc tests/ps2_pad_test.cpp src/ps2_pad.cpp

#include "ps2_pad.hpp"

#include <cstdio>

namespace {

bool press(float x, float y, port::PadButton button) {
    port::PadState pad;
    port::TouchPoint point;
    point.active = true;
    point.id = 1;
    point.x = x;
    point.y = y;
    port::update_touch_pad(pad, &point, 1);
    return pad.pressed(button);
}

}  // namespace

int main() {
    int failures = 0;
    auto check = [&](bool condition, const char *name) {
        std::printf("  %-42s %s\n", name, condition ? "PASS" : "FAIL");
        if (!condition) ++failures;
    };

    check(press(0.75f, 0.89f, port::PadButton::Start), "Start (0.75, 0.89)");
    check(press(0.75f, 0.85f, port::PadButton::Start), "Start (0.75, 0.85)");
    check(!press(0.75f, 0.99f, port::PadButton::Start),
          "borde inferior reservado a Android");
    check(press(0.75f, 0.765f, port::PadButton::Cross), "Cross");
    check(press(0.90f, 0.50f, port::PadButton::Circle), "Circle");
    check(press(0.25f, 0.20f, port::PadButton::Up), "D-pad arriba");
    check(press(0.10f, 0.50f, port::PadButton::Left), "D-pad izquierda");
    check(!press(0.25f, 0.50f, port::PadButton::Up), "zona muerta");

    std::printf("\nFALLOS: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
