# Módulo Android mínimo (SDL2 + GLES 3.0)

Este `CMakeLists.txt` compila todo `src/` en `libmain.so` y lo enlaza con
SDL2, `GLESv3` y `EGL`.

## Colocación

```
tu-modulo/
  CMakeLists.txt
  src/                     <- tus .c/.cc/.cpp
  jniLibs/arm64-v8a/
    libSDL2.so             <- SDL2 prebuilt para arm64 (se empaqueta en el APK)
```

Si SDL2 está en otra ruta:

```powershell
cmake -S . -B build-android `
  -DCMAKE_TOOLCHAIN_FILE="$env:ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" `
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 `
  -DPORT_SDL2_INCLUDE_DIR="C:/ruta/SDL2/include" `
  -DPORT_SDL2_LIBRARY="C:/ruta/arm64-v8a/libSDL2.so"
cmake --build build-android
```

## Gradle

Configuracion actual en `android-project/app/build.gradle.kts`:

```kotlin
android {
    compileSdk = 35
    ndkVersion = "27.2.12479018"

    defaultConfig {
        applicationId = "org.ps2native.recomp"
        minSdk = 29
        targetSdk = 35
        ndk { abiFilters += listOf("arm64-v8a") }

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DANDROID_PLATFORM=android-29",
                    "-DANDROID_ABI=arm64-v8a"
                )
                cppFlags += "-std=c++17"
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../../CMakeLists.txt")
            version = "3.31.6"
        }
    }

    buildTypes {
        debug { isMinifyEnabled = false }
        release {
            isMinifyEnabled = false
            isShrinkResources = false
            isDebuggable = false
            isJniDebuggable = false
            // APK final sin tabla de simbolos: AGP ejecuta
            // stripReleaseDebugSymbols y no genera native-debug-symbols.zip.
            ndk { debugSymbolLevel = "NONE" }
        }
    }

    packaging {
        jniLibs {
            useLegacyPackaging = true
            // Sin excepciones: libmain.so y libSDL2.so se empaquetan stripped.
            keepDebugSymbols.clear()
        }
    }
}
```

`libmain.so` se compila con `-O3 -fno-strict-aliasing` en Release/
RelWithDebInfo. El APK release se firma automaticamente si existe
`android-project/keystore.properties`; si no, `assembleRelease` genera
`app-release-unsigned.apk`.

## Empaquetado

- `libmain.so` lo genera CMake y Gradle lo mete en el APK.
- `libSDL2.so` es una librería importada: cópiala a
  `app/src/main/jniLibs/arm64-v8a/libSDL2.so` para que Gradle la empaquete.
- La actividad Java debe extender `org.libsdl.app.SDLActivity` y cargar
  `libmain.so`; el símbolo `SDL_main` queda exportado con
  `-Wl,--export-dynamic`.
- El APK final contiene solo `lib/arm64-v8a/libSDL2.so` y `libmain.so`.
- Antes de empaquetar, `stripReleaseDebugSymbols` elimina `.symtab` y
  `.debug_*`. El APK release final queda en **1.548.252 bytes**;
  `libmain.so` pasa de **6.425.360 bytes** (intermedio RelWithDebInfo con
  `-g0`) a **2.182.792 bytes** dentro del APK: **66,0 % menos**. El
  `native-debug-symbols.zip` de 1,66 MB ya no se genera.
- Si necesitas simbolizar un crash, compila temporalmente con
  `debugSymbolLevel = "FULL"` y `-g` para obtener el ZIP de símbolos; el APK
  de distribución debe volver a `NONE`.

## Release y crash log

- Banderas CMake recomendadas: `-O3`, `-fno-strict-aliasing`,
  `-fno-omit-frame-pointer`, `-DNDEBUG`.
- Gradle/AGP compila el APK release como `RelWithDebInfo`; CMake tambien
  aplica `-O3` a esa configuracion y `-g0` para no generar DWARF.
- `stripReleaseDebugSymbols` elimina `.symtab`/`.debug_*`; el APK final no
  lleva `native-debug-symbols.zip`.
- `src/crash_handler.cpp` instala `sigaction(SIGSEGV)` con pila alternativa y
  escribe `crash_log.txt` antes de reenviar la señal.
- El log queda en
  `/data/data/org.ps2native.recomp/files/crash_log.txt` y contiene la ultima
  direccion `FUN_xxxxxxxx`.

```powershell
adb shell run-as org.ps2native.recomp cat files/crash_log.txt
```

Documento detallado: `games/onimusha2/disasm/RELEASE_AND_CRASH.md`.

## Rutas de consola (`cdrom0:\...`)

`src/port_fs.cpp` intercepta las rutas del código descompilado sin tocar sus
llamadas:

- `fopen("cdrom0:\\DATA\\FILE.BIN;1")` → busca `DATA/FILE.BIN` bajo el
  directorio externo de la app.
- Quita el volumen (`cdrom0:`, `host0:`, `mc0:`, `pfs0:`, `mass:`, ...),
  cambia `\` por `/` y elimina el sufijo ISO `;1`.
- Busca **sin distinguir mayúsculas/minúsculas** en cada componente, porque
  Android es sensible a mayúsculas y el juego suele usar mayúsculas.
- Roots por defecto: `SDL_AndroidGetExternalStoragePath()`, su
  subcarpeta `data/`, `CD/`, `files/`, y `SDL_AndroidGetInternalStoragePath()`.
- También puedes añadir otro root:
  ```cpp
  portfs::add_root("/storage/emulated/0/MisJuegos/Onimusha2");
  ```
- Variable de entorno opcional: `PORT_FS_ROOT=/ruta1;/ruta2`.

La intercepción se hace en el enlace con:

```cmake
-Wl,--wrap=fopen -Wl,--wrap=open -Wl,--wrap=stat
-Wl,--wrap=access -Wl,--wrap=opendir
```

Por eso `fopen`, `open`, `stat`, `access` y `opendir` del código original
quedan redirigidos a `port_fs.cpp`; `fread`, `fseek`, etc. siguen funcionando
sobre el `FILE*` ya abierto. Para desactivarlo: `-DPORTFS_INTERCEPT_STDIO=OFF`.

Si no usas `--wrap`, puedes llamar directamente:

```cpp
FILE *f = portfs::fopen("cdrom0:\\DATA\\FILE.BIN;1", "rb");
```

o definir en tu cabecera:

```cpp
#define fopen portfs::fopen
```

`portfs::initialize()` se llama en `SDL_main` después de `SDL_Init`, que es
cuando `SDL_AndroidGetExternalStoragePath()` ya devuelve la ruta válida.

## Entrada: `InputManager`

`src/input_manager.hpp/.cpp` concentra el mando fisico y el HUD tactil.

`port::PadState` expone botones y sticks:

```cpp
port::PadState g_pad;   // buttons + left_x/left_y + right_x/right_y
```

Botones: `Up`, `Down`, `Left`, `Right`, `Triangle`, `Circle`, `Cross`,
`Square`, `L1`, `R1`, `Start`, `L2`, `R2`, `L3`, `R3`, `Select`.

Uso desde el juego:

```cpp
#include "ps2_pad.hpp"

extern port::PadState g_pad;

if (g_pad.pressed(port::PadButton::Cross)) { /* ... */ }
if (g_pad.pressed(port::PadButton::Left))  { /* ... */ }
float lx = g_pad.left_x;   // -1..1
```

`g_pad.active_low()` devuelve el estado invertido, por si el código espera el
formato del mando de PS2 (pulsado = 0). `scePadRead` convierte los sticks a
0..255 (0 = izquierda/arriba, 128 = centro, 255 = derecha/abajo).

### Mando fisico

`InputManager` abre todos los `SDL_GameController` disponibles y reacciona a
`SDL_CONTROLLERDEVICEADDED/REMOVED` (hotplug). Mapeo a DualShock 2:

| SDL | PS2 |
|---|---|
| `A` | Cross |
| `B` | Circle |
| `X` | Square |
| `Y` | Triangle |
| `LEFTSHOULDER` | L1 |
| `RIGHTSHOULDER` | R1 |
| `START` | Start |
| `BACK` | Select |
| gatillo izquierdo | L2 |
| gatillo derecho | R2 |
| click stick izquierdo | L3 |
| click stick derecho | R3 |
| D-pad | Up/Down/Left/Right |
| stick izquierdo | `left_x/left_y` |
| stick derecho | `right_x/right_y` |

Si hay al menos un mando, el HUD tactil se oculta y los toques no se consumen.
Al desconectarlo, el HUD vuelve automaticamente.

### HUD tactil (sin mando)

`DrawHUD()` dibuja con OpenGL ES 3.0 (VAO/VBO + shader, blending alfa):

```
+--------------------------+--------------------------+
|                          |        START             |
|                          |   L1            R1       |
|      STICK ANALOGICO     |        TRIANGULO         |
|        (izquierda)       |  CUADRADO       CIRCULO   |
|                          |        CROSS             |
+--------------------------+--------------------------+
```

- Stick analogico virtual a la izquierda con zona muerta y knob que sigue al
  dedo. Al inclinarlo mas de 0.55 tambien activa la cruceta, para menus.
- Botones PS2 a la derecha: Triangulo, Circulo, Cross, Cuadrado, L1, R1 y
  Start, con simbolos y etiquetas vectoriales (sin fuentes externas).
- Multi-touch real por `SDL_FingerID`: un dedo puede sostener el stick
  mientras otro pulsa Cross/Circulo (mover + absorber almas).
- Los taps rapidos se conservan un frame (`button_latch_`) para que el juego
  los vea aunque el dedo suba antes del siguiente `Frame()`.
- `main.cpp` llama a `Update()` antes de `game::Frame()` y copia el estado a
  `g_pad`; `port_draw_overlay()` dibuja el HUD antes de `SDL_GL_SwapWindow`.
- En Android se desactiva `SDL_HINT_TOUCH_MOUSE_EVENTS` para que un toque no
  llegue duplicado como evento de raton sintetico.

Log del telefono:

```text
I/OnimushaInput: Sin mando fisico: HUD tactil activo
I/OnimushaInput: HUD GLES 3.0 inicializado
I/OnimushaPort: viewport drawable=2400x1080 logical=2400x1080
I/OnimushaPort: Start tactil: loader=1
I/OnimushaInput: Mando conectado: PS4 Controller (instance=1)
I/OnimushaInput: Mando desconectado; HUD tactil activo
```

`port::update_touch_pad()` y las zonas invisibles de `ps2_pad.cpp` siguen
disponibles para pruebas, pero `main.cpp` usa `InputManager`.

### `SDL_RWFromFile`

Para código que usa `SDL_RWops` en vez de `FILE*`:

```cpp
#include "ps2_rw.hpp"

