#include "gif_renderer.hpp"

#include "display_layout.hpp"
#include "gif_packet.hpp"
#include "gs_render_pipeline.hpp"
#include "ps2_alpha_blend.hpp"
#include "ps2_mem.hpp"

#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

#if defined(__ANDROID__)
#include <android/log.h>
#define GIF_TAG "OnimushaGif"
#define GIF_LOGI(...) __android_log_print(ANDROID_LOG_INFO, GIF_TAG, __VA_ARGS__)
#define GIF_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, GIF_TAG, __VA_ARGS__)
#else
#define GIF_LOGI(...) do { \
    std::fprintf(stdout, "[OnimushaGif] "); \
    std::fprintf(stdout, __VA_ARGS__); \
    std::fputc('\n', stdout); \
} while (0)
#define GIF_LOGE(...) do { \
    std::fprintf(stderr, "[OnimushaGif] "); \
    std::fprintf(stderr, __VA_ARGS__); \
    std::fputc('\n', stderr); \
} while (0)
#endif

namespace ps2::gfx {
namespace {

GLenum ToGl(Ps2AlphaOp op) {
    switch (op) {
    case Ps2AlphaOp::Add: return GL_FUNC_ADD;
    case Ps2AlphaOp::Subtract: return GL_FUNC_SUBTRACT;
    case Ps2AlphaOp::ReverseSubtract: return GL_FUNC_REVERSE_SUBTRACT;
    }
    return GL_FUNC_ADD;
}

GLenum ToGl(Ps2AlphaFactor factor) {
    switch (factor) {
    case Ps2AlphaFactor::Zero: return GL_ZERO;
    case Ps2AlphaFactor::One: return GL_ONE;
    case Ps2AlphaFactor::SrcColor: return GL_SRC_COLOR;
    case Ps2AlphaFactor::OneMinusSrcColor: return GL_ONE_MINUS_SRC_COLOR;
    case Ps2AlphaFactor::DstColor: return GL_DST_COLOR;
    case Ps2AlphaFactor::OneMinusDstColor: return GL_ONE_MINUS_DST_COLOR;
    case Ps2AlphaFactor::SrcAlpha: return GL_SRC_ALPHA;
    case Ps2AlphaFactor::OneMinusSrcAlpha: return GL_ONE_MINUS_SRC_ALPHA;
    case Ps2AlphaFactor::DstAlpha: return GL_DST_ALPHA;
    case Ps2AlphaFactor::OneMinusDstAlpha: return GL_ONE_MINUS_DST_ALPHA;
    case Ps2AlphaFactor::ConstantAlpha: return GL_CONSTANT_ALPHA;
    case Ps2AlphaFactor::OneMinusConstantAlpha:
        return GL_ONE_MINUS_CONSTANT_ALPHA;
    }
    return GL_ONE;
}

}  // namespace

void ApplyPs2AlphaBlend(std::uint64_t alpha_reg) {
    const Ps2AlphaBlendState state = DecodePs2AlphaBlend(alpha_reg);
    const float constant = std::min(1.0f, static_cast<float>(state.fix) / 128.0f);
    glBlendColor(1.0f, 1.0f, 1.0f, constant);
    glEnable(GL_BLEND);
    glBlendEquationSeparate(ToGl(state.rgb_op), ToGl(state.alpha_op));
    glBlendFuncSeparate(ToGl(state.rgb_src), ToGl(state.rgb_dst),
                        ToGl(state.alpha_src), ToGl(state.alpha_dst));
}

}  // namespace ps2::gfx

