#pragma once

// Layout de pantalla del port.
//
// El juego renderiza en el framebuffer PS2 de 640x448, que se presenta como
// 4:3. En una ventana Android (por ejemplo 2400x1080) se calcula un viewport
// 4:3 centrado con bandas negras laterales (pillarbox) o superior/inferior
// (letterbox si la ventana es mas estrecha que 4:3).
//
// En modo Widescreen:
//   - el contenido 2D (menus/HUD del juego) sigue en el viewport 4:3,
//   - los modelos 3D de VU1 usan toda la ventana y una matriz de proyeccion
//     con el FOV horizontal ensanchado,
//   - la matriz 3D se ajusta multiplicando su fila X por 4/3 / aspecto.
//
// El header no depende de GLES: las funciones Apply* solo compilan el
// glViewport si el target define PS2_DISPLAY_USE_GL.

#include "vu1_renderer.hpp"

#include <cstdint>

namespace ps2::render {

inline constexpr int kPs2Width = 640;
inline constexpr int kPs2Height = 448;
inline constexpr float kPs2Aspect = 4.0f / 3.0f;

enum class AspectMode : std::uint8_t {
    Pillarbox4x3 = 0,
    Widescreen = 1,
};

struct DisplayLayout {
    int window_width = 0;
    int window_height = 0;

    // Viewport 4:3 centrado, en pixeles del drawable.
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool pillarbox = false;
    bool letterbox = false;
    bool widescreen = false;
    float full_aspect = 0.0f;

    // Ortho PS2: 0..640, 0..448 (origen arriba-izquierda) -> NDC.
    // Row-major, como las matrices de VU1.
    Vu1Renderer::Mat4 ortho{};
};

// Calcula el viewport 4:3 centrado con enteros (sin subpixel).
DisplayLayout ComputeDisplayLayout(int window_width, int window_height,
                                   AspectMode mode = AspectMode::Pillarbox4x3);

// Matriz ortografica row-major para el framebuffer PS2.
Vu1Renderer::Mat4 Ps2OrthoMatrix();

// Escala X que se aplica a la fila 0 de la proyeccion 3D para widescreen.
// 1.0 en 4:3 o en modo pillarbox.
float ComputeWidescreenScale(const DisplayLayout &layout);

// Multiplica la fila X (m[0..3]) por x_scale. No toca filas Y/Z/W.
void AdjustProjectionForWidescreen(Vu1Renderer::Mat4 &projection,
                                   float x_scale);

// Aplica glViewport. En builds sin GL son no-op.
void Apply2DViewport(const DisplayLayout &layout);
void Apply3DViewport(const DisplayLayout &layout, AspectMode mode);

// Limpia a negro las bandas del layout (sin tocar el contenido).
void ClearBars(const DisplayLayout &layout);

}  // namespace ps2::render
