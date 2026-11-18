#include "port_menu.hpp"

#include "crash_handler.hpp"
#include "game_main.hpp"
#include "gif_renderer.hpp"
#include "gs_render_pipeline.hpp"
#include "port_settings.hpp"
#include "quick_state.hpp"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>

#if defined(__ANDROID__)
#include <android/log.h>
#define MENU_TAG "OnimushaMenu"
#define MENU_LOGI(...) \
    __android_log_print(ANDROID_LOG_INFO, MENU_TAG, __VA_ARGS__)
#define MENU_LOGE(...) \
    __android_log_print(ANDROID_LOG_ERROR, MENU_TAG, __VA_ARGS__)
#else
#define MENU_LOGI(...)                        \
    do {                                      \
        std::fprintf(stdout, "[Menu] ");      \
        std::fprintf(stdout, __VA_ARGS__);    \
        std::fputc('\n', stdout);             \
        std::fflush(stdout);                  \
    } while (0)
#define MENU_LOGE(...)                        \
    do {                                      \
        std::fprintf(stderr, "[Menu] ");      \
        std::fprintf(stderr, __VA_ARGS__);    \
        std::fputc('\n', stderr);             \
        std::fflush(stderr);                  \
    } while (0)
#endif

namespace port {
namespace {

namespace fs = std::filesystem;

constexpr std::uint64_t kGestureMaxMs = 650;
constexpr float kGestureMoveThreshold = 0.06f;
constexpr std::size_t kMaxOverlays = 32;

bool parse_hex8(const std::string &text, std::size_t offset,
                std::uint32_t &value) {
    if (offset + 8 > text.size()) return false;
    char buffer[9] = {};
    std::memcpy(buffer, text.data() + offset, 8);
    char *end = nullptr;
    const unsigned long parsed = std::strtoul(buffer, &end, 16);
    if (end == buffer) return false;
    value = static_cast<std::uint32_t>(parsed);
    return true;
}

}  // namespace

PortMenu &Menu() {
    static PortMenu instance;
    return instance;
}

PortMenu::~PortMenu() {
    Shutdown();
}

bool PortMenu::Initialize(SDL_Window *window, SDL_GLContext context,
                          const std::string &overlay_dir) {
    if (initialized_) return true;
    window_ = window;
    context_ = context;
    overlay_dir_ = overlay_dir;

    int window_width = 0;
    int window_height = 0;
    SDL_GetWindowSize(window, &window_width, &window_height);
    menu_width_ = std::min(820.0f, static_cast<float>(window_width) * 0.70f);
    menu_height_ = std::min(760.0f, static_cast<float>(window_height) * 0.92f);

    LoadOverlayCatalog(overlay_dir);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;  // no escribir imgui.ini en el cwd de Android
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;

    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.FrameRounding = 5.0f;
    style.GrabRounding = 4.0f;
    style.FontScaleMain = 2.0f;  // pantalla Android de alta densidad
    style.ScaleAllSizes(2.0f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.07f, 0.10f, 0.94f);
    style.Colors[ImGuiCol_TitleBgActive] =
        ImVec4(0.13f, 0.32f, 0.62f, 1.0f);

    if (!ImGui_ImplSDL2_InitForOpenGL(window, context)) {
        MENU_LOGE("ImGui_ImplSDL2_InitForOpenGL fallo");
        ImGui::DestroyContext();
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 300 es")) {
        MENU_LOGE("ImGui_ImplOpenGL3_Init fallo");
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    initialized_ = true;
    MENU_LOGI("ImGui inicializado (%zu overlays)", overlays_.size());
    return true;
}

void PortMenu::Shutdown() {
    if (initialized_) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
    }
    initialized_ = false;
    visible_ = false;
}

void PortMenu::LoadOverlayCatalog(const std::string &overlay_dir) {
    overlays_.clear();
    selected_overlay_ = 0;
    active_overlay_ = -1;

    std::error_code error;
    const fs::path root(overlay_dir);
    if (!fs::is_directory(root, error)) {
        MENU_LOGE("no existe carpeta de overlays: %s", overlay_dir.c_str());
        return;
    }

    for (fs::directory_iterator it(root, error), end;
         !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error)) {
            error.clear();
            continue;
        }
        if (it->path().extension() != ".bin") continue;

