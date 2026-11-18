// main.cpp - entrada Android/NDK para SDL2 + OpenGL ES 3.0.
//
// Ventana a pantalla completa, contexto GLES 3.0 y toque basico (multitactil
// ligero). El bucle del juego es la traduccion de FUN_001496D0 en
// game_main.cpp: game::Frame() ejecuta una iteracion y hace
// SDL_GL_SwapWindow donde la PS2 llamaba a sceGsSyncV.
//
// Game_Render() limpia la pantalla. Los hooks DMA encolan sprites GIF y
// modelos VU1; port_vsync() ejecuta el pipeline 2D -> depth -> 3D antes de
// SDL_GL_SwapWindow. Las funciones debiles (weak) permiten compilar este
// archivo solo; tus definiciones fuertes las sustituyen al enlazar.

#include <SDL.h>
#include <GLES3/gl3.h>

#include "game_main.hpp"
#include "gif_renderer.hpp"
#include "gs_render_pipeline.hpp"
#include "crash_handler.hpp"
#include "input_manager.hpp"
#include "iso_reader.hpp"
#include "port_fs.hpp"
#include "port_menu.hpp"
#include "port_settings.hpp"
#include "ps2_audio_hle.hpp"
#include "ps2_audio_system.hpp"
#include "ps2_cd_stream.hpp"
#include "ps2_hle.hpp"
#include "ps2_mem.hpp"
#include "ps2_memory_card.hpp"
#include "quick_state.hpp"
#include "pss_video_player.hpp"
#include "vif1_parser.hpp"
#include "vu1_renderer.hpp"
#include "ps2_pad.hpp"
#if defined(PORT_STORAGE_SELFTEST)
#include "storage_selftest.hpp"
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef __ANDROID__
#include <android/log.h>
#include <jni.h>
#include <unistd.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#define LOG_TAG "OnimushaPort"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#define LOGI(...) do { std::fprintf(stdout, __VA_ARGS__); std::fputc('\n', stdout); } while (0)
#define LOGE(...) do { std::fprintf(stderr, __VA_ARGS__); std::fputc('\n', stderr); } while (0)
#endif

// ---------------------------------------------------------------------------
// Enganche con el motor / placeholder de dibujo.
// ---------------------------------------------------------------------------
void Game_Update();
void Game_Render();
void Game_Resize(int width, int height);
void Game_TouchDown(float x, float y);   // x,y normalizados 0..1
void Game_TouchMove(float x, float y);
void Game_TouchUp(float x, float y);

// Delta fijo (1/60 s) por si algun consumidor lo necesita.
float g_game_delta_time = 1.0f / 60.0f;

#if defined(__GNUC__) || defined(__clang__)
#define PORT_WEAK __attribute__((weak))
#else
#define PORT_WEAK
#endif

PORT_WEAK void Game_Update() {}
PORT_WEAK void Game_Render() {
    glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}
PORT_WEAK void Game_Resize(int, int) {}
PORT_WEAK void Game_TouchDown(float x, float y) {
    if (GlobalPssVideoPlayer().IsPlaying()) {
        GlobalPssVideoPlayer().RequestSkip();
        LOGI("PSS: skip por toque");
        return;
    }
    LOGI("touch down %.3f %.3f", x, y);
}
PORT_WEAK void Game_TouchMove(float x, float y) { (void)x; (void)y; }
PORT_WEAK void Game_TouchUp(float x, float y) { LOGI("touch up %.3f %.3f", x, y); }

// Estado del mando virtual que puede leer el juego:
//     extern port::PadState g_pad;
//     if (g_pad.pressed(port::PadButton::Cross)) ...
port::PadState g_pad;

// Lector compartido por el diagnostico ISO y la HLE de sceCdRead.
IsoReader g_iso;

