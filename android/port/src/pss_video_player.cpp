#include "pss_video_player.hpp"

#include "iso_reader.hpp"

#include <SDL.h>
#include <GLES3/gl3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#if defined(PORT_HAVE_FFMPEG)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#endif

#if defined(__ANDROID__)
#include <android/log.h>
#define PSS_TAG "OnimushaPss"
#define PSS_LOGI(...) __android_log_print(ANDROID_LOG_INFO, PSS_TAG, __VA_ARGS__)
#define PSS_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, PSS_TAG, __VA_ARGS__)
#else
#define PSS_LOGI(...) do { \
    std::fprintf(stdout, "[OnimushaPss] "); \
    std::fprintf(stdout, __VA_ARGS__); \
    std::fputc('\n', stdout); \
} while (0)
#define PSS_LOGE(...) do { \
    std::fprintf(stderr, "[OnimushaPss] "); \
    std::fprintf(stderr, __VA_ARGS__); \
    std::fputc('\n', stderr); \
} while (0)
#endif

namespace {

[[maybe_unused]] constexpr int kAudioRate = 48000;
[[maybe_unused]] constexpr int kAudioChannels = 2;

double now_seconds() {
    return static_cast<double>(SDL_GetTicks64()) / 1000.0;
}

#if defined(PORT_HAVE_FFMPEG)
struct IsoIo {
    IsoReader *iso = nullptr;
    std::uint32_t lba = 0;
    std::uint64_t size = 0;
    std::uint64_t position = 0;
};

int iso_read_packet(void *opaque, std::uint8_t *buffer, int buffer_size) {
    auto *io = static_cast<IsoIo *>(opaque);
    if (io == nullptr || io->iso == nullptr || buffer == nullptr ||
        buffer_size <= 0) {
        return AVERROR(EIO);
    }
    if (io->position >= io->size) return AVERROR_EOF;

    const std::uint64_t remaining = io->size - io->position;
    const int to_read = static_cast<int>(
        std::min<std::uint64_t>(static_cast<std::uint64_t>(buffer_size),
                                remaining));
    const std::uint64_t offset =
        static_cast<std::uint64_t>(io->lba) * 2048ull + io->position;

    if (!io->iso->ReadBytes(static_cast<Sint64>(offset), buffer,
                            static_cast<std::size_t>(to_read))) {
        return AVERROR(EIO);
    }
    io->position += static_cast<std::uint64_t>(to_read);
    return to_read;
}

int64_t iso_seek(void *opaque, int64_t offset, int whence) {
    auto *io = static_cast<IsoIo *>(opaque);
    if (io == nullptr) return AVERROR(EIO);

    if (whence == AVSEEK_SIZE) {
        return static_cast<int64_t>(io->size);
    }

    std::int64_t base = 0;
    switch (whence & ~AVSEEK_FORCE) {
    case SEEK_SET: base = 0; break;
    case SEEK_CUR: base = static_cast<std::int64_t>(io->position); break;
    case SEEK_END: base = static_cast<std::int64_t>(io->size); break;
    default: return AVERROR(EINVAL);
    }

    const std::int64_t target = base + offset;
    if (target < 0 || static_cast<std::uint64_t>(target) > io->size) {
        return AVERROR(EINVAL);
    }
    io->position = static_cast<std::uint64_t>(target);
    return target;
}
#endif

}  // namespace

struct PssVideoPlayer::Impl {
    std::string error;
    bool playing = false;
    std::atomic<bool> skip{false};
    bool gl_ready = false;
    GLuint texture = 0;
    GLuint program = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLint u_scale = -1;
    GLint u_texture = -1;
    int width = 0;
    int height = 0;
    double duration = 0.0;
    double start_time = 0.0;
    double next_video_time = 0.0;
    SDL_AudioDeviceID audio_device = 0;
    bool owns_audio = false;
    std::uint64_t audio_bytes_written = 0;

#if defined(PORT_HAVE_FFMPEG)
    AVFormatContext *format = nullptr;
    AVIOContext *avio = nullptr;
    IsoIo iso_io{};
    AVCodecContext *video_codec = nullptr;
    AVCodecContext *audio_codec = nullptr;
    SwsContext *sws = nullptr;
    SwrContext *swr = nullptr;
    AVPacket *pending_video = nullptr;
    AVFrame *frame = nullptr;
    int video_stream = -1;
    int audio_stream = -1;
    bool eof = false;
    std::vector<std::uint8_t> rgba_buffer;
    std::vector<std::uint8_t> audio_buffer;
#endif

#if defined(PORT_HAVE_FFMPEG)
    bool OpenFormat();
    bool OpenCodec(int stream_index, AVCodecContext **context,
                   const char *label);
    bool OpenAudioDevice();
#endif
    bool InitializeGL();
    void ShutdownGL();
    bool Update();
    void Render(int screen_width, int screen_height);
    void Close();
    void QueueAudio(const std::uint8_t *data, std::size_t bytes);
    double Position() const;
    void SetError(const std::string &message);
};