SDL_RWops *rw = PS2_RWFromFile("cdrom0:\\IMAGE\\FILE.BIN;1", "rb");
if (rw) {
    // SDL_RWread / SDL_RWseek / SDL_RWtell
    SDL_RWclose(rw);
}
```

`PS2_RWFromFile` hace lo mismo que el sistema de rutas:

- quita `cdrom0:`,
- cambia `\` por `/`,
- elimina `;1`,
- resuelve mayúsculas/minúsculas y roots de `port_fs`,
- y por último llama a `SDL_RWFromFile` con la ruta real bajo
  `SDL_AndroidGetExternalStoragePath()`.

También está disponible como `port::RWFromPS2File(...)`.

Si el código descompilado llama directamente a `SDL_RWFromFile`, puedes
redirigirlo con:

```cpp
#define SDL_RWFromFile PS2_RWFromFile
```

o añadir `-Wl,--wrap=SDL_RWFromFile` y un `__wrap_SDL_RWFromFile` que llame a
`PS2_RWFromFile`.

## IsoReader

`src/iso_reader.hpp/.cpp` abre `disc.iso` con `SDL_RWops` y posiciones `Sint64`,
así que soporta el archivo de 3,78 GB.

```cpp
#include "iso_reader.hpp"

IsoReader iso;
if (!iso.Open("disc.iso")) {
    SDL_Log("ISO: %s", iso.LastError().c_str());
    return;
}

std::printf("size=%lld sectores=%lld\n",
            (long long)iso.Size(), (long long)iso.SectorCount());

std::vector<std::uint8_t> pvd(IsoReader::kSectorSize);
if (iso.ReadSectors(16, 1, pvd.data())) {
    // PVD ISO9660: 0x01 'CD001'
}

