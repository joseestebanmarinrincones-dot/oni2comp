#include "gs_render_pipeline.hpp"

#include "display_layout.hpp"
#include "gif_renderer.hpp"

#include <GLES3/gl3.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

#if defined(__ANDROID__)
#include <android/log.h>
#define RENDER_TAG "OnimushaRender"
#define RENDER_LOGI(...) __android_log_print(ANDROID_LOG_INFO, RENDER_TAG, __VA_ARGS__)
#define RENDER_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, RENDER_TAG, __VA_ARGS__)
#else
#define RENDER_LOGI(...) do { \
    std::fprintf(stdout, "[OnimushaRender] "); \
    std::fprintf(stdout, __VA_ARGS__); \
    std::fputc('\n', stdout); \
} while (0)
#define RENDER_LOGE(...) do { \
    std::fprintf(stderr, "[OnimushaRender] "); \
    std::fprintf(stderr, __VA_ARGS__); \
    std::fputc('\n', stderr); \
} while (0)
#endif

namespace ps2::render {
namespace {

// El GS usa origen arriba-izquierda; las texturas GL usan abajo-izquierda.
// El depth map se adjunta como depth attachment, asi que hay que invertir las
// filas al subirlo para que el pixel (x, y) coincida con el framebuffer GL.
constexpr bool kFlipDepthRows = true;

std::vector<ps2::gif::DrawCommand> g_backgrounds;
std::vector<Vu1Renderer::Packet> g_models;
AspectMode g_aspect_mode = AspectMode::Pillarbox4x3;

// El Z-buffer del escenario se conserva hasta que el juego suba otro.
ps2::gif::DepthMap g_depth_map;
bool g_has_depth_map = false;

int g_internal_scale = 1;

// Resolucion logica de los modelos 3D de PS2. El fondo 2D sigue a 640x448;
// los modelos se pueden renderizar a 2x/3x de esta base.
constexpr int kNative3dWidth = 640;
constexpr int kNative3dHeight = 448;

GLuint g_fbo = 0;
GLuint g_color_texture = 0;
GLuint g_depth_texture = 0;
int g_target_width = 0;
int g_target_height = 0;
bool g_depth_is_16 = false;

GLuint g_composite_program = 0;
GLuint g_composite_vao = 0;
GLuint g_composite_vbo = 0;
GLint g_composite_texture = -1;

Vu1Renderer &Renderer() {
    // Se libera a proposito: evita llamar a GL durante la destruccion estatica.
    static Vu1Renderer *renderer = new Vu1Renderer();
    return *renderer;
}

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
        RENDER_LOGE("shader: %s", log);
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
        RENDER_LOGE("link: %s", log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

bool EnsureCompositeProgram() {
    if (g_composite_program != 0) return true;

    static constexpr char kVertex[] =
        "#version 300 es\n"
        "layout(location = 0) in vec2 a_pos;\n"
        "out vec2 v_uv;\n"
        "void main() {\n"
        "    v_uv = a_pos * 0.5 + 0.5;\n"
        "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
        "}\n";
    static constexpr char kFragment[] =
        "#version 300 es\n"
        "precision mediump float;\n"
        "uniform sampler2D u_texture;\n"
        "in vec2 v_uv;\n"
        "out vec4 fragColor;\n"
        "void main() { fragColor = texture(u_texture, v_uv); }\n";

    g_composite_program = link_program(kVertex, kFragment);
    if (g_composite_program == 0) return false;
    g_composite_texture =
        glGetUniformLocation(g_composite_program, "u_texture");

    static constexpr float kQuad[8] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f,  1.0f,
    };

    glGenVertexArrays(1, &g_composite_vao);
    glGenBuffers(1, &g_composite_vbo);
    glBindVertexArray(g_composite_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_composite_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float),
                          reinterpret_cast<void *>(0));
    glBindVertexArray(0);
    return true;
}

void DestroyTargets() {
    if (g_fbo != 0) glDeleteFramebuffers(1, &g_fbo);
    if (g_color_texture != 0) glDeleteTextures(1, &g_color_texture);
    if (g_depth_texture != 0) glDeleteTextures(1, &g_depth_texture);
    g_fbo = g_color_texture = g_depth_texture = 0;
    g_target_width = g_target_height = 0;
    g_depth_is_16 = false;
}

bool CreateTargets(int width, int height, bool depth16) {
    glGenTextures(1, &g_color_texture);
    glBindTexture(GL_TEXTURE_2D, g_color_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &g_depth_texture);
    glBindTexture(GL_TEXTURE_2D, g_depth_texture);
    glTexImage2D(GL_TEXTURE_2D, 0,
                 depth16 ? GL_DEPTH_COMPONENT16 : GL_DEPTH_COMPONENT24,
                 width, height, 0, GL_DEPTH_COMPONENT,
                 depth16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &g_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, g_color_texture, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           GL_TEXTURE_2D, g_depth_texture, 0);

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (status == GL_FRAMEBUFFER_COMPLETE) {
        g_target_width = width;
        g_target_height = height;
        g_depth_is_16 = depth16;
        return true;
    }

    RENDER_LOGE("FBO incompleto (0x%04X), depth %s",
                static_cast<unsigned>(status),
                depth16 ? "16" : "24");
    DestroyTargets();
    return false;
}

bool EnsureTargets(int width, int height) {
    if (width <= 0 || height <= 0) return false;
    if (g_fbo != 0 && g_target_width == width && g_target_height == height) {
        return true;
    }
    DestroyTargets();
    if (CreateTargets(width, height, false)) return true;
    if (CreateTargets(width, height, true)) {
        RENDER_LOGI("depth map: usando GL_DEPTH_COMPONENT16");
        return true;
    }
    return false;
}

int MaxTextureSize() {
    static int cached = 0;
    if (cached <= 0) {
        GLint value = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &value);
        cached = value > 0 ? static_cast<int>(value) : 4096;
    }
    return cached;
}