        const std::string stem = it->path().stem().string();
        OverlayEntry entry;
        entry.name = stem;
        if (!parse_hex8(stem, 0, entry.base)) continue;
        if (!parse_hex8(stem, 9, entry.stamp)) entry.stamp = 0;
        entry.size = static_cast<std::uint32_t>(it->file_size(error));
        if (error) {
            error.clear();
            entry.size = 0;
        }
        overlays_.push_back(std::move(entry));
    }

    std::sort(overlays_.begin(), overlays_.end(),
              [](const OverlayEntry &a, const OverlayEntry &b) {
                  if (a.base != b.base) return a.base < b.base;
                  return a.name < b.name;
              });
    if (overlays_.size() > kMaxOverlays) overlays_.resize(kMaxOverlays);
    for (std::size_t i = 0; i < overlays_.size(); ++i) {
        overlays_[i].index = static_cast<int>(i);
    }
    if (!overlays_.empty()) selected_overlay_ = 0;
}

void PortMenu::UpdateActiveOverlay() {
    active_address_ = ps2::crash::LastFunction();
    int best = -1;
    std::uint32_t best_base = 0;

    for (const OverlayEntry &entry : overlays_) {
        if (entry.size == 0) continue;
        if (active_address_ < entry.base ||
            active_address_ >= entry.base + entry.size) {
            continue;
        }
        if (best < 0 || entry.base > best_base) {
            best = entry.index;
            best_base = entry.base;
        }
    }
    active_overlay_ = best;
}

void PortMenu::SetVisible(bool visible) {
    if (visible_ == visible) return;
    visible_ = visible;
    if (!visible_) {
        ImGuiIO &io = ImGui::GetIO();
        if (menu_touch_down_) {
            io.AddMouseButtonEvent(0, false);
            menu_touch_down_ = false;
        }
        menu_touch_id_ = 0;
    } else {
        UpdateActiveOverlay();
        if (active_overlay_ >= 0) selected_overlay_ = active_overlay_;
    }
}

bool PortMenu::Visible() const {
    return visible_;
}

void PortMenu::Toggle() {
    SetVisible(!visible_);
}

void PortMenu::ReleaseTouches() {
    gesture_count_ = 0;
    gesture_armed_ = false;
    gesture_moved_ = false;
    suppress_menu_touch_ = false;
    if (menu_touch_down_ && initialized_) {
        ImGui::GetIO().AddMouseButtonEvent(0, false);
    }
    menu_touch_down_ = false;
    menu_touch_id_ = 0;
}

void PortMenu::SetJumpCallback(
    std::function<void(const OverlayEntry &)> callback) {
    jump_callback_ = std::move(callback);
}

const std::vector<OverlayEntry> &PortMenu::Overlays() const {
    return overlays_;
}

int PortMenu::ActiveOverlayIndex() const {
    return active_overlay_;
}

void PortMenu::ToggleFromGesture() {
    Toggle();
    MENU_LOGI("menu %s por gesto de tres dedos",
              visible_ ? "abierto" : "cerrado");
}