// Lectura por offset de 64 bits para tablas o directorios:
std::uint8_t root[2048];
iso.ReadBytes(16LL * 2048 + 156, root, sizeof root);
```

- `Open("disc.iso")` busca primero en
  `SDL_AndroidGetExternalStoragePath()`, luego en el almacenamiento interno y
  por último en la ruta relativa.
- `ReadSectors(lba, count, buffer)` usa sectores de **2048 bytes** y comprueba
  límites antes de leer.
- `ReadBytes(offset, buffer, bytes)` acepta offsets `Sint64` (>2 GB).
- `Size()` y `SectorCount()` son `Sint64`.
- El ISO de 3,78 GB tiene `1.848.336` sectores de 2048 bytes.

Validado con un archivo disperso de 3.785.392.128 bytes en PC: LBA 16, LBA
1.800.000 y `ReadBytes` en un offset >2 GB.

### Onboarding de disc.iso (Android)

`MainActivity.onCreate` comprueba si existe `disc.iso` en
`getExternalFilesDir(null)` (la carpeta `Android/data/<pkg>/files`) o en
`getFilesDir()`. Si no existe:

1. Reutiliza el URI persistido en `SharedPreferences` (`disc_iso/uri`) y abre
   su `ParcelFileDescriptor`.
2. Si el URI ya no es válido, lanza `Intent.ACTION_OPEN_DOCUMENT` con
   `CATEGORY_OPENABLE` y `*/*` para elegir la imagen desde Descargas.
3. Llama a `takePersistableUriPermission` para conservar el acceso entre
   reinicios.
4. Entrega el fd a `libmain` por JNI (`nativeSetDiscFd`); `IsoReader::OpenFd`
   lo duplica y lee con `pread`, sin copiar los 3,78 GB.

`SDL_main` espera hasta 10 minutos a que Java entregue el fd. Si el usuario
cancela el selector, Java llama a `nativeSetDiscUnavailable` y el juego arranca
sin ISO; el próximo inicio vuelve a pedirlo. Si hay un `disc.iso` local, Java
avisa con `nativeSetDiscFilePresent` y no se abre el selector.

```text
I/OnimushaDisc: Abriendo ACTION_OPEN_DOCUMENT para elegir disc.iso
I/OnimushaDisc: disc.iso listo por fd: disc.iso (3785392128 bytes)
I/OnimushaPort: ISO: usando descriptor del selector: disc.iso
I/OnimushaPort: ISO9660 OK: CD001
```

En el teléfono se validó el selector + fd con un ISO9660 de 1 MiB y el
`disc.iso` real de 3,78 GB por ruta local (`ISO PVD LBA 16: CD001
size=3785392128 sectors=1848336`).

El `ParcelFileDescriptor` sigue siendo propiedad de Java; el lado nativo hace
`dup()` y lo cierra con el `SDL_RWops` de `IsoReader`. Los métodos JNI son
`nativeSetDiscFd`, `nativeSetDiscUnavailable` y `nativeSetDiscFilePresent`.

## OverlayManager

`src/overlay_manager.hpp/.cpp` simula la RAM principal de PS2 y carga los 32
overlays capturados:

- Bloque de **32 MB** alineado a 4096 bytes (`0x00000000..0x01FFFFFF`).
- Ventana alta de **4 MB** (`0x02000000..0x023FFFFF`) para los overlays
  `0x0239xxxx`, que están por encima de la RAM principal.
- Cada `.bin` se carga en la dirección de su nombre:
  `01F64000_856903D1.bin` → `0x01F64000`.
- `Resolve(address)` devuelve el puntero host de la RAM o de la ventana alta.
- `LoadForAddress(address)` elige el overlay con la base más alta que contiene
  esa dirección.

```cpp
OverlayManager overlays;
if (overlays.Initialize("/ruta/a/overlays")) {
    overlays.Load("01F64000_856903D1");
    overlays.LoadForAddress(0x02390000);

    std::uint8_t *p = overlays.Resolve(0x01F64000);
}
```

Comprobado en PC y en el teléfono:

```
OverlayManager: 32 overlays, RAM 32 MB en 0x...1000, ventana alta 4 MB
SELFTEST overlays=32 aligned=1
SELFTEST main 0x01F64000 ptr=... bytes=76280242
SELFTEST high 0x02390000 ptr=... bytes=3600F2FF
```

## Autotest de almacenamiento

Con `-PstorageSelftest` se compila un APK de diagnóstico que, tras
`SDL_Init`/`portfs::initialize()`, abre `disc.iso` (si está en el directorio
externo) y carga dos overlays:

```powershell
.\gradlew.bat assembleDebug -PstorageSelftest
adb logcat -s "SDL/APP"
```

Busca las líneas `SELFTEST`. Sin esa propiedad el autotest no se compila.

## Comprobaciones

- API 29 o superior.
- Solo `arm64-v8a` (el script avisa si eliges otra ABI).
- `GLESv3` es OpenGL ES 3.0; para ES 3.1/3.2 cambia el enlace a `GLESv3` y
  usa `#version 310 es`/`320 es` en los shaders.

## OverlayDispatcher

`src/overlay_dispatcher.hpp/.cpp` asocia cada base virtual de overlay con su
funci�n nativa recompilada:

```cpp
OverlayDispatcher dispatcher;
dispatcher.SetLoader([&](std::uint32_t address) {
    return overlays.LoadForAddress(address); // OverlayManager
});

dispatcher.RegisterOverlay(0x01F64000, 0x28000, [&] {
    NativeEnter(0x01F64000, dispatcher.TargetAddress());
});
dispatcher.RegisterOverlay(0x02390000, 0x40000, [&] {
    NativeEnter(0x02390000, dispatcher.TargetAddress());
});

// Donde el CPU recompilado resuelve un jr/jalr din�mico:
if (dispatcher.JumpToOverlay(target_pc)) {
    return; // el m�dulo nativo ya ejecut�/atendi� el salto
}
```

- `std::unordered_map<uint32_t, std::function<void()>>` base ? handler.
- `sizes_` resuelve saltos a direcciones internas de la ventana, no solo a la base.
- `RegisterExact()` tiene prioridad para funciones internas o variantes como
  `01F90000` A/B, que comparten base.
- `SetLoader()` carga el `.bin` en RAM antes de ejecutar el handler.
- `TargetAddress()` devuelve la direcci�n PS2 exacta del salto; el handler debe
  pasarla al dispatcher del m�dulo nativo.
- Si hay ventanas solapadas, gana la base m�s alta que contiene la direcci�n
  (por ejemplo `02394000` sobre `02390000`).

Probado en PC: bases, entrada exacta, ventana alta, loader y direcci�n fuera de
rango. Compila con el NDK.

## Main traducido (game_main)

`src/game_main.hpp/.cpp` contiene la traduccion C++17 de:

| Original | Traduccion |
|---|---|
| `FUN_00149BF0` | `game::Initialize()` |
| `FUN_001496D0` | `game::Frame()` (una iteracion) |
| `FUN_00124D90` | `dispatch_load_state()` |
| `FUN_00124DC0` | `load_state_idle()` |
| `FUN_00124430` | `enqueue_load_request()` |
| `FUN_001242A0` | `pop_load_request()` |
| `SONY_libgraph_00100b28` | `SDL_GL_SwapWindow()` dentro de `Frame()` |

La tabla de estados es la de **`0x002BA0E0`**, no `0x002CA0E0`. Tiene 24
entradas y se reproduce exacta en `game::kLoadStateTable`. La cola tambien es
la original: `0x001847790` (8 x `0x18`), tail `0x001849484`, head
`0x001849488`, flag `0x001849478`.

Al inicializar se encola la primera peticion real, la misma de
`FUN_00144680 -> FUN_00124360(0x301)`:

```
tipo   = 1
indice = 0x0301
LBA    = 0x001596C7
size   = 0x0011B800
```

En el primer `Frame()` el estado 0 la ve, escribe `1` en `0x0018479F4` y llama
al handler `0x00125740`. Los handlers que aun no estan traducidos pasan por
`game::CallPS2()`, que primero atiende cola, tabla, DMA y VSync, y luego llama
a `game::CallRecompiled()`. El stub debil de `CallRecompiled()` registra una
vez cada direccion y continua. Para enlazar una unidad recompilada basta
definir una version fuerte de `game::CallRecompiled()`; el despachador interno
no se pierde.

El punto de `sceGsSyncV` esta en `Frame()`:

```cpp
CallPS2(0x00161BB0u);
port_vsync();                 // SDL_GL_SwapWindow + VSINT
CallPS2(0x0015FC20u, 0x80);
```

`port_vsync()` tambien pone `iGpffff9130 = 1`, que es la senal de VBlank que
el bucle original espera. El `93DC == 0` del original entra en una ruta de
apagado que nunca retorna; el port registra el aviso una vez y sigue
presentando mientras SDL siga vivo, sin modificar `93DC` ni la logica.

### Log de estados en Android Studio

`dispatch_load_state()` y `load_state_idle()` registran la direccion del
handler con:

```cpp
SDL_Log("Ejecutando estado: 0x%08X", estado);
```

Se registra al entrar en un estado nuevo, no en cada frame, para no inundar
Logcat. En Android Studio aparece con el tag `SDL/APP`:

```text
I/SDL/APP: Ejecutando estado: 0x00124DC0
I/SDL/APP: Ejecutando estado: 0x00125740
```

El ultimo log indica hasta donde llego el juego: `0x00124DC0` es el estado 0
(idle/cola) y `0x00125740` el estado 1 (cargador de script/overlay). Como el
handler `0x00125740` aun es un limite, el estado se queda ahi. Si quieres el
log en cada frame, mueve la linea fuera del `if (state != last_logged_state)`.
En PC (`GAME_MAIN_NO_SDL`) se usa `GAME_LOGI` con el mismo texto.

### Estado actual

- PC: `tests/game_main_smoke.cpp` valida cola `0x301`, LBA/tamano, transicion
  0 -> 1 y 11 frames.
- Telefono (Z2352N, Mali-G57): init completo, cola `0x301`, transicion 0 -> 1,
  latidos en frames 300 y 600, captura con el color de `glClear`.
- Pendiente: traducir el handler de estado 1 (`0x00125740`) y las tablas de
  opcodes `0x002BA120` / `0x002BA1B0`, que son las que eligen y cargan los
  overlays/menus reales. Hasta entonces el estado se queda en 1 y el handler
  nativo es un limite, no una emulacion.

### Pruebas de PC

```powershell
clang++ -std=c++17 -O2 -DGAME_MAIN_NO_SDL -Isrc `
  tests/game_main_smoke.cpp src/game_main.cpp src/ps2_hle.cpp `
  src/ps2_audio_hle.cpp src/ps2_audio_system.cpp `
  src/ps2_memory_card.cpp src/ps2_pad.cpp `
  src/ps2_sdk_stubs.cpp src/ps2_mem.cpp src/ps2_gs_sync.cpp `
  src/ps2_cd_stream.cpp src/game_stubs.cpp src/crash_handler.cpp `
  -o smoke.exe
.\smoke.exe

clang++ -std=c++17 -O2 -DGAME_MAIN_NO_SDL -DGAME_STUBS_NO_RECOMPILED_HOOK -Isrc `
  tests/game_main_override.cpp src/game_main.cpp src/ps2_hle.cpp `
  src/ps2_audio_hle.cpp src/ps2_audio_system.cpp `
  src/ps2_memory_card.cpp src/ps2_pad.cpp `
  src/ps2_sdk_stubs.cpp src/ps2_mem.cpp src/ps2_gs_sync.cpp `
  src/ps2_cd_stream.cpp src/game_stubs.cpp src/crash_handler.cpp `
  -o override.exe
.\override.exe

clang++ -std=c++17 -O2 -Isrc `
  tests/ps2_hle_test.cpp src/ps2_hle.cpp src/ps2_pad.cpp -o hle_test.exe
.\hle_test.exe

clang++ -std=c++17 -O2 -Isrc `
  tests/ps2_audio_hle_test.cpp src/ps2_audio_hle.cpp `
  src/ps2_audio_system.cpp -o audio_test.exe
.\audio_test.exe

clang++ -std=c++17 -O2 -Isrc `
  tests/ps2_audio_system_test.cpp src/ps2_audio_system.cpp `
  -o audio_system_test.exe
.\audio_system_test.exe

clang++ -std=c++17 -O2 -Isrc -DPS2_STATE_NO_ZLIB=1 -DPS2_STATE_NO_GIF=1 `
  tests/quick_state_test.cpp src/quick_state.cpp src/ps2_mem.cpp `
  src/ps2_audio_system.cpp src/overlay_dispatcher.cpp `
  -o quick_state_test.exe
.\quick_state_test.exe

clang++ -std=c++17 -O2 -Isrc `
  tests/character_texture_swap_test.cpp src/ps2_texture_cache.cpp `
  -o character_swap_test.exe
.\character_swap_test.exe

clang++ -std=c++17 -O2 -Isrc `
  tests/ps2_memory_card_test.cpp src/ps2_memory_card.cpp -o mc_test.exe
.\mc_test.exe

clang++ -std=c++17 -O2 -DSDL_MAIN_HANDLED -Isrc `
  -I../../third_party/sdl2/include/SDL2 `
  tests/input_manager_test.cpp src/input_manager.cpp `
  ../../third_party/sdl2/lib/libSDL2.dll.a -o input_test.exe
.\input_test.exe

clang++ -std=c++17 -O2 -Isrc `
  tests/display_layout_test.cpp src/display_layout.cpp -o layout_test.exe
.\layout_test.exe

clang++ -std=c++17 -O2 -Isrc `
  tests/crash_handler_test.cpp src/crash_handler.cpp -o crash_test.exe
.\crash_test.exe

clang++ -std=c++17 -O2 -Isrc `
  tests/ps2_mem_test.cpp src/ps2_mem.cpp -o mem_test.exe
.\mem_test.exe

clang++ -std=c++17 -O2 -Isrc `
  tests/ps2_sdk_stubs_test.cpp src/ps2_sdk_stubs.cpp -o sdk_test.exe
.\sdk_test.exe

clang++ -std=c++17 -O2 -Isrc `
  tests/ps2_globals_test.cpp src/ps2_mem.cpp -o globals_test.exe
.\globals_test.exe

clang++ -std=c++17 -O2 -DGAME_MAIN_NO_SDL -Isrc `
  tests/game_stubs_test.cpp src/game_stubs.cpp src/crash_handler.cpp `
  -o game_stubs_test.exe
.\game_stubs_test.exe

clang++ -std=c++17 -O2 -Isrc `
  tests/ps2_gs_sync_test.cpp src/ps2_gs_sync.cpp src/ps2_mem.cpp `
  -o gs_sync_test.exe
.\gs_sync_test.exe

clang++ -std=c++17 -O2 -Isrc `
  tests/ps2_cd_stream_test.cpp src/ps2_cd_stream.cpp src/ps2_mem.cpp `
  -o cd_test.exe
.\cd_test.exe

clang++ -std=c++17 -O2 -Isrc `
  tests/gif_packet_test.cpp src/gif_packet.cpp src/ps2_texture_cache.cpp `
  -o gif_test.exe
.\gif_test.exe

clang++ -std=c++17 -O2 -Isrc `
  tests/gs_blend_test.cpp src/ps2_alpha_blend.cpp src/gif_packet.cpp `
  src/ps2_texture_cache.cpp `
  -o gs_blend_test.exe
.\gs_blend_test.exe

clang++ -std=c++17 -O2 -Isrc `
  tests/vif1_parser_test.cpp src/vif1_parser.cpp src/ps2_mem.cpp `
  -o vif1_parser_test.exe
.\vif1_parser_test.exe

clang++ -std=c++17 -O2 -Isrc `
  tests/ps2_pad_test.cpp src/ps2_pad.cpp -o ps2_pad_test.exe
.\ps2_pad_test.exe
```

`game_main_override.cpp` define versiones fuertes de `CallRecompiled()` y
`CallRecompiledRet()` y comprueba que la cola y la transicion 0 -> 1 siguen
funcionando. `ps2_hle_test.cpp` valida CD de 2048 bytes, el reporte de mando y
las cadenas DMA END/REF sin SDL. `ps2_sdk_stubs_test.cpp` valida la tabla SDK,
las librerias nativas, los falsos positivos y los hooks.

## HLE de llamadas de sistema (ps2_hle)

`src/ps2_hle.hpp/.cpp` implementa las llamadas que el codigo desensamblado
espera del SDK:

| Funcion PS2 | Implementacion |
|---|---|
| `sceCdRead(lbn, sectores, buffer, mode)` | callback a `IsoReader::ReadSectors`, 2048 bytes/sector, sincrona |
| `sceCdSync(mode)` | devuelve 0 (completada) |
| `scePadRead(port, slot, data)` | reporte Sony de 32 bytes desde `port::PadState` |
| `sceDmaSend(channel, addr)` | recorre la cadena DMA y llama al hook |
| `sceDmaSync(channel, mode, timeout)` | devuelve 0; la HLE procesa de forma sincrona |

### CD

```cpp
IsoReader iso;
iso.Open("disc.iso");
ps2::hle::AttachIsoReader(&iso);
```

`AttachIsoReader` crea un callback con `IsOpen()`/`ReadSectors()`; no acopla
la HLE a la clase. Solo se aceptan sectores de 2048 (`mode[2] == 0`). Hay
estadisticas en `GetCdStats()` y el ultimo error en `LastCdError()`.

### Mando

`main.cpp` ya actualiza `g_pad` desde los eventos tactiles de SDL2. La HLE
solo lo consume:

```cpp
ps2::hle::SetPadState(&g_pad);
std::uint8_t data[32];
scePadRead(0, 0, data);
```

El reporte usa el formato de libpad: byte 0 estado, byte 1 `0x73` (analogico),
bytes 2/3 botones activo-bajo (alto primero), 4-7 sticks centrados, 8-19
presion a cero y 20/21 el word de menus. La tabla de conversion esta en
`PadButtonsActiveLow()`.

Start esta en `(0.75, 0.89)` y no en el borde inferior, porque Android reserva
esa franja para el gesto de navegacion. Los `down` se latchean un frame para
que un tap rapido no se pierda entre dos `update_touch_pad()`.

### DMA/GIF

```cpp
ps2::hle::AttachMemory(game::Ram(), game::RamSize());
ps2::hle::SetDmaSendHook([](const ps2::hle::DmaTransfer &dma) {
    for (const auto &tag : dma.tags) {
        const std::uint8_t *data = ps2::hle::TagData(tag);
        std::uint32_t bytes = ps2::hle::TagBytes(tag);
        // parsear GIF/VIF y alimentar Vu1Renderer / GLES 3.0
    }
});
```

`DmaTag` usa el formato source chain: ID en bits 28-30, QWC en 0-15, IRQ en
31, ADDR en 32-62 y SPR en 63. Para END/CNT/NEXT/CALL/RET los datos empiezan
en `tag_address + 16`; para REF/REFE/REFS en el campo ADDR. El paquete real de
`FUN_0014A340` es:

```
0x002C4040: DMA END tag (id=7, qwc=14)
0x002C4050: 14 qwords de datos GIF (224 bytes)
```

En `game_main.cpp`, `FUN_0014A2E0` llena la tabla de canales DMA
(`0x10008000..0x1000D400`, GIF en `0x1000A000`) y `FUN_0014A340` llama a
`sceDmaSync`/`sceDmaSend`. El blob estatico de `0x2C4040` se copia a la RAM
emulada porque en la PS2 forma parte del segmento de datos del ELF.

Resultado en el telefono:

```
I/OnimushaHLE: sceCdRead: lba=16 sectores=1 (2048 bytes)
I/OnimushaPort: HLE sceCdRead LBA 16: CD001
I/OnimushaPort: HLE scePadRead: id=0x73 botones=0xFFFF
I/OnimushaPort: HLE DMA: GIF canal=0x1000A000 source=0x002C4040 tags=1 parsed=1
I/OnimushaPort: HLE DMA tag: id=7 qwc=14 data=0x002C4050 valid=1
```

Limites actuales: solo sectores de 2048, un mando en `(0,0)`, sin presion ni
sticks analogicos, y el parser DMA resuelve RAM principal (no scratchpad). El
hook ya entrega los tags; el siguiente paso es el parser GIF y `Vu1Renderer`.

### VIF1 / VU1

`src/vif1_parser.hpp/.cpp` intercepta los DMA1 y procesa los VIFcode reales:

- Codigo en bits 30-24, NUM en 23-16, inmediato en 15-0.
- UNPACK 0x60-0x7F con direccion qword en `imm & 0x3FF`; bit 15 suma TOPS.
- V4_32/V4_16/V4_8/V4_5, V3_32/16/8, V2_32/16/8, S_32/16/8.
- STCYCL (CL/WL), BASE/OFFSET/ITOP, doble buffer, STMASK/STROW/STCOL,
  MPG, DIRECT/DIRECTHL, MSCAL/MSCALF/MSCNT.
- Snapshot de 16 KB de memoria VU1 y un `Capture` por MSCAL; `main.cpp`
  dibuja todos los captures de la cadena y los descarta.
- El juego usa END/NEXT con QWC y datos detrás (`0x7000000E`), así que se
  consumen igual que en `ps2_hle`.
- Layout fijo de Onimusha: matrices `0x0000/0x0D00`, posiciones `0x1000`,
  normales `0x1800`, UVs `0x2000`, pesos `0x2800`.
- Hook de escritura directa a `D1_CHCR` (`0x10009000`), además de
  `sceDmaSend`.

Cada vector UNPACK ocupa un qword de 16 bytes en VU1, asi que los buffers
usan stride 16 aunque el elemento de origen sea V3_32 (12 bytes) o V2_32
(8 bytes). El parser expande a lanes de 32 bits.

`src/vu1_renderer.hpp/.cpp` sube posiciones, UV, normales, color, fog y pesos
a un VAO/VBO y aplica un vertex shader GLSL ES 3.00:

```text
position -> skinning (hasta 32 huesos) -> world -> view -> projection
```

Las matrices PS2 son row-major; se suben con `glUniformMatrix4fv(...,
GL_TRUE, ...)` para adaptarlas a GLSL.

`BuildPacket(capture, layout, packet)` usa roles explícitos.
`BuildPacketOnimusha()` fija el layout real: matrices en `0x0000`/`0x0D00`,
posiciones en `0x1000`, normales en `0x1800`, UVs en `0x2000` y pesos en
`0x2800`. `BuildPacketAuto()` queda como fallback heurístico.

Documento detallado de los tags y `D1_CHCR`:
`games/onimusha2/disasm/VIF1_DMA_PACKETS.md`.

Los 9 `.bin` de `games/onimusha2/vu1` son microcódigo VU1. Se analizan con:

```powershell
python scripts/analyze_vu1.py
```

Documento detallado: `games/onimusha2/disasm/VIF1_VU1_PARSER.md`.

### Texturas PSMT8/PSMT4 y CLUT

`src/ps2_texture_cache.hpp/.cpp` implementa el decodificador de paletas que
usan los menús, letras y pantallas de título de Onimusha 2.

- Cada transferencia GIF (`BITBLTBUF` + `TRXREG` + `TRXDIR host->local`) se
  guarda por `DBP`, que coincide con `TEX0.TBP0`.
- Las CLUT se guardan por `CBP` y se decodifican con `CPSM` PSMCT32/24/16/16S.
- Soporta PSMT8, PSMT4, PSMT8H, PSMT4HL y PSMT4HH.
- `TEX0_1` + `TEX0_2` se combinan para obtener TBP0/PSM/TW/TH/CBP/CPSM/CSM/
  CSA/CLD/TCC. Antes solo se leían los bits bajos.
- `CSA` selecciona el banco de 16 colores de las texturas de 4 bits.
- La cache guarda un shadow lineal de 4 MB de VRAM (`Ps2TextureCache::Vram()`).
- El parser ya no confunde los índices de una textura (0,1,2,3...) con un
  GIFtag cuando hay una transferencia pendiente.
- Cada `UploadImage()` reemplaza el registro del mismo `DBP` e invalida la
  cache decodificada; la clave incluye la `generation` de textura y CLUT. Al
  cambiar de personaje, Jubei/Ekei/Magoichi/Kotaro/Oyu pueden reutilizar los
  mismos TBP0/CBP sin heredar píxeles del set anterior.
- Regresión: `tests/character_texture_swap_test.cpp` recorre los cinco
  personajes, vuelve a Jubei y comprueba textura indexada, CLUT, textura CT32,
  HUD compartido y shadow de VRAM tras cada cambio.

Documento detallado: `games/onimusha2/disasm/PS2_TEXTURE_CACHE_CLUT.md`.

### Pipeline 2D + Z-buffer + 3D

`src/gs_render_pipeline.hpp/.cpp` ordena el frame antes de
`SDL_GL_SwapWindow`:

1. **Fondo 2D**: todos los sprites GIF con `GL_DEPTH_TEST` desactivado y
   `glDepthMask(GL_FALSE)`.
2. **Z-buffer**: si el juego subió un depth map por `GIF IMAGE`
   (`BITBLTBUF.DPSM` = PSMZ32/24/16/16S), se sube a una textura
   `GL_DEPTH_COMPONENT24` (fallback 16).
3. **Modelos 3D**: `GL_DEPTH_TEST` + `GL_LEQUAL` + `glDepthMask(GL_TRUE)`.
   Con Z-buffer se dibujan en un FBO cuyo depth attachment contiene el mapa;
   luego se compone el color sobre el fondo con blending premultiplicado.
   Sin Z-buffer se dibujan directamente sobre el framebuffer por defecto.

`gif_renderer.cpp` ya no dibuja al parsear: encola `QueueBackground()` y
`QueueDepthMap()`. El hook VIF1 encola `QueueModel()`. `port_vsync()` llama a
`ExecuteFrame()` antes del swap.

El depth map se conserva entre frames hasta que llegue otro, porque los
escenarios de Onimusha son estáticos. El parser Z acepta `TRXDIR` host→local.

Documento detallado: `games/onimusha2/disasm/RENDER_PIPELINE_2D_3D.md`.

## Overlays de arranque: logos y menu

`src/room_overlay.hpp/.cpp` traduce las dos variantes que comparten la base
`0x01F90000`:

| Archivo | Nombre interno | Evento | Papel |
|---|---|---|---|
| `01F90000_86124295.bin` | `Room010.bin` | `E000C` | secuencia de logos |
| `01F90000_D3325583.bin` | `Room000.bin` | `E000D` | menu principal |

Los overlays de `0x01F64000/0x01F70000` son `Pl00.bin` (jugador), no menus.

La traduccion cubre:

- Init de recursos y callbacks (`FUN_01f92080`, `FUN_01f920d0`).
- Wrapper de frame con el chequeo `E000C` / `E000D`.
- Dispatcher y maquina de estados:
  - `Room010`: 9 estados; espera las fases `0x17DE832 == 1..7` y cambia los
    estados de los paquetes GS.
  - `Room000`: 3 estados; con `ctx+0x14 == 1` crea los paquetes base y con
    `ctx+0x14 == 7` crea un paquete por cada recurso `0x6BC/0x6C0/0x6C4`.
- Contexto con offsets exactos (`+0x14`, `+0x70`, `+0x6BC`, `+0x784`,
  `+0x788..0x794`).

Las dependencias del ejecutable principal se inyectan con
`onimusha2::room::RoomServices`: `input_ready()`, `boot_phase()`, lectura y
escritura de RAM, `call()`, `allocate_packet()` (FUN_00137450),
`set_packet_u16/u32()` y `set_frame_callback()`. Los callbacks graficos
`FUN_01f92460` y `FUN_01f927d0` quedan como hooks para el renderer GS/VU1.

Prueba de PC:

```powershell
clang++ -std=c++17 -O2 -Isrc `
  tests/room_overlay_test.cpp src/room_overlay.cpp -o room_test.exe
.\room_test.exe
```

Resultado:

```
PASS rooms: Room010 E000C 0->9, Room000 E000D 0->3, 4+2+3 paquetes
```

Documento detallado: `games/onimusha2/disasm/ROOM_OVERLAYS.md`.

## Audio nativo (`Ps2AudioSystem`)

`src/ps2_audio_system.hpp/.cpp` sustituye al SPU2/IOP para el audio del juego:

- RAM SPU2 emulada de 2 MB (direcciones de 21 bits).
- 2 cores x 24 voces (`SD_VOICE(core, voice)`), pitch, volumen L/R y loop.
- Decodificador PS2/PSX ADPCM: bloque de 16 bytes con cabecera
  `shift/filter`, byte de flags y 14 bytes de nibbles (28 muestras).
- Cola de streaming para musica/voces leidas de `disc.iso`.
- Mezcla PCM S16 estereo a 48000 Hz en el callback de
  `SDL_OpenAudioDevice`.

Formato del bloque ADPCM:

```text
byte 0   shift (bits 0..3) + filter (bits 4..7)
byte 1   flags: bit0 loop end, bit1 loop repeat, bit2 loop start
bytes 2..15  28 nibbles (primero el bajo) = 28 muestras
```

Filtros de prediccion (escala 64): `f0=0`, `f1=60`, `f2=115,-52`,
`f3=98,-55`, `f4=122,-60`.

La API libsd se engancha en `ps2_audio_hle.cpp`:

| Llamada | Efecto |
|---|---|
| `sceSdRemoteInit` / `sceSdInit` | abre el dispositivo SDL |
| `sceSdTransToIOP(ee, spu, size, blocking)` | copia el banco a RAM SPU2 |
| `sceSdVoiceTrans` / `sceSdBlockTrans` | copia voces/bloques a `TSA` |
| `sceSdSetParam` | volumen, pitch, ADSR |
| `sceSdSetSwitch` | KON/KOFF por mascara de 24 voces |
| `sceSdSetAddr` | TSA, SSA, LSAX, NAX |
| `sceSdSetCoreAttr` | mute y atributos de core |

El streaming se alimenta desde el hilo de carga:

```cpp
#include "ps2_audio_hle.hpp"

// `adpcm` son bloques completos de 16 bytes leidos de disc.iso.
ps2::audio::StreamAdpcm(adpcm, bytes, /*loop=*/true);
```

Limites actuales:

- `sceSdRemote` sigue sin dispatcher RPC completo: se registran los comandos
  `0x80xx` para mapearlos con Logcat, pero no se ejecutan.
- ADSR simplificado (ataque/decay/sustain/release aproximados); no hay
  reverb, modulacion ni ruido.
- El streaming mezcla un solo flujo de musica/voz ademas de las 48 voces.

`game_main.cpp` aplica `Invoke()` antes de `CallRecompiled()`; una
implementacion recompilada de libsd nunca se ejecuta y no puede bloquear.
`main.cpp` entrega `game::Ram()` como memoria EE para resolver los punteros
de `sceSdTransToIOP`; `Ps2AudioSystem` inicializa el subsistema de audio de
SDL bajo demanda.

Log del telefono:

```text
I/OnimushaAudio: SDL audio: 48000 Hz, 2 canales, 1024 muestras, dispositivo=2
I/OnimushaAudio: TransToIOP: EE=0x00123456 SPU2=0x01000 size=32768
I/OnimushaAudio: keyon voz=0 core=0 addr=0x01000 pitch=0x1000
I/OnimushaAudio: stream: +16384 bytes (total=16384 loop=1)
```

Documento detallado: `games/onimusha2/disasm/PS2_AUDIO_SYSTEM.md`.

## Memory Card (`Ps2MemoryCard`)

`src/ps2_memory_card.hpp/.cpp` implementa libmc sobre archivos normales en:

```text
<SDL_AndroidGetInternalStoragePath()>/memcard0/
```

Funciones enlazadas por direccion (libmc estatico de SLUS_203.93):

| Direccion | Funcion |
|---|---|
| `0x0010E288` | `sceMcInit` |
| `0x0010EDC0` | `sceMcGetInfo` |
| `0x0010E5E8` | `sceMcOpen` |
| `0x0010E758` | `sceMcClose` |
| `0x0010E810` | `sceMcSeek` |
| `0x0010E978` | `sceMcRead` |
| `0x0010EA98` | `sceMcWrite` |
| `0x0010EC88` | `sceMcSync` |
| `0x0010E720` | `sceMcMkDir` |
| `0x0010EF48` | `sceMcGetDir` |
| `0x0010F128` | `sceMcChDir` |
| `0x0010F278` | `sceMcFormat` |
| `0x0010F340` | `sceMcDelete` |
| `0x0010F860` | `sceMcUnformat` |
| `0x0010F950` | `sceMcGetEntSpace` |

Contrato asincrono de libmc:

```text
sceMcOpen(...)                  -> 0 (RPC aceptado)
sceMcSync(1, &cmd, &result)     -> 1, result = fd o codigo negativo
sceMcSync(1, ...) otra vez      -> -1 (no hay operacion pendiente)
```

`Ps2MemoryCard` ejecuta el trabajo de inmediato, pero guarda el resultado
hasta que el juego llama a `sceMcSync`, igual que MCSERV. Los codigos son los
de `libmc-common.h`: `0`, `-1` tarjeta cambiada, `-2` sin formato, `-3`
llena, `-4` no existe, `-5` permiso/ruta, `-6` directorio no vacio.

Ejemplo de uso interno:

```cpp
ps2::mc::System().Initialize(SDL_AndroidGetInternalStoragePath());
ps2::mc::SetEeMemory(game::Ram(), game::RamSize());

