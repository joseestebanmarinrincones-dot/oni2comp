# CMakeLists.txt para Android NDK (arm64-v8a, API 29+)
#
# Compila todos los .c/.cc/.cpp de src/ en una única librería compartida
# libmain.so, enlazada con SDL2 y OpenGL ES 3.0.
#
# Coloca este archivo junto a la carpeta src/:
#
#   modulo/
#     CMakeLists.txt   <- este archivo
#     src/             <- tus fuentes
#     jniLibs/arm64-v8a/libSDL2.so   (o la ruta que indiques)
#
# Configuración típica de Gradle (app/build.gradle):
#
#   android {
#       defaultConfig {
#           minSdk 29
#           ndk { abiFilters "arm64-v8a" }
#           externalNativeBuild {
#               cmake {
#                   arguments "-DANDROID_PLATFORM=android-29",
#                             "-DANDROID_ABI=arm64-v8a"
#                   cppFlags "-std=c++17"
#               }
#           }
#       }
#       externalNativeBuild {
#           cmake { path "CMakeLists.txt"; version "3.22.1" }
#       }
#   }
#
# Si SDL2 está en otra ruta, pásala en la configuración:
#   -DPORT_SDL2_INCLUDE_DIR=C:/ruta/SDL2/include
#   -DPORT_SDL2_LIBRARY=C:/ruta/arm64-v8a/libSDL2.so

cmake_minimum_required(VERSION 3.22)
project(OnimushaPort LANGUAGES C CXX)

if(NOT ANDROID)
    message(FATAL_ERROR "Este CMakeLists es para el NDK de Android. Usa el toolchain android.toolchain.cmake.")
endif()

# ---------------------------------------------------------------------------
# Estándar y opciones generales
# ---------------------------------------------------------------------------
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# API mínima. Gradle pasa ANDROID_PLATFORM=android-29; esto lo comprueba.
if(DEFINED ANDROID_PLATFORM AND NOT ANDROID_PLATFORM STREQUAL "")
    string(REGEX MATCH "([0-9]+)$" _api_match "${ANDROID_PLATFORM}")
    if(_api_match AND CMAKE_MATCH_1 LESS 29)
        message(FATAL_ERROR "API ${CMAKE_MATCH_1} no soportada: usa API 29 o superior.")
    endif()
endif()

if(NOT ANDROID_ABI STREQUAL "arm64-v8a")
    message(WARNING "ANDROID_ABI=${ANDROID_ABI}. Este proyecto está pensado para arm64-v8a.")
endif()

# ---------------------------------------------------------------------------
# SDL2 (prebuilt para Android)
# ---------------------------------------------------------------------------
set(PORT_SDL2_ROOT "" CACHE PATH "Raíz de SDL2 (la carpeta que contiene include/ y lib/)")
set(PORT_SDL2_INCLUDE_DIR "" CACHE PATH "Carpeta con SDL.h, SDL_main.h, ...")
set(PORT_SDL2_LIBRARY "" CACHE FILEPATH "libSDL2.so para la ABI objetivo")

if(NOT PORT_SDL2_INCLUDE_DIR)
    set(_sdl2_include_candidates
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL2/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL2/include/SDL2"
        "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/SDL2/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/SDL2/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/sdl2-src/SDL2-2.32.10/include"
        "${PORT_SDL2_ROOT}/include"
        "${PORT_SDL2_ROOT}/include/SDL2"
    )
    foreach(_candidate IN LISTS _sdl2_include_candidates)
        if(EXISTS "${_candidate}/SDL.h")
            set(PORT_SDL2_INCLUDE_DIR "${_candidate}")
            break()
        endif()
    endforeach()
endif()

if(NOT PORT_SDL2_LIBRARY)
    set(_sdl2_library_candidates
        "${CMAKE_CURRENT_SOURCE_DIR}/jniLibs/${ANDROID_ABI}/libSDL2.so"
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/${ANDROID_ABI}/libSDL2.so"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL2/lib/${ANDROID_ABI}/libSDL2.so"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL2/libs/${ANDROID_ABI}/libSDL2.so"
        "${CMAKE_CURRENT_SOURCE_DIR}/../jniLibs/${ANDROID_ABI}/libSDL2.so"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../build/sdl2-android/libSDL2.so"
        "${PORT_SDL2_ROOT}/lib/${ANDROID_ABI}/libSDL2.so"
        "${PORT_SDL2_ROOT}/libs/${ANDROID_ABI}/libSDL2.so"
    )
    foreach(_candidate IN LISTS _sdl2_library_candidates)
        if(EXISTS "${_candidate}")
            set(PORT_SDL2_LIBRARY "${_candidate}")
            break()
        endif()
    endforeach()
