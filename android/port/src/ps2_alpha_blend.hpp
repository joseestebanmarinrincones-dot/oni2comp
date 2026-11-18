#pragma once

// Traduccion del registro ALPHA_1/ALPHA_2 del Graphics Synthesizer de PS2 a
// estados de blending de OpenGL ES 3.0.
//
// El GS calcula:  ((A - B) * C >> 7) + D
//
//   A/B: 0 = color fuente (Cs), 1 = framebuffer (Cd), 2 = 0
//   C:   0 = alpha fuente (As), 1 = alpha framebuffer (Ad), 2 = FIX
//   D:   0 = Cs, 1 = Cd, 2 = 0
//
// FIX son los bits 32-39; 0x80 equivale a 1.0. El estado devuelto se puede
// comprobar en PC sin enlazar GLES.

#include <cstdint>

namespace ps2::gfx {

enum class Ps2AlphaOp : std::uint8_t {
    Add = 0,
    Subtract,
    ReverseSubtract,
};

enum class Ps2AlphaFactor : std::uint8_t {
    Zero = 0,
    One,
    SrcColor,
    OneMinusSrcColor,
    DstColor,
    OneMinusDstColor,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstAlpha,
    OneMinusDstAlpha,
    ConstantAlpha,
    OneMinusConstantAlpha,
};

struct Ps2AlphaBlendState {
    Ps2AlphaOp rgb_op = Ps2AlphaOp::Add;
    Ps2AlphaOp alpha_op = Ps2AlphaOp::Add;
    Ps2AlphaFactor rgb_src = Ps2AlphaFactor::SrcAlpha;
    Ps2AlphaFactor rgb_dst = Ps2AlphaFactor::OneMinusSrcAlpha;
    Ps2AlphaFactor alpha_src = Ps2AlphaFactor::One;
    Ps2AlphaFactor alpha_dst = Ps2AlphaFactor::OneMinusSrcAlpha;
    std::uint8_t fix = 0x80;
    bool supported = false;
    const char* name = "fallback";
};

// Decodifica un valor ALPHA_1/ALPHA_2 (64 bits).
Ps2AlphaBlendState DecodePs2AlphaBlend(std::uint64_t alpha_reg);

// Aplica el estado con glBlendEquationSeparate/glBlendFuncSeparate.
// Debe llamarse con GL_BLEND habilitado y solo si PRIM.ABE != 0.
void ApplyPs2AlphaBlend(std::uint64_t alpha_reg);

}  // namespace ps2::gfx