void ScaleTargetSize(int width, int height, int scale,
                     int &out_width, int &out_height) {
    const int safe_scale = std::max(1, std::min(3, scale));
    int scaled_width = std::max(1, width) * safe_scale;
    int scaled_height = std::max(1, height) * safe_scale;
    const int max_size = MaxTextureSize();
    if (scaled_width > max_size || scaled_height > max_size) {
        const float factor = std::min(
            static_cast<float>(max_size) / static_cast<float>(scaled_width),
            static_cast<float>(max_size) / static_cast<float>(scaled_height));
        scaled_width = std::max(1, static_cast<int>(
            static_cast<float>(scaled_width) * factor));
        scaled_height = std::max(1, static_cast<int>(
            static_cast<float>(scaled_height) * factor));
        static int clamp_logs = 0;
        if (clamp_logs < 4) {
            ++clamp_logs;
            RENDER_LOGI("resolucion interna limitada a %dx%d (max GL %d)",
                        scaled_width, scaled_height, max_size);
        }
    }
    out_width = scaled_width;
    out_height = scaled_height;
}

bool UploadDepthMap(const ps2::gif::DepthMap &map, int target_width,
                    int target_height) {
    if (!map.valid || map.width == 0 || map.height == 0) return false;
    if (target_width <= 0 || target_height <= 0) return false;
    const std::size_t source_count =
        static_cast<std::size_t>(map.width) * map.height;
    if (map.depth.size() < source_count) return false;
    const std::size_t target_count =
        static_cast<std::size_t>(target_width) * target_height;

    glBindTexture(GL_TEXTURE_2D, g_depth_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, g_depth_is_16 ? 2 : 4);

    if (g_depth_is_16) {
        std::vector<std::uint16_t> flipped(target_count);
        for (int y = 0; y < target_height; ++y) {
            const std::uint32_t source_y =
                (kFlipDepthRows
                     ? static_cast<std::uint32_t>(target_height - 1 - y)
                     : static_cast<std::uint32_t>(y)) *
                map.height / static_cast<std::uint32_t>(target_height);
            const std::uint32_t *source =
                map.depth.data() +
                static_cast<std::size_t>(source_y) * map.width;
            std::uint16_t *dest =
                flipped.data() + static_cast<std::size_t>(y) * target_width;
            for (int x = 0; x < target_width; ++x) {
                const std::uint32_t source_x =
                    static_cast<std::uint32_t>(x) * map.width /
                    static_cast<std::uint32_t>(target_width);
                dest[x] = static_cast<std::uint16_t>(source[source_x] >> 8);
            }
        }
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, target_width, target_height,
                        GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT,
                        flipped.data());
    } else {
        std::vector<std::uint32_t> flipped(target_count);
        for (int y = 0; y < target_height; ++y) {
            const std::uint32_t source_y =
                (kFlipDepthRows
                     ? static_cast<std::uint32_t>(target_height - 1 - y)
                     : static_cast<std::uint32_t>(y)) *
                map.height / static_cast<std::uint32_t>(target_height);
            const std::uint32_t *source =
                map.depth.data() +
                static_cast<std::size_t>(source_y) * map.width;
            std::uint32_t *dest =
                flipped.data() + static_cast<std::size_t>(y) * target_width;
            for (int x = 0; x < target_width; ++x) {
                const std::uint32_t source_x =
                    static_cast<std::uint32_t>(x) * map.width /
                    static_cast<std::uint32_t>(target_width);
                dest[x] = source[source_x];
            }
        }
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, target_width, target_height,
                        GL_DEPTH_COMPONENT, GL_UNSIGNED_INT,
                        flipped.data());
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void BindModelState() {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void DrawModels(float projection_x_scale) {
    Vu1Renderer &renderer = Renderer();
    if (!renderer.Ready() && !renderer.Initialize()) return;
    for (const Vu1Renderer::Packet &packet : g_models) {
        renderer.Render(packet, projection_x_scale);
    }
}

void DrawModelsDirect(const GLint viewport[4], float projection_x_scale) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    BindModelState();
    DrawModels(projection_x_scale);
}

