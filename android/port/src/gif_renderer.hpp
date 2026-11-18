#pragma once

// Renderer GLES 3.0 para los sprites/texturas extraidos de paquetes GIF.
//
// El juego escribe el paquete en ps2_ram y llama a sceDmaSend(DMA2/GIF,
// source). SubmitChain() recorre la cadena DMA desde ps2_ram, parsea los
// GIFtags y dibuja los sprites encontrados como quads texturizados.

#include "gif_packet.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ps2::gif {

// Tamano del viewport (pixeles reales del drawable).
void SetScreenSize(int width, int height);

// Tamano logico del framebuffer PS2 para mapear coordenadas (12.4 fijo).
// Por defecto 640x448. Se conserva por compatibilidad; el pipeline fija la
// matriz ortografica con SetProjection().
void SetContentSize(float width, float height);

// Matriz ortografica row-major 0..640, 0..448 -> NDC. La calcula
// display_layout y la aplica gs_render_pipeline antes del pase 2D.
void SetProjection(const float matrix[16]);

// VRAM simulada (4 MB) para el menu de depuracion.
const std::uint8_t *VramData();
std::uint8_t *VramDataMutable();
std::size_t VramSize();

// Registros de textura y CLUT para SaveQuickState/LoadQuickState.
void SaveTextureCacheState(std::vector<std::uint8_t> &out);
bool LoadTextureCacheState(const std::uint8_t *data, std::size_t size);

// Recorre una cadena DMA2/GIF en ps2_ram desde source_address.
// Devuelve true si proceso al menos un bloque.
bool SubmitChain(std::uint32_t source_address);

// Procesa un bloque GIF ya extraido (fallback para DMA en game::Ram).
// Los sprites y depth maps se encolan en ps2::render; no se dibujan aqui.
void SubmitPacket(const std::uint8_t *data, std::size_t bytes);

// Dibuja un comando ya parseado. Lo usa el pipeline de frame en la fase 2D
// (depth test desactivado y glDepthMask(GL_FALSE)).
void RenderCommand(const DrawCommand &command);

void Shutdown();

}  // namespace ps2::gif
