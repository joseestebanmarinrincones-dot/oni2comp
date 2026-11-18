#include "ps2_audio_system.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#if defined(PS2_AUDIO_USE_SDL)
#include <SDL.h>
#endif

#if defined(__ANDROID__)
#include <android/log.h>
#define AUDIO_SYS_TAG "OnimushaAudio"
#define AS_LOGI(...) \
    __android_log_print(ANDROID_LOG_INFO, AUDIO_SYS_TAG, __VA_ARGS__)
#define AS_LOGE(...) \
    __android_log_print(ANDROID_LOG_ERROR, AUDIO_SYS_TAG, __VA_ARGS__)
#else
#define AS_LOGI(...)                        \
    do {                                    \
        std::fprintf(stdout, "[Audio] ");   \
        std::fprintf(stdout, __VA_ARGS__);  \
        std::fputc('\n', stdout);           \
        std::fflush(stdout);                \
    } while (0)
#define AS_LOGE(...)                        \
    do {                                    \
        std::fprintf(stderr, "[Audio] ");   \
        std::fprintf(stderr, __VA_ARGS__);  \
        std::fputc('\n', stderr);           \
        std::fflush(stderr);                \
    } while (0)
#endif

#if defined(PS2_AUDIO_USE_SDL)
void SDLCALL Ps2AudioSdlCallback(void *userdata, Uint8 *stream, int len);
#endif

namespace ps2::audio {
namespace {

// Coeficientes de prediccion PSX/PS2 (escalados por 64).
constexpr int kFilterTable[5][2] = {
    {0, 0},
    {60, 0},
    {115, -52},
    {98, -55},
    {122, -60},
};

constexpr std::size_t kMaxStreamBytes = 32u * 1024u * 1024u;
constexpr std::uint32_t kSpu2Mask =
    Ps2AudioSystem::kSpu2RamSize - 1u;

inline std::uint32_t spu2_address(std::uint32_t address) {
    return address & kSpu2Mask;
}

inline std::int16_t clamp_s16(float value) {
    const float clamped = std::clamp(value, -32768.0f, 32767.0f);
    return static_cast<std::int16_t>(std::lround(clamped));
}

// Los registros de volumen del SPU2 usan 0x4000 como unidad (0 dB).
inline float volume_to_float(std::uint16_t value) {
    const int signed_value = static_cast<std::int16_t>(value);
    return std::clamp(static_cast<float>(signed_value) / 16384.0f,
                      -1.0f, 1.0f);
}

enum class EnvPhase : std::uint8_t {
    Off,
    Attack,
    Decay,
    Sustain,
    Release,
};

// Anillo de muestras ya decodificadas. Permite interpolar entre bloques y
// desacoplar el pitch de 48 kHz del tamano fijo de 28 muestras por bloque.
struct SampleRing {
    static constexpr int kCapacity = 128;

    std::int16_t samples[kCapacity]{};
    int read = 0;
    int count = 0;
    double frac = 0.0;
    int hist1 = 0;
    int hist2 = 0;

    void Reset() {
        read = 0;
        count = 0;
        frac = 0.0;
        hist1 = 0;
        hist2 = 0;
    }

    bool Append(const std::int16_t *block, int block_samples) {
        if (block == nullptr || block_samples <= 0) return false;
        if (count + block_samples > kCapacity) return false;
        for (int i = 0; i < block_samples; ++i) {
            samples[(read + count + i) % kCapacity] = block[i];
        }
        count += block_samples;
        return true;
    }

    float Next(double step) {
        if (count <= 0) return 0.0f;

        const int index0 = read;
        const int index1 = (read + 1) % kCapacity;
        float sample = static_cast<float>(samples[index0]);
        if (count > 1) {
            sample += (static_cast<float>(samples[index1]) - sample) *
                      static_cast<float>(frac);
        }

        frac += step;
        while (frac >= 1.0) {
            frac -= 1.0;
            read = (read + 1) % kCapacity;
            --count;
            if (count <= 0) {
                count = 0;
                frac = 0.0;
                break;
            }
        }
        return sample;
    }
};

struct Voice {
    bool active = false;
    bool has_ssa = false;
    bool has_nax = false;
    bool has_loop = false;
    bool end_after_block = false;

    std::uint32_t ssa = 0;
    std::uint32_t lsax = 0;
    std::uint32_t nax = 0;
    std::uint32_t decode_address = 0;

    std::uint16_t pitch = 0x1000;
    std::uint16_t adsr1 = 0;
    std::uint16_t adsr2 = 0;
    std::int16_t voll = 0;
    std::int16_t volr = 0;
    std::uint16_t envx = 0;

    SampleRing ring;

    EnvPhase phase = EnvPhase::Off;
    float env = 0.0f;
    float sustain = 1.0f;
    float attack_step = 0.0f;
    float decay_step = 0.0f;
    float release_step = 0.0f;
};

struct Stream {
    std::vector<std::uint8_t> data;
    std::size_t read = 0;
    std::size_t loop_start = 0;
    bool loop = false;
    bool paused = false;
    bool ended = false;
    float vol_l = 1.0f;
    float vol_r = 1.0f;
    SampleRing ring;
};

}  // namespace

struct Ps2AudioSystem::Impl {
    mutable std::mutex mutex;

    std::array<std::uint8_t, kSpu2RamSize> spu2{};
    std::array<Voice, kVoices> voices{};
    Stream stream;

    std::int16_t master_l = 0x4000;
    std::int16_t master_r = 0x4000;
    bool muted = false;

    std::uint32_t transfer_address = 0;
    std::uint32_t esa[kCores] = {};
    std::uint32_t eea[kCores] = {};
    std::uint32_t irqa = 0;
    std::uint32_t endx[kCores] = {};
    std::uint32_t keyon[kCores] = {};
    std::uint16_t core_attr[16] = {};

    std::uint32_t device = 0;
    bool initialized = false;
    bool owns_audio_subsystem = false;

