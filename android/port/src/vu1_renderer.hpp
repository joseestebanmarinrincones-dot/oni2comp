#pragma once

// Plantilla de render GLES 3.0 para la etapa de transformación de VU1.
//
// Los 9 .bin de games/onimusha2/vu1 son microcódigo VU1 (16 KB), no listas de
// vértices. El juego les envía paquetes VIF con matrices y vértices; esta clase
// recibe ya extraídos esos datos y reproduce la transformación en la GPU:
//
//   position -> (skinning) -> world -> view -> projection -> gl_Position
//   color, uv, fog -> fragment shader
//
// El shader cubre las variantes habituales de VU1: rígido, skinning de hasta
// 32 huesos, billboard (si el caller genera la matriz), color por vértice,
// textura RGBA8, fog por vértice y alpha test.
//
// Matrices: se guardan en row-major (como las de PS2/VU1, traslación en la
// fila 3). Al subirlas a GL se transponen (GL_TRUE), porque GLSL espera
// column-major.

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

class Vu1Renderer {
public:
    static constexpr int kMaxBones = 32;

    struct Mat4 {
        float m[16]{};
    };

    struct Vec3 {
        float v[3]{};
    };

    // Formato interleaved que se sube al VBO.
    struct Vertex {
        float position[4];   // x,y,z,w (VU1 V4_32)
        float uv[2];         // S,T normalizados (o /Q ya aplicado)
        float normal[3];     // para modo lit; puede ser 0
        float fog;           // 0..1, factor de niebla VU1
        std::uint8_t color[4];    // RGBA 0..255, se normaliza en GL
        std::uint8_t weights[4];  // pesos de hueso 0..255, normalizados
        std::uint8_t bones[4];    // índices de hueso 0..31
        std::uint8_t pad[4];      // alineación
    };

    struct Packet {
        Mat4 world = Identity();
        Mat4 view = Identity();
        Mat4 projection = Identity();
        std::vector<Mat4> bones;          // hasta kMaxBones; si está vacío, identidad
        std::vector<Vertex> vertices;
        std::vector<std::uint16_t> indices;

        std::uint32_t texture = 0;        // textura RGBA8 del GS (GLuint)
        bool textured = false;
        bool skinned = false;
        bool lit = false;
        bool triangle_strip = false;      // VU1 suele emitir tiras

        float alpha_ref = 0.0f;           // 0 = sin alpha test
        Vec3 fog_color{0.0f, 0.0f, 0.0f};
        Vec3 light_dir{0.0f, 0.0f, -1.0f};
        Vec3 light_color{1.0f, 1.0f, 1.0f};
        Vec3 ambient{0.2f, 0.2f, 0.2f};
    };

    Vu1Renderer() = default;
    ~Vu1Renderer();

    Vu1Renderer(const Vu1Renderer &) = delete;
    Vu1Renderer &operator=(const Vu1Renderer &) = delete;

    bool Initialize();
    void Shutdown();
    bool Ready() const { return program_ != 0; }

    void Render(const Packet &packet, float projection_x_scale = 1.0f);

    static Mat4 Identity() {
        Mat4 result{};
        result.m[0] = 1.0f;
        result.m[5] = 1.0f;
        result.m[10] = 1.0f;
        result.m[15] = 1.0f;
        return result;
    }

    // Producto row-major a*b. Se define inline para poder usar Packet en
    // tests de PC sin enlazar GLES.
    static Mat4 Multiply(const Mat4 &a, const Mat4 &b) {
        Mat4 result{};
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                float value = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    value += a.m[row * 4 + k] * b.m[k * 4 + col];
                }
                result.m[row * 4 + col] = value;
            }
        }
        return result;
    }

private:
    void set_matrix(std::int32_t location, const Mat4 &matrix) const;

    std::uint32_t program_ = 0;
    std::uint32_t vao_ = 0;
    std::uint32_t vbo_ = 0;
    std::uint32_t ebo_ = 0;

    std::int32_t u_world_ = -1;
    std::int32_t u_view_ = -1;
    std::int32_t u_projection_ = -1;
    std::int32_t u_bones_ = -1;
    std::int32_t u_skinned_ = -1;
    std::int32_t u_lit_ = -1;
    std::int32_t u_light_dir_ = -1;
    std::int32_t u_light_color_ = -1;
    std::int32_t u_ambient_ = -1;
    std::int32_t u_texture_ = -1;
    std::int32_t u_textured_ = -1;
    std::int32_t u_alpha_ref_ = -1;
    std::int32_t u_fog_color_ = -1;
};
