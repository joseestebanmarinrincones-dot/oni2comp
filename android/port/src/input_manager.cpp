#include "input_manager.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>

#if defined(PS2_INPUT_USE_GL)
#include <GLES3/gl3.h>
#endif

#if defined(__ANDROID__)
#include <android/log.h>
#define INPUT_TAG "OnimushaInput"
#define INPUT_LOGI(...) \
    __android_log_print(ANDROID_LOG_INFO, INPUT_TAG, __VA_ARGS__)
#define INPUT_LOGE(...) \
    __android_log_print(ANDROID_LOG_ERROR, INPUT_TAG, __VA_ARGS__)
#else
#define INPUT_LOGI(...)                       \
    do {                                      \
        std::fprintf(stdout, "[Input] ");     \
        std::fprintf(stdout, __VA_ARGS__);    \
        std::fputc('\n', stdout);             \
        std::fflush(stdout);                  \
    } while (0)
#define INPUT_LOGE(...)                       \
    do {                                      \
        std::fprintf(stderr, "[Input] ");     \
        std::fprintf(stderr, __VA_ARGS__);    \
        std::fputc('\n', stderr);             \
        std::fflush(stderr);                  \
    } while (0)
#endif

namespace port {
namespace {

constexpr float kStickDeadzone = 0.15f;
constexpr float kDpadThreshold = 0.55f;
constexpr int kCircleSegments = 40;

float axis_norm(Sint16 value) {
    return std::clamp(static_cast<float>(value) / 32767.0f, -1.0f, 1.0f);
}

float apply_deadzone(float value) {
    const float magnitude = std::fabs(value);
    if (magnitude <= kStickDeadzone) return 0.0f;
    return std::copysign((magnitude - kStickDeadzone) /
                             (1.0f - kStickDeadzone),
                         value);
}

void add_vertex(std::vector<float> &out, float x, float y,
                const float color[4]) {
    out.push_back(x);
    out.push_back(y);
    out.push_back(color[0]);
    out.push_back(color[1]);
    out.push_back(color[2]);
    out.push_back(color[3]);
}

void add_triangle(std::vector<float> &out, float x0, float y0, float x1,
                  float y1, float x2, float y2, const float color[4]) {
    add_vertex(out, x0, y0, color);
    add_vertex(out, x1, y1, color);
    add_vertex(out, x2, y2, color);
}

void add_quad(std::vector<float> &out, float x, float y, float width,
              float height, const float color[4]) {
    add_triangle(out, x, y, x + width, y, x + width, y + height, color);
    add_triangle(out, x, y, x + width, y + height, x, y + height, color);
}

void add_line(std::vector<float> &out, float x0, float y0, float x1,
              float y1, float thickness, const float color[4]) {
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length < 0.001f) return;
    const float half = std::max(0.75f, thickness * 0.5f);
    const float nx = -dy / length * half;
    const float ny = dx / length * half;
    add_triangle(out, x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx,
                 y1 - ny, color);
    add_triangle(out, x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx,
                 y0 - ny, color);
}

void add_circle(std::vector<float> &out, float cx, float cy, float radius,
                const float color[4]) {
    if (radius <= 0.0f) return;
    for (int i = 0; i < kCircleSegments; ++i) {
        const float a0 = static_cast<float>(i) /
                         static_cast<float>(kCircleSegments) * 6.2831853f;
        const float a1 = static_cast<float>(i + 1) /
                         static_cast<float>(kCircleSegments) * 6.2831853f;
        add_triangle(out, cx, cy, cx + std::cos(a0) * radius,
                     cy + std::sin(a0) * radius,
                     cx + std::cos(a1) * radius,
                     cy + std::sin(a1) * radius, color);
    }
}

void add_ring(std::vector<float> &out, float cx, float cy, float radius,
              float thickness, const float color[4]) {
    const float inner = std::max(0.0f, radius - thickness * 0.5f);
    const float outer = radius + thickness * 0.5f;
    for (int i = 0; i < kCircleSegments; ++i) {
        const float a0 = static_cast<float>(i) /
                         static_cast<float>(kCircleSegments) * 6.2831853f;
        const float a1 = static_cast<float>(i + 1) /
                         static_cast<float>(kCircleSegments) * 6.2831853f;
        const float c0 = std::cos(a0), s0 = std::sin(a0);
        const float c1 = std::cos(a1), s1 = std::sin(a1);
        add_triangle(out, cx + c0 * inner, cy + s0 * inner,
                     cx + c1 * inner, cy + s1 * inner,
                     cx + c1 * outer, cy + s1 * outer, color);
        add_triangle(out, cx + c0 * inner, cy + s0 * inner,
                     cx + c1 * outer, cy + s1 * outer,
                     cx + c0 * outer, cy + s0 * outer, color);
    }
}

void add_rect_outline(std::vector<float> &out, float left, float top,
                      float right, float bottom, float thickness,
                      const float color[4]) {
    add_line(out, left, top, right, top, thickness, color);
    add_line(out, right, top, right, bottom, thickness, color);
    add_line(out, right, bottom, left, bottom, thickness, color);
    add_line(out, left, bottom, left, top, thickness, color);
}

struct Glyph {
    char letter;
    std::uint8_t rows[5];
};

constexpr Glyph kGlyphs[] = {
    {'A', {0x0E, 0x11, 0x1F, 0x11, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'R', {0x1E, 0x11, 0x1E, 0x14, 0x13}},
    {'S', {0x0F, 0x10, 0x0E, 0x01, 0x1E}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x0E}},
};

const Glyph *find_glyph(char letter) {
    for (const Glyph &glyph : kGlyphs) {
        if (glyph.letter == letter) return &glyph;
    }
    return nullptr;
}

#if defined(PS2_INPUT_USE_GL)

GLuint compile_shader(GLenum type, const char *source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == 0) {
        char log[1024]{};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        INPUT_LOGE("HUD shader: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

#endif

}  // namespace

InputManager::~InputManager() {
    Shutdown();
}

InputManager &GlobalInput() {
    static InputManager instance;
    return instance;
}

bool InputManager::Initialize() {
    if (initialized_) return true;

    if (SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
            INPUT_LOGE("SDL_InitSubSystem(GameController): %s",
                       SDL_GetError());
        }
    }
    SDL_GameControllerEventState(SDL_ENABLE);

    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i) != 0) OpenController(i);
    }

    initialized_ = true;
    if (controllers_.empty()) {
        INPUT_LOGI("Sin mando fisico: HUD tactil activo");
    } else {
        INPUT_LOGI("%zu mando(s) fisico(s) detectado(s)",
                   controllers_.size());
    }
    return true;
}