    int keyon_logs = 0;
    int stream_logs = 0;
};

namespace {

int voice_from_entry(std::uint16_t entry) {
    const int core = entry & 1;
    const int voice = (entry >> 1) & 0x1F;
    if (voice >= Ps2AudioSystem::kVoicesPerCore) return -1;
    return core * Ps2AudioSystem::kVoicesPerCore + voice;
}

void set_endx(Ps2AudioSystem::Impl &impl, int voice, bool value) {
    const int core = voice / Ps2AudioSystem::kVoicesPerCore;
    const int local = voice % Ps2AudioSystem::kVoicesPerCore;
    if (core < 0 || core >= Ps2AudioSystem::kCores) return;
    const std::uint32_t bit = 1u << local;
    if (value) {
        impl.endx[core] |= bit;
        impl.keyon[core] &= ~bit;
    } else {
        impl.endx[core] &= ~bit;
    }
}

void start_adsr(Ps2AudioSystem::Impl &impl, Voice &voice, int index) {
    (void)impl;
    voice.env = 0.0f;
    voice.envx = 0;

    // Si el juego no ha configurado ADSR, la voz suena al instante. Evita
    // que los efectos queden en silencio por registros a cero.
    if (voice.adsr1 == 0) {
        voice.env = 1.0f;
        voice.sustain = 1.0f;
        voice.phase = EnvPhase::Sustain;
        return;
    }

    const int sustain_level = voice.adsr1 & 0x0F;
    voice.sustain = std::clamp(static_cast<float>(sustain_level) / 15.0f,
                               0.0f, 1.0f);
    if (voice.sustain < 0.0001f) voice.sustain = 0.0001f;

    const int attack_rate = (voice.adsr1 >> 8) & 0x7F;
    const float attack_time = 0.003f + (127 - attack_rate) * 0.010f;
    voice.attack_step = 1.0f / (attack_time * Ps2AudioSystem::kSampleRate);

    const int decay_rate = (voice.adsr1 >> 4) & 0x0F;
    const float decay_time = 0.003f + (15 - decay_rate) * 0.020f;
    voice.decay_step =
        (1.0f - voice.sustain) / (decay_time * Ps2AudioSystem::kSampleRate);

    voice.phase = EnvPhase::Attack;
    (void)index;
}

void start_release(Ps2AudioSystem::Impl &impl, Voice &voice, int index) {
    (void)impl;
    if (!voice.active) return;

    if (voice.adsr2 == 0) {
        voice.env = 0.0f;
        voice.phase = EnvPhase::Off;
        voice.active = false;
        set_endx(impl, index, true);
        return;
    }

    const int release_rate = voice.adsr2 & 0x1F;
    const float release_time = 0.003f + (31 - release_rate) * 0.020f;
    const float steps = release_time * Ps2AudioSystem::kSampleRate;
    voice.release_step = steps > 0.0f ? voice.env / steps : voice.env;
    if (voice.release_step <= 0.0f) voice.release_step = voice.env;
    voice.phase = EnvPhase::Release;
}

void key_on_locked(Ps2AudioSystem::Impl &impl, int index) {
    if (index < 0 || index >= Ps2AudioSystem::kVoices) return;
    Voice &voice = impl.voices[static_cast<std::size_t>(index)];

    voice.active = true;
    voice.end_after_block = false;
    voice.ring.Reset();
    voice.env = 0.0f;
    voice.envx = 0;
    const std::uint32_t start =
        voice.has_nax ? voice.nax : (voice.has_ssa ? voice.ssa : voice.nax);
    voice.decode_address = spu2_address(start);
    voice.nax = voice.decode_address;
    voice.has_nax = false;

    if (!voice.has_loop) {
        voice.lsax = voice.decode_address;
        voice.has_loop = true;
    } else {
        voice.lsax = spu2_address(voice.lsax);
    }

    set_endx(impl, index, false);
    const int core = index / Ps2AudioSystem::kVoicesPerCore;
    const int local = index % Ps2AudioSystem::kVoicesPerCore;
    impl.keyon[core] |= (1u << local);

    start_adsr(impl, voice, index);

    if (impl.keyon_logs < 12) {
        ++impl.keyon_logs;
        AS_LOGI("keyon voz=%d core=%d addr=0x%05X pitch=0x%04X",
                local, core, voice.decode_address, voice.pitch);
    }
}

void key_off_locked(Ps2AudioSystem::Impl &impl, int index) {
    if (index < 0 || index >= Ps2AudioSystem::kVoices) return;
    Voice &voice = impl.voices[static_cast<std::size_t>(index)];
    const int core = index / Ps2AudioSystem::kVoicesPerCore;
    const int local = index % Ps2AudioSystem::kVoicesPerCore;
    impl.keyon[core] &= ~(1u << local);
    start_release(impl, voice, index);
}

void advance_envelope(Ps2AudioSystem::Impl &impl, Voice &voice, int index) {
    switch (voice.phase) {
    case EnvPhase::Attack:
        voice.env += voice.attack_step;
        if (voice.env >= 1.0f) {
            voice.env = 1.0f;
            voice.phase = voice.sustain >= 0.999f ? EnvPhase::Sustain
                                                  : EnvPhase::Decay;
        }
        break;
    case EnvPhase::Decay:
        voice.env -= voice.decay_step;
        if (voice.env <= voice.sustain) {
            voice.env = voice.sustain;
            voice.phase = EnvPhase::Sustain;
        }
        break;
    case EnvPhase::Sustain:
        voice.env = voice.sustain;
        break;
    case EnvPhase::Release:
        voice.env -= voice.release_step;
        if (voice.env <= 0.0f) {
            voice.env = 0.0f;
            voice.phase = EnvPhase::Off;
            voice.active = false;
            set_endx(impl, index, true);
        }
        break;
    case EnvPhase::Off:
        break;
    }

    voice.envx = static_cast<std::uint16_t>(
        std::clamp(voice.env, 0.0f, 1.0f) * 32767.0f);
}

bool fetch_voice_block(Ps2AudioSystem::Impl &impl, Voice &voice,
                       std::uint8_t block[Ps2AudioSystem::kAdpcmBlockBytes]) {
    if (!voice.active || voice.end_after_block) return false;

    const std::uint32_t address = spu2_address(voice.decode_address);
    for (std::size_t i = 0; i < Ps2AudioSystem::kAdpcmBlockBytes; ++i) {
        block[i] = impl.spu2[(address + i) & kSpu2Mask];
    }

    voice.nax = address;
    const std::uint8_t flags = block[1] & 0x07;

    if ((flags & sd::kAdpcmLoopStart) != 0) {
        voice.lsax = address;
        voice.has_loop = true;
    }

    std::uint32_t next = spu2_address(address + 16);
    if ((flags & sd::kAdpcmLoopEnd) != 0) {
        if ((flags & sd::kAdpcmLoop) != 0 && voice.has_loop) {
            next = spu2_address(voice.lsax);
        } else {
            voice.end_after_block = true;
        }
    }

    voice.decode_address = next;
    return true;
}

void fill_voice(Ps2AudioSystem::Impl &impl, Voice &voice) {
    while (voice.ring.count <=
           SampleRing::kCapacity - Ps2AudioSystem::kSamplesPerBlock) {
        std::uint8_t block[Ps2AudioSystem::kAdpcmBlockBytes]{};
        if (!fetch_voice_block(impl, voice, block)) break;

        std::int16_t samples[Ps2AudioSystem::kSamplesPerBlock]{};
        Ps2AudioSystem::DecodeBlock(block, samples, voice.ring.hist1,
                                    voice.ring.hist2);
        if (!voice.ring.Append(samples, Ps2AudioSystem::kSamplesPerBlock)) {
            break;
        }
    }
}

float next_voice_sample(Ps2AudioSystem::Impl &impl, Voice &voice, int index) {
    if (!voice.active) return 0.0f;

    fill_voice(impl, voice);
    if (voice.ring.count <= 0) {
        if (voice.end_after_block) {
            voice.active = false;
            voice.phase = EnvPhase::Off;
            set_endx(impl, index, true);
        }
        return 0.0f;
    }

    double step = static_cast<double>(voice.pitch) / 4096.0;
    if (step <= 0.0) step = 1.0;
    return voice.ring.Next(step);
}

bool fetch_stream_block(Stream &stream,
                        std::uint8_t block[Ps2AudioSystem::kAdpcmBlockBytes]) {
    if (stream.read + Ps2AudioSystem::kAdpcmBlockBytes > stream.data.size()) {
        if (stream.loop &&
            stream.loop_start + Ps2AudioSystem::kAdpcmBlockBytes <=
                stream.data.size() &&
            stream.read != stream.loop_start) {
            stream.read = stream.loop_start;
        } else {
            stream.ended = true;
            return false;
        }
    }

    std::memcpy(block, stream.data.data() + stream.read,
                Ps2AudioSystem::kAdpcmBlockBytes);
    stream.read += Ps2AudioSystem::kAdpcmBlockBytes;
    stream.ended = false;
    return true;
}

void fill_stream(Stream &stream) {
    while (stream.ring.count <=
           SampleRing::kCapacity - Ps2AudioSystem::kSamplesPerBlock) {
        std::uint8_t block[Ps2AudioSystem::kAdpcmBlockBytes]{};
        if (!fetch_stream_block(stream, block)) break;

        std::int16_t samples[Ps2AudioSystem::kSamplesPerBlock]{};
        Ps2AudioSystem::DecodeBlock(block, samples, stream.ring.hist1,
                                    stream.ring.hist2);
        if (!stream.ring.Append(samples, Ps2AudioSystem::kSamplesPerBlock)) {
            break;
        }
    }
}

void write_spu2_locked(Ps2AudioSystem::Impl &impl, std::uint32_t address,
                       const void *data, std::size_t bytes) {
    if (data == nullptr || bytes == 0) return;
    const auto *source = static_cast<const std::uint8_t *>(data);
    std::size_t offset = spu2_address(address);

    while (bytes > 0) {
        const std::size_t chunk =
            std::min<std::size_t>(bytes, Ps2AudioSystem::kSpu2RamSize - offset);
        std::memcpy(impl.spu2.data() + offset, source, chunk);
        offset = (offset + chunk) & kSpu2Mask;
        source += chunk;
        bytes -= chunk;
    }
}

void read_spu2_locked(const Ps2AudioSystem::Impl &impl,
                      std::uint32_t address, void *out, std::size_t bytes) {
    if (out == nullptr || bytes == 0) return;
    auto *destination = static_cast<std::uint8_t *>(out);
    std::size_t offset = spu2_address(address);

    while (bytes > 0) {
        const std::size_t chunk =
            std::min<std::size_t>(bytes, Ps2AudioSystem::kSpu2RamSize - offset);
        std::memcpy(destination, impl.spu2.data() + offset, chunk);
        offset = (offset + chunk) & kSpu2Mask;
        destination += chunk;
        bytes -= chunk;
    }
}

}  // namespace

Ps2AudioSystem::Ps2AudioSystem() : impl_(std::make_unique<Impl>()) {}

Ps2AudioSystem::~Ps2AudioSystem() {
    Shutdown();
}

Ps2AudioSystem::AdpcmBlock Ps2AudioSystem::DecodeBlock(
    const std::uint8_t *block) {
    AdpcmBlock decoded{};
    if (block == nullptr) return decoded;

    decoded.shift = static_cast<std::uint8_t>(block[0] & 0x0F);
    decoded.filter = static_cast<std::uint8_t>((block[0] >> 4) & 0x0F);
    decoded.flags = static_cast<std::uint8_t>(block[1] & 0x07);

    int hist1 = 0;
    int hist2 = 0;
    DecodeBlock(block, decoded.samples, hist1, hist2);
    return decoded;
}

void Ps2AudioSystem::DecodeBlock(const std::uint8_t *block,
                                 std::int16_t *out, int &hist1, int &hist2) {
    if (block == nullptr || out == nullptr) return;

    const int shift = block[0] & 0x0F;
    const int filter = (block[0] >> 4) & 0x0F;
    const int *coefficients =
        filter < 5 ? kFilterTable[filter] : kFilterTable[0];

    for (int i = 0; i < kSamplesPerBlock; ++i) {
        const std::uint8_t byte = block[2 + i / 2];
        const int nibble = (i & 1) != 0 ? (byte >> 4) : (byte & 0x0F);

        // El nibble es de 4 bits con signo. El cast a int16 antes del
        // desplazamiento replica el comportamiento del SPU2.
        std::int32_t sample =
            static_cast<std::int32_t>(static_cast<std::int16_t>(nibble << 12));
        sample >>= shift;
        sample += (hist1 * coefficients[0] + hist2 * coefficients[1] + 32) >> 6;
        sample = std::clamp<std::int32_t>(sample, -32768, 32767);

        out[i] = static_cast<std::int16_t>(sample);
        hist2 = hist1;
        hist1 = out[i];
    }
}

void Ps2AudioSystem::Reset() {
    std::lock_guard<std::mutex> lock(impl_->mutex);

    impl_->spu2.fill(0);
    for (Voice &voice : impl_->voices) voice = Voice{};
    impl_->stream = Stream{};
    impl_->master_l = 0x4000;
    impl_->master_r = 0x4000;
    impl_->muted = false;
    impl_->transfer_address = 0;
    std::fill(std::begin(impl_->esa), std::end(impl_->esa), 0);
    std::fill(std::begin(impl_->eea), std::end(impl_->eea), 0);
    std::fill(std::begin(impl_->endx), std::end(impl_->endx), 0);
    std::fill(std::begin(impl_->keyon), std::end(impl_->keyon), 0);
    std::fill(std::begin(impl_->core_attr), std::end(impl_->core_attr), 0);
    impl_->keyon_logs = 0;
    impl_->stream_logs = 0;
}

void Ps2AudioSystem::WriteSpu2(std::uint32_t address, const void *data,
                               std::size_t bytes) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    write_spu2_locked(*impl_, address, data, bytes);
}

