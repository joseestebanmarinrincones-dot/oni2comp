#pragma once

// Reproductor de cinemáticas .PSS (MPEG-2 Program Stream de PS2).
//
// Backend: FFmpeg (libavformat/libavcodec/libswscale/libswresample).
// Se activa con -DPORT_ENABLE_FFMPEG=ON y -DPORT_FFMPEG_ROOT=<ruta>.
// Sin FFmpeg el modulo compila como stub y OpenFile/OpenFromIso devuelven
// false con LastError() explicando que falta la dependencia.
//
// Uso en el hilo con contexto GL:
//
//   PssVideoPlayer player;
//   player.OpenFromIso(&iso, lba, size);
//   player.InitializeGL();
//   while (player.IsPlaying()) {
//       if (!player.Update()) break;
//       player.Render(drawable_w, drawable_h);
//   }
//   player.Close();
//
// El audio se encola con SDL_QueueAudio() en un device SDL abierto en modo
// cola (callback == nullptr). Si no se pasa un device, el player abre uno
// propio a 48000 Hz, S16, estéreo.

#include <cstdint>
#include <memory>
#include <string>

class IsoReader;

class PssVideoPlayer {
public:
    using TextureId = std::uint32_t;

    PssVideoPlayer();
    ~PssVideoPlayer();

    PssVideoPlayer(const PssVideoPlayer &) = delete;
    PssVideoPlayer &operator=(const PssVideoPlayer &) = delete;

    // Abre un .pss directo del filesystem.
    bool OpenFile(const std::string &path);

    // Abre un .pss dentro de disc.iso.
    // lba: sector inicial (2048 bytes). size: tamaño en bytes del PSS.
    bool OpenFromIso(IsoReader *iso, std::uint32_t lba, std::uint64_t size);

    // Device SDL ya abierto en modo cola. Si no se llama, el player abre uno.
    void SetAudioDevice(std::uint32_t device);

    // Debe llamarse en el hilo con contexto GLES 3.0 actual.
    bool InitializeGL();
    void ShutdownGL();

    // Decodifica/avanza. Sube el frame actual con glTexSubImage2D.
    // Devuelve false al terminar, al hacer skip o si no hay backend.
    bool Update();

    // Dibuja el frame actual a pantalla completa (letterbox).
    void Render(int screen_width, int screen_height);

    // Touch para saltar la cinemática.
    void RequestSkip();
    bool SkipRequested() const;

    bool IsPlaying() const;
    void Close();

    TextureId Texture() const;
    int Width() const;
    int Height() const;
    double Duration() const;
    double Position() const;

    const std::string &LastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Instancia global para que el codigo del juego y el handler tactil puedan
// abrir/saltar la cinematica sin duplicar el player.
PssVideoPlayer &GlobalPssVideoPlayer();
