#pragma once

// Sistema de entrada del port:
//   - Detecta mandos SDL_GameController por hotplug y los mapea a DualShock 2.
//   - Si no hay mando, muestra un HUD tactil semitransparente con stick
//     analogico a la izquierda y botones PS2 a la derecha.
//   - Multi-touch real: cada SDL_FingerID puede sostener el stick y un boton
//     a la vez (mover + absorber almas).
//
// El HUD usa OpenGL ES 3.0 (VAO/VBO + shader). En builds de PC sin GL,
// DrawHUD() es un no-op para poder probar la logica de entrada.

#include "ps2_pad.hpp"

#include <SDL.h>

#include <array>
#include <cstdint>
#include <vector>

namespace port {

class InputManager {
public:
    InputManager() = default;
    ~InputManager();

    InputManager(const InputManager &) = delete;
    InputManager &operator=(const InputManager &) = delete;

    bool Initialize();
    void Shutdown();

    // Procesa un evento SDL. Devuelve true si un toque fue consumido por el
    // HUD y el juego no debe recibirlo.
    bool HandleEvent(const SDL_Event &event);

    // Entrada tactil normalizada (0..1). Se usan para SDL_FINGER* y para el
    // raton del emulador/escritorio.
    bool TouchDown(std::uint64_t id, float x, float y);
    bool TouchMove(std::uint64_t id, float x, float y);
    bool TouchUp(std::uint64_t id);
    void ReleaseAll();

    // Reconstruye PadState desde mandos fisicos o desde el HUD.
    void Update();

    // Tamano del drawable para layout y dibujo.
    void Resize(int width, int height);
    void DrawHUD();

    bool HasController() const;
    int ControllerCount() const;
    bool HudVisible() const;
    const PadState &Pad() const;

    // Utilidades de layout/pruebas: devuelven coordenadas normalizadas.
    bool ButtonCenter(PadButton button, float &x, float &y) const;
    bool StickCenter(float &x, float &y) const;

private:
    struct ControllerSlot {
        SDL_GameController *handle = nullptr;
        SDL_JoystickID instance = -1;
    };

    enum class TouchKind : std::uint8_t {
        None,
        Stick,
        Button,
    };

    struct TouchSlot {
        bool active = false;
        std::uint64_t id = 0;
        TouchKind kind = TouchKind::None;
        PadButton button = PadButton::Cross;
    };

    struct HudButton {
        PadButton button = PadButton::Cross;
        bool rectangle = false;
        float cx = 0.0f;
        float cy = 0.0f;
        float radius = 0.0f;
        float left = 0.0f;
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;
    };

    static constexpr std::size_t kMaxTouches = 10;
    static constexpr std::size_t kHudButtonCount = 7;

    void OpenController(int joystick_index);
    void CloseController(SDL_JoystickID instance);
    void CloseAllControllers();
    void RecomputeTouchButtons();
    void UpdateStickFromPosition(float px, float py);
    void RecomputeLayout();

    int HitTest(float px, float py) const;  // -1 ninguno, -2 stick, >=0 boton
    int FindTouch(std::uint64_t id) const;
    int FreeTouch() const;

    bool EnsureGL();
    void ShutdownGL();
    void DrawStick();
    void DrawButton(const HudButton &button);
    void DrawFaceSymbol(PadButton button, float cx, float cy, float radius);
    void DrawText(const char *text, float cx, float cy, float pixel,
                  const float color[4]);

    std::vector<ControllerSlot> controllers_;
    std::array<TouchSlot, kMaxTouches> touches_{};
    std::array<HudButton, kHudButtonCount> hud_buttons_{};

    PadState pad_{};
    std::uint16_t touch_buttons_ = 0;
    std::uint16_t button_latch_ = 0;

    bool stick_active_ = false;
    float stick_x_ = 0.0f;
    float stick_y_ = 0.0f;

    int width_ = 0;
    int height_ = 0;
    float left_cx_ = 0.0f;
    float left_cy_ = 0.0f;
    float left_r_ = 0.0f;

    bool initialized_ = false;

    // HUD GLES 3.0 (solo en el build Android).
    std::vector<float> vertices_;  // x,y,r,g,b,a por vertice
#if defined(PS2_INPUT_USE_GL)
    unsigned int program_ = 0;
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    int viewport_loc_ = -1;
    bool gl_ready_ = false;
    bool gl_failed_ = false;
#endif
};

// Instancia global usada por main.cpp.
InputManager &GlobalInput();

}  // namespace port