bool PortMenu::GestureEvent(const SDL_Event &event) {
    const bool down = event.type == SDL_FINGERDOWN;
    const bool motion = event.type == SDL_FINGERMOTION;
    const bool up = event.type == SDL_FINGERUP;
    if (!down && !motion && !up) return false;

    const std::uint64_t id =
        static_cast<std::uint64_t>(event.tfinger.fingerId);
    const float x = event.tfinger.x;
    const float y = event.tfinger.y;

    if (down) {
        if (gesture_count_ == 0) {
            gesture_start_ms_ = SDL_GetTicks64();
            gesture_armed_ = false;
            gesture_moved_ = false;
        }
        bool found = false;
        for (int i = 0; i < gesture_count_; ++i) {
            if (gesture_[i].id == id) {
                found = true;
                break;
            }
        }
        if (!found && gesture_count_ < kMaxGestureFingers) {
            gesture_[gesture_count_].id = id;
            gesture_[gesture_count_].x = x;
            gesture_[gesture_count_].y = y;
            ++gesture_count_;
        }
        if (gesture_count_ == 3) {
            gesture_armed_ = true;
            gesture_moved_ = false;
            gesture_start_ms_ = SDL_GetTicks64();
            suppress_menu_touch_ = true;
            if (menu_touch_down_) {
                ImGui::GetIO().AddMouseButtonEvent(0, false);
                menu_touch_down_ = false;
                menu_touch_id_ = 0;
            }
        } else if (gesture_count_ > 3) {
            gesture_armed_ = false;
        }
        return suppress_menu_touch_;
    }

    int index = -1;
    for (int i = 0; i < gesture_count_; ++i) {
        if (gesture_[i].id == id) {
            index = i;
            break;
        }
    }
    if (index < 0) return suppress_menu_touch_;

    if (motion && gesture_armed_) {
        const float dx = x - gesture_[index].x;
        const float dy = y - gesture_[index].y;
        if (dx * dx + dy * dy > kGestureMoveThreshold * kGestureMoveThreshold) {
            gesture_armed_ = false;
            gesture_moved_ = true;
        }
        gesture_[index].x = x;
        gesture_[index].y = y;
    }

    if (up) {
        for (int i = index; i + 1 < gesture_count_; ++i) {
            gesture_[i] = gesture_[i + 1];
        }
        --gesture_count_;

        if (gesture_count_ == 0) {
            const bool tap =
                gesture_armed_ && !gesture_moved_ &&
                SDL_GetTicks64() - gesture_start_ms_ <= kGestureMaxMs;
            gesture_armed_ = false;
            if (tap) {
                suppress_menu_touch_ = false;
                ToggleFromGesture();
                return true;
            }
            suppress_menu_touch_ = false;
        }
    }

    return suppress_menu_touch_;
}

bool PortMenu::TouchEvent(const SDL_Event &event) {
    if (!visible_ || !initialized_ || suppress_menu_touch_) return false;
    const bool down = event.type == SDL_FINGERDOWN;
    const bool motion = event.type == SDL_FINGERMOTION;
    const bool up = event.type == SDL_FINGERUP;
    if (!down && !motion && !up) return false;

    const std::uint64_t id =
        static_cast<std::uint64_t>(event.tfinger.fingerId);
    if (down && menu_touch_id_ != 0 && id != menu_touch_id_) {
        return true;  // solo un puntero virtual para ImGui
    }
    if (down && menu_touch_id_ == 0) {
        menu_touch_id_ = id;
        menu_touch_down_ = true;
    }
    if (id != menu_touch_id_) return false;

    ImGuiIO &io = ImGui::GetIO();
    float width = io.DisplaySize.x;
    float height = io.DisplaySize.y;
    if (width <= 0.0f || height <= 0.0f) {
        int window_width = 0;
        int window_height = 0;
        SDL_GetWindowSize(window_, &window_width, &window_height);
        width = static_cast<float>(window_width);
        height = static_cast<float>(window_height);
    }

    io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
    io.AddMousePosEvent(event.tfinger.x * width,
                        event.tfinger.y * height);

    if (down) {
        io.AddMouseButtonEvent(0, true);
    } else if (up) {
        io.AddMouseButtonEvent(0, false);
        menu_touch_down_ = false;
        menu_touch_id_ = 0;
    }
    return true;
}

bool PortMenu::ImGuiEvent(const SDL_Event &event) {
    if (!visible_ || !initialized_) return false;

    if (event.type == SDL_KEYDOWN) {
        const SDL_Scancode scancode = event.key.keysym.scancode;
        if (scancode == SDL_SCANCODE_F3) {
            Toggle();
            return true;
        }
        if (scancode == SDL_SCANCODE_ESCAPE ||
            scancode == SDL_SCANCODE_AC_BACK) {
            SetVisible(false);
            return true;
        }
    }

    switch (event.type) {
    case SDL_MOUSEMOTION:
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
    case SDL_MOUSEWHEEL:
    case SDL_KEYDOWN:
    case SDL_KEYUP:
    case SDL_TEXTINPUT:
        ImGui_ImplSDL2_ProcessEvent(&event);
        return true;
    default:
        return false;
    }
}

bool PortMenu::HandleEvent(const SDL_Event &event) {
    if (event.type == SDL_APP_WILLENTERBACKGROUND) {
        ReleaseTouches();
        return false;
    }
    if (GestureEvent(event)) return true;
    if (TouchEvent(event)) return true;
    if (event.type == SDL_KEYDOWN &&
        event.key.keysym.scancode == SDL_SCANCODE_F3) {
        Toggle();
        MENU_LOGI("menu %s por F3", visible_ ? "abierto" : "cerrado");
        return true;
    }
    return ImGuiEvent(event);
}