endif()

if(NOT EXISTS "${PORT_SDL2_INCLUDE_DIR}/SDL.h")
    message(FATAL_ERROR
        "No encuentro SDL2. Pasa -DPORT_SDL2_INCLUDE_DIR=<carpeta con SDL.h> "
        "y -DPORT_SDL2_LIBRARY=<libSDL2.so de arm64-v8a>.")
endif()
if(NOT EXISTS "${PORT_SDL2_LIBRARY}")
    message(FATAL_ERROR
        "No encuentro libSDL2.so. Pasa -DPORT_SDL2_LIBRARY=<ruta a libSDL2.so de arm64-v8a>.")
endif()

add_library(SDL2::SDL2 SHARED IMPORTED)
set_target_properties(SDL2::SDL2 PROPERTIES
    IMPORTED_LOCATION "${PORT_SDL2_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${PORT_SDL2_INCLUDE_DIR}"
)

message(STATUS "SDL2 include: ${PORT_SDL2_INCLUDE_DIR}")
message(STATUS "SDL2 library: ${PORT_SDL2_LIBRARY}")

# ---------------------------------------------------------------------------
# Dear ImGui (core + SDL2 + OpenGL ES 3.0)
# ---------------------------------------------------------------------------
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/imgui")
if(NOT EXISTS "${IMGUI_DIR}/imgui.cpp")
    message(FATAL_ERROR
        "No encuentro Dear ImGui en ${IMGUI_DIR}. Clona "
        "https://github.com/ocornut/imgui en third_party/imgui.")
endif()

add_library(imgui STATIC
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
)
target_include_directories(imgui PUBLIC
    "${IMGUI_DIR}"
    "${IMGUI_DIR}/backends"
)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_ES3=1)
target_link_libraries(imgui PUBLIC SDL2::SDL2 GLESv3)
target_compile_options(imgui PRIVATE
    -Wall
    -Wextra
    -fno-strict-aliasing
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:RelWithDebInfo>:-O3>
)

