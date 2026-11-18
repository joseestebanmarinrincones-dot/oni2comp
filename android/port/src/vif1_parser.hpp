#pragma once

// Parser de paquetes VIF1 (DMA1) para extraer lo que el juego sube a VU1:
//
//   UNPACK  -> microcodigo/datos en la memoria de datos VU1
//   MSCAL   -> ejecuta un programa VU1 (marca el draw)
//
// Formato del VIFcode (32 bits, little endian):
//
//   bit 31      IRQ
//   bits 30-24  comando (0x60..0x7F = UNPACK)
//   bits 23-16  NUM (elementos del UNPACK, 0 = 256)
//   bits 15-0   inmediato (direccion, tamano, etc.)
//
// En UNPACK el inmediato contiene la direccion qword de VU1 en los bits 0-9.
// Si el bit 15 esta puesto se suma TOPS (doble buffer). El formato de datos
// sale de los bits 2-3 (VN: S/V2/V3/V4) y 0-1 (VL: 32/16/8/V4_5) del comando.
//
// VIF escribe cada vector en un qword de 16 bytes, aunque el elemento de
// origen sea V3_32 (12 bytes) o V2_32 (8 bytes). Por eso los buffers leidos
// desde memoria VU1 siempre tienen stride 16; el tamano del elemento solo
// afecta al stream DMA de origen.
//
// El parser mantiene una copia de 16 KB de la memoria de datos VU1, captura
// todos los UNPACK y guarda un Capture cada vez que aparece un MSCAL/MSCNT.
// NewCaptures() devuelve los captures producidos por el ultimo Submit; el
// llamador debe consumirlos y llamar a ClearCaptures().
//
// Los roles (matrices, vertices, normales, UV, pesos) dependen del
// microcodigo VU1 de cada juego. Vif1Parser::BuildPacket() usa un Layout
// explicito; BuildPacketAuto() aplica una heuristica por formato/tamano de
// UNPACK y sirve para arrancar, pero lo correcto es fijar el Layout tras
// revisar los 9 .bin de vu1/ y los streams VIF1 reales.

#include "vu1_renderer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ps2::vif1 {

// Bases de matrices observadas en los microprogramas de Onimusha 2:
//   familia A: qword 0xD0 (byte 0x0D00), 4 matrices 4x4 seguidas
//   familia B: qword 0x00 (byte 0x0000), 4 matrices 4x4 seguidas
// Cada matriz ocupa 4 qwords (64 bytes). La asignacion world/view/projection
// es una convencion del port; el orden semantico exacto depende del programa.
inline constexpr std::uint32_t kOnimushaMatrixBaseA = 0x0D00;
inline constexpr std::uint32_t kOnimushaMatrixBaseB = 0x0000;

// Bases de los buffers VIF1 por atributo, en bytes de memoria de datos VU1.
// Salen de los IADDIU de los microprogramas: 0x1001/0x1801/0x2001/0x2801
// => qwords 0x100/0x180/0x200/0x280 => bytes 0x1000/0x1800/0x2000/0x2800.
inline constexpr std::uint32_t kOnimushaPositions = 0x1000;
inline constexpr std::uint32_t kOnimushaNormals = 0x1800;
inline constexpr std::uint32_t kOnimushaUvs = 0x2000;
inline constexpr std::uint32_t kOnimushaWeights = 0x2800;

struct Unpack {
    std::uint32_t address = 0;       // primer qword destino en memoria VU1
    std::uint8_t format = 0;         // formato normalizado (S/V2/V3/V4 + VL)
    std::uint8_t components = 0;
    std::uint32_t count = 0;         // vectores escritos (0 => 256)
    std::uint32_t element_size = 0;  // bytes por vector en el stream de origen
    std::vector<std::uint8_t> data;  // payload tal como llega por DMA
};