void PortMenu::RequestJump() {
    if (overlays_.empty()) return;
    selected_overlay_ = std::clamp(
        selected_overlay_, 0, static_cast<int>(overlays_.size()) - 1);
    const OverlayEntry &entry =
        overlays_[static_cast<std::size_t>(selected_overlay_)];

    if (jump_callback_) {
        jump_callback_(entry);
    } else {
        game::RequestOverlayJump(entry.index, entry.base, entry.stamp,
                                 entry.name);
    }

    char status[192];
    std::snprintf(status, sizeof(status),
                  "Solicitado #%d %s (0x%08X / %08X). Loader state=%u",
                  entry.index, entry.name.c_str(), entry.base, entry.stamp,
                  game::CurrentLoadState());
    last_jump_status_ = status;
}

void PortMenu::Draw() {
    if (!visible_ || !initialized_) return;

    UpdateActiveOverlay();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowSize(ImVec2(menu_width_, menu_height_),
                             ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(48.0f, 48.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowCollapsed(false, ImGuiCond_FirstUseEver);

    bool open = true;
    if (ImGui::Begin("Onimusha 2 - Puerto", &open,
                     ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::Text("Tres dedos o F3 para cerrar");
        ImGui::Separator();

        const ImGuiIO &io = ImGui::GetIO();
        ++frame_count_;
        ImGui::Text("FPS: %.1f  (%.2f ms)", io.Framerate,
                    io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f);
        ImGui::Text("Frame: %llu",
                    static_cast<unsigned long long>(frame_count_));

        const std::uint64_t now = SDL_GetTicks64();
        if (last_vram_scan_ms_ == 0 ||
            now - last_vram_scan_ms_ >= 1000) {
            last_vram_scan_ms_ = now;
            const std::uint8_t *data = ps2::gif::VramData();
            const std::size_t size = ps2::gif::VramSize();
            vram_used_bytes_ = 0;
            if (data != nullptr) {
                for (std::size_t i = 0; i < size; ++i) {
                    if (data[i] != 0) ++vram_used_bytes_;
                }
            }
        }

        const std::size_t vram_total = ps2::gif::VramSize();
        const float vram_mb =
            static_cast<float>(vram_used_bytes_) / (1024.0f * 1024.0f);
        const float vram_total_mb =
            static_cast<float>(vram_total) / (1024.0f * 1024.0f);
        ImGui::Text("gs_vram: %.2f / %.2f MB", vram_mb, vram_total_mb);
        const float vram_fraction =
            vram_total > 0
                ? static_cast<float>(vram_used_bytes_) /
                      static_cast<float>(vram_total)
                : 0.0f;
        char vram_label[64];
        std::snprintf(vram_label, sizeof(vram_label), "%.1f%%",
                      vram_fraction * 100.0f);
        ImGui::ProgressBar(vram_fraction, ImVec2(-1.0f, 0.0f), vram_label);

        ImGui::Separator();
        if (active_overlay_ >= 0 &&
            active_overlay_ < static_cast<int>(overlays_.size())) {
            const OverlayEntry &entry =
                overlays_[static_cast<std::size_t>(active_overlay_)];
            ImGui::Text("Overlay activo: #%d", active_overlay_);
            ImGui::Text("ID: %s", entry.name.c_str());
            ImGui::Text("Base: 0x%08X  Stamp: 0x%08X", entry.base,
                        entry.stamp);
        } else {
            ImGui::Text("Overlay activo: principal / menu");
            ImGui::Text("Ultima FUN ejecutada: 0x%08X", active_address_);
        }
        ImGui::Text("Loader state: %u  handler: 0x%08X",
                    game::CurrentLoadState(),
                    game::LoadStateHandler(game::CurrentLoadState()));

        {
            static bool logged_stats = false;
            if (!logged_stats) {
                logged_stats = true;
                MENU_LOGI("stats fps=%.1f vram=%.2f/%.2fMB active=%d state=%u",
                          io.Framerate, vram_mb, vram_total_mb,
                          active_overlay_, game::CurrentLoadState());
            }
        }

        ImGui::Separator();
        ImGui::Text("Overlay / mapa");

        const char *preview = "(sin overlays)";
        char preview_buffer[160] = {};
        if (!overlays_.empty()) {
            selected_overlay_ = std::clamp(
                selected_overlay_, 0,
                static_cast<int>(overlays_.size()) - 1);
            const OverlayEntry &entry =
                overlays_[static_cast<std::size_t>(selected_overlay_)];
            std::snprintf(preview_buffer, sizeof(preview_buffer),
                          "#%02d  %s  0x%08X", entry.index,
                          entry.name.c_str(), entry.base);
            preview = preview_buffer;
        }

        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##overlay", preview)) {
            for (const OverlayEntry &entry : overlays_) {
                char label[192];
                std::snprintf(label, sizeof(label),
                              "#%02d  %s  0x%08X / %08X", entry.index,
                              entry.name.c_str(), entry.base, entry.stamp);
                const bool selected = entry.index == selected_overlay_;
                if (ImGui::Selectable(label, selected)) {
                    selected_overlay_ = entry.index;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if (ImGui::Button("Forzar salto", ImVec2(280.0f, 0.0f))) {
            RequestJump();
        }
        ImGui::SameLine();
        if (ImGui::Button("Recargar catalogo", ImVec2(320.0f, 0.0f))) {
            LoadOverlayCatalog(overlay_dir_);
        }

        if (!last_jump_status_.empty()) {
            ImGui::TextWrapped("%s", last_jump_status_.c_str());
        }

        ImGui::Separator();
        ImGui::Text("Estado rapido");
        if (ImGui::Button("Guardar (F5)", ImVec2(280.0f, 0.0f))) {
            const bool saved = ps2::state::SaveQuickState("quickstate.qs");
            const std::string &error = ps2::state::LastError();
            last_quick_state_status_ =
                saved ? "Guardado en quickstate.qs"
                      : "Error al guardar: " + error;
            MENU_LOGI("quickstate guardar=%d error=%s", saved ? 1 : 0,
                      error.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("Cargar (F9)", ImVec2(280.0f, 0.0f))) {
            const bool loaded = ps2::state::LoadQuickState("quickstate.qs");
            const std::string &error = ps2::state::LastError();
            last_quick_state_status_ =
                loaded ? "Cargado desde quickstate.qs"
                       : "Error al cargar: " + error;
            MENU_LOGI("quickstate cargar=%d error=%s", loaded ? 1 : 0,
                      error.c_str());
        }
        if (!last_quick_state_status_.empty()) {
            ImGui::TextWrapped("%s", last_quick_state_status_.c_str());
        }

        ImGui::Separator();
        bool widescreen = ps2::render::WidescreenEnabled();
        if (ImGui::Checkbox("Widescreen 16:9", &widescreen)) {
            ps2::render::SetWidescreen(widescreen);
            MENU_LOGI("widescreen=%d", widescreen ? 1 : 0);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(F2)");

        ImGui::Separator();
        ImGui::Text("Mejoras graficas");
        int internal_index = ps2::render::InternalResolutionScale() - 1;
        if (internal_index < 0) internal_index = 0;
        if (internal_index > 2) internal_index = 2;
        const char *scale_items[] = {
            "1x (640x448)",
            "2x (1280x896)",
            "3x (1920x1344)",
        };
        ImGui::SetNextItemWidth(240.0f);
        if (ImGui::Combo("Resolucion 3D", &internal_index,
                         scale_items, 3)) {
            const int scale = internal_index + 1;
            ps2::render::SetInternalResolutionScale(scale);
            MENU_LOGI("internal_resolution=%dx", scale);
        }

        bool unlocked_60 = port::settings::Unlocked60Fps();
        if (ImGui::Checkbox("60 FPS desbloqueados", &unlocked_60)) {
            port::settings::SetUnlocked60Fps(unlocked_60);
            MENU_LOGI("60fps=%d", unlocked_60 ? 1 : 0);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(NTSC 30/60)");

        ImGui::Separator();
        if (ImGui::Button("Cerrar", ImVec2(280.0f, 0.0f))) {
            open = false;
        }
    }
    ImGui::End();

    if (!open) visible_ = false;

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

}  // namespace port