#if defined(__ANDROID__)
namespace {

std::mutex g_disc_mutex;
std::condition_variable g_disc_cv;
int g_disc_fd = -1;
std::string g_disc_name;
std::string g_disc_error;
bool g_disc_done = false;

void close_pending_disc_fd_locked() {
    if (g_disc_fd >= 0) {
        ::close(g_disc_fd);
        g_disc_fd = -1;
    }
}

// Espera a que MainActivity entregue un fd (o avise de que no habra ISO).
// Devuelve el fd duplicado, o -1 con `error` explicando el motivo.
int wait_for_disc_fd(std::string &name, std::string &error) {
    std::unique_lock<std::mutex> lock(g_disc_mutex);
    const bool ready = g_disc_cv.wait_for(
        lock, std::chrono::minutes(10), [] { return g_disc_done; });
    if (!ready) {
        g_disc_done = true;
        error = "tiempo de espera agotado esperando disc.iso";
        return -1;
    }
    if (g_disc_fd < 0) {
        error = g_disc_error.empty() ? "no se selecciono disc.iso"
                                     : g_disc_error;
        return -1;
    }
    const int fd = g_disc_fd;
    g_disc_fd = -1;
    name = g_disc_name;
    return fd;
}

void discard_pending_disc_fd() {
    std::lock_guard<std::mutex> lock(g_disc_mutex);
    close_pending_disc_fd_locked();
    g_disc_done = true;
}

}  // namespace

// MainActivity entrega aqui el fd de ACTION_OPEN_DOCUMENT. Se duplica porque
// Java conserva la propiedad del ParcelFileDescriptor hasta cerrarlo.
extern "C" JNIEXPORT void JNICALL
Java_org_ps2native_recomp_MainActivity_nativeSetDiscFd(JNIEnv *env, jclass,
                                                       jint fd,
                                                       jstring name) {
    std::string label = "disc.iso (DocumentProvider)";
    if (env != nullptr && name != nullptr) {
        const char *utf = env->GetStringUTFChars(name, nullptr);
        if (utf != nullptr) {
            label = utf;
            env->ReleaseStringUTFChars(name, utf);
        }
    }

    std::lock_guard<std::mutex> lock(g_disc_mutex);
    if (fd < 0 || g_disc_done) return;

    const int owned = ::dup(fd);
    if (owned < 0) {
        g_disc_error = "no se pudo duplicar el descriptor de disc.iso";
        g_disc_done = true;
        g_disc_cv.notify_all();
        return;
    }

    close_pending_disc_fd_locked();
    g_disc_fd = owned;
    g_disc_name = label;
    g_disc_error.clear();
    g_disc_done = true;
    g_disc_cv.notify_all();
}

extern "C" JNIEXPORT void JNICALL
Java_org_ps2native_recomp_MainActivity_nativeSetDiscUnavailable(JNIEnv *env,
                                                                jclass,
                                                                jstring reason) {
    std::string message = "sin disc.iso";
    if (env != nullptr && reason != nullptr) {
        const char *utf = env->GetStringUTFChars(reason, nullptr);
        if (utf != nullptr) {
            message = utf;
            env->ReleaseStringUTFChars(reason, utf);
        }
    }

    std::lock_guard<std::mutex> lock(g_disc_mutex);
    if (g_disc_done) return;
    close_pending_disc_fd_locked();
    g_disc_error = message;
    g_disc_done = true;
    g_disc_cv.notify_all();
}

// Java encontro Android/data/<pkg>/files/disc.iso. Si el intento nativo de
// abrirlo falla, no tiene sentido esperar al selector.
extern "C" JNIEXPORT void JNICALL
Java_org_ps2native_recomp_MainActivity_nativeSetDiscFilePresent(JNIEnv *,
                                                                jclass) {
    std::lock_guard<std::mutex> lock(g_disc_mutex);
    if (g_disc_done) return;
    g_disc_error = "disc.iso local presente pero no se pudo abrir";
    g_disc_done = true;
    g_disc_cv.notify_all();
}
#endif  // __ANDROID__