void InputManager::Shutdown() {
    CloseAllControllers();
    ReleaseAll();
    ShutdownGL();
    initialized_ = false;
}

void InputManager::OpenController(int joystick_index) {
    if (SDL_IsGameController(joystick_index) == 0) return;

    const SDL_JoystickID instance =
        SDL_JoystickGetDeviceInstanceID(joystick_index);
    for (const ControllerSlot &slot : controllers_) {
        if (slot.instance == instance && slot.handle != nullptr) return;
    }

    SDL_GameController *handle =
        SDL_GameControllerOpen(joystick_index);
    if (handle == nullptr) {
        INPUT_LOGE("SDL_GameControllerOpen(%d): %s", joystick_index,
                   SDL_GetError());
        return;
    }

    ControllerSlot slot;
    slot.handle = handle;
    slot.instance = SDL_JoystickInstanceID(
        SDL_GameControllerGetJoystick(handle));
    controllers_.push_back(slot);
    ReleaseAll();

    const char *name = SDL_GameControllerName(handle);
    INPUT_LOGI("Mando conectado: %s (instance=%d)", name ? name : "?",
               static_cast<int>(slot.instance));
}

void InputManager::CloseController(SDL_JoystickID instance) {
    for (auto it = controllers_.begin(); it != controllers_.end(); ++it) {
        if (it->instance != instance) continue;
        if (it->handle != nullptr) SDL_GameControllerClose(it->handle);
        controllers_.erase(it);
        ReleaseAll();
        INPUT_LOGI("Mando desconectado; HUD tactil %s",
                   controllers_.empty() ? "activo" : "oculto");
        return;
    }
}

