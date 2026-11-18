// main.cpp mínimo para Android: SDL2 (video + audio) y OpenGL ES 3.0.
//
// - Ventana a pantalla completa.
// - Contexto OpenGL ES 3.0.
// - Bucle a 60 FPS.
// - Limpia la pantalla en azul oscuro.
// - Procesa SDL_QUIT y SDL_FINGERDOWN.
//
// Compilar dentro de un proyecto NDK/CMake que enlace:
//   SDL2, GLESv3, EGL, android, log
// y genere libmain.so.

#include <SDL.h>
#include <GLES3/gl3.h>

#include <cstdint>
#include <thread>

namespace {

constexpr double kTargetFps = 60.0;
constexpr double kFrameTime = 1.0 / kTargetFps;

void log_gl_info() {
    const GLubyte *version = glGetString(GL_VERSION);
    const GLubyte *renderer = glGetString(GL_RENDERER);
    SDL_Log("OpenGL ES: %s", version ? reinterpret_cast<const char *>(version) : "?");
    SDL_Log("Renderer:  %s", renderer ? reinterpret_cast<const char *>(renderer) : "?");

    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    if (major < 3) {
        SDL_Log("Aviso: contexto GLES %d.%d, se esperaba 3.0", major, minor);
    }
}

// Limita el bucle a 60 FPS con el reloj de alta resolución de SDL.
// Duerme hasta 1 ms antes del objetivo y hace un giro corto para no pasarse.
class FrameLimiter {
public:
    FrameLimiter()
        : frequency_(SDL_GetPerformanceFrequency()),
          next_(SDL_GetPerformanceCounter()) {}

    void wait() {
        next_ += static_cast<Uint64>(static_cast<double>(frequency_) * kFrameTime);

        const Uint64 now = SDL_GetPerformanceCounter();
        if (next_ > now) {
            const double remaining = static_cast<double>(next_ - now) / static_cast<double>(frequency_);
            if (remaining > 0.001) {
                SDL_Delay(static_cast<Uint32>((remaining - 0.001) * 1000.0));
            }
            while (SDL_GetPerformanceCounter() < next_) {
                std::this_thread::yield();
            }
        } else if (now - next_ > frequency_ / 5) {
            // Nos hemos quedado muy atrás (pausa, carga): no acumules ráfagas.
            next_ = now;
        }
    }

private:
    Uint64 frequency_;
    Uint64 next_;
};

}  // namespace

extern "C" int SDL_main(int, char **) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    SDL_Window *window = SDL_CreateWindow(
        "SDL2 + OpenGL ES 3.0",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720,
        SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_SHOWN);
    if (!window) {
        SDL_Log("SDL_CreateWindow: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) {
        SDL_Log("SDL_GL_CreateContext: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    if (SDL_GL_MakeCurrent(window, context) != 0) {
        SDL_Log("SDL_GL_MakeCurrent: %s", SDL_GetError());
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);

    // El limitador controla los 60 FPS; con vsync pon 1 y quita wait().
    SDL_GL_SetSwapInterval(0);

    int drawable_width = 0, drawable_height = 0;
    SDL_GL_GetDrawableSize(window, &drawable_width, &drawable_height);
    glViewport(0, 0, drawable_width, drawable_height);

    log_gl_info();

    const GLfloat dark_blue[4] = {0.02f, 0.06f, 0.16f, 1.0f};
    glClearColor(dark_blue[0], dark_blue[1], dark_blue[2], dark_blue[3]);

    bool running = true;
    FrameLimiter limiter;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                running = false;
                break;

            case SDL_FINGERDOWN:
                SDL_Log("SDL_FINGERDOWN id=%lld x=%.3f y=%.3f pressure=%.3f",
                        static_cast<long long>(event.tfinger.fingerId),
                        event.tfinger.x, event.tfinger.y, event.tfinger.pressure);
                break;

            default:
                break;
            }
        }

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        SDL_GL_SwapWindow(window);
        limiter.wait();
    }

    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
