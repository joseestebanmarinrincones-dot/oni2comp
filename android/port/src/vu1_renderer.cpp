#include "vu1_renderer.hpp"

#include <GLES3/gl3.h>

#include <cstddef>
#include <cstdio>
#include <cstring>

#if defined(__ANDROID__)
#include <android/log.h>
#define VU1_TAG "OnimushaVu1"
#define VU1_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, VU1_TAG, __VA_ARGS__)
#else
#define VU1_LOGE(...) do { \
    std::fprintf(stderr, "[OnimushaVu1] "); \
    std::fprintf(stderr, __VA_ARGS__); \
    std::fputc('\n', stderr); \
} while (0)
#endif

namespace {

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
        VU1_LOGE("shader: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint link_program(const char *vertex, const char *fragment) {
    const GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex);
    const GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment);
    if (vs == 0 || fs == 0) {
        if (vs != 0) glDeleteShader(vs);
        if (fs != 0) glDeleteShader(fs);
        return 0;
    }
    const GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok == GL_FALSE) {
        char log[1024] = {};
        glGetProgramInfoLog(program, sizeof(log) - 1, nullptr, log);
        VU1_LOGE("link: %s", log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

}  // namespace

Vu1Renderer::~Vu1Renderer() {
    Shutdown();
}

bool Vu1Renderer::Initialize() {
    if (program_ != 0) return true;

    static constexpr char kVertex[] =
        "#version 300 es\n"
        "layout(location = 0) in vec4 a_position;\n"
        "layout(location = 1) in vec2 a_uv;\n"
        "layout(location = 2) in vec3 a_normal;\n"
        "layout(location = 3) in float a_fog;\n"
        "layout(location = 4) in vec4 a_color;\n"
        "layout(location = 5) in vec4 a_weights;\n"
        "layout(location = 6) in vec4 a_bones;\n"
        "uniform mat4 u_world;\n"
        "uniform mat4 u_view;\n"
        "uniform mat4 u_projection;\n"
        "uniform mat4 u_bones[32];\n"
        "uniform bool u_skinned;\n"
        "out vec2 v_uv;\n"
        "out vec3 v_normal;\n"
        "out vec4 v_color;\n"
        "out float v_fog;\n"
        "void main() {\n"
        "    vec4 position = a_position;\n"
        "    if (u_skinned) {\n"
        "        int b0 = clamp(int(a_bones.x + 0.5), 0, 31);\n"
        "        int b1 = clamp(int(a_bones.y + 0.5), 0, 31);\n"
        "        int b2 = clamp(int(a_bones.z + 0.5), 0, 31);\n"
        "        int b3 = clamp(int(a_bones.w + 0.5), 0, 31);\n"
        "        mat4 skin = u_bones[b0] * a_weights.x +\n"
        "                    u_bones[b1] * a_weights.y +\n"
        "                    u_bones[b2] * a_weights.z +\n"
        "                    u_bones[b3] * a_weights.w;\n"
        "        position = skin * position;\n"
        "    }\n"
        "    vec4 world = u_world * position;\n"
        "    v_normal = normalize(mat3(u_world) * a_normal);\n"
        "    v_uv = a_uv;\n"
        "    v_color = a_color;\n"
        "    v_fog = a_fog;\n"
        "    gl_Position = u_projection * u_view * world;\n"
        "}\n";

    static constexpr char kFragment[] =
        "#version 300 es\n"
        "precision mediump float;\n"
        "in vec2 v_uv;\n"
        "in vec3 v_normal;\n"
        "in vec4 v_color;\n"
        "in float v_fog;\n"
        "uniform sampler2D u_texture;\n"
        "uniform bool u_textured;\n"
        "uniform bool u_lit;\n"
        "uniform vec3 u_light_dir;\n"
        "uniform vec3 u_light_color;\n"
        "uniform vec3 u_ambient;\n"
        "uniform vec3 u_fog_color;\n"
        "uniform float u_alpha_ref;\n"
        "out vec4 fragColor;\n"
        "void main() {\n"
        "    vec4 base = u_textured ? texture(u_texture, v_uv) : vec4(1.0);\n"
        "    base *= v_color;\n"
        "    vec3 color = base.rgb;\n"
        "    if (u_lit) {\n"
        "        float ndl = max(dot(normalize(v_normal),\n"
        "                             normalize(-u_light_dir)), 0.0);\n"
        "        color = base.rgb * (u_ambient + u_light_color * ndl);\n"
        "    }\n"
        "    color = mix(color, u_fog_color, clamp(v_fog, 0.0, 1.0));\n"
        "    if (base.a < u_alpha_ref) discard;\n"
        "    fragColor = vec4(color, base.a);\n"
        "}\n";

    program_ = link_program(kVertex, kFragment);
    if (program_ == 0) return false;

    u_world_ = glGetUniformLocation(program_, "u_world");
    u_view_ = glGetUniformLocation(program_, "u_view");
    u_projection_ = glGetUniformLocation(program_, "u_projection");
    u_bones_ = glGetUniformLocation(program_, "u_bones[0]");
    u_skinned_ = glGetUniformLocation(program_, "u_skinned");
    u_lit_ = glGetUniformLocation(program_, "u_lit");
    u_light_dir_ = glGetUniformLocation(program_, "u_light_dir");
    u_light_color_ = glGetUniformLocation(program_, "u_light_color");
    u_ambient_ = glGetUniformLocation(program_, "u_ambient");
    u_texture_ = glGetUniformLocation(program_, "u_texture");
    u_textured_ = glGetUniformLocation(program_, "u_textured");
    u_alpha_ref_ = glGetUniformLocation(program_, "u_alpha_ref");
    u_fog_color_ = glGetUniformLocation(program_, "u_fog_color");

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void *>(
                              offsetof(Vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void *>(offsetof(Vertex, uv)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void *>(offsetof(Vertex, normal)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void *>(offsetof(Vertex, fog)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex),
                          reinterpret_cast<void *>(offsetof(Vertex, color)));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex),
                          reinterpret_cast<void *>(offsetof(Vertex, weights)));
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void *>(offsetof(Vertex, bones)));
    glBindVertexArray(0);

    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return true;
}

void Vu1Renderer::Shutdown() {
    if (ebo_ != 0) glDeleteBuffers(1, &ebo_);
    if (vbo_ != 0) glDeleteBuffers(1, &vbo_);
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
    if (program_ != 0) glDeleteProgram(program_);
    ebo_ = vbo_ = vao_ = program_ = 0;
}

void Vu1Renderer::set_matrix(GLint location, const Mat4 &matrix) const {
    if (location >= 0) {
        // Las matrices de VU1 son row-major; GLSL espera column-major.
        glUniformMatrix4fv(location, 1, GL_TRUE, matrix.m);
    }
}

void Vu1Renderer::Render(const Packet &packet, float projection_x_scale) {
    if (program_ == 0 || packet.vertices.empty()) return;

    glUseProgram(program_);
    set_matrix(u_world_, packet.world);
    set_matrix(u_view_, packet.view);

    Mat4 projection = packet.projection;
    if (projection_x_scale > 0.0f && projection_x_scale != 1.0f) {
        // Fila X en row-major; ensancha el FOV horizontal sin tocar Y/Z.
        for (int column = 0; column < 4; ++column) {
            projection.m[column] *= projection_x_scale;
        }
    }
    set_matrix(u_projection_, projection);

    const bool skinned = packet.skinned && !packet.bones.empty();
    glUniform1i(u_skinned_, skinned ? 1 : 0);
    if (skinned) {
        const GLsizei count = static_cast<GLsizei>(
            packet.bones.size() < static_cast<std::size_t>(kMaxBones)
                ? packet.bones.size()
                : static_cast<std::size_t>(kMaxBones));
        if (u_bones_ >= 0) {
            glUniformMatrix4fv(u_bones_, count, GL_TRUE,
                               packet.bones[0].m);
        }
    }

    glUniform1i(u_lit_, packet.lit ? 1 : 0);
    glUniform3f(u_light_dir_, packet.light_dir.v[0], packet.light_dir.v[1],
                packet.light_dir.v[2]);
    glUniform3f(u_light_color_, packet.light_color.v[0],
                packet.light_color.v[1], packet.light_color.v[2]);
    glUniform3f(u_ambient_, packet.ambient.v[0], packet.ambient.v[1],
                packet.ambient.v[2]);
    glUniform3f(u_fog_color_, packet.fog_color.v[0], packet.fog_color.v[1],
                packet.fog_color.v[2]);
    glUniform1f(u_alpha_ref_, packet.alpha_ref);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, packet.textured ? packet.texture : 0);
    glUniform1i(u_texture_, 0);
    glUniform1i(u_textured_, packet.textured ? 1 : 0);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(packet.vertices.size() *
                                         sizeof(Vertex)),
                 packet.vertices.data(), GL_DYNAMIC_DRAW);

    const GLenum mode =
        packet.triangle_strip ? GL_TRIANGLE_STRIP : GL_TRIANGLES;

    if (!packet.indices.empty()) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(packet.indices.size() *
                                             sizeof(std::uint16_t)),
                     packet.indices.data(), GL_DYNAMIC_DRAW);
        glDrawElements(mode,
                       static_cast<GLsizei>(packet.indices.size()),
                       GL_UNSIGNED_SHORT, nullptr);
    } else {
        glDrawArrays(mode, 0,
                     static_cast<GLsizei>(packet.vertices.size()));
    }

    glBindVertexArray(0);
    glUseProgram(0);
}