void InputManager::CloseAllControllers() {
    for (ControllerSlot &slot : controllers_) {
        if (slot.handle != nullptr) SDL_GameControllerClose(slot.handle);
    }
    controllers_.clear();
}

bool InputManager::HandleEvent(const SDL_Event &event) {
    switch (event.type) {
    case SDL_CONTROLLERDEVICEADDED:
        OpenController(event.cdevice.which);
        return false;
    case SDL_CONTROLLERDEVICEREMOVED:
        CloseController(event.cdevice.which);
        return false;
    case SDL_CONTROLLERDEVICEREMAPPED:
        return false;
    case SDL_FINGERDOWN:
        return TouchDown(static_cast<std::uint64_t>(event.tfinger.fingerId),
                         event.tfinger.x, event.tfinger.y);
    case SDL_FINGERMOTION:
        return TouchMove(static_cast<std::uint64_t>(event.tfinger.fingerId),
                         event.tfinger.x, event.tfinger.y);
    case SDL_FINGERUP:
        return TouchUp(static_cast<std::uint64_t>(event.tfinger.fingerId));
    default:
        return false;
    }
}

void InputManager::Resize(int width, int height) {
    width_ = std::max(0, width);
    height_ = std::max(0, height);
    RecomputeLayout();
}

void InputManager::RecomputeLayout() {
    if (width_ <= 0 || height_ <= 0) return;

    const float unit = static_cast<float>(std::min(width_, height_));
    const float margin = unit * 0.035f;

    left_r_ = unit * 0.115f;
    left_cx_ = margin + left_r_ * 1.25f;
    left_cy_ = static_cast<float>(height_) - margin - left_r_ * 1.15f;

    const float face_r = unit * 0.050f;
    const float offset = unit * 0.112f;
    const float right_cx =
        static_cast<float>(width_) - margin - unit * 0.170f;
    const float right_cy = static_cast<float>(height_) * 0.60f;

    hud_buttons_[0] = {PadButton::Triangle, false, right_cx,
                       right_cy - offset, face_r, 0, 0, 0, 0};
    hud_buttons_[1] = {PadButton::Circle, false, right_cx + offset,
                       right_cy, face_r, 0, 0, 0, 0};
    hud_buttons_[2] = {PadButton::Cross, false, right_cx,
                       right_cy + offset, face_r, 0, 0, 0, 0};
    hud_buttons_[3] = {PadButton::Square, false, right_cx - offset,
                       right_cy, face_r, 0, 0, 0, 0};

    const float shoulder_w = unit * 0.095f;
    const float shoulder_h = unit * 0.050f;
    const float shoulder_y = right_cy - offset * 1.85f;
    hud_buttons_[4] = {PadButton::L1, true, right_cx - offset,
                       shoulder_y, 0, right_cx - offset - shoulder_w * 0.5f,
                       shoulder_y - shoulder_h * 0.5f,
                       right_cx - offset + shoulder_w * 0.5f,
                       shoulder_y + shoulder_h * 0.5f};
    hud_buttons_[5] = {PadButton::R1, true, right_cx + offset,
                       shoulder_y, 0, right_cx + offset - shoulder_w * 0.5f,
                       shoulder_y - shoulder_h * 0.5f,
                       right_cx + offset + shoulder_w * 0.5f,
                       shoulder_y + shoulder_h * 0.5f};

    const float start_w = unit * 0.160f;
    const float start_h = unit * 0.050f;
    const float start_y = right_cy - offset * 2.55f;
    hud_buttons_[6] = {PadButton::Start, true, right_cx, start_y, 0,
                       right_cx - start_w * 0.5f,
                       start_y - start_h * 0.5f,
                       right_cx + start_w * 0.5f,
                       start_y + start_h * 0.5f};
}

