#pragma once

// Menu flotante Dear ImGui para el port.
//
// Se muestra/oculta con un toque de tres dedos (o F3 en teclado/emulador).
// Muestra FPS, VRAM simulada, overlay activo y permite pedir un salto a
// cualquier overlay y activar/desactivar Widescreen.

#include <SDL.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace port {

struct OverlayEntry {
    int index = 0;
    std::string name;
    std::uint32_t base = 0;
    std::uint32_t stamp = 0;
    std::uint32_t size = 0;
};

class PortMenu {
public:
    PortMenu() = default;
    ~PortMenu();

    PortMenu(const PortMenu &) = delete;
    PortMenu &operator=(const PortMenu &) = delete;

    bool Initialize(SDL_Window *window, SDL_GLContext context,
                    const std::string &overlay_dir);
    void Shutdown();

    // Procesa eventos. Devuelve true si el menu consumio el evento y el juego
    // no debe recibirlo.
    bool HandleEvent(const SDL_Event &event);

    // Dibuja el menu si esta visible. Debe llamarse antes de SwapWindow.
    void Draw();

    void SetVisible(bool visible);
    bool Visible() const;
    void Toggle();
    void ReleaseTouches();

    void SetJumpCallback(
        std::function<void(const OverlayEntry &)> callback);

    const std::vector<OverlayEntry> &Overlays() const;
    int ActiveOverlayIndex() const;

private:
    bool ImGuiEvent(const SDL_Event &event);
    bool GestureEvent(const SDL_Event &event);
    bool TouchEvent(const SDL_Event &event);
    void ToggleFromGesture();
    void LoadOverlayCatalog(const std::string &overlay_dir);
    void UpdateActiveOverlay();
    void RequestJump();

    SDL_Window *window_ = nullptr;
    SDL_GLContext context_ = nullptr;
    bool initialized_ = false;
    bool visible_ = false;

    std::vector<OverlayEntry> overlays_;
    std::string overlay_dir_;
    int selected_overlay_ = 0;
    int active_overlay_ = -1;
    std::uint32_t active_address_ = 0;
    std::string last_jump_status_;
    std::string last_quick_state_status_;

    std::function<void(const OverlayEntry &)> jump_callback_;

    // Gesture de tres dedos.
    struct GestureFinger {
        std::uint64_t id = 0;
        float x = 0.0f;
        float y = 0.0f;
    };
    static constexpr int kMaxGestureFingers = 8;
    GestureFinger gesture_[kMaxGestureFingers]{};
    int gesture_count_ = 0;
    bool gesture_armed_ = false;
    bool gesture_moved_ = false;
    std::uint64_t gesture_start_ms_ = 0;
    bool suppress_menu_touch_ = false;

    // Toque que controla ImGui (un puntero virtual).
    std::uint64_t menu_touch_id_ = 0;
    bool menu_touch_down_ = false;

    // VRAM: se recalcula cada segundo para no recorrer 4 MB por frame.
    std::uint64_t last_vram_scan_ms_ = 0;
    std::size_t vram_used_bytes_ = 0;
    std::uint64_t frame_count_ = 0;
    float menu_width_ = 820.0f;
    float menu_height_ = 760.0f;
};

PortMenu &Menu();

}  // namespace port