namespace ps2::gif {
namespace {

constexpr int kDefaultContentWidth = 640;
constexpr int kDefaultContentHeight = 448;

GLuint compile_shader(GLenum type, const char *source) {
    const GLuint shader = glCreateShader(type);
    if (shader == 0) return 0;
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_FALSE) {
        char log[1024] = {};
        glGetShaderInfoLog(shader, sizeof(log) - 1, nullptr, log);
        GIF_LOGE("shader compile error: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint link_program(const char *vertex_source, const char *fragment_source) {
    const GLuint vertex = compile_shader(GL_VERTEX_SHADER, vertex_source);
    const GLuint fragment = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
    if (vertex == 0 || fragment == 0) {
        if (vertex != 0) glDeleteShader(vertex);
        if (fragment != 0) glDeleteShader(fragment);
        return 0;
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok == GL_FALSE) {
        char log[1024] = {};
        glGetProgramInfoLog(program, sizeof(log) - 1, nullptr, log);
        GIF_LOGE("program link error: %s", log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

struct QuadVertex {
    float x;
    float y;
    float u;
    float v;
};

class Renderer {
public:
    Renderer() {
        const auto ortho = ps2::render::Ps2OrthoMatrix();
        std::memcpy(projection_, ortho.m, sizeof(projection_));
    }

    bool Initialize() {
        if (program_ != 0) return true;
        if (failed_) return false;

        static constexpr char kVertexShader[] =
            "#version 300 es\n"
            "layout(location = 0) in vec2 a_pos;\n"
            "layout(location = 1) in vec2 a_uv;\n"
            "uniform mat4 u_projection;\n"
            "out vec2 v_uv;\n"
            "void main() {\n"
            "    gl_Position = u_projection * vec4(a_pos, 0.0, 1.0);\n"
            "    v_uv = a_uv;\n"
            "}\n";

        static constexpr char kFragmentShader[] =
            "#version 300 es\n"
            "precision mediump float;\n"
            "uniform sampler2D u_texture;\n"
            "uniform vec4 u_color;\n"
            "uniform int u_use_texture;\n"
            "in vec2 v_uv;\n"
            "out vec4 fragColor;\n"
            "void main() {\n"
            "    vec4 texel = u_use_texture != 0\n"
            "        ? texture(u_texture, v_uv) : vec4(1.0);\n"
            "    fragColor = texel * u_color;\n"
            "}\n";

        program_ = link_program(kVertexShader, kFragmentShader);
        if (program_ == 0) {
            failed_ = true;
            return false;
        }

        u_projection_ = glGetUniformLocation(program_, "u_projection");
        u_texture_ = glGetUniformLocation(program_, "u_texture");
        u_color_ = glGetUniformLocation(program_, "u_color");
        u_use_texture_ = glGetUniformLocation(program_, "u_use_texture");

        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glGenBuffers(1, &ebo_);

        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(sizeof(QuadVertex) * 4),
                     nullptr, GL_DYNAMIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                              sizeof(QuadVertex),
                              reinterpret_cast<void *>(offsetof(QuadVertex, x)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                              sizeof(QuadVertex),
                              reinterpret_cast<void *>(offsetof(QuadVertex, u)));

        static constexpr std::uint16_t kIndices[6] = {0, 1, 2, 2, 3, 0};
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kIndices), kIndices,
                     GL_STATIC_DRAW);
        glBindVertexArray(0);

        glGenTextures(1, &texture_);
        glGenTextures(1, &fallback_);
        create_fallback_texture();

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        GIF_LOGI("GIF renderer inicializado");
        return true;
    }

    bool Ready() const { return program_ != 0; }

    void Shutdown() {
        if (texture_ != 0) glDeleteTextures(1, &texture_);
        if (fallback_ != 0) glDeleteTextures(1, &fallback_);
        if (ebo_ != 0) glDeleteBuffers(1, &ebo_);
        if (vbo_ != 0) glDeleteBuffers(1, &vbo_);
        if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
        if (program_ != 0) glDeleteProgram(program_);
        texture_ = fallback_ = ebo_ = vbo_ = vao_ = program_ = 0;
        failed_ = false;
    }

    void SetScreenSize(int width, int height) {
        screen_width_ = std::max(1, width);
        screen_height_ = std::max(1, height);
    }

    void SetContentSize(float width, float height) {
        if (width > 0.0f) content_width_ = width;
        if (height > 0.0f) content_height_ = height;
    }

    void SetProjection(const float matrix[16]) {
        if (matrix != nullptr) {
            std::memcpy(projection_, matrix, sizeof(projection_));
        }
    }

    void Render(const DrawCommand &command) {
        if (program_ == 0 || command.vertex_count < 2) return;

        float x0 = command.vertices[0].x;
        float x1 = x0;
        float y0 = command.vertices[0].y;
        float y1 = y0;
        float u0 = command.vertices[0].u;
        float u1 = u0;
        float v0 = command.vertices[0].v;
        float v1 = v0;

        for (int i = 1; i < command.vertex_count; ++i) {
            x0 = std::min(x0, command.vertices[i].x);
            x1 = std::max(x1, command.vertices[i].x);
            y0 = std::min(y0, command.vertices[i].y);
            y1 = std::max(y1, command.vertices[i].y);
            u0 = std::min(u0, command.vertices[i].u);
            u1 = std::max(u1, command.vertices[i].u);
            v0 = std::min(v0, command.vertices[i].v);
            v1 = std::max(v1, command.vertices[i].v);
        }

        if (!std::isfinite(x0) || !std::isfinite(x1) ||
            !std::isfinite(y0) || !std::isfinite(y1)) return;
        if (x1 - x0 < 0.01f || y1 - y0 < 0.01f) return;
        // Descarta paquetes corruptos con coordenadas fuera del framebuffer.
        if (x0 < -4096.0f || x1 > 4096.0f ||
            y0 < -4096.0f || y1 > 4096.0f) return;

        // Un sprite texturizado sin textura valida dibujaria el checkerboard
        // de respaldo como un cuadro solido. Mejor omitirlo y dejar el log.
        if (command.textured && !command.has_texture) {
            static int missing_logs = 0;
            if (missing_logs < 12) {
                ++missing_logs;
                GIF_LOGI("sprite sin textura: prim=0x%03X blend=%s",
                         command.prim, BlendModeName(command.blend_mode));
            }
            return;
        }

        if (command.blend_mode == BlendMode::Opaque) {
            glDisable(GL_BLEND);
        } else {
            ps2::gfx::ApplyPs2AlphaBlend(command.alpha_register);
        }

        static int blend_logs[4] = {0, 0, 0, 0};
        const int mode_index = static_cast<int>(command.blend_mode);
        if (mode_index >= 0 && mode_index < 4 &&
            blend_logs[mode_index] < 4) {
            ++blend_logs[mode_index];
            GIF_LOGI("blend %s prim=0x%03X alpha=0x%08llX tfx=%u",
                     BlendModeName(command.blend_mode), command.prim,
                     static_cast<unsigned long long>(
                         command.alpha_register & 0xFFFFFFFFull),
                     command.tfx);
        }

        const QuadVertex vertices[4] = {
            {x0, y0, u0, v0},
            {x1, y0, u1, v0},
            {x1, y1, u1, v1},
            {x0, y1, u0, v1},
        };

        glUseProgram(program_);
        glUniformMatrix4fv(u_projection_, 1, GL_TRUE, projection_);
        glUniform1i(u_texture_, 0);

        const Vertex &first = command.vertices[0];
        glUniform4f(u_color_,
                    static_cast<float>(first.rgba[0]) / 255.0f,
                    static_cast<float>(first.rgba[1]) / 255.0f,
                    static_cast<float>(first.rgba[2]) / 255.0f,
                    static_cast<float>(first.rgba[3]) / 255.0f);

        glActiveTexture(GL_TEXTURE0);
        if (command.textured && command.has_texture &&
            command.texture.valid && !command.texture.rgba.empty()) {
            upload_texture(command.texture);
            glBindTexture(GL_TEXTURE_2D, texture_);
            glUniform1i(u_use_texture_, 1);
        } else {
            // Primitiva de color plano (PRIM.TME=0): no muestrear la textura.
            glBindTexture(GL_TEXTURE_2D, fallback_);
            glUniform1i(u_use_texture_, 0);
        }

        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr);
        glBindVertexArray(0);
        glUseProgram(0);
    }

private:
    void upload_texture(const Texture &texture) {
        glBindTexture(GL_TEXTURE_2D, texture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                     static_cast<GLsizei>(texture.width),
                     static_cast<GLsizei>(texture.height), 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, texture.rgba.data());

        // Las fuentes/menús usan PSMT8/PSMT4; NEAREST mantiene los glifos
        // nítidos. Las texturas de color siguen con filtrado lineal.
        const bool indexed = texture.psm == 19u || texture.psm == 20u ||
                             texture.psm == 26u || texture.psm == 27u ||
                             texture.psm == 44u;
        const GLenum filter = indexed ? GL_NEAREST : GL_LINEAR;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    void create_fallback_texture() {
        constexpr int kSize = 64;
        std::vector<std::uint8_t> pixels(kSize * kSize * 4u);
        for (int y = 0; y < kSize; ++y) {
            for (int x = 0; x < kSize; ++x) {
                const bool checker = (((x / 8) + (y / 8)) & 1) != 0;
                const std::size_t offset =
                    static_cast<std::size_t>(y * kSize + x) * 4u;
                pixels[offset + 0u] = checker ? 255u : 32u;
                pixels[offset + 1u] = checker ? 32u : 255u;
                pixels[offset + 2u] = checker ? 255u : 32u;
                pixels[offset + 3u] = 255u;
            }
        }

        glBindTexture(GL_TEXTURE_2D, fallback_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kSize, kSize, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;
    GLuint texture_ = 0;
    GLuint fallback_ = 0;

    GLint u_projection_ = -1;
    GLint u_texture_ = -1;
    GLint u_color_ = -1;
    GLint u_use_texture_ = -1;

    int screen_width_ = 1280;
    int screen_height_ = 720;
    float content_width_ = static_cast<float>(kDefaultContentWidth);
    float content_height_ = static_cast<float>(kDefaultContentHeight);
    float projection_[16]{};
    bool failed_ = false;
};

Renderer g_renderer;
PacketParser g_parser;

bool read_ram(std::uint32_t address, void *out, std::size_t bytes) {
    const std::uint32_t physical = Ps2Memory::PhysicalAddress(address);
    if (physical >= kPs2RamSize) return false;
    if (bytes > kPs2RamSize - physical) return false;
    std::memcpy(out, ps2_ram + physical, bytes);
    return true;
}

const std::uint8_t *ram_ptr(std::uint32_t address, std::size_t bytes) {
    const std::uint32_t physical = Ps2Memory::PhysicalAddress(address);
    if (physical >= kPs2RamSize) return nullptr;
    if (bytes > kPs2RamSize - physical) return nullptr;
    return ps2_ram + physical;
}

bool advance(std::uint32_t base, std::uint32_t qwords,
             std::uint32_t &next) {
    const std::uint64_t delta = 16ull + static_cast<std::uint64_t>(qwords) * 16ull;
    const std::uint64_t value = static_cast<std::uint64_t>(base) + delta;
    if (value > 0xFFFFFFFFull) return false;
    next = static_cast<std::uint32_t>(value);
    return true;
}

}  // namespace

void SetScreenSize(int width, int height) {
    g_renderer.SetScreenSize(width, height);
}

void SetContentSize(float width, float height) {
    g_renderer.SetContentSize(width, height);
}

void SetProjection(const float matrix[16]) {
    g_renderer.SetProjection(matrix);
}

const std::uint8_t *VramData() {
    return g_parser.Vram().data();
}

std::uint8_t *VramDataMutable() {
    return const_cast<std::uint8_t *>(g_parser.Vram().data());
}

std::size_t VramSize() {
    return g_parser.Vram().size();
}

void SaveTextureCacheState(std::vector<std::uint8_t> &out) {
    g_parser.SaveCacheState(out);
}

bool LoadTextureCacheState(const std::uint8_t *data, std::size_t size) {
    return g_parser.LoadCacheState(data, size);
}

void SubmitPacket(const std::uint8_t *data, std::size_t bytes) {
    if (data == nullptr || bytes == 0) return;

    DrawCommand command;
    if (!g_parser.ParseBlock(data, bytes, command)) return;

    static int draw_logs = 0;
    if (draw_logs < 8) {
        ++draw_logs;
        GIF_LOGI("GIF queue: %d vertices tex=%ux%u has_tex=%d depth=%d "
                 "v0=(%.1f,%.1f) uv0=(%.3f,%.3f)",
                 command.vertex_count,
                 command.texture_width_hint, command.texture_height_hint,
                 command.has_texture ? 1 : 0,
                 command.has_depth_map ? 1 : 0,
                 command.vertices[0].x, command.vertices[0].y,
                 command.vertices[0].u, command.vertices[0].v);
    }

    if (command.has_depth_map && command.depth_map.valid) {
        ps2::render::QueueDepthMap(std::move(command.depth_map));
    }
    if (command.vertex_count >= 2) {
        ps2::render::QueueBackground(std::move(command));
    }
}

void RenderCommand(const DrawCommand &command) {
    if (!g_renderer.Ready() && !g_renderer.Initialize()) return;
    g_renderer.Render(command);
}

bool SubmitChain(std::uint32_t source_address) {
    static int chain_logs = 0;
    if (chain_logs < 3) {
        ++chain_logs;
        GIF_LOGI("GIF chain source=0x%08X", source_address);
    }

    std::uint32_t current = source_address;
    std::uint32_t returns[2] = {0, 0};
    int asp = 0;
    bool parsed = false;

    for (int index = 0; index < 256; ++index) {
        std::uint64_t qword = 0;
        if (!read_ram(current, &qword, sizeof(qword))) break;
        if (qword == 0 || qword == ~0ull) break;

        const std::uint32_t qwc = static_cast<std::uint32_t>(qword & 0xFFFFu);
        const std::uint8_t id = static_cast<std::uint8_t>((qword >> 28) & 0x7u);
        const std::uint32_t addr =
            static_cast<std::uint32_t>((qword >> 32) & 0x7FFFFFFFu);

        const std::uint32_t data_address =
            (id == 0u || id == 3u || id == 4u) ? addr : current + 16u;
        const std::size_t data_bytes = static_cast<std::size_t>(qwc) * 16u;

        if (data_bytes != 0) {
            if (const std::uint8_t *data = ram_ptr(data_address, data_bytes)) {
                SubmitPacket(data, data_bytes);
                parsed = true;
            }
        }

        switch (id) {
        case 0:  // REFE
            return parsed;
        case 1:  // CNT
            if (!advance(current, qwc, current)) return parsed;
            break;
        case 2:  // NEXT
            if (addr == 0) return parsed;
            current = addr;
            break;
        case 3:  // REF
        case 4:  // REFS
            current += 16u;
            break;
        case 5: {  // CALL
            if (asp >= 2) return parsed;
            std::uint32_t resume = 0;
            if (!advance(current, qwc, resume)) return parsed;
            returns[asp++] = resume;
            if (addr == 0) return parsed;
            current = addr;
            break;
        }
        case 6:  // RET
            if (asp == 0) return parsed;
            current = returns[--asp];
            break;
        case 7:  // END
            return parsed;
        default:
            return parsed;
        }

        if (current == 0) return parsed;
    }

    return parsed;
}

void Shutdown() {
    g_renderer.Shutdown();
    g_parser.Reset();
}

}  // namespace ps2::gif

// Sustituye la version weak de main.cpp: guarda el tamano del drawable.
void Game_Resize(int width, int height) {
    ps2::gif::SetScreenSize(width, height);
}