# ---------------------------------------------------------------------------
# Fuentes: todo lo que haya en src/ (incluidas subcarpetas)
# ---------------------------------------------------------------------------
file(GLOB_RECURSE PORT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

if(NOT PORT_SOURCES)
    message(FATAL_ERROR "No hay fuentes en ${CMAKE_CURRENT_SOURCE_DIR}/src (se esperan .c/.cc/.cpp).")
endif()

add_library(main SHARED ${PORT_SOURCES})

target_include_directories(main PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/include"
    "${PORT_SDL2_INCLUDE_DIR}"
)

# SDL_main debe quedar exportado para que SDLActivity lo encuentre con dlsym.
target_link_options(main PRIVATE "-Wl,--export-dynamic")

# Intercepción de fopen/open/stat/access/opendir para las rutas cdrom0:\...
# El código descompilado no se toca: el linker redirige los símbolos.
option(PORTFS_INTERCEPT_STDIO "Interceptar rutas de consola en fopen/open/stat" ON)
if(PORTFS_INTERCEPT_STDIO)
    target_compile_definitions(main PRIVATE PORTFS_LINK_WRAP=1)
    target_link_options(main PRIVATE
        "-Wl,--wrap=fopen"
        "-Wl,--wrap=open"
        "-Wl,--wrap=stat"
        "-Wl,--wrap=access"
        "-Wl,--wrap=opendir"
    )
endif()

target_compile_options(main PRIVATE
    -Wall
    -Wextra
    -fno-omit-frame-pointer
    # El codigo de Ghidra hace casts entre punteros/enteros y reinterpreta
    # ps2_ram; -fno-strict-aliasing evita optimizaciones incorrectas.
    -fno-strict-aliasing
    # Release final: -O3. Debug conserva -O1. Gradle/AGP usa RelWithDebInfo
    # para el APK release, asi que tambien se optimiza a -O3.
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Release>:-DNDEBUG>
    $<$<CONFIG:Debug>:-O1>
    $<$<CONFIG:Debug>:-g>
    # El APK release usa RelWithDebInfo, pero sin -g: Gradle/AGP elimina
    # igualmente .symtab/.debug_* y no queremos inflar el .so intermedio.
    $<$<CONFIG:RelWithDebInfo>:-O3>
    $<$<CONFIG:RelWithDebInfo>:-DNDEBUG>
    # El toolchain Android añade -g a RelWithDebInfo; -g0 lo desactiva.
    $<$<CONFIG:RelWithDebInfo>:-g0>
)

# Opcional: eliminar secciones no usadas en Release. Se deja apagado por
# defecto porque el juego usa tablas de funciones y simbolos debiles; activalo
# solo despues de probar que SDL_main y los stubs siguen enlazando.
option(PORT_RELEASE_GC_SECTIONS "Compilar Release con --gc-sections" OFF)
if(PORT_RELEASE_GC_SECTIONS)
    target_compile_options(main PRIVATE
        $<$<CONFIG:Release>:-ffunction-sections>
        $<$<CONFIG:Release>:-fdata-sections>
    )
    target_link_options(main PRIVATE
        $<$<CONFIG:Release>:-Wl,--gc-sections>
    )
endif()

# Ps2AudioSystem abre SDL_OpenAudioDevice solo en el build Android. Las
# pruebas de PC compilan el mismo .cpp sin SDL y usan RenderPcm().
target_compile_definitions(main PRIVATE PS2_AUDIO_USE_SDL=1)

# El HUD tactil usa GLES 3.0; en pruebas de PC se compila sin GL.
target_compile_definitions(main PRIVATE PS2_INPUT_USE_GL=1)

# display_layout aplica glViewport/glScissor solo en el target Android.
target_compile_definitions(main PRIVATE PS2_DISPLAY_USE_GL=1)

# zlib viene incluida en el NDK de Android (libz.so / zlib.h).
find_package(ZLIB REQUIRED)

# OpenGL ES 3.0 + EGL. Android y log son del NDK; m/dl son stubs en Android.
target_link_libraries(main PRIVATE
    imgui
    SDL2::SDL2
    ZLIB::ZLIB
    GLESv3
    EGL
    android
    log
    m
    dl
)

# libmain.so, exactamente el nombre que carga org.libsdl.app.SDLActivity.
set_target_properties(main PROPERTIES
    OUTPUT_NAME "main"
    PREFIX "lib"
)

option(PORT_STORAGE_SELFTEST "Compilar el autotest de IsoReader/OverlayManager" OFF)
if(PORT_STORAGE_SELFTEST)
    target_compile_definitions(main PRIVATE PORT_STORAGE_SELFTEST=1)
endif()

# ---------------------------------------------------------------------------
# FFmpeg opcional para PssVideoPlayer (.PSS / MPEG-2)
# ---------------------------------------------------------------------------
option(PORT_ENABLE_FFMPEG "Habilitar PssVideoPlayer con FFmpeg" OFF)
set(PORT_FFMPEG_ROOT "" CACHE PATH "Raiz de FFmpeg Android (include/ y lib/<abi>)")

if(PORT_ENABLE_FFMPEG)
    set(_ffmpeg_include "${PORT_FFMPEG_ROOT}/include")
    set(_ffmpeg_libdir "${PORT_FFMPEG_ROOT}/lib/${ANDROID_ABI}")

    find_path(PORT_FFMPEG_INCLUDE_DIR
        NAMES libavformat/avformat.h
        PATHS "${_ffmpeg_include}"
        NO_DEFAULT_PATH)

    foreach(_component avformat avcodec avutil swscale swresample)
        find_library(PORT_FFMPEG_${_component}_LIBRARY
            NAMES ${_component}
            PATHS "${_ffmpeg_libdir}"
            NO_DEFAULT_PATH)
    endforeach()

    if(PORT_FFMPEG_INCLUDE_DIR
       AND PORT_FFMPEG_avformat_LIBRARY
       AND PORT_FFMPEG_avcodec_LIBRARY
       AND PORT_FFMPEG_avutil_LIBRARY
       AND PORT_FFMPEG_swscale_LIBRARY
       AND PORT_FFMPEG_swresample_LIBRARY)
        target_compile_definitions(main PRIVATE PORT_HAVE_FFMPEG=1)
        target_include_directories(main PRIVATE "${PORT_FFMPEG_INCLUDE_DIR}")
        target_link_libraries(main PRIVATE
            "${PORT_FFMPEG_avformat_LIBRARY}"
            "${PORT_FFMPEG_avcodec_LIBRARY}"
            "${PORT_FFMPEG_avutil_LIBRARY}"
            "${PORT_FFMPEG_swscale_LIBRARY}"
            "${PORT_FFMPEG_swresample_LIBRARY}"
            z)
        message(STATUS "PssVideoPlayer: FFmpeg ${PORT_FFMPEG_INCLUDE_DIR}")
    else()
        message(WARNING
            "PORT_ENABLE_FFMPEG=ON pero no encontre headers/libs en "
            "${PORT_FFMPEG_ROOT}. PssVideoPlayer se compila como stub.")
    endif()
endif()

install(TARGETS main LIBRARY DESTINATION "lib/${ANDROID_ABI}")