void Ps2AudioSystem::ReadSpu2(std::uint32_t address, void *out,
                              std::size_t bytes) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    read_spu2_locked(*impl_, address, out, bytes);
}

void Ps2AudioSystem::SetParam(std::uint16_t entry, std::uint16_t value) {
    std::lock_guard<std::mutex> lock(impl_->mutex);

    if ((entry & 0x80) != 0) {
        switch (entry >> 8) {
        case 0x09:
            impl_->master_l = static_cast<std::int16_t>(value);
            break;
        case 0x0A:
            impl_->master_r = static_cast<std::int16_t>(value);
            break;
        default:
            break;
        }
        return;
    }

    const int index = voice_from_entry(entry);
    if (index < 0) return;
    Voice &voice = impl_->voices[static_cast<std::size_t>(index)];

    switch (entry >> 8) {
    case 0x00:
        voice.voll = static_cast<std::int16_t>(value);
        break;
    case 0x01:
        voice.volr = static_cast<std::int16_t>(value);
        break;
    case 0x02:
        voice.pitch = static_cast<std::uint16_t>(value & 0x3FFF);
        break;
    case 0x03:
        voice.adsr1 = value;
        break;
    case 0x04:
        voice.adsr2 = value;
        break;
    case 0x05:
        // ENVX es de solo lectura en el hardware.
        break;
    default:
        break;
    }
}