// En game_main.cpp, antes de ps2::sdk::IsNativeAddress:
if (ps2::mc::IsMemoryCardAddress(address)) {
    ps2::mc::Invoke(address, a0, a1, a2, a3, nullptr);
    return;
}
```

Almacenamiento:

- Nombres PS2 como `BISLPS-XXXXX/icon.sys` se guardan con la misma ruta
  relativa dentro de `memcard0/`.
- Se rechazan rutas absolutas fuera de la tarjeta, `..` y componentes que
  escapen de `memcard0/`.
- `sceMcFormat`/`sceMcUnformat` vacian la carpeta.
- `sceMcGetDir` rellena `sceMcTblGetDir` (64 bytes) con nombre, tamano y
  atributos.
- Fechas: se usa `last_write_time` del archivo host.

Log del telefono:

```text
I/OnimushaMC: memcard0=/data/data/org.ps2native.recomp/files/memcard0
I/OnimushaMC: Init OK
I/OnimushaMC: Open BISLPS-XXXXX/save.dat mode=0x202 fd=3
I/OnimushaMC: Sync cmd=0x02 result=3
```

Limite actual: `sceMcGetDir` necesita 6 argumentos (maxent y tabla llegan en
`t0`/`t1`), pero `CallPS2()` solo entrega 4. La clase implementa el listado
completo en `GetDir()`/`GetDirEx()`; el dispatcher de `0x0010EF48` queda con
0 entradas hasta ampliar la convencion de llamada.

Documento detallado: `games/onimusha2/disasm/PS2_MEMORY_CARD.md`.

## Memoria PS2 (`PS2_MEM_READ` / `PS2_MEM_WRITE`)

`src/ps2_mem.hpp/.cpp` proporciona la RAM emulada y el acceso interceptado a
hardware:

```cpp
#include "ps2_mem.hpp"

