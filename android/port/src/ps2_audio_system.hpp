#pragma once

// Sistema de audio nativo para sustituir al SPU2/IOP de PS2 en el port.
//
// El juego habla con LIBSD.IRX por RPC: inicializa el SPU2, sube bancos de
// efectos a su RAM de 2 MB y reproduce voces ADPCM. La musica y las voces de
// cinematica se leen de disc.iso en bloques de 16 bytes.
//
// Esta clase implementa el lado IOP:
//   - RAM SPU2 de 2 MB (direcciones de 21 bits).
//   - 2 cores x 24 voces con pitch, volumen L/R, loop y ADSR simplificado.
//   - Decodificador PS2/PSX ADPCM: 16 bytes = 1 cabecera + 1 flags + 14 bytes
//     de nibbles (28 muestras).
//   - Cola de streaming para musica/voces ADPCM que llegan de disc.iso.
//   - Mezcla final en PCM S16 estereo a 48000 Hz dentro del callback de
//     SDL_OpenAudioDevice.
//
// El header no depende de SDL: `RenderPcm()` se puede usar en pruebas de PC
// sin abrir dispositivo. `Initialize()` solo abre SDL si el .cpp se compilo
// con PS2_AUDIO_USE_SDL (lo define el CMake de Android).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace ps2::audio {

// Registros libsd (libsd-common.h). La entrada de voz es
//     SD_VOICE(core, voice) | (parametro << 8)
// con core 0/1 y voice 0..23.
namespace sd {

constexpr std::uint16_t Voice(int core, int voice) {
    return static_cast<std::uint16_t>(core | (voice << 1));
}

constexpr std::uint16_t kVParamVoll = 0x00u << 8;
constexpr std::uint16_t kVParamVolr = 0x01u << 8;
constexpr std::uint16_t kVParamPitch = 0x02u << 8;
constexpr std::uint16_t kVParamAdsr1 = 0x03u << 8;
constexpr std::uint16_t kVParamAdsr2 = 0x04u << 8;
constexpr std::uint16_t kVParamEnvx = 0x05u << 8;
constexpr std::uint16_t kVParamVolxl = 0x06u << 8;
constexpr std::uint16_t kVParamVolxr = 0x07u << 8;

constexpr std::uint16_t kParamMmix = 0x08u << 8;
constexpr std::uint16_t kParamMvoll = (0x09u << 8) | 0x80u;
constexpr std::uint16_t kParamMvolr = (0x0Au << 8) | 0x80u;
constexpr std::uint16_t kParamEvoll = (0x0Bu << 8) | 0x80u;
constexpr std::uint16_t kParamEvolr = (0x0Cu << 8) | 0x80u;
constexpr std::uint16_t kParamAvoll = (0x0Du << 8) | 0x80u;
constexpr std::uint16_t kParamAvolr = (0x0Eu << 8) | 0x80u;
constexpr std::uint16_t kParamBvoll = (0x0Fu << 8) | 0x80u;
constexpr std::uint16_t kParamBvolr = (0x10u << 8) | 0x80u;

constexpr std::uint16_t kSwitchPmon = 0x13u << 8;
constexpr std::uint16_t kSwitchNon = 0x14u << 8;
constexpr std::uint16_t kSwitchKon = 0x15u << 8;
constexpr std::uint16_t kSwitchKoff = 0x16u << 8;
constexpr std::uint16_t kSwitchEndx = 0x17u << 8;
constexpr std::uint16_t kSwitchVmixl = 0x18u << 8;
constexpr std::uint16_t kSwitchVmixel = 0x19u << 8;
constexpr std::uint16_t kSwitchVmixr = 0x1Au << 8;
constexpr std::uint16_t kSwitchVmixer = 0x1Bu << 8;

constexpr std::uint16_t kAddrEsa = 0x1Cu << 8;
constexpr std::uint16_t kAddrEea = 0x1Du << 8;
constexpr std::uint16_t kAddrTsa = 0x1Eu << 8;
constexpr std::uint16_t kAddrIrqa = 0x1Fu << 8;
constexpr std::uint16_t kVAddrSsa = 0x20u << 8;
constexpr std::uint16_t kVAddrLsax = 0x21u << 8;
constexpr std::uint16_t kVAddrNax = 0x22u << 8;

constexpr std::uint16_t kCoreEffectEnable = 0x2u;
constexpr std::uint16_t kCoreIrqEnable = 0x4u;
constexpr std::uint16_t kCoreMuteEnable = 0x6u;
constexpr std::uint16_t kCoreNoiseClk = 0x8u;
constexpr std::uint16_t kCoreSpdifMode = 0xAu;

constexpr std::uint8_t kAdpcmLoopStart = 4;
constexpr std::uint8_t kAdpcmLoop = 2;
constexpr std::uint8_t kAdpcmLoopEnd = 1;

}  // namespace sd

