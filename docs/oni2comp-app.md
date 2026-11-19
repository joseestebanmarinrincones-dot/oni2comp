# Oni2comp: app de Windows y Android

El proyecto se puede usar como una app, sin escribir comandos.

## Windows

El paquete portable contiene `Oni2comp.exe`, la app, los scripts y las
dependencias nativas. No incluye ISO, `games/`, `generated/`, `.gzf` ni APK.

1. Descomprime `Oni2comp-portable.zip` en una carpeta corta, por ejemplo
   `C:\Oni2comp`.
2. Copia tu ISO a la carpeta `isos` (o elige cualquier otra ruta).
3. Ejecuta `Oni2comp.exe`.
4. Pulsa **Add a game**, elige el ISO y **Build**. La primera vez descarga las
   herramientas portables (unos 312 MB) y compila el juego.
5. Pulsa **Play** o crea un acceso directo.

La app abre directamente en la carpeta `isos`. No se abre ninguna ventana de
CMD ni de PowerShell: el lanzador nativo inicia la interfaz WinForms oculta.

Para generar el paquete desde el código:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package-oni2comp.ps1
```

Queda en `dist\Oni2comp\` y `dist\Oni2comp-portable.zip`. `dist/` está ignorado
por git.

## Android

El APK se genera a partir de **tu propio ISO PAL** y se instala en el teléfono.
No se distribuye el APK con el juego dentro.

```powershell
powershell -ExecutionPolicy Bypass -File android\apk\build-pal.ps1 `
  -Iso "C:\ruta\Onimusha 2 SD.iso"

powershell -ExecutionPolicy Bypass -File scripts\package-oni2comp-android.ps1
```

Queda en `dist\Oni2comp-Android\Oni2comp.apk` y
`dist\Oni2comp-Android.zip`.

En el teléfono:

1. Copia el ISO a `/sdcard/Oni2comp/` (cualquier nombre) o a
   `/sdcard/Onimusha2.iso`.
2. Instala `Oni2comp.apk` (Android 10+, arm64).
3. Abre la app y concede **acceso a todos los archivos**.
4. Elige **Español** y **50 Hz** en el juego. Usa los controles táctiles o un
   mando Bluetooth/USB.

La app busca primero cualquier `.iso` en `/sdcard/Oni2comp/`; después una ISO
PAL/española en `/sdcard` o `/sdcard/Download`, y por último
`/sdcard/Onimusha2.iso`.

## Rendimiento en Android

El APK 1.3 fuerza la velocidad de consola (50 Hz en el disco PAL) y lleva los
microprogramas VU1 compilados. Antes podía heredar un `keys.ini` con
`fps_limit = 30`, que lo dejaba a 30 FPS; ahora `PS2_FPS_LIMIT=console` manda
sobre ese archivo. Si el teléfono no llega a 50, el siguiente paso es bajar la
resolución interna o el estilo de imagen desde el menú (F1/MENU).

## Corrección del cierre en la parte de historia

El runtime se detenía al entrar en la historia porque faltaban funciones del
programa PAL (`0x00292B20`, `0x00292B50`, `0x0029310C`, ...) y tres overlays
(`01F64000_10BB0490`, `01F70000_27F5BFB1`, `01F90000_A2BB43DF`, ...). Se
generaron las entradas y las imágenes de overlay completas; la prueba en PC
pasó 15.000 frames sin `missing code` y mostró la escena con subtítulos en
español. El APK 1.2 incluye esa traducción.

## Aviso

El APK contiene código recompilado de la versión PAL. Distribúyelo solo a
personas que tengan su propia copia del juego. El paquete de Windows no lleva
código del juego: cada usuario lo genera desde su ISO.