uint32_t v = PS2_MEM_READ<uint32_t>(0x00100000);
PS2_MEM_WRITE<uint32_t>(0x00100000, v);
PS2_MEM_WRITE<uint64_t>(0x12001000, 0x8000000000000000ull);
```

- `0x00000000..0x01FFFFFF` → `ps2::ps2_ram[32 * 1024 * 1024]` alineado a 64.
- `0x10000000..0x12001FFF` → shadow de DMA, INTC, timers y GS, con log
  `PS2HW` en Logcat.
- `KSEG0`/`KSEG1` (`0x80000000+`/`0xA0000000+`) se normalizan.
- Lecturas de hardware devuelven el último valor escrito; se puede instalar
  `SetHardwareReadHook` para emular bits de estado (DMA/GS) y evitar
  busy-waits.
- `SetHardwareWriteHook` permite reaccionar a escrituras GS/DMA/INTC.
- `SetHardwareLogging(false)` silencia el log sin perder el shadow.

Ejemplo de Logcat:

```text
I/PS2HW: WRITE32 DMA 0x10008000 = 0x0000000000000105
I/PS2HW: WRITE32 INTC 0x1000F000 = 0x0000000000000001
I/PS2HW: WRITE64 GS 0x12001000 = 0x8000000000000000
```

El bloque `0x02000000..0x023FFFFF` sigue siendo responsabilidad de
`OverlayManager`; el scratchpad `0x70000000` no está cubierto. Además,
`ps2::ps2_ram` es un bloque nuevo: para compartirlo con el port actual hay que
migrar `game::Ram()` a `Ps2Memory::Ram()` o copiar el estado.

Documento detallado: `games/onimusha2/disasm/PS2_MEMORY_LAYER.md`.

## Stubs de SDK de PS2 (`ps2_sdk_stubs`)

`src/ps2_sdk_stubs.h/.cpp` contiene la tabla de 229 funciones SDK
identificadas en Ghidra. 78 tienen sustituto nativo/HLE:

```text
libgraph 17, libdma 11, libcdvd 31, libmc 3, libsd 3, libpad2 13
```

`libvu0`, `libkernel`, `libc` y `crt` estan identificadas pero no se
interceptan todavia: siguen ejecutandose en el codigo recompilado o en el
runtime.

`game_main.cpp` consulta la tabla antes de `CallRecompiled()`:

```cpp
if (ps2::sdk::IsNativeAddress(address)) {
    ps2::sdk::Call(address, a0, a1, a2, a3, nullptr);
    return;
}
```

Los casos especiales (`FlushCache`, cola del loader, `sceGsSyncV`) y el stub de
audio van antes, para conservar su comportamiento. El stub registra una sola
linea por direccion:

```text
I/PS2SDK: stub 0x00100E40 libgraph (FUN_00100e40) -> 0x00000000
```

Se puede enlazar una funcion concreta a C++/SDL2 con
`ps2::sdk::SetHook(...)` o una libreria entera con
`ps2::sdk::SetLibraryHook(...)`.

La tabla se regenera desde el CSV de Ghidra:

```powershell
$env:JAVA_HOME = "tools\ghidra\jdk-25.0.5+8"
tools\ghidra\ghidra_12.2_PUBLIC\support\analyzeHeadless.bat `
  tools\ghidra_projects Onimusha2EE4 `
  -process "SLUS_203.93" -noanalysis `
  -scriptPath scripts\ghidra `
  -postScript IdentifyPs2Sdk.java `
  games\onimusha2\disasm\sdk_report

powershell -ExecutionPolicy Bypass `
  -File scripts\generate_ps2_sdk_stubs.ps1