void PssVideoPlayer::Impl::SetError(const std::string &message) {
    error = message;
    PSS_LOGE("%s", message.c_str());
}

#if defined(PORT_HAVE_FFMPEG)

bool PssVideoPlayer::Impl::OpenFormat() {
    if (format == nullptr) {
        SetError("PssVideoPlayer: no hay AVFormatContext");
        return false;
    }
    if (avformat_find_stream_info(format, nullptr) < 0) {
        SetError("PssVideoPlayer: avformat_find_stream_info fallo");
        return false;
    }

    video_stream = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1,
                                       nullptr, 0);
    audio_stream = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1,
                                       nullptr, 0);
    if (video_stream < 0) {
        SetError("PssVideoPlayer: no hay stream de video MPEG-2");
        return false;
    }

    if (!OpenCodec(video_stream, &video_codec, "video")) return false;
    if (audio_stream >= 0) {
        if (!OpenCodec(audio_stream, &audio_codec, "audio")) {
            PSS_LOGE("PSS sin audio: %s", error.c_str());
            audio_codec = nullptr;
            audio_stream = -1;
        }
    }

    if (format->duration > 0) {
        duration = static_cast<double>(format->duration) / AV_TIME_BASE;
    }
    if (!OpenAudioDevice()) return false;

    frame = av_frame_alloc();
    if (frame == nullptr) {
        SetError("PssVideoPlayer: av_frame_alloc fallo");
        return false;
    }

    playing = true;
    skip.store(false);
    eof = false;
    start_time = now_seconds();
    next_video_time = start_time;
    PSS_LOGI("PSS abierto: video=%s %dx%d audio=%s duracion=%.2fs",
             avcodec_get_name(
                 format->streams[video_stream]->codecpar->codec_id),
             format->streams[video_stream]->codecpar->width,
             format->streams[video_stream]->codecpar->height,
             audio_stream >= 0
                 ? avcodec_get_name(
                       format->streams[audio_stream]->codecpar->codec_id)
                 : "none",
             duration);
    return true;
}

bool PssVideoPlayer::Impl::OpenCodec(int stream_index,
                                     AVCodecContext **context,
                                     const char *label) {
    AVStream *stream = format->streams[stream_index];
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec == nullptr) {
        SetError(std::string("PssVideoPlayer: decoder ") + label +
                 " no disponible");
        return false;
    }

    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    if (ctx == nullptr) {
        SetError(std::string("PssVideoPlayer: avcodec_alloc_context3 ") +
                 label + " fallo");
        return false;
    }
    if (avcodec_parameters_to_context(ctx, stream->codecpar) < 0 ||
        avcodec_open2(ctx, codec, nullptr) < 0) {
        avcodec_free_context(&ctx);
        SetError(std::string("PssVideoPlayer: avcodec_open2 ") + label +
                 " fallo");
        return false;
    }

    if (stream_index == video_stream) ctx->thread_count = 2;
    *context = ctx;
    return true;
}

