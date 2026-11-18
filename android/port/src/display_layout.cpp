#include "display_layout.hpp"

#include <algorithm>
#include <cmath>

#if defined(PS2_DISPLAY_USE_GL)
#include <GLES3/gl3.h>
#endif

namespace ps2::render {
namespace {

int centered_offset(int total, int content) {
    return std::max(0, (total - content) / 2);
}

}  // namespace

Vu1Renderer::Mat4 Ps2OrthoMatrix() {
    Vu1Renderer::Mat4 result{};
    // x: 0..640 -> -1..1, y: 0..448 -> 1..-1 (origen arriba-izquierda).
    result.m[0] = 2.0f / static_cast<float>(kPs2Width);
    result.m[3] = -1.0f;
    result.m[5] = -2.0f / static_cast<float>(kPs2Height);
    result.m[7] = 1.0f;
    result.m[10] = -1.0f;
    result.m[15] = 1.0f;
    return result;
}

DisplayLayout ComputeDisplayLayout(int window_width, int window_height,
                                   AspectMode mode) {
    DisplayLayout layout;
    layout.window_width = window_width;
    layout.window_height = window_height;
    if (window_width <= 0 || window_height <= 0) {
        layout.ortho = Ps2OrthoMatrix();
        return layout;
    }

    layout.full_aspect =
        static_cast<float>(window_width) / static_cast<float>(window_height);

    // Encaja un rectangulo 4:3 dentro de la ventana. Se redondea a enteros
    // para que los bordes no caigan en medio pixel.
    const int width_for_height = static_cast<int>(
        std::lround(static_cast<double>(window_height) * 4.0 / 3.0));
    if (width_for_height <= window_width) {
        layout.width = std::max(1, width_for_height);
        layout.height = window_height;
        layout.x = centered_offset(window_width, layout.width);
        layout.y = 0;
        layout.pillarbox = layout.width < window_width;
    } else {
        const int height_for_width = static_cast<int>(
            std::lround(static_cast<double>(window_width) * 3.0 / 4.0));
        layout.width = window_width;
        layout.height = std::max(1, height_for_width);
        layout.x = 0;
        layout.y = centered_offset(window_height, layout.height);
        layout.letterbox = layout.height < window_height;
    }

    layout.ortho = Ps2OrthoMatrix();
    layout.widescreen =
        mode == AspectMode::Widescreen && layout.full_aspect > kPs2Aspect;
    return layout;
}

float ComputeWidescreenScale(const DisplayLayout &layout) {
    if (!layout.widescreen || layout.full_aspect <= kPs2Aspect) return 1.0f;
    return kPs2Aspect / layout.full_aspect;
}

void AdjustProjectionForWidescreen(Vu1Renderer::Mat4 &projection,
                                   float x_scale) {
    if (!(x_scale > 0.0f) || x_scale == 1.0f) return;
    // Fila X de una matriz row-major: m[0..3]. Las filas Y/Z/W no cambian,
    // asi que el FOV vertical y la profundidad quedan intactos.
    for (int column = 0; column < 4; ++column) {
        projection.m[column] *= x_scale;
    }
}

void Apply2DViewport(const DisplayLayout &layout) {
#if defined(PS2_DISPLAY_USE_GL)
    if (layout.width > 0 && layout.height > 0) {
        glViewport(layout.x, layout.y, layout.width, layout.height);
    }
#else
    (void)layout;
#endif
}

void Apply3DViewport(const DisplayLayout &layout, AspectMode mode) {
#if defined(PS2_DISPLAY_USE_GL)
    if (mode == AspectMode::Widescreen &&
        layout.full_aspect > kPs2Aspect) {
        glViewport(0, 0, layout.window_width, layout.window_height);
        return;
    }
    Apply2DViewport(layout);
#else
    (void)layout;
    (void)mode;
#endif
}

void ClearBars(const DisplayLayout &layout) {
#if defined(PS2_DISPLAY_USE_GL)
    const int full_width = layout.window_width;
    const int full_height = layout.window_height;
    if (full_width <= 0 || full_height <= 0) return;
    if (!layout.pillarbox && !layout.letterbox) return;

    glEnable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    if (layout.pillarbox) {
        const int left_width = layout.x;
        const int right_width =
            full_width - layout.x - layout.width;
        if (left_width > 0) {
            glScissor(0, 0, left_width, full_height);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        if (right_width > 0) {
            glScissor(layout.x + layout.width, 0, right_width, full_height);
            glClear(GL_COLOR_BUFFER_BIT);
        }
    }
    if (layout.letterbox) {
        const int bottom_height = layout.y;
        const int top_height =
            full_height - layout.y - layout.height;
        if (bottom_height > 0) {
            glScissor(0, 0, full_width, bottom_height);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        if (top_height > 0) {
            glScissor(0, layout.y + layout.height, full_width, top_height);
            glClear(GL_COLOR_BUFFER_BIT);
        }
    }

    glDisable(GL_SCISSOR_TEST);
#else
    (void)layout;
#endif
}

}  // namespace ps2::render