bool CompositeTargets(const GLint viewport[4]) {
    if (!EnsureCompositeProgram()) return false;

    // Composicion premultiplicada sobre el fondo ya dibujado. La textura de
    // color puede ser mayor que el viewport: GL_LINEAR la reduce y suaviza
    // los bordes (supersampling).
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(g_composite_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_color_texture);
    glUniform1i(g_composite_texture, 0);
    glBindVertexArray(g_composite_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glUseProgram(0);
    return true;
}

bool DrawModelsWithDepthMap(const GLint viewport[4],
                            float projection_x_scale) {
    if (!EnsureCompositeProgram()) return false;

    int target_width = 0;
    int target_height = 0;
    ScaleTargetSize(static_cast<int>(g_depth_map.width),
                    static_cast<int>(g_depth_map.height), g_internal_scale,
                    target_width, target_height);
    if (!EnsureTargets(target_width, target_height)) return false;
    if (!UploadDepthMap(g_depth_map, target_width, target_height)) return false;

    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glViewport(0, 0, target_width, target_height);
    glDisable(GL_SCISSOR_TEST);

    // No se limpia GL_DEPTH_BUFFER_BIT: el depth map cargado es la
    // profundidad inicial contra la que compiten los modelos.
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    BindModelState();
    DrawModels(projection_x_scale);

    return CompositeTargets(viewport);
}

bool DrawModelsToTarget(const GLint viewport[4],
                        float projection_x_scale) {
    if (!EnsureCompositeProgram()) return false;

    // Base nativa 640x448. En widescreen se conserva la altura nativa y se
    // ensancha el ancho para que el FBO tenga el mismo aspect que la ventana.
    int base_width = kNative3dWidth;
    int base_height = kNative3dHeight;
    if (viewport[2] > 0 && viewport[3] > 0) {
        const float aspect = static_cast<float>(viewport[2]) /
                             static_cast<float>(viewport[3]);
        if (aspect > 1.5f) {
            base_width = std::max(
                1, static_cast<int>(kNative3dHeight * aspect + 0.5f));
        }
    }

    int target_width = 0;
    int target_height = 0;
    ScaleTargetSize(base_width, base_height, g_internal_scale,
                    target_width, target_height);
    if (!EnsureTargets(target_width, target_height)) return false;

    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glViewport(0, 0, target_width, target_height);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    BindModelState();
    DrawModels(projection_x_scale);

    return CompositeTargets(viewport);
}

void ClearQueues() {
    g_backgrounds.clear();
    g_models.clear();
}

}  // namespace