std::uint16_t Ps2AudioSystem::GetParam(std::uint16_t entry) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);

    if ((entry & 0x80) != 0) {
        switch (entry >> 8) {
        case 0x09:
            return static_cast<std::uint16_t>(impl_->master_l);
        case 0x0A:
            return static_cast<std::uint16_t>(impl_->master_r);
        default:
            return 0;
        }
    }

    const int index = voice_from_entry(entry);
    if (index < 0) return 0;
    const Voice &voice = impl_->voices[static_cast<std::size_t>(index)];

    switch (entry >> 8) {
    case 0x00:
        return static_cast<std::uint16_t>(voice.voll);
    case 0x01:
        return static_cast<std::uint16_t>(voice.volr);
    case 0x02:
        return voice.pitch;
    case 0x03:
        return voice.adsr1;
    case 0x04:
        return voice.adsr2;
    case 0x05:
        return voice.envx;
    default:
        return 0;
    }
}

void Ps2AudioSystem::SetSwitch(std::uint16_t entry, std::uint32_t value) {
    std::lock_guard<std::mutex> lock(impl_->mutex);

    const int core = entry & 1;
    const std::uint32_t mask = value & 0x00FFFFFFu;

    switch (entry >> 8) {
    case 0x15:  // KON
        for (int voice = 0; voice < kVoicesPerCore; ++voice) {
            if ((mask & (1u << voice)) != 0) {
                key_on_locked(*impl_, core * kVoicesPerCore + voice);
            }
        }
        break;
    case 0x16:  // KOFF
        for (int voice = 0; voice < kVoicesPerCore; ++voice) {
            if ((mask & (1u << voice)) != 0) {
                key_off_locked(*impl_, core * kVoicesPerCore + voice);
            }
        }
        break;
    case 0x17:  // ENDX: escribir bits los reconoce/limpia
        impl_->endx[core] &= ~mask;
        break;
    default:
        break;
    }
}

std::uint32_t Ps2AudioSystem::GetSwitch(std::uint16_t entry) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const int core = entry & 1;
    if (core < 0 || core >= kCores) return 0;

    switch (entry >> 8) {
    case 0x15:
        return impl_->keyon[core];
    case 0x17:
        return impl_->endx[core];
    default:
        return 0;
    }
}

