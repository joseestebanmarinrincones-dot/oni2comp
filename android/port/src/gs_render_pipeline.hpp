#pragma once

// Pipeline de frame del port: combina fondos 2D (sprites GIF) con modelos 3D
// procesados por VU1.
//
// Orden de ejecucion:
//   1. Fondo 2D: todos los sprites GIF, con GL_DEPTH_TEST desactivado y
//      glDepthMask(GL_FALSE). El juego los envia en orden; se respeta.
//   2. Z-buffer: si el juego subio un depth map por GIF IMAGE (PSMZ32/24/16),
//      se sube a una textura GL_DEPTH_COMPONENT y se usa como profundidad
//      inicial de los modelos.
//   3. Modelos 3D: GL_DEPTH_TEST + GL_LEQUAL + glDepthMask(GL_TRUE).
//
// Con depth map, los modelos se dibujan en un FBO offscreen cuyo attachment
// de profundidad contiene el Z-buffer del juego; luego se compone el color
// sobre el fondo. Sin depth map, se dibujan directamente sobre el framebuffer
// por defecto (el depth buffer se limpia en Game_Render).
//
// El pipeline se ejecuta desde port_vsync(), antes de SDL_GL_SwapWindow.
// Los comandos se acumulan durante game::Frame() en los hooks DMA GIF/VIF1.

#include "gif_packet.hpp"
#include "vu1_renderer.hpp"

#include <cstdint>

namespace ps2::render {

// Modo de presentacion. Pillarbox4x3 es el comportamiento por defecto;
// Widescreen ensancha el FOV de VU1 pero mantiene el 2D en 4:3.
void SetWidescreen(bool enabled);
bool WidescreenEnabled();
void ToggleWidescreen();

// Multiplicador de resolucion interna para los modelos 3D (1, 2 o 3). El
// fondo 2D se mantiene a la resolucion nativa; los modelos se renderizan en un
// FBO mayor y se componen con filtrado lineal.
void SetInternalResolutionScale(int scale);
int InternalResolutionScale();

// Cola de fondo 2D (sprites GIF).
void QueueBackground(ps2::gif::DrawCommand command);

// Z-buffer enviado por el juego. Se conserva entre frames hasta que llegue
// otro; Onimusha usa camaras/escenarios estaticos y suele reutilizarlo.
void QueueDepthMap(ps2::gif::DepthMap depth_map);

// Modelo 3D ya extraido de VU1.
void QueueModel(Vu1Renderer::Packet packet);

// Ejecuta las tres fases. Debe llamarse en el hilo con contexto GL actual.
// Devuelve true si dibujo algo.
bool ExecuteFrame();

// Descarta las colas y libera FBO/texturas/shaders. Llamar antes de destruir
// el contexto GL.
void Shutdown();

// Limpia colas y cache de profundidad sin tocar recursos GL.
void Reset();

}  // namespace ps2::render