int InputManager::HitTest(float px, float py) const {
    for (std::size_t i = 0; i < hud_buttons_.size(); ++i) {
        const HudButton &button = hud_buttons_[i];
        if (button.rectangle) {
            if (px >= button.left && px <= button.right &&
                py >= button.top && py <= button.bottom) {
                return static_cast<int>(i);
            }
        } else {
            const float dx = px - button.cx;
            const float dy = py - button.cy;
            const float radius = button.radius * 1.10f;
            if (dx * dx + dy * dy <= radius * radius) {
                return static_cast<int>(i);
            }
        }
    }

    const float dx = px - left_cx_;
    const float dy = py - left_cy_;
    const float radius = left_r_ * 1.45f;
    if (dx * dx + dy * dy <= radius * radius) return -2;
    return -1;
}

int InputManager::FindTouch(std::uint64_t id) const {
    for (std::size_t i = 0; i < touches_.size(); ++i) {
        if (touches_[i].active && touches_[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int InputManager::FreeTouch() const {
    for (std::size_t i = 0; i < touches_.size(); ++i) {
        if (!touches_[i].active) return static_cast<int>(i);
    }
    return -1;
}

void InputManager::RecomputeTouchButtons() {
    touch_buttons_ = 0;
    for (const TouchSlot &slot : touches_) {
        if (slot.active && slot.kind == TouchKind::Button) {
            touch_buttons_ = static_cast<std::uint16_t>(
                touch_buttons_ | mask(slot.button));
        }
    }
}

void InputManager::UpdateStickFromPosition(float px, float py) {
    float dx = (px - left_cx_) / left_r_;
    float dy = (py - left_cy_) / left_r_;
    float magnitude = std::sqrt(dx * dx + dy * dy);

    if (magnitude > 1.0f) {
        dx /= magnitude;
        dy /= magnitude;
        magnitude = 1.0f;
    }
    if (magnitude < kStickDeadzone) {
        stick_x_ = 0.0f;
        stick_y_ = 0.0f;
        return;
    }

    const float scaled =
        (magnitude - kStickDeadzone) / (1.0f - kStickDeadzone);
    stick_x_ = dx / magnitude * scaled;
    stick_y_ = dy / magnitude * scaled;
}

bool InputManager::TouchDown(std::uint64_t id, float x, float y) {
    if (HasController() || width_ <= 0 || height_ <= 0) return false;

    const float px = std::clamp(x, 0.0f, 1.0f) * width_;
    const float py = std::clamp(y, 0.0f, 1.0f) * height_;
    const int hit = HitTest(px, py);
    if (hit == -1) return false;

    int slot_index = FindTouch(id);
    if (slot_index < 0) slot_index = FreeTouch();
    if (slot_index < 0) return false;

    TouchSlot &slot = touches_[static_cast<std::size_t>(slot_index)];
    if (slot.active && slot.kind == TouchKind::Stick) {
        stick_active_ = false;
        stick_x_ = 0.0f;
        stick_y_ = 0.0f;
    }
    slot = {};
    slot.active = true;
    slot.id = id;

    if (hit == -2) {
        slot.kind = TouchKind::Stick;
        stick_active_ = true;
        UpdateStickFromPosition(px, py);
    } else {
        slot.kind = TouchKind::Button;
        slot.button = hud_buttons_[static_cast<std::size_t>(hit)].button;
        button_latch_ = static_cast<std::uint16_t>(
            button_latch_ | mask(slot.button));
    }
    RecomputeTouchButtons();
    return true;
}

bool InputManager::TouchMove(std::uint64_t id, float x, float y) {
    if (HasController() || width_ <= 0 || height_ <= 0) return false;
    const int slot_index = FindTouch(id);
    if (slot_index < 0) return false;

    TouchSlot &slot = touches_[static_cast<std::size_t>(slot_index)];
    if (slot.kind == TouchKind::Stick) {
        const float px = std::clamp(x, 0.0f, 1.0f) * width_;
        const float py = std::clamp(y, 0.0f, 1.0f) * height_;
        UpdateStickFromPosition(px, py);
    }
    return true;
}

bool InputManager::TouchUp(std::uint64_t id) {
    const int slot_index = FindTouch(id);
    if (slot_index < 0) return false;

    TouchSlot &slot = touches_[static_cast<std::size_t>(slot_index)];
    if (slot.kind == TouchKind::Stick) {
        stick_active_ = false;
        stick_x_ = 0.0f;
        stick_y_ = 0.0f;
    }
    slot = {};
    RecomputeTouchButtons();
    return true;
}

void InputManager::ReleaseAll() {
    touches_.fill(TouchSlot{});
    touch_buttons_ = 0;
    button_latch_ = 0;
    stick_active_ = false;
    stick_x_ = 0.0f;
    stick_y_ = 0.0f;
}

void InputManager::Update() {
    pad_.clear();

    if (!controllers_.empty()) {
        SDL_GameControllerUpdate();
        button_latch_ = 0;
        touch_buttons_ = 0;
        for (const ControllerSlot &slot : controllers_) {
            SDL_GameController *controller = slot.handle;
            if (controller == nullptr) continue;

            const auto pressed = [controller](SDL_GameControllerButton b) {
                return SDL_GameControllerGetButton(controller, b) != 0;
            };

            if (pressed(SDL_CONTROLLER_BUTTON_A)) pad_.set(PadButton::Cross);
            if (pressed(SDL_CONTROLLER_BUTTON_B)) pad_.set(PadButton::Circle);
            if (pressed(SDL_CONTROLLER_BUTTON_X)) pad_.set(PadButton::Square);
            if (pressed(SDL_CONTROLLER_BUTTON_Y)) pad_.set(PadButton::Triangle);
            if (pressed(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) {
                pad_.set(PadButton::L1);
            }
            if (pressed(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) {
                pad_.set(PadButton::R1);
            }
            if (pressed(SDL_CONTROLLER_BUTTON_START)) {
                pad_.set(PadButton::Start);
            }
            if (pressed(SDL_CONTROLLER_BUTTON_BACK)) {
                pad_.set(PadButton::Select);
            }
            if (pressed(SDL_CONTROLLER_BUTTON_LEFTSTICK)) {
                pad_.set(PadButton::L3);
            }
            if (pressed(SDL_CONTROLLER_BUTTON_RIGHTSTICK)) {
                pad_.set(PadButton::R3);
            }
            if (SDL_GameControllerGetAxis(
                    controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 12000) {
                pad_.set(PadButton::L2);
            }
            if (SDL_GameControllerGetAxis(
                    controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 12000) {
                pad_.set(PadButton::R2);
            }
            if (pressed(SDL_CONTROLLER_BUTTON_DPAD_UP)) {
                pad_.set(PadButton::Up);
            }
            if (pressed(SDL_CONTROLLER_BUTTON_DPAD_DOWN)) {
                pad_.set(PadButton::Down);
            }
            if (pressed(SDL_CONTROLLER_BUTTON_DPAD_LEFT)) {
                pad_.set(PadButton::Left);
            }
            if (pressed(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) {
                pad_.set(PadButton::Right);
            }

            const float lx = apply_deadzone(
                axis_norm(SDL_GameControllerGetAxis(
                    controller, SDL_CONTROLLER_AXIS_LEFTX)));
            const float ly = apply_deadzone(
                axis_norm(SDL_GameControllerGetAxis(
                    controller, SDL_CONTROLLER_AXIS_LEFTY)));
            const float rx = apply_deadzone(
                axis_norm(SDL_GameControllerGetAxis(
                    controller, SDL_CONTROLLER_AXIS_RIGHTX)));
            const float ry = apply_deadzone(
                axis_norm(SDL_GameControllerGetAxis(
                    controller, SDL_CONTROLLER_AXIS_RIGHTY)));

            if (std::fabs(lx) > std::fabs(pad_.left_x)) pad_.left_x = lx;
            if (std::fabs(ly) > std::fabs(pad_.left_y)) pad_.left_y = ly;
            if (std::fabs(rx) > std::fabs(pad_.right_x)) pad_.right_x = rx;
            if (std::fabs(ry) > std::fabs(pad_.right_y)) pad_.right_y = ry;
        }
    } else {
        pad_.buttons = static_cast<std::uint16_t>(touch_buttons_ |
                                                  button_latch_);
        button_latch_ = 0;
        pad_.left_x = stick_x_;
        pad_.left_y = stick_y_;

        if (stick_x_ > kDpadThreshold) pad_.set(PadButton::Right);
        if (stick_x_ < -kDpadThreshold) pad_.set(PadButton::Left);
        if (stick_y_ > kDpadThreshold) pad_.set(PadButton::Down);
        if (stick_y_ < -kDpadThreshold) pad_.set(PadButton::Up);
    }
}

bool InputManager::HasController() const {
    return !controllers_.empty();
}

int InputManager::ControllerCount() const {
    return static_cast<int>(controllers_.size());
}

bool InputManager::HudVisible() const {
    return controllers_.empty();
}

const PadState &InputManager::Pad() const {
    return pad_;
}

bool InputManager::ButtonCenter(PadButton button, float &x,
                                float &y) const {
    if (width_ <= 0 || height_ <= 0) return false;
    for (const HudButton &entry : hud_buttons_) {
        if (entry.button != button) continue;
        x = entry.cx / static_cast<float>(width_);
        y = entry.cy / static_cast<float>(height_);
        return true;
    }
    return false;
}

bool InputManager::StickCenter(float &x, float &y) const {
    if (width_ <= 0 || height_ <= 0) return false;
    x = left_cx_ / static_cast<float>(width_);
    y = left_cy_ / static_cast<float>(height_);
    return true;
}

bool InputManager::EnsureGL() {
#if defined(PS2_INPUT_USE_GL)
    if (gl_ready_) return true;
    if (gl_failed_) return false;

    const char *vertex_source = R"(#version 300 es
        layout(location = 0) in vec2 aPos;
        layout(location = 1) in vec4 aColor;
        uniform vec2 uViewport;
        out vec4 vColor;
        void main() {
            vec2 ndc = vec2(
                (aPos.x / uViewport.x) * 2.0 - 1.0,
                1.0 - (aPos.y / uViewport.y) * 2.0);
            gl_Position = vec4(ndc, 0.0, 1.0);
            vColor = aColor;
        }
    )";

    const char *fragment_source = R"(#version 300 es
        precision mediump float;
        in vec4 vColor;
        out vec4 outColor;
        void main() {
            outColor = vColor;
        }
    )";

    const GLuint vertex = compile_shader(GL_VERTEX_SHADER, vertex_source);
    const GLuint fragment = compile_shader(GL_FRAGMENT_SHADER,
                                           fragment_source);
    if (vertex == 0 || fragment == 0) {
        if (vertex != 0) glDeleteShader(vertex);
        if (fragment != 0) glDeleteShader(fragment);
        gl_failed_ = true;
        return false;
    }

    program_ = glCreateProgram();
    glAttachShader(program_, vertex);
    glAttachShader(program_, fragment);
    glLinkProgram(program_);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint linked = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    if (linked == 0) {
        char log[1024]{};
        glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
        INPUT_LOGE("HUD program: %s", log);
        glDeleteProgram(program_);
        program_ = 0;
        gl_failed_ = true;
        return false;
    }

    viewport_loc_ = glGetUniformLocation(program_, "uViewport");
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(0);
    gl_ready_ = true;
    INPUT_LOGI("HUD GLES 3.0 inicializado");
    return true;
#else
    return false;
#endif
}

void InputManager::ShutdownGL() {
#if defined(PS2_INPUT_USE_GL)
    if (vbo_ != 0) glDeleteBuffers(1, &vbo_);
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
    if (program_ != 0) glDeleteProgram(program_);
    vbo_ = 0;
    vao_ = 0;
    program_ = 0;
    gl_ready_ = false;
    gl_failed_ = false;
#endif
}

void InputManager::DrawStick() {
    float base[4] = {1.0f, 1.0f, 1.0f, 0.06f};
    add_circle(vertices_, left_cx_, left_cy_, left_r_, base);

    float outline[4] = {1.0f, 1.0f, 1.0f, 0.38f};
    add_ring(vertices_, left_cx_, left_cy_, left_r_,
             std::max(1.5f, left_r_ * 0.025f), outline);

    const float knob_x = left_cx_ + stick_x_ * left_r_ * 0.72f;
    const float knob_y = left_cy_ + stick_y_ * left_r_ * 0.72f;
    const float knob_r = left_r_ * 0.38f;

    float knob[4] = {1.0f, 1.0f, 1.0f, stick_active_ ? 0.32f : 0.18f};
    add_circle(vertices_, knob_x, knob_y, knob_r, knob);
    float knob_outline[4] = {1.0f, 1.0f, 1.0f, 0.72f};
    add_ring(vertices_, knob_x, knob_y, knob_r,
             std::max(1.5f, left_r_ * 0.018f), knob_outline);
}

void InputManager::DrawFaceSymbol(PadButton button, float cx, float cy,
                                  float radius) {
    float white[4] = {1.0f, 1.0f, 1.0f, 0.92f};
    const float thickness = std::max(2.0f, radius * 0.10f);
    const float r = radius * 0.42f;

    switch (button) {
    case PadButton::Triangle:
        add_line(vertices_, cx, cy - r, cx - r, cy + r * 0.8f, thickness,
                 white);
        add_line(vertices_, cx - r, cy + r * 0.8f, cx + r, cy + r * 0.8f,
                 thickness, white);
        add_line(vertices_, cx + r, cy + r * 0.8f, cx, cy - r, thickness,
                 white);
        break;
    case PadButton::Circle:
        add_ring(vertices_, cx, cy, r, thickness, white);
        break;
    case PadButton::Cross:
        add_line(vertices_, cx - r, cy - r, cx + r, cy + r, thickness,
                 white);
        add_line(vertices_, cx + r, cy - r, cx - r, cy + r, thickness,
                 white);
        break;
    case PadButton::Square:
        add_rect_outline(vertices_, cx - r, cy - r, cx + r, cy + r,
                         thickness, white);
        break;
    default:
        break;
    }
}

void InputManager::DrawText(const char *text, float cx, float cy,
                            float pixel, const float color[4]) {
    if (text == nullptr || *text == '\0' || pixel <= 0.0f) return;
    const int length = static_cast<int>(std::strlen(text));
    const float advance = pixel * 6.0f;
    const float width = static_cast<float>(length) * advance - pixel;
    float x = cx - width * 0.5f;
    const float y = cy - pixel * 2.5f;

    for (int i = 0; i < length; ++i) {
        const Glyph *glyph = find_glyph(text[i]);
        if (glyph != nullptr) {
            for (int row = 0; row < 5; ++row) {
                for (int col = 0; col < 5; ++col) {
                    if ((glyph->rows[row] & (1u << (4 - col))) == 0) {
                        continue;
                    }
                    add_quad(vertices_, x + static_cast<float>(col) * pixel,
                             y + static_cast<float>(row) * pixel,
                             pixel * 0.86f, pixel * 0.86f, color);
                }
            }
        }
        x += advance;
    }
}

void InputManager::DrawButton(const HudButton &button) {
    const bool down = pad_.pressed(button.button);
    float outline[4] = {1.0f, 1.0f, 1.0f, down ? 0.95f : 0.55f};
    const float outline_width = std::max(1.5f, button.radius * 0.06f);

    if (button.rectangle) {
        float fill[4] = {1.0f, 1.0f, 1.0f, down ? 0.30f : 0.10f};
        add_quad(vertices_, button.left, button.top,
                 button.right - button.left, button.bottom - button.top,
                 fill);
        add_rect_outline(vertices_, button.left, button.top, button.right,
                         button.bottom, std::max(1.5f, outline_width),
                         outline);

        const char *label = button.button == PadButton::Start ? "START"
                            : button.button == PadButton::L1 ? "L1"
                                                             : "R1";
        const float box_w = button.right - button.left;
        const float box_h = button.bottom - button.top;
        const float pixel = std::min(box_h * 0.15f,
                                     box_w / (6.0f * static_cast<float>(
                                                        std::strlen(label))));
        float text[4] = {1.0f, 1.0f, 1.0f, 0.92f};
        DrawText(label, button.cx, button.cy, pixel, text);
        return;
    }

    float color[3] = {0.85f, 0.85f, 0.85f};
    switch (button.button) {
    case PadButton::Triangle:
        color[0] = 0.20f;
        color[1] = 0.75f;
        color[2] = 0.40f;
        break;
    case PadButton::Circle:
        color[0] = 0.90f;
        color[1] = 0.30f;
        color[2] = 0.25f;
        break;
    case PadButton::Cross:
        color[0] = 0.25f;
        color[1] = 0.55f;
        color[2] = 0.95f;
        break;
    case PadButton::Square:
        color[0] = 0.90f;
        color[1] = 0.30f;
        color[2] = 0.60f;
        break;
    default:
        break;
    }

    float fill[4] = {color[0], color[1], color[2], down ? 0.48f : 0.22f};
    add_circle(vertices_, button.cx, button.cy, button.radius, fill);
    add_ring(vertices_, button.cx, button.cy, button.radius,
             std::max(1.5f, button.radius * 0.06f), outline);
    DrawFaceSymbol(button.button, button.cx, button.cy, button.radius);
}

void InputManager::DrawHUD() {
#if defined(PS2_INPUT_USE_GL)
    if (!HudVisible() || width_ <= 0 || height_ <= 0) return;
    if (!EnsureGL()) return;
    if (viewport_loc_ < 0) return;

    vertices_.clear();
    DrawStick();
    for (const HudButton &button : hud_buttons_) DrawButton(button);
    if (vertices_.empty()) return;

    GLboolean depth_test = glIsEnabled(GL_DEPTH_TEST);
    GLboolean cull_face = glIsEnabled(GL_CULL_FACE);
    GLboolean blend = glIsEnabled(GL_BLEND);
    GLboolean depth_mask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);

    GLint old_program = 0;
    GLint old_vao = 0;
    GLint old_vbo = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &old_program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &old_vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &old_vbo);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    glUseProgram(program_);
    glUniform2f(viewport_loc_, static_cast<float>(width_),
                static_cast<float>(height_));
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices_.size() *
                                         sizeof(float)),
                 vertices_.data(), GL_STREAM_DRAW);

    constexpr GLsizei stride = static_cast<GLsizei>(6 * sizeof(float));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void *>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1, 4, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<const void *>(2 * sizeof(float)));

    glDrawArrays(GL_TRIANGLES, 0,
                 static_cast<GLsizei>(vertices_.size() / 6));

    glBindVertexArray(static_cast<GLuint>(old_vao));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(old_vbo));
    glUseProgram(static_cast<GLuint>(old_program));
    glDepthMask(depth_mask);
    if (depth_test) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (cull_face) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
#else
    (void)0;
#endif
}

}  // namespace port
