#pragma once

// Tipos de textura y Z-buffer compartidos por el parser GIF y la cache GS.

#include <cstdint>
#include <vector>

namespace ps2::gif {

struct Texture {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t psm = 0;
    std::vector<std::uint8_t> rgba;
    bool valid = false;
};

// Z-buffer subido por el juego mediante una transferencia GIF IMAGE
// (BITBLTBUF con PSMZ32/PSMZ24/PSMZ16/PSMZ16S + TRXREG + TRXDIR host->local).
// Los valores son enteros de 24 bits (0 = near, 0xFFFFFF = far) tal como los
// usa el GS. El renderer los convierte a GL_DEPTH_COMPONENT24.
struct DepthMap {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t psm = 0;
    std::vector<std::uint32_t> depth;  // row-major, 0..0xFFFFFF
    bool valid = false;
};

}  // namespace ps2::gif