// Comprobacion ISO 9660: abre disc.iso y lee el PVD (LBA 16).
void iso_probe() {
    bool opened = g_iso.Open("disc.iso");
    if (!opened) {
        LOGI("ISO probe: %s", g_iso.LastError().c_str());
#if defined(__ANDROID__)
        // MainActivity abre ACTION_OPEN_DOCUMENT si no hay disc.iso local.
        std::string label;
        std::string error;
        const int fd = wait_for_disc_fd(label, error);
        if (fd < 0) {
            LOGE("ISO: %s", error.c_str());
            return;
        }
        LOGI("ISO: usando descriptor del selector: %s", label.c_str());
        opened = g_iso.OpenFd(fd, label);
        if (!opened) {
            LOGE("ISO fd: %s", g_iso.LastError().c_str());
            return;
        }
#else
        return;
#endif
    } else {
#if defined(__ANDROID__)
        // Si Java lanzo el selector y luego aparecio un disc.iso local, el fd
        // pendiente ya no se usa.
        discard_pending_disc_fd();
#endif
    }

    std::vector<std::uint8_t> sector(IsoReader::kSectorSize);
    if (g_iso.ReadSectors(0, 1, sector.data())) {
        LOGI("ISO LBA 0: %02X %02X %02X %02X (size=%lld)",
             sector[0], sector[1], sector[2], sector[3],
             static_cast<long long>(g_iso.Size()));
    }

    if (!g_iso.ReadSectors(16, 1, sector.data())) {
        LOGE("ISO PVD LBA 16: %s", g_iso.LastError().c_str());
        return;
    }
    const char *id = reinterpret_cast<const char *>(sector.data() + 1);
    LOGI("ISO PVD LBA 16: %.5s size=%lld sectors=%lld",
         id, static_cast<long long>(g_iso.Size()), static_cast<long long>(g_iso.SectorCount()));
    if (std::memcmp(id, "CD001", 5) == 0) {
        LOGI("ISO9660 OK: CD001");
    } else {
        LOGE("ISO9660: CD001 no encontrado");
    }
}

// Carpeta de los 32 overlays. En Android vive en el almacenamiento externo
// de la app (scoped storage), junto a disc.iso.
std::string overlay_directory() {
    std::string directory = "overlays";
#if defined(__ANDROID__)
    const char *external = SDL_AndroidGetExternalStoragePath();
    if (external && *external) directory = std::string(external) + "/overlays";
#endif
    return directory;
}

// Ruta interna de la app, usada por crash_log.txt y memcard0/.
std::string internal_storage_path() {
    const char *internal = SDL_AndroidGetInternalStoragePath();
    return (internal != nullptr && internal[0] != '\0') ? internal : ".";
}

namespace {

// Entrada completa: mandos SDL_GameController + HUD tactil con multitouch.
port::InputManager g_input_manager;

int g_drawable_width = 0;
int g_drawable_height = 0;
int g_logical_width = 0;
int g_logical_height = 0;

// Alimenta el parser VIF1 desde una cadena DMA ya resuelta. Se usa tanto
// para sceDmaSend (HLE) como para las escrituras directas a D1_CHCR.
void ProcessVif1Transfer(std::uint32_t source,
                         const std::vector<ps2::hle::DmaTag> *tags) {
    static ps2::vif1::Parser parser;

    bool parsed = false;
    if (source != 0) parsed = parser.SubmitChain(source);
    if (!parsed && tags != nullptr) {
        parser.DiscardPendingStream();
        for (const ps2::hle::DmaTag &tag : *tags) {
            if (!tag.data_valid) continue;
            const std::uint8_t *data = ps2::hle::TagData(tag);
            if (data != nullptr) {
                parser.SubmitPacket(data, ps2::hle::TagBytes(tag));
            }
        }
    }

    for (const ps2::vif1::Capture &capture : parser.NewCaptures()) {
        Vu1Renderer::Packet packet;
        if (parser.BuildPacketOnimusha(capture, packet) ||
            parser.BuildPacketAuto(capture, packet)) {
            ps2::render::QueueModel(std::move(packet));
        }
    }
    parser.ClearCaptures();
}

void update_viewport(SDL_Window *window) {
    SDL_GL_GetDrawableSize(window, &g_drawable_width, &g_drawable_height);
    SDL_GetWindowSize(window, &g_logical_width, &g_logical_height);
    LOGI("viewport drawable=%dx%d logical=%dx%d", g_drawable_width,
         g_drawable_height, g_logical_width, g_logical_height);
    glViewport(0, 0, g_drawable_width, g_drawable_height);
    g_input_manager.Resize(g_drawable_width, g_drawable_height);
    Game_Resize(g_drawable_width, g_drawable_height);
}

// Limitador de fotogramas por reloj de alta resoluci??n. Duerme hasta 1 ms
// antes del objetivo y hace un giro corto para no pasarse (SDL_Delay tiene
// resoluci??n de milisegundos en Android).
class FramePacer {
public:
    explicit FramePacer(double fps)
        : period_(1.0 / fps),
          frequency_(SDL_GetPerformanceFrequency()),
          next_(SDL_GetPerformanceCounter()) {}

    void reset() {
        next_ = SDL_GetPerformanceCounter() + ticks(period_);
    }

    void set_fps(double fps) {
        if (fps <= 0.0) return;
        period_ = 1.0 / fps;
        reset();
    }