bool PssVideoPlayer::Impl::OpenAudioDevice() {
    if (audio_device != 0) return true;

    if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            SetError(std::string("PssVideoPlayer: SDL_InitSubSystem(AUDIO): ") +
                     SDL_GetError());
            return false;
        }
    }

    SDL_AudioSpec want{};
    SDL_AudioSpec have{};
    want.freq = kAudioRate;
    want.format = AUDIO_S16SYS;
    want.channels = kAudioChannels;
    want.samples = 1024;
    want.callback = nullptr;

    audio_device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (audio_device == 0) {
        SetError(std::string("PssVideoPlayer: SDL_OpenAudioDevice fallo: ") +
                 SDL_GetError());
        return false;
    }
    if (have.freq != kAudioRate || have.channels != kAudioChannels ||
        have.format != AUDIO_S16SYS) {
        SDL_CloseAudioDevice(audio_device);
        audio_device = 0;
        SetError("PssVideoPlayer: el device no es 48000 Hz S16 estereo");
        return false;
    }

    owns_audio = true;
    SDL_PauseAudioDevice(audio_device, 0);
    return true;
}

void PssVideoPlayer::Impl::QueueAudio(const std::uint8_t *data,
                                      std::size_t bytes) {
    if (audio_device == 0 || data == nullptr || bytes == 0) return;
    if (SDL_QueueAudio(audio_device, data, static_cast<Uint32>(bytes)) == 0) {
        audio_bytes_written += bytes;
    }
}