void Ps2AudioSystem::SetAddr(std::uint16_t entry, std::uint32_t value) {
    std::lock_guard<std::mutex> lock(impl_->mutex);

    const int command = entry >> 8;
    const int core = entry & 1;

    switch (command) {
    case 0x1C:
        impl_->esa[core] = value;
        return;
    case 0x1D:
        impl_->eea[core] = value;
        return;
    case 0x1E:
        impl_->transfer_address = spu2_address(value);
        return;
    case 0x1F:
        impl_->irqa = value;
        return;
    default:
        break;
    }

    const int index = voice_from_entry(entry);
    if (index < 0) return;
    Voice &voice = impl_->voices[static_cast<std::size_t>(index)];

    switch (command) {
    case 0x20:  // SSA: inicio de la voz
        voice.ssa = spu2_address(value);
        voice.has_ssa = true;
        voice.has_nax = false;
        if (!voice.active) voice.nax = voice.ssa;
        break;
    case 0x21:  // LSAX: inicio del loop
        voice.lsax = spu2_address(value);
        voice.has_loop = true;
        break;
    case 0x22:  // NAX: direccion actual
        voice.nax = spu2_address(value);
        voice.has_nax = true;
        voice.decode_address = voice.nax;
        voice.end_after_block = false;
        voice.ring.Reset();
        break;
    default:
        break;
    }
}

std::uint32_t Ps2AudioSystem::GetAddr(std::uint16_t entry) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);

    const int command = entry >> 8;
    const int core = entry & 1;

    switch (command) {
    case 0x1C:
        return impl_->esa[core];
    case 0x1D:
        return impl_->eea[core];
    case 0x1E:
        return impl_->transfer_address;
    case 0x1F:
        return impl_->irqa;
    default:
        break;
    }

    const int index = voice_from_entry(entry);
    if (index < 0) return 0;
    const Voice &voice = impl_->voices[static_cast<std::size_t>(index)];

    switch (command) {
    case 0x20:
        return voice.ssa;
    case 0x21:
        return voice.lsax;
    case 0x22:
        return voice.nax;
    default:
        return 0;
    }
}

void Ps2AudioSystem::SetCoreAttr(std::uint16_t entry, std::uint16_t value) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const std::size_t index = static_cast<std::size_t>(entry & 0x0F);
    impl_->core_attr[index] = value;
    if (entry == sd::kCoreMuteEnable) {
        impl_->muted = value != 0;
    }
}

std::uint16_t Ps2AudioSystem::GetCoreAttr(std::uint16_t entry) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const std::size_t index = static_cast<std::size_t>(entry & 0x0F);
    return impl_->core_attr[index];
}

void Ps2AudioSystem::SetTransferAddress(std::uint32_t spu_address) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->transfer_address = spu2_address(spu_address);
}

std::uint32_t Ps2AudioSystem::TransferAddress() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->transfer_address;
}

void Ps2AudioSystem::UploadVoices(const void *data, std::size_t bytes) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    write_spu2_locked(*impl_, impl_->transfer_address, data, bytes);
    impl_->transfer_address =
        spu2_address(impl_->transfer_address + static_cast<std::uint32_t>(bytes));
}

void Ps2AudioSystem::UploadBlock(const void *data, std::size_t bytes) {
    UploadVoices(data, bytes);
}

void Ps2AudioSystem::KeyOn(int voice) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    key_on_locked(*impl_, voice);
}

void Ps2AudioSystem::KeyOff(int voice) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    key_off_locked(*impl_, voice);
}

void Ps2AudioSystem::SetVoiceVolume(int voice, std::int16_t left,
                                    std::int16_t right) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (voice < 0 || voice >= kVoices) return;
    impl_->voices[static_cast<std::size_t>(voice)].voll = left;
    impl_->voices[static_cast<std::size_t>(voice)].volr = right;
}

void Ps2AudioSystem::SetVoicePitch(int voice, std::uint16_t pitch) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (voice < 0 || voice >= kVoices) return;
    impl_->voices[static_cast<std::size_t>(voice)].pitch =
        static_cast<std::uint16_t>(pitch & 0x3FFF);
}

void Ps2AudioSystem::SetVoiceLoop(int voice, std::uint32_t loop_address) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (voice < 0 || voice >= kVoices) return;
    Voice &v = impl_->voices[static_cast<std::size_t>(voice)];
    v.lsax = spu2_address(loop_address);
    v.has_loop = true;
}

bool Ps2AudioSystem::IsVoiceActive(int voice) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (voice < 0 || voice >= kVoices) return false;
    return impl_->voices[static_cast<std::size_t>(voice)].active;
}

void Ps2AudioSystem::SetMasterVolume(std::int16_t left, std::int16_t right) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->master_l = left;
    impl_->master_r = right;
}

void Ps2AudioSystem::SetMuted(bool muted) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->muted = muted;
}

bool Ps2AudioSystem::QueueStream(const void *adpcm, std::size_t bytes) {
    if (adpcm == nullptr || bytes == 0) return false;
    if ((bytes % kAdpcmBlockBytes) != 0) {
        AS_LOGE("QueueStream: %zu bytes no es multiplo de 16", bytes);
        return false;
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
    Stream &stream = impl_->stream;

    if (!stream.loop) {
        if (stream.read >= stream.data.size()) {
            stream.data.clear();
            stream.read = 0;
            stream.loop_start = 0;
            stream.ended = false;
        } else if (stream.read >= 256u * 1024u) {
            // Descarta el prefijo ya consumido para que un stream largo sin
            // loop no crezca hasta el limite de 32 MB.
            stream.data.erase(stream.data.begin(),
                              stream.data.begin() +
                                  static_cast<std::ptrdiff_t>(stream.read));
            stream.read = 0;
            stream.loop_start = 0;
        }
    }

    if (stream.data.size() + bytes > kMaxStreamBytes) {
        AS_LOGE("QueueStream: cola llena (%zu + %zu)", stream.data.size(),
                bytes);
        return false;
    }

    const auto *source = static_cast<const std::uint8_t *>(adpcm);
    stream.data.insert(stream.data.end(), source, source + bytes);

    if (impl_->stream_logs < 8) {
        ++impl_->stream_logs;
        AS_LOGI("stream: +%zu bytes (total=%zu loop=%d)",
                bytes, stream.data.size(), stream.loop ? 1 : 0);
    }
    return true;
}

void Ps2AudioSystem::ClearStream() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->stream = Stream{};
}

void Ps2AudioSystem::SetStreamLoop(bool loop) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    Stream &stream = impl_->stream;
    stream.loop = loop;
    if (loop) {
        stream.loop_start = stream.read;
    } else {
        stream.loop_start = 0;
    }
}

