// Prueba del layout de pantalla: pillarbox 4:3, ortho PS2 y widescreen.
// Compila con:
//   clang++ -std=c++17 -O2 -Isrc tests/display_layout_test.cpp `
//     src/display_layout.cpp -o layout_test.exe

#include "display_layout.hpp"

#include <cmath>
#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char *name) {
    std::printf("  %-52s %s\n", name, condition ? "PASS" : "FAIL");
    if (!condition) ++failures;
}

bool nearly(float a, float b, float tolerance = 0.0001f) {
    return std::fabs(a - b) <= tolerance;
}

struct Ndc {
    float x;
    float y;
};

Ndc transform(const Vu1Renderer::Mat4 &m, float x, float y) {
    return {m.m[0] * x + m.m[1] * y + m.m[3],
            m.m[4] * x + m.m[5] * y + m.m[7]};
}

void test_layout() {
    std::printf("\n=== Pillarbox/letterbox ===\n");

    auto layout = ps2::render::ComputeDisplayLayout(2400, 1080);
    check(layout.x == 480 && layout.y == 0 && layout.width == 1440 &&
              layout.height == 1080,
          "2400x1080 -> 1440x1080 centrado");
    check(layout.pillarbox && !layout.letterbox, "2400x1080 pillarbox");
    check(nearly(layout.full_aspect, 2400.0f / 1080.0f), "aspect ventana");

    layout = ps2::render::ComputeDisplayLayout(1280, 720);
    check(layout.x == 160 && layout.width == 960 && layout.height == 720,
          "1280x720 -> 960x720 centrado");

    layout = ps2::render::ComputeDisplayLayout(1024, 768);
    check(layout.x == 0 && layout.y == 0 && layout.width == 1024 &&
              layout.height == 768,
          "4:3 exacto sin bandas");
    check(!layout.pillarbox && !layout.letterbox, "4:3 sin bandas");

    layout = ps2::render::ComputeDisplayLayout(800, 800);
    check(layout.x == 0 && layout.y == 100 && layout.width == 800 &&
              layout.height == 600,
          "800x800 -> letterbox 800x600");
    check(!layout.pillarbox && layout.letterbox, "800x800 letterbox");

    layout = ps2::render::ComputeDisplayLayout(1920, 1080);
    check(layout.x == 240 && layout.width == 1440, "1920x1080 centrado");
}

void test_ortho() {
    std::printf("\n=== Proyeccion ortografica PS2 ===\n");
    const auto ortho = ps2::render::Ps2OrthoMatrix();

    const Ndc top_left = transform(ortho, 0.0f, 0.0f);
    check(nearly(top_left.x, -1.0f) && nearly(top_left.y, 1.0f),
          "(0,0) -> (-1,1)");

    const Ndc bottom_right = transform(ortho, 640.0f, 448.0f);
    check(nearly(bottom_right.x, 1.0f) && nearly(bottom_right.y, -1.0f),
          "(640,448) -> (1,-1)");

    const Ndc center = transform(ortho, 320.0f, 224.0f);
    check(nearly(center.x, 0.0f) && nearly(center.y, 0.0f),
          "centro -> (0,0)");
}

void test_widescreen() {
    std::printf("\n=== FOV widescreen ===\n");
    const auto pillarbox =
        ps2::render::ComputeDisplayLayout(2400, 1080);
    check(!pillarbox.widescreen &&
              nearly(ps2::render::ComputeWidescreenScale(pillarbox), 1.0f),
          "pillarbox no ajusta FOV");

    const auto wide = ps2::render::ComputeDisplayLayout(
        2400, 1080, ps2::render::AspectMode::Widescreen);
    check(wide.widescreen, "2400x1080 modo widescreen");
    check(nearly(ps2::render::ComputeWidescreenScale(wide), 0.6f),
          "2400x1080 escala X = 0.6");

    const auto hd = ps2::render::ComputeDisplayLayout(
        1920, 1080, ps2::render::AspectMode::Widescreen);
    check(nearly(ps2::render::ComputeWidescreenScale(hd), 0.75f),
          "1920x1080 escala X = 0.75");

    const auto four_three = ps2::render::ComputeDisplayLayout(
        1024, 768, ps2::render::AspectMode::Widescreen);
    check(!four_three.widescreen &&
              nearly(ps2::render::ComputeWidescreenScale(four_three), 1.0f),
          "4:3 no cambia el FOV");

    Vu1Renderer::Mat4 projection = Vu1Renderer::Identity();
    projection.m[0] = 2.0f;
    projection.m[1] = 1.0f;
    projection.m[2] = 0.5f;
    projection.m[3] = 0.25f;
    projection.m[4] = 7.0f;
    projection.m[5] = 8.0f;
    const Vu1Renderer::Mat4 original = projection;
    ps2::render::AdjustProjectionForWidescreen(projection, 0.5f);

    check(nearly(projection.m[0], 1.0f) &&
              nearly(projection.m[1], 0.5f) &&
              nearly(projection.m[2], 0.25f) &&
              nearly(projection.m[3], 0.125f),
          "fila X multiplicada");
    check(nearly(projection.m[4], original.m[4]) &&
              nearly(projection.m[5], original.m[5]),
          "filas Y/Z/W intactas");
}

}  // namespace

int main() {
    test_layout();
    test_ortho();
    test_widescreen();

    std::printf("\nFALLOS: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