```

`ps2_sdk_ranges.txt` se puede pasar a `ExportAtlas.java` para que el atlas no
descompile esas funciones. Documento detallado:
`games/onimusha2/disasm/PS2_SDK_STUBS.md`.

## Globals PS2 (`ps2_globals.h`)

`src/ps2_globals.h` mapea los globals absolutos de `main` y del cargador
directamente sobre `ps2::ps2_ram`:

```cpp
#include "ps2_globals.h"

uRam017e0350 = 0;       // 0x0017E0350, estado de la hebra del cargador
iGpffff9130 = 1;        // 0x001849130, flag de VSync
g_frame_flags[0] = 1;   // 0x00172A378..0x00172A37D
g_dma_channels[2] = 0x1000A000u;
```

Incluye:

- Escalares de `FUN_001496D0`: `iGpffff9130`, `cGpffff93d4`,
  `cGpffff93dc`, `bGpffff9460`, `sGpffff93f8`, `sGpffff93fc`,
  `bGpffff9414`, `uGpffff945c`, `uGpffff9458`, `cRam008ace60`,
  `cGpffff9418`, `sGpffff941c`, los seis `bRam0172a37x`.
- Bloques de datos: draw envs, paquete GIF `0x002C4040`, listas de tags DMA,
  tabla `0x000898C50`, canales DMA.
- Cargador: cola `0x001847790`, contexto `0x0018479F0`, estado
  `0x0018479F4`, flag/head/tail, tablas `0x002B4660`, `0x002BA0E0`,
  `0x002BA120`, `0x002BA1B0`, `0x002C3CB8`.
- Nombres de módulos IOP: `SIO2MAN.IRX`, `LIBSD.IRX`, etc.
- Constantes de init/display: `DAT_002c40f0`, `DAT_002c4100`,
  `uGpffff92c8..d0`.

Las direcciones de hardware GS (`0x12001000`, `0x12000080`, `0x12000090`) no
están en `ps2_ram`; se acceden con `PS2_MEM_READ/WRITE` y `ps2_mem` las
redirige al shadow.

El header define direcciones, no carga el contenido inicial del ELF. Las
cadenas de módulos y las tablas solo tendrán sus valores reales si se copia el
segmento de datos del ELF a `ps2_ram` en los mismos offsets. Ahora mismo el
port solo copia el paquete GIF de `0x002C4040`.

Prueba de PC:

```powershell
clang++ -std=c++17 -O2 -Isrc `
  tests/ps2_globals_test.cpp src/ps2_mem.cpp -o globals_test.exe
.\globals_test.exe

clang++ -std=c++17 -O2 -DGAME_MAIN_NO_SDL -Isrc `
  tests/game_stubs_test.cpp src/game_stubs.cpp src/crash_handler.cpp `
  -o game_stubs_test.exe
.\game_stubs_test.exe
```