void QueueBackground(ps2::gif::DrawCommand command) {
    g_backgrounds.push_back(std::move(command));
}

void QueueDepthMap(ps2::gif::DepthMap depth_map) {
    if (!depth_map.valid) return;
    g_depth_map = std::move(depth_map);
    g_has_depth_map = true;
    static int depth_logs = 0;
    if (depth_logs < 8) {
        ++depth_logs;
        RENDER_LOGI("depth map %ux%u psm=0x%02X",
                    g_depth_map.width, g_depth_map.height, g_depth_map.psm);
    }
}

void QueueModel(Vu1Renderer::Packet packet) {
    g_models.push_back(std::move(packet));
}

void SetWidescreen(bool enabled) {
    g_aspect_mode = enabled ? AspectMode::Widescreen
                            : AspectMode::Pillarbox4x3;
}

bool WidescreenEnabled() {
    return g_aspect_mode == AspectMode::Widescreen;
}

void ToggleWidescreen() {
    SetWidescreen(!WidescreenEnabled());
}

void SetInternalResolutionScale(int scale) {
    scale = std::max(1, std::min(3, scale));
    if (scale == g_internal_scale) return;
    g_internal_scale = scale;
    // Las texturas se recrean en el siguiente ExecuteFrame(); liberar ya
    // evita mantener dos juegos de FBOs grandes durante el cambio.
    DestroyTargets();

    // Verificacion temprana del FBO de modelos (se crea de nuevo al dibujar).
    if (scale > 1) {
        int width = 0;
        int height = 0;
        ScaleTargetSize(kNative3dWidth, kNative3dHeight, scale, width,
                        height);
        if (EnsureTargets(width, height)) {
            RENDER_LOGI("resolucion interna: %dx (%dx%d)", g_internal_scale,
                        width, height);
        } else {
            RENDER_LOGE("resolucion interna %dx: FBO %dx%d incompleto",
                        g_internal_scale, width, height);
        }
    } else {
        RENDER_LOGI("resolucion interna: 1x (nativa)");
    }
}

int InternalResolutionScale() {
    return g_internal_scale;
}