struct Capture {
    std::vector<Unpack> unpacks;
    std::array<std::uint8_t, 16384> memory{};
    std::uint32_t mscal_address = 0;  // PC en bytes dentro de VU1 code memory
    std::uint32_t tops = 0;           // TOPS en qwords al ejecutar MSCAL
    std::uint32_t itop = 0;           // ITOP en qwords al ejecutar MSCAL
    bool completed = false;
};

struct Layout {
    // Orden convencional: [0]=world, [1]=view, [2]=projection.
    std::uint32_t matrix_address = 0;
    std::uint32_t matrix_count = 3;

    std::uint32_t bone_matrix_address = 0;
    std::uint32_t bone_count = 0;

    std::uint32_t vertex_address = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t vertex_stride = 16;  // qwords de VU1

    std::uint32_t normal_address = 0;
    std::uint32_t normal_stride = 16;  // qwords de VU1
    bool has_normals = false;

    std::uint32_t uv_address = 0;
    std::uint32_t uv_stride = 16;      // qwords de VU1
    bool has_uvs = false;

    std::uint32_t color_address = 0;
    bool has_colors = false;

    std::uint32_t weight_address = 0;
    std::uint32_t bone_index_address = 0;
    bool skinned = false;

    std::uint32_t index_address = 0;
    std::uint32_t index_count = 0;
    bool triangle_strip = false;
};

class Parser {
public:
    void Reset();

    // Recorre una cadena DMA desde ps2_ram y consume los bloques VIF1.
    // Devuelve true si pudo leer algun bloque de datos.
    bool SubmitChain(std::uint32_t source_address);

    // Consume un bloque VIF1 ya extraido (fallback para game::Ram).
    // Devuelve true si produjo al menos un Capture nuevo.
    bool SubmitPacket(const std::uint8_t *data, std::size_t bytes);

    // Descarta bytes VIF a medio comando. Util antes del fallback por tags
    // cuando SubmitChain no pudo leer la cadena desde ps2_ram.
    void DiscardPendingStream() { stream_.clear(); }

    // Captures producidos desde el ultimo ClearCaptures().
    const std::vector<Capture> &NewCaptures() const { return captures_; }
    void ClearCaptures() { captures_.clear(); }

    // Compatibilidad: ultimo capture y flag historico.
    bool HasCapture() const { return has_capture_; }
    const Capture &LastCapture() const { return last_capture_; }

    // Rellena vertices/normales/UV/matrices usando roles explicitos.
    bool BuildPacket(const Layout &layout, Vu1Renderer::Packet &out) const;
    bool BuildPacket(const Capture &capture, const Layout &layout,
                     Vu1Renderer::Packet &out) const;

    // Heuristica por formato/tamano de UNPACK. Util para depurar.
    bool BuildPacketAuto(Vu1Renderer::Packet &out) const;
    bool BuildPacketAuto(const Capture &capture,
                         Vu1Renderer::Packet &out) const;

    // Layout fijo deducido de los microprogramas VU1:
    //   matrices 0x000/0xD00, posiciones 0x1000, normales 0x1800,
    //   UVs 0x2000, pesos 0x2800. Los buffers son paralelos (mismo indice).
    bool BuildPacketOnimusha(const Capture &capture,
                             Vu1Renderer::Packet &out) const;

private:
    void ParseStream();
    void Finalize(std::uint32_t mscal_address);
    static std::uint32_t ElementSize(std::uint8_t format,
                                     std::uint8_t components);
    static std::uint8_t NormalizeFormat(std::uint8_t opcode);

    std::vector<std::uint8_t> stream_;
    std::array<std::uint8_t, 16384> memory_{};
    Capture current_;
    Capture last_capture_;
    std::vector<Capture> captures_;
    bool has_capture_ = false;

    // Registros VIF1 relevantes para UNPACK/doble buffer.
    std::uint32_t base_ = 0;
    std::uint32_t tops_ = 0;
    std::uint32_t itop_ = 0;
    std::uint32_t offset_ = 0;
    std::uint8_t cl_ = 1;
    std::uint8_t wl_ = 1;
    bool dbf_ = false;
    std::uint32_t last_mscal_ = 0;
};

}  // namespace ps2::vif1