void Ps2AudioSystem::SetStreamVolume(float left, float right) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->stream.vol_l = std::clamp(left, -1.0f, 1.0f);
    impl_->stream.vol_r = std::clamp(right, -1.0f, 1.0f);
}

void Ps2AudioSystem::SetStreamPaused(bool paused) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->stream.paused = paused;
}

std::size_t Ps2AudioSystem::StreamQueuedBytes() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const Stream &stream = impl_->stream;
    if (stream.read >= stream.data.size()) return 0;
    return stream.data.size() - stream.read;
}

bool Ps2AudioSystem::Initialize() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->initialized) return true;

#if defined(PS2_AUDIO_USE_SDL)
    if (SDL_WasInit(SDL_INIT_AUDIO) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            AS_LOGE("SDL_InitSubSystem(audio): %s", SDL_GetError());
            return false;
        }
        impl_->owns_audio_subsystem = true;
    }

    SDL_AudioSpec want{};
    SDL_AudioSpec have{};
    want.freq = kSampleRate;
    want.format = AUDIO_S16SYS;
    want.channels = kChannels;
    want.samples = 1024;
    want.callback = &Ps2AudioSdlCallback;
    want.userdata = this;

    impl_->device =
        SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (impl_->device == 0) {
        AS_LOGE("SDL_OpenAudioDevice: %s", SDL_GetError());
        if (impl_->owns_audio_subsystem) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            impl_->owns_audio_subsystem = false;
        }
        return false;
    }

    if (have.freq != kSampleRate || have.channels != kChannels ||
        have.format != AUDIO_S16SYS) {
        AS_LOGE("audio no coincide: %d Hz %d canales fmt=0x%04X",
                have.freq, have.channels, have.format);
        SDL_CloseAudioDevice(impl_->device);
        impl_->device = 0;
        if (impl_->owns_audio_subsystem) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            impl_->owns_audio_subsystem = false;
        }
        return false;
    }

    impl_->initialized = true;
    SDL_PauseAudioDevice(impl_->device, 0);
    AS_LOGI("SDL audio: %d Hz, %d canales, %d muestras, dispositivo=%u",
            have.freq, have.channels, have.samples,
            static_cast<unsigned>(impl_->device));
    return true;
#else
    AS_LOGI("audio sin SDL (PS2_AUDIO_USE_SDL no definido); "
            "RenderPcm sigue disponible");
    return false;
#endif
}

void Ps2AudioSystem::Shutdown() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->initialized && impl_->device == 0) return;

#if defined(PS2_AUDIO_USE_SDL)
    if (impl_->device != 0) {
        SDL_PauseAudioDevice(impl_->device, 1);
        SDL_CloseAudioDevice(impl_->device);
        impl_->device = 0;
    }
    if (impl_->owns_audio_subsystem) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        impl_->owns_audio_subsystem = false;
    }
#endif
    impl_->initialized = false;
    AS_LOGI("audio detenido");
}

bool Ps2AudioSystem::IsInitialized() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->initialized;
}

std::uint32_t Ps2AudioSystem::DeviceId() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->device;
}

void Ps2AudioSystem::RenderPcm(std::int16_t *out, std::size_t frames) {
    if (out == nullptr || frames == 0) return;
    std::lock_guard<std::mutex> lock(impl_->mutex);

    const float master_l = volume_to_float(
        static_cast<std::uint16_t>(impl_->master_l));
    const float master_r = volume_to_float(
        static_cast<std::uint16_t>(impl_->master_r));

    for (std::size_t frame = 0; frame < frames; ++frame) {
        float left = 0.0f;
        float right = 0.0f;

        for (int index = 0; index < kVoices; ++index) {
            Voice &voice = impl_->voices[static_cast<std::size_t>(index)];
            if (!voice.active) continue;

            const float sample = next_voice_sample(*impl_, voice, index);
            if (!voice.active) continue;

            const float env = voice.env;
            left += sample * env * volume_to_float(
                static_cast<std::uint16_t>(voice.voll));
            right += sample * env * volume_to_float(
                static_cast<std::uint16_t>(voice.volr));
            advance_envelope(*impl_, voice, index);
        }

        if (!impl_->stream.paused) {
            fill_stream(impl_->stream);
            if (impl_->stream.ring.count > 0) {
                const float sample = impl_->stream.ring.Next(1.0);
                left += sample * impl_->stream.vol_l;
                right += sample * impl_->stream.vol_r;
            }
        }

        left *= master_l;
        right *= master_r;
        if (impl_->muted) {
            left = 0.0f;
            right = 0.0f;
        }

        out[frame * 2] = clamp_s16(left);
        out[frame * 2 + 1] = clamp_s16(right);
    }
}

void Ps2AudioSystem::RenderRealtime(std::int16_t *out, std::size_t frames) {
    if (out == nullptr || frames == 0) return;

    std::unique_lock<std::mutex> lock(impl_->mutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        std::memset(out, 0, frames * kChannels * sizeof(std::int16_t));
        return;
    }

    const float master_l = volume_to_float(
        static_cast<std::uint16_t>(impl_->master_l));
    const float master_r = volume_to_float(
        static_cast<std::uint16_t>(impl_->master_r));

    for (std::size_t frame = 0; frame < frames; ++frame) {
        float left = 0.0f;
        float right = 0.0f;

        for (int index = 0; index < kVoices; ++index) {
            Voice &voice = impl_->voices[static_cast<std::size_t>(index)];
            if (!voice.active) continue;

            const float sample = next_voice_sample(*impl_, voice, index);
            if (!voice.active) continue;

            const float env = voice.env;
            left += sample * env * volume_to_float(
                static_cast<std::uint16_t>(voice.voll));
            right += sample * env * volume_to_float(
                static_cast<std::uint16_t>(voice.volr));
            advance_envelope(*impl_, voice, index);
        }

        if (!impl_->stream.paused) {
            fill_stream(impl_->stream);
            if (impl_->stream.ring.count > 0) {
                const float sample = impl_->stream.ring.Next(1.0);
                left += sample * impl_->stream.vol_l;
                right += sample * impl_->stream.vol_r;
            }
        }

        left *= master_l;
        right *= master_r;
        if (impl_->muted) {
            left = 0.0f;
            right = 0.0f;
        }

        out[frame * 2] = clamp_s16(left);
        out[frame * 2 + 1] = clamp_s16(right);
    }
}