Resultado:

```text
PASS ps2_globals: offsets, escalares, arrays, strings y tablas
```

## Traza del orden de `main` (`game_stubs`)

`src/game_stubs.h/.cpp` contiene stubs de las 29 direcciones `FUN_` que
`main` invoca por `CallPS2`, más `FUN_00149BF0`. Cada stub hace:

```cpp
STUB_LOG("FUN_001701B0");
return 0;
```

y `game::stubs::LogCall()` registra las direcciones nativas/SDK antes de
despacharlas. `CallRecompiled()/CallRecompiledRet()` usan la tabla de stubs,
así que el orden real de `main` aparece en Logcat:

```text
[GAME_STUB] MAIN -> 0x001014B8 FUN_001014B8 (native)
[GAME_STUB] MAIN -> 0x00100EE0 FUN_00100EE0 (native)
[GAME_STUB] FUN_001701B0
[GAME_STUB] FUN_00150EF0
[GAME_STUB] FUN_0019AE20
[GAME_STUB] MAIN -> 0x00124D90 FUN_00124D90 (native)
[GAME_STUB] FUN_0019AE90
```

En Android se lee con:

```powershell
adb logcat -s SDL/APP | Select-String GAME_STUB
```

Notas:

- `FUN_00117520` se llama `0x1000` veces en la init; solo se registra la
  primera para no inundar Logcat.
- `FUN_00149BF0` está stubbeado: si quieres ver también el orden interno de la
  init, compila con `-DGAME_STUBS_NO_INIT_STUB` y usa `game::Initialize()` o
  exporta `FUN_00149BF0`.
- Los stubs devuelven `0`. Si alguna función necesita `true`, cambia su
  `return` en `game_stubs.cpp`.
- Para el test de override, compila con `-DGAME_STUBS_NO_RECOMPILED_HOOK`
  porque `game_main_override.cpp` ya define `CallRecompiled()`.

## Bloqueo de DMA/VIF (`ps2_gs_sync`)

`0x00100B28` es `sceGsSyncPath`, no un simple VSync. En la ruta `mode == 0`
espera a que se limpien:

```text
0x10009000  DMA VIF1 CHCR.STR (bit 0x100)
0x1000A000  DMA GIF  CHCR.STR (bit 0x100)
0x10003C00  VIF1 status (mask 0x1F0003)
0x10003020  GIF status/mode (mask 0xC00)
COP2 vi13   VU1 status (bit 0x100)
```

En Android no hay DMAC que limpie `STR`, así que el shadow de `ps2_mem` se
queda con el bit puesto y el bucle recorre el timeout de `0x01000000`
iteraciones. `src/ps2_gs_sync.hpp/.cpp` lo resuelve:

```cpp
ps2::gs::CompletePath();          // limpia STR/CIS/VIF1
ps2::gs::sceGsSyncPath(0);        // devuelve 0 inmediatamente
```

`port_vsync()` llama a `CompletePath()` y además marca `GS_CSR` bit 13 y
`iGpffff9130` (`0x001849130`) para que main no se quede en:

```c
do {
} while (iGpffff9130 == 0);
```

Documento detallado: `games/onimusha2/disasm/PS2_GS_SYNC.md`.

## Lectura de DVD (`ps2_cd_stream`)

El loader de `SLUS_203.93` usa streaming de libcdvd:

```text
FUN_00125740 -> FUN_00124C20 -> sceCdStRead (0x0010DEA8)
```

`src/ps2_cd_stream.hpp/.cpp` implementa esa API conectada directamente a
`IsoReader::ReadSectors`:

```cpp
ps2::cd::AttachIsoReader(&g_iso);        // en main.cpp
ps2::cd::sceCdStStart(lba, 0);
int err = 0;
ps2::cd::sceCdStRead(sectores, destino, 0, &err);
```

La copia se hace sobre `ps2::ps2_ram`:

```cpp
reader->ReadSectors(lba, count,
    ps2::ps2_ram + ps2::Ps2Memory::PhysicalAddress(destination));
```

Se soportan los alias `0x20000000`, KSEG0 y KSEG1. El loader usa
`0x20000000 | 0x1827740` para su double buffer, y `ps2_mem` ahora normaliza
ese alias.

Prueba de PC:

```powershell
clang++ -std=c++17 -O2 -Isrc `
  tests/ps2_cd_stream_test.cpp src/ps2_cd_stream.cpp src/ps2_mem.cpp `
  -o cd_test.exe
.\cd_test.exe
```

Resultado:

```text
PASS ps2_cd_stream: ISO -> ps2_ram, alias 0x20000000, LBA y errores
```

Documento detallado: `games/onimusha2/disasm/PS2_CD_STREAM.md`.

## Menu flotante Dear ImGui

`third_party/imgui` (v1.93.1 WIP) se compila como target `imgui` con los
backends SDL2 y OpenGL ES 3.0.

`src/port_menu.hpp/.cpp` implementa:

- Apertura/cierre con **tres dedos** (`SDL_FingerID`) o **F3**.
- FPS reales (`ImGuiIO::Framerate`) y frame time.
- `gs_vram` simulada: 4 MB de `Ps2TextureCache::vram_`, uso aproximado
  recalculado una vez por segundo.
- Overlay activo 0..31: catalogo de `overlays/*.bin`, base/stamp y ultima
  `FUN_xxxxxxxx` via `ps2::crash::LastFunction()`.
- Selector de overlay/mapa con boton **Forzar salto**.
- Botones **Guardar (F5)** / **Cargar (F9)** de estado rapido.
- Checkbox **Widescreen 16:9** conectado a `ps2::render::SetWidescreen`.
- Combo **Resolucion 3D**: 1x, 2x o 3x (`ps2::render::SetInternalResolutionScale`).
- Checkbox **60 FPS desbloqueados**: cambia el divisor NTSC del pacer
  (`port::settings::SetUnlocked60Fps`).

El menu se dibuja en `port_draw_overlay()`, despues del HUD tactil y antes de
`SDL_GL_SwapWindow`. Los toques del menu se traducen a eventos de ImGui con
`ImGuiMouseSource_TouchScreen`.

Log del telefono:

```text
I/OnimushaMenu: ImGui inicializado (32 overlays)
I/OnimushaMenu: menu abierto por F3
I/OnimushaMenu: widescreen=1
I/OnimushaGame: overlay jump solicitado: #12 ... (loader state=1)
```

Documento detallado: `games/onimusha2/disasm/DEBUG_MENU_IMGUI.md`.

## Resolucion interna 3D

`ps2::render::SetInternalResolutionScale(1|2|3)` cambia el tamano del FBO de
modelos:

| Escala | Resolucion 3D (4:3) |
|---:|---:|
| 1x | 640x448 |
| 2x | 1280x896 |
| 3x | 1920x1344 |

El fondo 2D sigue dibujandose a la resolucion del contenido. Los modelos se
renderizan en el FBO con su depth map escalado (nearest) y luego se componen
con `GL_LINEAR`; el resultado reduce aliasing sin tocar menus ni HUD. En
widescreen se conserva la altura nativa y se ajusta el ancho al aspect de la
ventana. Si el tamano supera `GL_MAX_TEXTURE_SIZE`, se recorta y se registra
en Logcat.

## Modo 60 FPS

El bucle de `main.cpp` usa un divisor NTSC:

```cpp
int frame_divisor = port::settings::Unlocked60Fps() ? 1 : 2;
double target_fps = 60.0 / frame_divisor;  // 60 o 30
FramePacer pacer(target_fps);
g_game_delta_time = 1.0f / target_fps;
```

Al cambiar el checkbox, el `FramePacer` se reajusta y el `delta_time` se
actualiza. El juego recompilado sigue avanzando por frame; `g_game_delta_time`
queda disponible para los hooks que escalen movimiento. El menu muestra los
FPS reales para comprobar que el modo de 60 Hz se mantiene constante.

Documento detallado: `games/onimusha2/disasm/DEBUG_MENU_IMGUI.md`.

## Estado rapido (QuickState)

`src/quick_state.hpp/.cpp` implementa `ps2::state::SaveQuickState()` y
`LoadQuickState()`. El archivo es un contenedor versionado con zlib del NDK:

```text
QPS2QST1 | version=1 | num_secciones
  seccion: tag[4] flags u32 raw_size u64 comp_size u64 crc32 u32 reservado u32
```

Secciones:

| Tag | Contenido |
| --- | --- |
| `META` | texto/fecha de la captura |
| `PS2R` | `ps2_ram` completa (32 MB) |
| `GAMR` | `game::Ram()` (36 MB) |
| `GSVR` | `gs_vram` simulada (4 MB) |
| `GFXC` | registros de `Ps2TextureCache` (texturas y CLUT) |
| `AUDS` | `Ps2AudioSystem` (SPU2 2 MB, 48 voces, stream) |
| `OVDS` | `OverlayDispatcher` (target/base/mapa de tamanos) |