class Ps2AudioSystem {
public:
    static constexpr int kSampleRate = 48000;
    static constexpr int kChannels = 2;
    static constexpr int kCores = 2;
    static constexpr int kVoicesPerCore = 24;
    static constexpr int kVoices = kCores * kVoicesPerCore;
    static constexpr std::uint32_t kSpu2RamSize = 2u * 1024u * 1024u;
    static constexpr std::size_t kAdpcmBlockBytes = 16;
    static constexpr int kSamplesPerBlock = 28;

    // Estado interno (definido en ps2_audio_system.cpp). Es publico solo
    // para que las funciones auxiliares del .cpp puedan nombrarlo; los
    // campos siguen siendo privados.
    struct Impl;

    // Un bloque PS2 ADPCM ya decodificado.
    struct AdpcmBlock {
        std::uint8_t shift = 0;    // nibble bajo de la cabecera
        std::uint8_t filter = 0;   // nibble alto de la cabecera (0..4)
        std::uint8_t flags = 0;    // loop start / loop / loop end
        std::int16_t samples[kSamplesPerBlock]{};
    };

    Ps2AudioSystem();
    ~Ps2AudioSystem();

    Ps2AudioSystem(const Ps2AudioSystem &) = delete;
    Ps2AudioSystem &operator=(const Ps2AudioSystem &) = delete;

    // Abre el dispositivo SDL (S16, estereo, 48000 Hz). Es idempotente.
    bool Initialize();
    void Shutdown();
    bool IsInitialized() const;
    std::uint32_t DeviceId() const;

    // Decodifica un bloque de 16 bytes. La segunda version conserva
    // hist1/hist2 entre bloques (prediccion ADPCM).
    static AdpcmBlock DecodeBlock(const std::uint8_t *block);
    static void DecodeBlock(const std::uint8_t *block, std::int16_t *out,
                            int &hist1, int &hist2);

    // Borra voces, RAM SPU2 y cola de streaming.
    void Reset();

    // RAM SPU2 (2 MB, direcciones de 21 bits).
    void WriteSpu2(std::uint32_t address, const void *data, std::size_t bytes);
    void ReadSpu2(std::uint32_t address, void *out, std::size_t bytes) const;

    // API libsd. Las entradas usan el formato SD_VOICE(core, voice) | param.
    void SetParam(std::uint16_t entry, std::uint16_t value);
    std::uint16_t GetParam(std::uint16_t entry) const;
    void SetSwitch(std::uint16_t entry, std::uint32_t value);
    std::uint32_t GetSwitch(std::uint16_t entry) const;
    void SetAddr(std::uint16_t entry, std::uint32_t value);
    std::uint32_t GetAddr(std::uint16_t entry) const;
    void SetCoreAttr(std::uint16_t entry, std::uint16_t value);
    std::uint16_t GetCoreAttr(std::uint16_t entry) const;

    // Transferencias de bancos. TSA la fija SetAddr(SD_ADDR_TSA, ...).
    void SetTransferAddress(std::uint32_t spu_address);
    std::uint32_t TransferAddress() const;
    void UploadVoices(const void *data, std::size_t bytes);
    void UploadBlock(const void *data, std::size_t bytes);

    // Control directo de una voz (indice global 0..47).
    void KeyOn(int voice);
    void KeyOff(int voice);
    void SetVoiceVolume(int voice, std::int16_t left, std::int16_t right);
    void SetVoicePitch(int voice, std::uint16_t pitch);
    void SetVoiceLoop(int voice, std::uint32_t loop_address);
    bool IsVoiceActive(int voice) const;

    void SetMasterVolume(std::int16_t left, std::int16_t right);
    void SetMuted(bool muted);

    // Streaming de musica/voces leidas de disc.iso. `adpcm` debe contener
    // bloques completos de 16 bytes. Es seguro llamarlo desde el hilo de
    // carga mientras el callback mezcla.
    bool QueueStream(const void *adpcm, std::size_t bytes);
    void ClearStream();
    void SetStreamLoop(bool loop);
    void SetStreamVolume(float left, float right);
    void SetStreamPaused(bool paused);
    std::size_t StreamQueuedBytes() const;

    // Mezcla `frames` marcos S16 estereo. RenderPcm bloquea; RenderRealtime
    // (usado por el callback) nunca espera: si el hilo de juego esta
    // actualizando el estado, devuelve silencio en ese periodo.
    void RenderPcm(std::int16_t *out, std::size_t frames);
    void RenderRealtime(std::int16_t *out, std::size_t frames);

    // Instantanea de SPU2 RAM, voces, envolventes y stream. No guarda el
    // dispositivo SDL abierto; restaurar no corta el audio.
    void SaveState(std::vector<std::uint8_t> &out) const;
    bool LoadState(const std::uint8_t *data, std::size_t size);

private:
    std::unique_ptr<Impl> impl_;
};

// Instancia global usada por la HLE de libsd.
Ps2AudioSystem &System();

}  // namespace ps2::audio