namespace {

struct AudioStateWriter {
    std::vector<std::uint8_t> &out;
    void u8(std::uint8_t value) { out.push_back(value); }
    void u16(std::uint16_t value) {
        std::uint8_t bytes[2];
        std::memcpy(bytes, &value, 2);
        out.insert(out.end(), bytes, bytes + 2);
    }
    void u32(std::uint32_t value) {
        std::uint8_t bytes[4];
        std::memcpy(bytes, &value, 4);
        out.insert(out.end(), bytes, bytes + 4);
    }
    void u64(std::uint64_t value) {
        std::uint8_t bytes[8];
        std::memcpy(bytes, &value, 8);
        out.insert(out.end(), bytes, bytes + 8);
    }
    void i16(std::int16_t value) { u16(static_cast<std::uint16_t>(value)); }
    void i32(std::int32_t value) { u32(static_cast<std::uint32_t>(value)); }
    void f32(float value) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, 4);
        u32(bits);
    }
    void f64(double value) {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, 8);
        u64(bits);
    }
    void bytes(const void *data, std::size_t size) {
        const auto *source = static_cast<const std::uint8_t *>(data);
        out.insert(out.end(), source, source + size);
    }
};

struct AudioStateReader {
    const std::uint8_t *data = nullptr;
    std::size_t size = 0;
    std::size_t offset = 0;
    bool ok = true;

    bool can(std::size_t bytes) const {
        return ok && bytes <= size - std::min(offset, size);
    }
    std::uint8_t u8() {
        if (!can(1)) { ok = false; return 0; }
        return data[offset++];
    }
    std::uint16_t u16() {
        if (!can(2)) { ok = false; return 0; }
        std::uint16_t value = 0;
        std::memcpy(&value, data + offset, 2);
        offset += 2;
        return value;
    }
    std::uint32_t u32() {
        if (!can(4)) { ok = false; return 0; }
        std::uint32_t value = 0;
        std::memcpy(&value, data + offset, 4);
        offset += 4;
        return value;
    }
    std::uint64_t u64() {
        if (!can(8)) { ok = false; return 0; }
        std::uint64_t value = 0;
        std::memcpy(&value, data + offset, 8);
        offset += 8;
        return value;
    }
    std::int16_t i16() { return static_cast<std::int16_t>(u16()); }
    std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
    float f32() {
        const std::uint32_t bits = u32();
        float value = 0.0f;
        std::memcpy(&value, &bits, 4);
        return value;
    }
    double f64() {
        const std::uint64_t bits = u64();
        double value = 0.0;
        std::memcpy(&value, &bits, 8);
        return value;
    }
    const std::uint8_t *take(std::size_t bytes) {
        if (!can(bytes)) { ok = false; return nullptr; }
        const std::uint8_t *result = data + offset;
        offset += bytes;
        return result;
    }
};

void save_ring(AudioStateWriter &writer, const SampleRing &ring) {
    for (std::int16_t sample : ring.samples) writer.i16(sample);
    writer.i32(ring.read);
    writer.i32(ring.count);
    writer.f64(ring.frac);
    writer.i32(ring.hist1);
    writer.i32(ring.hist2);
}

bool load_ring(AudioStateReader &reader, SampleRing &ring) {
    for (std::int16_t &sample : ring.samples) sample = reader.i16();
    ring.read = reader.i32();
    ring.count = reader.i32();
    ring.frac = reader.f64();
    ring.hist1 = reader.i32();
    ring.hist2 = reader.i32();
    if (!reader.ok) return false;
    if (ring.read < 0 || ring.read >= SampleRing::kCapacity ||
        ring.count < 0 || ring.count > SampleRing::kCapacity) {
        return false;
    }
    return true;
}

void save_voice(AudioStateWriter &writer, const Voice &voice) {
    writer.u8(voice.active ? 1 : 0);
    writer.u8(voice.has_ssa ? 1 : 0);
    writer.u8(voice.has_nax ? 1 : 0);
    writer.u8(voice.has_loop ? 1 : 0);
    writer.u8(voice.end_after_block ? 1 : 0);
    writer.u32(voice.ssa);
    writer.u32(voice.lsax);
    writer.u32(voice.nax);
    writer.u32(voice.decode_address);
    writer.u16(voice.pitch);
    writer.u16(voice.adsr1);
    writer.u16(voice.adsr2);
    writer.i16(voice.voll);
    writer.i16(voice.volr);
    writer.u16(voice.envx);
    save_ring(writer, voice.ring);
    writer.u8(static_cast<std::uint8_t>(voice.phase));
    writer.f32(voice.env);
    writer.f32(voice.sustain);
    writer.f32(voice.attack_step);
    writer.f32(voice.decay_step);
    writer.f32(voice.release_step);
}

bool load_voice(AudioStateReader &reader, Voice &voice) {
    voice = Voice{};
    voice.active = reader.u8() != 0;
    voice.has_ssa = reader.u8() != 0;
    voice.has_nax = reader.u8() != 0;
    voice.has_loop = reader.u8() != 0;
    voice.end_after_block = reader.u8() != 0;
    voice.ssa = reader.u32();
    voice.lsax = reader.u32();
    voice.nax = reader.u32();
    voice.decode_address = reader.u32();
    voice.pitch = reader.u16();
    voice.adsr1 = reader.u16();
    voice.adsr2 = reader.u16();
    voice.voll = reader.i16();
    voice.volr = reader.i16();
    voice.envx = reader.u16();
    if (!load_ring(reader, voice.ring)) return false;
    const std::uint8_t phase = reader.u8();
    if (phase > static_cast<std::uint8_t>(EnvPhase::Release)) return false;
    voice.phase = static_cast<EnvPhase>(phase);
    voice.env = reader.f32();
    voice.sustain = reader.f32();
    voice.attack_step = reader.f32();
    voice.decay_step = reader.f32();
    voice.release_step = reader.f32();
    return reader.ok;
}