    void wait() {
        for (;;) {
            const std::uint64_t now = SDL_GetPerformanceCounter();
            if (now >= next_) {
                next_ += ticks(period_);
                // Si nos hemos quedado muy atr??s (pausa, carga), no acumules
                // una r??faga de fotogramas.
                if (now - next_ > ticks(period_ * 4.0)) next_ = now + ticks(period_);
                return;
            }
            const double remaining = static_cast<double>(next_ - now) / static_cast<double>(frequency_);
            if (remaining > 0.002) {
                SDL_Delay(static_cast<Uint32>((remaining - 0.001) * 1000.0));
            } else {
                std::this_thread::yield();
            }
        }
    }

private:
    std::uint64_t ticks(double seconds) const {
        return static_cast<std::uint64_t>(seconds * static_cast<double>(frequency_));
    }

    double period_;
    std::uint64_t frequency_;
    std::uint64_t next_;
};

bool create_window_and_context(SDL_Window *&window, SDL_GLContext &context) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);

    window = SDL_CreateWindow(
        "Onimusha Port",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720,
        SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_SHOWN);
    if (!window) {
        LOGE("SDL_CreateWindow: %s", SDL_GetError());
        return false;
    }

    context = SDL_GL_CreateContext(window);
    if (!context) {
        LOGE("SDL_GL_CreateContext: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        window = nullptr;
        return false;
    }
    if (SDL_GL_MakeCurrent(window, context) != 0) {
        LOGE("SDL_GL_MakeCurrent: %s", SDL_GetError());
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        context = nullptr;
        window = nullptr;
        return false;
    }

    // Limitamos nosotros a 60 Hz; con vsync pon 1 y quita FramePacer::wait().
    SDL_GL_SetSwapInterval(0);

    const GLubyte *version = glGetString(GL_VERSION);
    const GLubyte *renderer = glGetString(GL_RENDERER);
    LOGI("OpenGL ES: %s", version ? reinterpret_cast<const char *>(version) : "?");
    LOGI("Renderer: %s", renderer ? reinterpret_cast<const char *>(renderer) : "?");

    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    if (major < 3) LOGE("Se ha creado un contexto GLES %d.%d, se esperaba 3.0", major, minor);

    update_viewport(window);
    return true;
}