bool PssVideoPlayer::Impl::Update() {
    if (!playing || skip.load()) return false;
    if (!gl_ready) {
        SetError("PssVideoPlayer: InitializeGL no fue llamado");
        return false;
    }

    const double now = now_seconds();
    if (now < next_video_time) {
        // Decodifica audio hasta encontrar el proximo paquete de video.
        while (!pending_video) {
            AVPacket *packet = av_packet_alloc();
            if (packet == nullptr) return false;
            const int ret = av_read_frame(format, packet);
            if (ret < 0) {
                eof = true;
                av_packet_free(&packet);
                return false;
            }
            if (packet->stream_index == video_stream) {
                pending_video = av_packet_clone(packet);
                av_packet_free(&packet);
                break;
            }
            if (packet->stream_index == audio_stream && audio_codec != nullptr) {
                if (avcodec_send_packet(audio_codec, packet) >= 0) {
                    AVFrame *audio_frame = av_frame_alloc();
                    while (audio_frame != nullptr &&
                           avcodec_receive_frame(audio_codec, audio_frame) == 0) {
                        const int in_channels =
                            audio_frame->ch_layout.nb_channels > 0
                                ? audio_frame->ch_layout.nb_channels
                                : 2;
                        if (swr == nullptr) {
                            AVChannelLayout out_layout;
                            AVChannelLayout in_layout;
                            av_channel_layout_default(&out_layout, 2);
                            av_channel_layout_default(&in_layout, in_channels);
#if LIBSWRESAMPLE_VERSION_MAJOR >= 4
                            if (swr_alloc_set_opts2(
                                    &swr, &out_layout, AV_SAMPLE_FMT_S16,
                                    kAudioRate, &in_layout,
                                    static_cast<AVSampleFormat>(
                                        audio_frame->format),
                                    audio_frame->sample_rate, 0,
                                    nullptr) < 0) {
                                swr = nullptr;
                            }
#else
                            swr = swr_alloc_set_opts(
                                nullptr, AV_CH_LAYOUT_STEREO,
                                AV_SAMPLE_FMT_S16, kAudioRate,
                                av_get_default_channel_layout(in_channels),
                                static_cast<AVSampleFormat>(
                                    audio_frame->format),
                                audio_frame->sample_rate, 0, nullptr);
#endif
                            if (swr != nullptr && swr_init(swr) < 0) {
                                swr_free(&swr);
                            }
                        }
                        if (swr != nullptr) {
                            const int out_samples = static_cast<int>(
                                av_rescale_rnd(
                                    swr_get_delay(swr,
                                                  audio_frame->sample_rate) +
                                        audio_frame->nb_samples,
                                    kAudioRate, audio_frame->sample_rate,
                                    AV_ROUND_UP));
                            audio_buffer.resize(
                                static_cast<std::size_t>(out_samples) *
                                kAudioChannels * 2u);
                            std::uint8_t *out[1] = {audio_buffer.data()};
                            const std::uint8_t *input[AV_NUM_DATA_POINTERS] = {};
                            for (int channel = 0;
                                 channel < in_channels &&
                                 channel < AV_NUM_DATA_POINTERS;
                                 ++channel) {
                                input[channel] =
                                    audio_frame->extended_data[channel];
                            }
                            const int converted = swr_convert(
                                swr, out, out_samples,
                                reinterpret_cast<const std::uint8_t *const *>(
                                    input),
                                audio_frame->nb_samples);
                            if (converted > 0) {
                                QueueAudio(
                                    audio_buffer.data(),
                                    static_cast<std::size_t>(converted) *
                                        kAudioChannels * 2u);
                            }
                        }
                        av_frame_unref(audio_frame);
                    }
                    av_frame_free(&audio_frame);
                }
            }
            av_packet_free(&packet);
        }
        return true;
    }

    if (!pending_video) {
        // Lee hasta encontrar el primer paquete de video.
        AVPacket *packet = av_packet_alloc();
        if (packet == nullptr) return false;
        while (true) {
            const int ret = av_read_frame(format, packet);
            if (ret < 0) {
                eof = true;
                av_packet_free(&packet);
                return false;
            }
            if (packet->stream_index == video_stream) {
                pending_video = av_packet_clone(packet);
                av_packet_free(&packet);
                break;
            }
            if (packet->stream_index == audio_stream && audio_codec != nullptr) {
                avcodec_send_packet(audio_codec, packet);
                av_packet_unref(packet);
            } else {
                av_packet_unref(packet);
            }
        }
    }

    if (pending_video != nullptr) {
        const int sent = avcodec_send_packet(video_codec, pending_video);
        av_packet_free(&pending_video);
        if (sent < 0 && sent != AVERROR(EAGAIN)) {
            SetError("PssVideoPlayer: avcodec_send_packet video fallo");
            return false;
        }

        const int received = avcodec_receive_frame(video_codec, frame);
        if (received == AVERROR(EAGAIN)) return true;
        if (received < 0) {
            SetError("PssVideoPlayer: avcodec_receive_frame video fallo");
            return false;
        }

        const int frame_width = frame->width;
        const int frame_height = frame->height;
        if (frame_width <= 0 || frame_height <= 0) {
            av_frame_unref(frame);
            return true;
        }

        if (sws == nullptr || width != frame_width || height != frame_height) {
            sws_freeContext(sws);
            sws = sws_getContext(frame_width, frame_height,
                                 static_cast<AVPixelFormat>(frame->format),
                                 frame_width, frame_height, AV_PIX_FMT_RGBA,
                                 SWS_BILINEAR, nullptr, nullptr, nullptr);
            width = frame_width;
            height = frame_height;
        }
        if (sws == nullptr) {
            av_frame_unref(frame);
            SetError("PssVideoPlayer: sws_getContext fallo");
            return false;
        }

        rgba_buffer.resize(static_cast<std::size_t>(width) * height * 4u);
        std::uint8_t *dst[4] = {rgba_buffer.data(), nullptr, nullptr, nullptr};
        const int dst_lines[4] = {width * 4, 0, 0, 0};
        sws_scale(sws, frame->data, frame->linesize, 0, height, dst,
                  dst_lines);

        if (texture == 0) {
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, rgba_buffer.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                            GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                            GL_CLAMP_TO_EDGE);
        } else {
            glBindTexture(GL_TEXTURE_2D, texture);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                            GL_RGBA, GL_UNSIGNED_BYTE, rgba_buffer.data());
        }

        const double fps =
            (format->streams[video_stream]->avg_frame_rate.num > 0)
                ? av_q2d(format->streams[video_stream]->avg_frame_rate)
                : 0.0;
        next_video_time = now + (fps > 0.0 ? 1.0 / fps : 1.0 / 30.0);
        av_frame_unref(frame);
        return true;
    }

    return true;
}

#else  // !PORT_HAVE_FFMPEG

bool PssVideoPlayer::Impl::Update() {
    return false;
}

#endif  // PORT_HAVE_FFMPEG