void save_stream(AudioStateWriter &writer, const Stream &stream) {
    writer.u64(static_cast<std::uint64_t>(stream.data.size()));
    if (!stream.data.empty()) {
        writer.bytes(stream.data.data(), stream.data.size());
    }
    writer.u64(static_cast<std::uint64_t>(stream.read));
    writer.u64(static_cast<std::uint64_t>(stream.loop_start));
    writer.u8(stream.loop ? 1 : 0);
    writer.u8(stream.paused ? 1 : 0);
    writer.u8(stream.ended ? 1 : 0);
    writer.f32(stream.vol_l);
    writer.f32(stream.vol_r);
    save_ring(writer, stream.ring);
}

bool load_stream(AudioStateReader &reader, Stream &stream) {
    stream = Stream{};
    const std::uint64_t size = reader.u64();
    if (!reader.ok || size > (64ull << 20)) return false;
    const std::uint8_t *data =
        reader.take(static_cast<std::size_t>(size));
    if (data == nullptr) return false;
    stream.data.assign(data, data + size);
    stream.read = static_cast<std::size_t>(reader.u64());
    stream.loop_start = static_cast<std::size_t>(reader.u64());
    stream.loop = reader.u8() != 0;
    stream.paused = reader.u8() != 0;
    stream.ended = reader.u8() != 0;
    stream.vol_l = reader.f32();
    stream.vol_r = reader.f32();
    if (!load_ring(reader, stream.ring)) return false;
    if (stream.read > stream.data.size() ||
        stream.loop_start > stream.data.size()) {
        return false;
    }
    return reader.ok;
}

}  // namespace

void Ps2AudioSystem::SaveState(std::vector<std::uint8_t> &out) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    AudioStateWriter writer{out};

    writer.bytes(impl_->spu2.data(), impl_->spu2.size());
    writer.u32(static_cast<std::uint32_t>(impl_->voices.size()));
    for (const Voice &voice : impl_->voices) save_voice(writer, voice);
    save_stream(writer, impl_->stream);

    writer.i16(impl_->master_l);
    writer.i16(impl_->master_r);
    writer.u8(impl_->muted ? 1 : 0);
    writer.u32(impl_->transfer_address);
    for (std::uint32_t value : impl_->esa) writer.u32(value);
    for (std::uint32_t value : impl_->eea) writer.u32(value);
    writer.u32(impl_->irqa);
    for (std::uint32_t value : impl_->endx) writer.u32(value);
    for (std::uint32_t value : impl_->keyon) writer.u32(value);
    for (std::uint16_t value : impl_->core_attr) writer.u16(value);
}

bool Ps2AudioSystem::LoadState(const std::uint8_t *data, std::size_t size) {
    if (data == nullptr || size == 0) return false;
    AudioStateReader reader{data, size};

    std::vector<std::uint8_t> spu2(kSpu2RamSize);
    const std::uint8_t *spu2_data = reader.take(spu2.size());
    if (spu2_data == nullptr) return false;
    std::memcpy(spu2.data(), spu2_data, spu2.size());

    const std::uint32_t voice_count = reader.u32();
    if (!reader.ok || voice_count != kVoices) return false;
    std::array<Voice, kVoices> voices{};
    for (Voice &voice : voices) {
        if (!load_voice(reader, voice)) return false;
    }

    Stream stream;
    if (!load_stream(reader, stream)) return false;

    const std::int16_t master_l = reader.i16();
    const std::int16_t master_r = reader.i16();
    const bool muted = reader.u8() != 0;
    const std::uint32_t transfer_address = reader.u32();
    std::uint32_t esa[kCores] = {};
    std::uint32_t eea[kCores] = {};
    for (std::uint32_t &value : esa) value = reader.u32();
    for (std::uint32_t &value : eea) value = reader.u32();
    const std::uint32_t irqa = reader.u32();
    std::uint32_t endx[kCores] = {};
    std::uint32_t keyon[kCores] = {};
    for (std::uint32_t &value : endx) value = reader.u32();
    for (std::uint32_t &value : keyon) value = reader.u32();
    std::uint16_t core_attr[16] = {};
    for (std::uint16_t &value : core_attr) value = reader.u16();
    if (!reader.ok) return false;

    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::copy(spu2.begin(), spu2.end(), impl_->spu2.begin());
    impl_->voices = voices;
    impl_->stream = std::move(stream);
    impl_->master_l = master_l;
    impl_->master_r = master_r;
    impl_->muted = muted;
    impl_->transfer_address = transfer_address;
    std::copy(std::begin(esa), std::end(esa), std::begin(impl_->esa));
    std::copy(std::begin(eea), std::end(eea), std::begin(impl_->eea));
    impl_->irqa = irqa;
    std::copy(std::begin(endx), std::end(endx), std::begin(impl_->endx));
    std::copy(std::begin(keyon), std::end(keyon), std::begin(impl_->keyon));
    std::copy(std::begin(core_attr), std::end(core_attr),
              std::begin(impl_->core_attr));
    impl_->keyon_logs = 0;
    impl_->stream_logs = 0;
    return true;
}

Ps2AudioSystem &System() {
    static Ps2AudioSystem instance;
    return instance;
}

}  // namespace ps2::audio

#if defined(PS2_AUDIO_USE_SDL)
void SDLCALL Ps2AudioSdlCallback(void *userdata, Uint8 *stream, int len) {
    auto *system = static_cast<ps2::audio::Ps2AudioSystem *>(userdata);
    if (system == nullptr || stream == nullptr || len <= 0) return;

    const std::size_t frames =
        static_cast<std::size_t>(len) /
        (ps2::audio::Ps2AudioSystem::kChannels * sizeof(std::int16_t));
    system->RenderRealtime(reinterpret_cast<std::int16_t *>(stream), frames);
}
#endif
