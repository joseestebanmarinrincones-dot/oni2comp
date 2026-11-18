# Proyecto Android Studio / Gradle

Módulo listo para abrir en Android Studio o compilar por consola.

## Abrir

Android Studio → **Open** → selecciona esta carpeta:

```
android/port/android-project
```

Gradle sincroniza y genera `libmain.so` desde `android/port/CMakeLists.txt`
(fuentes en `android/port/src`), además de empaquetar `jniLibs/arm64-v8a/libSDL2.so`.

## Versiones usadas

| Herramienta | Versión |
|---|---|
| Gradle | 8.14 |
| Android Gradle Plugin | 8.13.1 |
| JDK | 21 (`tools/jdk21/jdk-21.0.13+9`) |
| compileSdk / targetSdk | 35 |
| minSdk | 29 |
| NDK | 27.2.12479018 |
| CMake | 3.31.6 |
| ABI | arm64-v8a |

`gradle.properties` apunta a la JDK 21 portable con `org.gradle.java.home`.
Si Android Studio usa otra JDK, cambia esa línea o elimínala y configura
**Settings → Build Tools → Gradle → Gradle JDK**.

`local.properties` contiene la ruta del SDK de esta máquina; no se versiona.

## Compilar

```powershell
$env:JAVA_HOME = "tools\jdk21\jdk-21.0.13+9"
$env:ANDROID_HOME = "$env:LOCALAPPDATA\Android\Sdk"
.\gradlew.bat assembleDebug
```

APK:

```
app/build/outputs/apk/debug/app-debug.apk
```

## Instalar

```powershell
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n org.ps2native.recomp/.MainActivity
```

Los archivos del juego (`DATA/`, `IMAGE/`, etc.) van en el directorio externo
de la app, normalmente:

```
/storage/emulated/0/Android/data/org.ps2native.recomp/files/
```

## Java de SDL2

Las clases `org.libsdl.app.*` se copiaron de
`third_party/sdl2-src/SDL2-2.32.10/android-project/.../org/libsdl/app/`.
La `libSDL2.so` prebuilt no incluye esas clases Java; el APK las necesita.