void handle_event(const SDL_Event &event, bool &running, bool &paused, bool &resumed) {
    const bool menu_was_visible = port::Menu().Visible();
    if (port::Menu().HandleEvent(event)) {
        if (menu_was_visible != port::Menu().Visible()) {
            g_input_manager.ReleaseAll();
            g_pad.clear();
        }
        return;
    }

    switch (event.type) {
    case SDL_QUIT:
    case SDL_APP_TERMINATING:
        running = false;
        break;

    case SDL_APP_WILLENTERBACKGROUND:
        paused = true;
        g_input_manager.ReleaseAll();
        g_pad.clear();
        break;

    case SDL_APP_DIDENTERFOREGROUND:
        resumed = true;
        break;

    case SDL_CONTROLLERDEVICEADDED:
    case SDL_CONTROLLERDEVICEREMOVED:
    case SDL_CONTROLLERDEVICEREMAPPED:
        g_input_manager.HandleEvent(event);
        break;

    case SDL_KEYDOWN:
        if (event.key.keysym.scancode == SDL_SCANCODE_AC_BACK ||
            event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
            running = false;
        } else if (event.key.keysym.scancode == SDL_SCANCODE_F2) {
            ps2::render::ToggleWidescreen();
            LOGI("Widescreen=%d",
                 ps2::render::WidescreenEnabled() ? 1 : 0);
        } else if (event.key.keysym.scancode == SDL_SCANCODE_F5) {
            const bool saved =
                ps2::state::SaveQuickState("quickstate.qs");
            LOGI("QuickState guardado=%d (%s)", saved ? 1 : 0,
                 ps2::state::LastError().c_str());
        } else if (event.key.keysym.scancode == SDL_SCANCODE_F9) {
            const bool loaded =
                ps2::state::LoadQuickState("quickstate.qs");
            LOGI("QuickState cargado=%d (%s)", loaded ? 1 : 0,
                 ps2::state::LastError().c_str());
        }
        break;

    case SDL_FINGERDOWN: {
        const float x = event.tfinger.x;
        const float y = event.tfinger.y;
        if (GlobalPssVideoPlayer().IsPlaying()) {
            Game_TouchDown(x, y);
        } else if (!g_input_manager.HandleEvent(event)) {
            Game_TouchDown(x, y);
        }
        break;
    }

    case SDL_FINGERMOTION: {
        const float x = event.tfinger.x;
        const float y = event.tfinger.y;
        if (!g_input_manager.HandleEvent(event)) {
            Game_TouchMove(x, y);
        }
        break;
    }

    case SDL_FINGERUP: {
        const float x = event.tfinger.x;
        const float y = event.tfinger.y;
        if (!g_input_manager.HandleEvent(event)) {
            Game_TouchUp(x, y);
        }
        break;
    }

    // Raton: util en emulador o escritorio; en Android suele no llegar.
    case SDL_MOUSEBUTTONDOWN:
        if (event.button.button == SDL_BUTTON_LEFT) {
            const float x =
                g_logical_width ? static_cast<float>(event.button.x) /
                                      g_logical_width
                                : 0.0f;
            const float y =
                g_logical_height ? static_cast<float>(event.button.y) /
                                       g_logical_height
                                 : 0.0f;
            if (!g_input_manager.TouchDown(0, x, y)) Game_TouchDown(x, y);
        }
        break;

    case SDL_MOUSEBUTTONUP:
        if (event.button.button == SDL_BUTTON_LEFT) {
            const float x =
                g_logical_width ? static_cast<float>(event.button.x) /
                                      g_logical_width
                                : 0.0f;
            const float y =
                g_logical_height ? static_cast<float>(event.button.y) /
                                       g_logical_height
                                 : 0.0f;
            if (!g_input_manager.TouchUp(0)) Game_TouchUp(x, y);
        }
        break;

    case SDL_MOUSEMOTION:
        if (event.motion.state & SDL_BUTTON_LMASK) {
            const float x =
                g_logical_width ? static_cast<float>(event.motion.x) /
                                      g_logical_width
                                : 0.0f;
            const float y =
                g_logical_height ? static_cast<float>(event.motion.y) /
                                       g_logical_height
                                 : 0.0f;
            if (!g_input_manager.TouchMove(0, x, y)) Game_TouchMove(x, y);
        }
        break;

    case SDL_WINDOWEVENT:
        if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
            event.window.event == SDL_WINDOWEVENT_RESIZED) {
            // update_viewport necesita la ventana; se resuelve en el bucle.
        }
        break;

    default:
        break;
    }
}

}  // namespace

// Hook fuerte que game_main.cpp llama justo antes de SDL_GL_SwapWindow.
void port_draw_overlay() {
    g_input_manager.DrawHUD();
    port::Menu().Draw();
}

extern "C" int SDL_main(int, char **) {
#ifdef __ANDROID__
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "0");
    // Los toques se procesan por SDL_FINGER*, no como raton sintetico.
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
#endif

    // El audio se inicializa bajo demanda en Ps2AudioSystem (SDL_InitSubSystem
    // dentro de sceSdRemoteInit), para que un fallo de audio no impida jugar.
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS |
                 SDL_INIT_GAMECONTROLLER) != 0) {
        LOGE("SDL_Init: %s", SDL_GetError());
        return 1;
    }

    // Crash handler lo antes posible; en Android escribe en files/crash_log.txt.
    const std::string crash_log_path =
        internal_storage_path() + "/crash_log.txt";
    ps2::crash::Install(crash_log_path.c_str());
    LOGI("crash log: %s", crash_log_path.c_str());

    // Rutas cdrom0:\... -> SDL_AndroidGetExternalStoragePath()/...
    portfs::initialize();
    iso_probe();

    g_input_manager.Initialize();

    // HLE: sceCdRead usa el mismo IsoReader; scePadRead lee g_pad, que
    // main.cpp actualiza desde los eventos tactiles de SDL2.
    ps2::hle::AttachIsoReader(&g_iso);
    ps2::cd::AttachIsoReader(&g_iso);
    ps2::hle::SetPadState(&g_pad);

    std::array<std::uint8_t, ps2::hle::kCdSectorSize> hle_sector{};
    if (::sceCdRead(16, 1, hle_sector.data(), nullptr) == 1) {
        LOGI("HLE sceCdRead LBA 16: %.5s",
             reinterpret_cast<const char *>(hle_sector.data() + 1));
    } else {
        LOGE("HLE sceCdRead LBA 16: %s", ps2::hle::LastCdError().c_str());
    }

    std::uint8_t hle_pad[32]{};
    if (::scePadRead(0, 0, hle_pad) == 1) {
        LOGI("HLE scePadRead: id=0x%02X botones=0x%02X%02X",
             hle_pad[1], hle_pad[2], hle_pad[3]);
    }