bool ExecuteFrame() {
    const bool has_background = !g_backgrounds.empty();
    const bool has_models = !g_models.empty();

    GLint viewport[4] = {};
    glGetIntegerv(GL_VIEWPORT, viewport);
    if (viewport[2] <= 0 || viewport[3] <= 0) {
        ClearQueues();
        return false;
    }

    const DisplayLayout layout = ComputeDisplayLayout(
        static_cast<int>(viewport[2]), static_cast<int>(viewport[3]),
        g_aspect_mode);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    ClearBars(layout);

    if (!has_background && !has_models) {
        ClearQueues();
        return false;
    }

    static int last_width = -1;
    static int last_height = -1;
    static int last_mode = -1;
    const int mode_value = static_cast<int>(g_aspect_mode);
    if (last_width != layout.window_width ||
        last_height != layout.window_height || last_mode != mode_value) {
        last_width = layout.window_width;
        last_height = layout.window_height;
        last_mode = mode_value;
        RENDER_LOGI("layout %dx%d -> %d,%d %dx%d %s",
                    layout.window_width, layout.window_height, layout.x,
                    layout.y, layout.width, layout.height,
                    g_aspect_mode == AspectMode::Widescreen
                        ? "widescreen"
                        : (layout.pillarbox ? "pillarbox"
                                            : (layout.letterbox ? "letterbox"
                                                                : "4:3")));
    }

    // Fase 1: fondo 2D. Menus/HUD del juego siempre en 4:3, sin test de
    // profundidad. La proyeccion es la ortho PS2 0..640, 0..448.
    if (has_background) {
        Apply2DViewport(layout);
        ps2::gif::SetProjection(layout.ortho.m);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        for (const ps2::gif::DrawCommand &command : g_backgrounds) {
            ps2::gif::RenderCommand(command);
        }
    }
    glDepthMask(GL_TRUE);

    // Fases 2 y 3: modelos 3D. En widescreen se usa toda la ventana y se
    // ensancha la fila X de la proyeccion de VU1; en 4:3 se mantiene el
    // viewport del contenido.
    const bool wide = layout.widescreen;
    const float projection_scale =
        wide ? ComputeWidescreenScale(layout) : 1.0f;

    GLint model_viewport[4] = {
        viewport[0], viewport[1], viewport[2], viewport[3]};
    if (!wide) {
        model_viewport[0] = layout.x;
        model_viewport[1] = layout.y;
        model_viewport[2] = layout.width;
        model_viewport[3] = layout.height;
    }

    bool drew_models = false;
    if (has_models) {
        // En widescreen no se usa el depth map del juego: fue capturado en
        // 4:3 y estirarlo al ancho completo romperia la oclusion. Se dibuja
        // directo con el FOV ensanchado.
        const bool use_depth =
            !wide && g_has_depth_map && g_depth_map.valid;
        if (use_depth &&
            DrawModelsWithDepthMap(model_viewport, projection_scale)) {
            drew_models = true;
        } else if (g_internal_scale > 1 &&
                   DrawModelsToTarget(model_viewport, projection_scale)) {
            drew_models = true;
        } else {
            // Sin Z-buffer del juego, o si el FBO falla, se usa el depth
            // buffer normal, que Game_Render limpia al inicio del frame.
            DrawModelsDirect(model_viewport, projection_scale);
            drew_models = true;
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glDisable(GL_DEPTH_TEST);
    // Dejar la mascara a TRUE para que Game_Render pueda limpiar el depth
    // buffer del framebuffer por defecto al inicio del proximo frame.
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    static int log_count = 0;
    if (log_count < 8) {
        ++log_count;
        RENDER_LOGI("frame bg=%zu models=%zu depth=%s mode=%s scale=%.3f "
                    "internal=%dx",
                    g_backgrounds.size(), g_models.size(),
                    (g_has_depth_map && g_depth_map.valid) ? "si" : "no",
                    wide ? "wide" : "4:3", projection_scale,
                    g_internal_scale);
    }

    ClearQueues();
    return has_background || drew_models;
}

void Reset() {
    ClearQueues();
    g_depth_map = ps2::gif::DepthMap{};
    g_has_depth_map = false;
}

void Shutdown() {
    Reset();
    DestroyTargets();
    if (g_composite_vbo != 0) glDeleteBuffers(1, &g_composite_vbo);
    if (g_composite_vao != 0) glDeleteVertexArrays(1, &g_composite_vao);
    if (g_composite_program != 0) glDeleteProgram(g_composite_program);
    g_composite_vbo = g_composite_vao = g_composite_program = 0;
    Renderer().Shutdown();
}

}  // namespace ps2::render