bool PssVideoPlayer::Impl::InitializeGL() {
    if (gl_ready) return true;
    if (texture == 0) {
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    static constexpr char kVertex[] =
        "#version 300 es\n"
        "layout(location=0) in vec2 a_pos;\n"
        "layout(location=1) in vec2 a_uv;\n"
        "uniform vec2 u_scale;\n"
        "out vec2 v_uv;\n"
        "void main() {\n"
        "    gl_Position = vec4(a_pos * u_scale, 0.0, 1.0);\n"
        "    v_uv = a_uv;\n"
        "}\n";
    static constexpr char kFragment[] =
        "#version 300 es\n"
        "precision mediump float;\n"
        "uniform sampler2D u_texture;\n"
        "in vec2 v_uv;\n"
        "out vec4 fragColor;\n"
        "void main() { fragColor = texture(u_texture, v_uv); }\n";

    auto compile = [](GLenum type, const char *source) -> GLuint {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint ok = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (ok == GL_FALSE) {
            char log[512] = {};
            glGetShaderInfoLog(shader, sizeof(log) - 1, nullptr, log);
            PSS_LOGE("PSS shader: %s", log);
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    };

    const GLuint vs = compile(GL_VERTEX_SHADER, kVertex);
    const GLuint fs = compile(GL_FRAGMENT_SHADER, kFragment);
    if (vs == 0 || fs == 0) {
        if (vs != 0) glDeleteShader(vs);
        if (fs != 0) glDeleteShader(fs);
        SetError("PssVideoPlayer: shader de video fallo");
        return false;
    }
    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
        SetError("PssVideoPlayer: link de shader de video fallo");
        glDeleteProgram(program);
        program = 0;
        return false;
    }

    u_scale = glGetUniformLocation(program, "u_scale");
    u_texture = glGetUniformLocation(program, "u_texture");

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    static constexpr float kQuad[16] = {
        -1.0f, -1.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 0.0f,
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<void *>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<void *>(2 * sizeof(float)));
    glBindVertexArray(0);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl_ready = true;
    return true;
}

void PssVideoPlayer::Impl::ShutdownGL() {
    if (vbo != 0) glDeleteBuffers(1, &vbo);
    if (vao != 0) glDeleteVertexArrays(1, &vao);
    if (texture != 0) glDeleteTextures(1, &texture);
    if (program != 0) glDeleteProgram(program);
    vbo = vao = texture = program = 0;
    gl_ready = false;
}

void PssVideoPlayer::Impl::Render(int screen_width, int screen_height) {
    if (!gl_ready || texture == 0 || width <= 0 || height <= 0 ||
        program == 0 || screen_width <= 0 || screen_height <= 0) {
        return;
    }

    const float video_aspect = static_cast<float>(width) / height;
    const float screen_aspect =
        static_cast<float>(screen_width) / screen_height;
    float scale_x = 1.0f;
    float scale_y = 1.0f;
    if (video_aspect > screen_aspect) {
        scale_y = screen_aspect / video_aspect;
    } else {
        scale_x = video_aspect / screen_aspect;
    }

    glUseProgram(program);
    glUniform2f(u_scale, scale_x, scale_y);
    glUniform1i(u_texture, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glUseProgram(0);
}

void PssVideoPlayer::Impl::Close() {
    playing = false;
    skip.store(false);

#if defined(PORT_HAVE_FFMPEG)
    av_packet_free(&pending_video);
    av_frame_free(&frame);
    sws_freeContext(sws);
    sws = nullptr;
    swr_free(&swr);
    avcodec_free_context(&video_codec);
    avcodec_free_context(&audio_codec);
    if (avio != nullptr) {
        if (format != nullptr) avformat_close_input(&format);
        av_freep(&avio->buffer);
        avio_context_free(&avio);
    } else if (format != nullptr) {
        avformat_close_input(&format);
    }
    format = nullptr;
    eof = false;
#endif

    if (owns_audio && audio_device != 0) {
        SDL_ClearQueuedAudio(audio_device);
        SDL_CloseAudioDevice(audio_device);
    }
    audio_device = 0;
    owns_audio = false;
    audio_bytes_written = 0;
    width = 0;
    height = 0;
    duration = 0.0;
}

double PssVideoPlayer::Impl::Position() const {
    if (duration <= 0.0) return 0.0;
    const double value = now_seconds() - start_time;
    return value < 0.0 ? 0.0 : (value > duration ? duration : value);
}

// ---------------------------------------------------------------------------
// Fachada publica
// ---------------------------------------------------------------------------

PssVideoPlayer::PssVideoPlayer() : impl_(std::make_unique<Impl>()) {}
PssVideoPlayer::~PssVideoPlayer() { Close(); }

bool PssVideoPlayer::OpenFile(const std::string &path) {
    Close();
#if defined(PORT_HAVE_FFMPEG)
    if (avformat_open_input(&impl_->format, path.c_str(), nullptr, nullptr) < 0) {
        impl_->SetError("PssVideoPlayer: no se pudo abrir " + path);
        return false;
    }
    return impl_->OpenFormat();
#else
    (void)path;
    impl_->SetError(
        "PssVideoPlayer: compilado sin FFmpeg. Activa PORT_ENABLE_FFMPEG.");
    return false;
#endif
}

bool PssVideoPlayer::OpenFromIso(IsoReader *iso, std::uint32_t lba,
                                 std::uint64_t size) {
    Close();
    if (iso == nullptr || size == 0) {
        impl_->SetError("PssVideoPlayer: ISO/LBA/tamano invalido");
        return false;
    }
#if defined(PORT_HAVE_FFMPEG)
    impl_->iso_io.iso = iso;
    impl_->iso_io.lba = lba;
    impl_->iso_io.size = size;
    impl_->iso_io.position = 0;

    std::uint8_t *buffer = static_cast<std::uint8_t *>(av_malloc(65536));
    if (buffer == nullptr) {
        impl_->SetError("PssVideoPlayer: av_malloc del buffer AVIO fallo");
        return false;
    }
    impl_->avio = avio_alloc_context(buffer, 65536, 0, &impl_->iso_io,
                                     iso_read_packet, nullptr, iso_seek);
    if (impl_->avio == nullptr) {
        av_free(buffer);
        impl_->SetError("PssVideoPlayer: avio_alloc_context fallo");
        return false;
    }

    impl_->format = avformat_alloc_context();
    if (impl_->format == nullptr) {
        impl_->SetError("PssVideoPlayer: avformat_alloc_context fallo");
        return false;
    }
    impl_->format->pb = impl_->avio;
    impl_->format->flags |= AVFMT_FLAG_CUSTOM_IO;

    if (avformat_open_input(&impl_->format, nullptr, nullptr, nullptr) < 0) {
        impl_->SetError("PssVideoPlayer: no se pudo abrir el PSS del ISO");
        return false;
    }
    return impl_->OpenFormat();
#else
    (void)lba;
    impl_->SetError(
        "PssVideoPlayer: compilado sin FFmpeg. Activa PORT_ENABLE_FFMPEG.");
    return false;
#endif
}

void PssVideoPlayer::SetAudioDevice(std::uint32_t device) {
    if (impl_->owns_audio && impl_->audio_device != 0) {
        SDL_ClearQueuedAudio(impl_->audio_device);
        SDL_CloseAudioDevice(impl_->audio_device);
    }
    impl_->audio_device = static_cast<SDL_AudioDeviceID>(device);
    impl_->owns_audio = false;
}

bool PssVideoPlayer::InitializeGL() { return impl_->InitializeGL(); }
void PssVideoPlayer::ShutdownGL() { impl_->ShutdownGL(); }
bool PssVideoPlayer::Update() { return impl_->Update(); }
void PssVideoPlayer::Render(int w, int h) { impl_->Render(w, h); }
void PssVideoPlayer::RequestSkip() { impl_->skip.store(true); }
bool PssVideoPlayer::SkipRequested() const { return impl_->skip.load(); }
bool PssVideoPlayer::IsPlaying() const {
    return impl_->playing && !impl_->skip.load();
}
void PssVideoPlayer::Close() { impl_->Close(); }

PssVideoPlayer::TextureId PssVideoPlayer::Texture() const {
    return impl_->texture;
}
int PssVideoPlayer::Width() const { return impl_->width; }
int PssVideoPlayer::Height() const { return impl_->height; }
double PssVideoPlayer::Duration() const { return impl_->duration; }
double PssVideoPlayer::Position() const { return impl_->Position(); }
const std::string &PssVideoPlayer::LastError() const { return impl_->error; }

PssVideoPlayer &GlobalPssVideoPlayer() {
    // Se libera a proposito: evita usar SDL/GL durante la destruccion
    // estatica, despues de SDL_Quit.
    static PssVideoPlayer *player = new PssVideoPlayer();
    return *player;
}