Cada seccion se comprime con `compress2` en nivel `Z_BEST_SPEED` (nivel 1) y
lleva CRC32. `LoadQuickState` descomprime y valida todas las secciones antes de
escribir memoria, para que un archivo truncado no deje el estado a medias. El
guardado escribe `quickstate.qs.tmp` y lo renombra de forma atomica sobre
`quickstate.qs`.

Registro desde `main.cpp`:

```cpp
ps2::state::SetStorageDirectory(internal_storage_path());
ps2::state::SetGameMemory(game::Ram(), game::RamSize());
ps2::state::SetGsVram(ps2::gif::VramDataMutable(),
                      static_cast<std::uint32_t>(ps2::gif::VramSize()));
ps2::state::SetOverlayDispatcher(&dispatcher);  // opcional
ps2::state::SaveQuickState("quickstate.qs");
ps2::state::LoadQuickState("quickstate.qs");
```

En el telefono el archivo queda en
`/data/data/org.ps2native.recomp/files/quickstate.qs`. Se dispara con **F5/F9**
o con los botones **Guardar/Cargar** del menu ImGui. Una captura real del loader
state 1 ocupa unos 340 KB y tarda ~0.3 s porque las 70 MB de RAM estan casi
todas a cero.

Prueba de ida/vuelta en PC (sin zlib/GIF: usa el camino sin comprimir):

```powershell
clang++ -std=c++17 -O2 -Isrc -DPS2_STATE_NO_ZLIB=1 -DPS2_STATE_NO_GIF=1 `
  tests/quick_state_test.cpp src/quick_state.cpp src/ps2_mem.cpp `
  src/ps2_audio_system.cpp src/overlay_dispatcher.cpp `
  -o quick_state_test.exe
.\quick_state_test.exe
```

Log del telefono:

```text
I/OnimushaState: guardado /data/data/org.ps2native.recomp/files/quickstate.qs (349623 bytes, 6 secciones)
I/OnimushaPort: QuickState guardado=1 ()
I/OnimushaState: cargado /data/data/org.ps2native.recomp/files/quickstate.qs (6 secciones)
I/OnimushaPort: QuickState cargado=1 ()
```

## Presentacion: pillarbox 4:3 y widescreen

`src/display_layout.hpp/.cpp` calcula el viewport de presentacion:

- El contenido PS2 se trata como 4:3 (640x448) y se centra con enteros.
- Si la ventana es mas ancha (2400x1080), se aplica pillarbox:
  `1440x1080` en `x=480`, con bandas negras laterales.
- Si es mas estrecha que 4:3, se aplica letterbox superior/inferior.
- `ClearBars()` pinta las bandas a negro con `glScissor` + `glClear`.

La fase 2D (menus, texto, HUD del juego) usa `glViewport` 4:3 y la matriz
ortografica PS2:

```text
(0,0) -> (-1,1)       (640,448) -> (1,-1)
```

`gif_renderer` recibe esa matriz en `u_projection`; ya no estira los sprites
al ancho completo.

Modo widescreen:

```cpp
ps2::render::SetWidescreen(true);
ps2::render::ToggleWidescreen();   // F2 en teclado/emulador
```

- El 2D sigue en 4:3 (los menus no se deforman).
- Los modelos VU1 usan toda la ventana.
- La fila X de la proyeccion se multiplica por
  `(4/3) / (ancho/alto)`: 0.75 en 1920x1080 y 0.6 en 2400x1080.
- El FOV vertical y la profundidad no cambian; solo se ensancha el horizontal.
- En widescreen se ignora el depth map 4:3 del juego y los modelos se dibujan
  directos para no estirar la oclusion.

Documento detallado: `games/onimusha2/disasm/DISPLAY_LAYOUT.md`.

## GIF Packet Parser + renderer GLES

`src/gif_packet.hpp/.cpp` lee el GIFtag de 128 bits y extrae:

- `A+D` -> `BITBLTBUF`, `TRXPOS`, `TRXREG`, `TRXDIR` + pixeles, `PRIM` y
  `ALPHA (0x42)`.
- `PRIM`, `RGBAQ`, `UV`, `XYZ2` -> sprite 2D.
- `PRIM.ABE` + `ALPHA` -> blending GS: opaco (`ABE=0`), alpha normal
  (`0x44`), aditivo con alpha (`0x48`) y aditivo fijo (`0x68` + FIX `0x80`).
  Tambien se traducen `0x64` (FIX), `0x46` (fade a framebuffer),
  `0x84` (subtract) y `0x54`/`0x58` (alpha del framebuffer).
- `PRIM.TME` decide si el sprite muestrea textura. Un sprite texturizado sin
  textura valida se omite para no dibujar el checkerboard como cuadro solido;
  una primitiva `TME=0` se dibuja con color plano.

`src/gif_renderer.hpp/.cpp` dibuja el sprite como quad texturizado con GLES
3.0. El blend se aplica con `ps2::gfx::ApplyPs2AlphaBlend(alpha_reg)`, que
traduce la ecuacion GS a `glBlendEquationSeparate` + `glBlendFuncSeparate`:
`GL_SRC_ALPHA/GL_ONE` para aditivo con alpha, `GL_ONE/GL_ONE` para aditivo
fijo y `GL_CONSTANT_ALPHA` cuando la ecuacion usa FIX. Esto evita que las
particulas de absorcion de almas y los ataques Genma (rayo, hielo, viento) se
vean como cuadros negros o blancos.

En `main.cpp`, el hook DMA2/GIF hace:

```cpp
if (!ps2::gif::SubmitChain(dma.source)) {
    for (const ps2::hle::DmaTag &tag : dma.tags) {
        if (!tag.data_valid) continue;
        const std::uint8_t *data = ps2::hle::TagData(tag);
        if (data) ps2::gif::SubmitPacket(data, ps2::hle::TagBytes(tag));
    }
}
```

`SubmitChain` recorre la cadena en `ps2_ram`; el fallback usa los tags que ya
resolvio `ps2_hle` en `game::Ram`. `SubmitPacket` ya no dibuja: encola los
sprites y los Z-buffers en `gs_render_pipeline`; el dibujo ocurre en
`port_vsync()`.

Prueba de PC:

```powershell
clang++ -std=c++17 -O2 -Isrc `
  tests/gif_packet_test.cpp src/gif_packet.cpp src/ps2_texture_cache.cpp `
  -o gif_test.exe
.\gif_test.exe

clang++ -std=c++17 -O2 -Isrc `
  tests/gs_blend_test.cpp src/ps2_alpha_blend.cpp src/gif_packet.cpp `
  src/ps2_texture_cache.cpp `
  -o gs_blend_test.exe
.\gs_blend_test.exe

clang++ -std=c++17 -O2 -Isrc `
  tests/vif1_parser_test.cpp src/vif1_parser.cpp src/ps2_mem.cpp `
  -o vif1_parser_test.exe
.\vif1_parser_test.exe

clang++ -std=c++17 -O2 -Isrc `
  tests/ps2_pad_test.cpp src/ps2_pad.cpp -o ps2_pad_test.exe
.\ps2_pad_test.exe
```

Resultado:

```text
PASS gif_packet: GIFtag, textura, sprite, Z-buffer PSMZ32/24/16 y CLUT PSMT8/PSMT4
vif1_parser_test.exe: FALLOS: 0
```

Limitaciones actuales: no se aplica el swizzle de VRAM del GS (las texturas
se interpretan lineales), y solo se dibuja el ultimo sprite valido de cada
bloque. PSMT8/PSMT4 con CLUT ya estan soportados por `ps2_texture_cache`.
Documento detallado: `games/onimusha2/disasm/GIF_PARSER.md`.

## Cinemáticas .PSS (`PssVideoPlayer`)

`src/pss_video_player.hpp/.cpp` reproduce cinemáticas `.PSS` (MPEG-2 Program
Stream de PS2):

- Demux/decodificación con FFmpeg (`avformat`, `avcodec`, `swscale`,
  `swresample`).
- Video MPEG-2 -> RGBA -> textura GLES 3.0 con `glTexSubImage2D`.
- Audio -> S16 estéreo 48000 Hz -> `SDL_QueueAudio`.
- `OpenFromIso(&iso, lba, size)` lee el PSS directamente de `disc.iso`.
- `RequestSkip()` para saltar con toque; `main.cpp` ya lo engancha en
  `Game_TouchDown`.

FFmpeg es opcional para no romper el APK:

```powershell
cmake -S android/port -B build/port-android `
  -DPORT_ENABLE_FFMPEG=ON `
  -DPORT_FFMPEG_ROOT=C:/ffmpeg-android `
  ...
```

Si no está, el módulo compila como stub y `OpenFile/OpenFromIso` devuelven
false con un mensaje claro. Requiere FFmpeg con demuxer `pss` y decoder
`mpeg2video`; para audio PS2 ADPCM, el decoder `adpcm_psx` o el
`Ps2AudioSystem`.

Documento detallado: `games/onimusha2/disasm/PSS_VIDEO_PLAYER.md`.