#if defined(PORT_STORAGE_SELFTEST)
    port_storage_self_test();
#endif

    SDL_Window *window = nullptr;
    SDL_GLContext context = nullptr;
    if (!create_window_and_context(window, context)) {
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
    update_viewport(window);

    // Menu flotante ImGui (tres dedos / F3).
    if (!port::Menu().Initialize(window, context, overlay_directory())) {
        LOGE("ImGui no pudo inicializarse; el juego continua sin menu");
    }
    port::Menu().SetJumpCallback([](const port::OverlayEntry &entry) {
        game::RequestOverlayJump(entry.index, entry.base, entry.stamp,
                                 entry.name);
    });

    // Memory Card en <internal storage>/memcard0. Se inicializa antes de
    // game::Initialize porque FUN_00149BF0 llama a sceMcInit.
    const std::string mc_base = internal_storage_path();
    if (!ps2::mc::System().Initialize(mc_base)) {
        LOGE("Memory Card: no se pudo inicializar en %s", mc_base.c_str());
    }

    // Traduccion de FUN_00149BF0 + arranque de la cola del cargador.
    if (!game::Initialize(window, overlay_directory())) {
        LOGE("game::Initialize fallo");
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    LOGI("Juego inicializado: estado de carga %u", game::CurrentLoadState());

    // Punteros EE para sceSdTransToIOP (bancos de efectos/musica).
    ps2::audio::SetEeMemory(game::Ram(), game::RamSize());
    // Punteros EE para nombres/resultados de la Memory Card.
    ps2::mc::SetEeMemory(game::Ram(), game::RamSize());

    // La HLE resuelve las direcciones PS2 de los tags DMA en la misma RAM.
    ps2::hle::AttachMemory(game::Ram(), game::RamSize());

    // SaveQuickState/LoadQuickState: memoria y estado del port.
    ps2::state::SetStorageDirectory(internal_storage_path());
    ps2::state::SetGameMemory(game::Ram(), game::RamSize());
    ps2::state::SetGsVram(
        ps2::gif::VramDataMutable(),
        static_cast<std::uint32_t>(ps2::gif::VramSize()));
    ps2::hle::SetDmaSendHook([](const ps2::hle::DmaTransfer &dma) {
        static int logged = 0;
        if (logged < 3) {
            ++logged;
            LOGI("HLE DMA: %s canal=0x%08X source=0x%08X tags=%zu parsed=%d",
                 ps2::hle::DmaChannelName(dma.channel), dma.channel_address,
                 dma.source, dma.tags.size(), dma.parsed ? 1 : 0);
            if (!dma.tags.empty()) {
                const ps2::hle::DmaTag &tag = dma.tags.front();
                LOGI("HLE DMA tag: id=%u qwc=%u data=0x%08X valid=%d",
                     tag.id, tag.qwords, tag.data_address,
                     tag.data_valid ? 1 : 0);
            }
        }
        // GIF Packet Parser: primero intenta leer la cadena desde ps2_ram.
        // Si el DMA se preparo en game::Ram (p. ej. el blob estatico), usa
        // los tags ya resueltos por ps2_hle.
        if (dma.channel == ps2::hle::DmaChannel::Gif &&
            !ps2::gif::SubmitChain(dma.source)) {
            for (const ps2::hle::DmaTag &tag : dma.tags) {
                if (!tag.data_valid) continue;
                const std::uint8_t *data = ps2::hle::TagData(tag);
                if (data != nullptr) {
                    ps2::gif::SubmitPacket(data, ps2::hle::TagBytes(tag));
                }
            }
        }

        // VIF1: UNPACK de matrices/vertices/normales/UV + MSCAL.
        // Cada capture corresponde a un MSCAL/MSCNT; se dibuja en orden.
        if (dma.channel == ps2::hle::DmaChannel::Vif1) {
            ProcessVif1Transfer(dma.source, &dma.tags);
        }
    });

    // FUN_001495D0 no llama a sceDmaSend: escribe D1_MADR/TADR/QWC/CHCR a
    // mano. Este hook captura el arranque de la cadena VIF1 y la alimenta al
    // mismo parser. CHCR.STR = 0x100.
    ps2::Ps2Memory::SetHardwareWriteHook(
        [](const ps2::Ps2Memory::HardwareAccess &access) {
            if (access.address != 0x10009000u || access.size < 4u) return;
            const std::uint32_t chcr =
                static_cast<std::uint32_t>(access.value);
            if ((chcr & 0x100u) == 0u) return;

            const std::uint32_t raw_tadr =
                ps2::Ps2Memory::Read<std::uint32_t>(0x10009030u);
            const std::uint32_t tadr = raw_tadr & 0x1FFFFFFFu;

            static int logs = 0;
            if (logs < 8) {
                ++logs;
                LOGI("D1_CHCR=0x%08X TADR=0x%08X -> VIF1", chcr, tadr);
            }

            std::vector<ps2::hle::DmaTag> tags;
            const bool walked =
                ps2::hle::WalkDmaChain(tadr, tags, 4096);
            if (walked) {
                ProcessVif1Transfer(0u, &tags);
            } else {
                ProcessVif1Transfer(tadr, nullptr);
            }
        });

    bool running = true;
    bool paused = false;
    bool resumed = false;
    // Divisor NTSC: 2 -> 30 Hz, 1 -> 60 Hz desbloqueados. El delta time se
    // ajusta al mismo periodo para los hooks que escalan movimiento.
    int frame_divisor = port::settings::Unlocked60Fps() ? 1 : 2;
    double target_fps = 60.0 / static_cast<double>(frame_divisor);
    FramePacer pacer(target_fps);
    double last_target_fps = target_fps;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            handle_event(event, running, paused, resumed);
            if (event.type == SDL_WINDOWEVENT &&
                (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                 event.window.event == SDL_WINDOWEVENT_RESIZED)) {
                update_viewport(window);
            }
        }
        if (!running) {
            game::RequestExit();
            break;
        }

        if (resumed) {
            resumed = false;
            paused = false;
            pacer.reset();
        }

        if (paused) {
            SDL_Delay(16);
            continue;
        }

        frame_divisor = port::settings::Unlocked60Fps() ? 1 : 2;
        target_fps = 60.0 / static_cast<double>(frame_divisor);
        if (target_fps != last_target_fps) {
            pacer.set_fps(target_fps);
            last_target_fps = target_fps;
            LOGI("frame divisor=%d target=%.0f fps delta=%.4f",
                 frame_divisor, target_fps, 1.0 / target_fps);
        }

        // Mando: SDL_GameController si hay uno; si no, HUD tactil.
        g_input_manager.Update();
        g_pad = g_input_manager.Pad();

        static bool previous_start = false;
        const bool start = g_pad.pressed(port::PadButton::Start);
        if (start && !previous_start) {
            LOGI("Start tactil: loader=%u", game::CurrentLoadState());
        }
        previous_start = start;

        g_game_delta_time = static_cast<float>(1.0 / target_fps);

        // Cinemática .PSS: mientras este activa, tiene prioridad sobre el
        // frame del juego. El toque la salta (Game_TouchDown).
        if (GlobalPssVideoPlayer().IsPlaying()) {
            const bool alive =
                GlobalPssVideoPlayer().InitializeGL() &&
                GlobalPssVideoPlayer().Update();
            if (alive) {
                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                GlobalPssVideoPlayer().Render(g_drawable_width,
                                              g_drawable_height);
            } else {
                GlobalPssVideoPlayer().Close();
            }
            g_input_manager.DrawHUD();
            port::Menu().Draw();
            SDL_GL_SwapWindow(window);
            pacer.wait();
            continue;
        }

        // Game_Render limpia la pantalla. Los hooks DMA encolan fondos GIF y
        // modelos VU1; port_vsync ejecuta el pipeline 2D -> depth -> 3D antes
        // de SDL_GL_SwapWindow. El HUD se dibuja en port_draw_overlay().
        Game_Render();
        if (!game::Frame(window)) break;
        pacer.wait();
    }

    ps2::render::Shutdown();
    ps2::gif::Shutdown();
    ps2::audio::System().Shutdown();
    ps2::mc::System().Shutdown();
    g_input_manager.Shutdown();
    port::Menu().Shutdown();
    ps2::crash::Shutdown();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
