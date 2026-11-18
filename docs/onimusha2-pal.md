# Onimusha 2 PAL (SLES_509.78) en español

El ISO PAL de Onimusha 2 no es el `SLUS_203.93` NTSC del port manual
`android/port`. Es otro ejecutable:

```text
SYSTEM.CNF
  BOOT2 = cdrom0:\SLES_509.78;1
  VMODE = PAL
```

El port manual (`Onicomp`, APK de `android/port`) contiene direcciones y
funciones traducidas del NTSC, así que no puede ejecutar ese ELF. La app
universal de `android/apk` sí: lee el ejecutable del ISO, lo traduce con
`eerecomp` y arranca el juego con el runtime PAL.

## Probado

En PC, con el ISO

```text
C:\Users\Gamer\Desktop\Onimusha 2 Samurai's Destiny [PAL-NTSC] [Es,Fr,De,En,It]\Onimusha 2 SD.iso
```

se verificó:

- selección de idioma con las cinco opciones (English, Français, Deutsch,
  Italiano, Español);
- selección de frecuencia PAL (50 Hz / 60 Hz) en español;
- menú principal en español: NUEVA PARTIDA, CARGAR PARTIDA, OPCIONES,
  FUNCIONES ESPECIALES;
- introducción con subtítulos en español;
- resolución nativa PAL de 512x448.

En Android (Z2352N, Android 13, 2400x1080) se instaló el APK con las unidades
PAL y se verificó, con capturas internas del runtime:

- pantalla de selección de idioma PAL a 512x448;
- logo de Capcom y aviso en español;
- pantalla de título de Onimusha 2;
- 49 a 60 fps durante menús y título, unos 800 MB de RAM.

La captura de pantalla de Android puede salir negra por la superficie de
hardware; las capturas `--frames-dir` del propio runtime confirman el contenido.

## Traducir el ejecutable PAL

El resultado queda en `generated/onimusha2-pal/`, que está ignorado por git
porque es código derivado de un disco del usuario.

Si aún no tienes `build\tools\eerecomp.exe`, compila las herramientas:

```powershell
cmake -S . -B build/tools -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/tools --target eerecomp vurecomp -j
```

Después extrae el ejecutable y tradúcelo:

```powershell
python scripts/disc.py extract "C:\ruta\Onimusha 2 SD.iso" `
  "games\onimusha2-pal\disc" "SLES_509.78" "SYSTEM.CNF"

build\tools\eerecomp.exe "games\onimusha2-pal\disc\SLES_509.78" `
  --out "generated\onimusha2-pal"
```

## PC

```powershell
cmake -S . -B build/pal-native -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_CXX_COMPILER=clang++ `
  "-DCMAKE_CXX_FLAGS=-mavx2 -ffp-contract=off" `
  "-DPS2_GENERATED=$PWD\generated\onimusha2-pal"

cmake --build build/pal-native --target ps2run -j

build\pal-native\ps2run.exe --disc "C:\ruta\Onimusha 2 SD.iso" --pal --window
```

## Android

```powershell
powershell -ExecutionPolicy Bypass -File android\apk\build-pal.ps1 `
  -Iso "C:\ruta\Onimusha 2 SD.iso"

adb install -r android\apk\ps2native-pal.apk
adb push "C:\ruta\Onimusha 2 SD.iso" /sdcard/Onimusha2_PAL_ES.iso
adb shell am start -n org.ps2native.recomp/.MainActivity
```

`MainActivity.findIso()` prefiere un ISO cuyo nombre contenga `espa`,
`spanish`, `spain`, `pal` o `sles`; si no, usa `/sdcard/Onimusha2.iso`, y
después cualquier ISO con "onimusha" en el nombre. El APK PAL y el NTSC usan
el mismo paquete `org.ps2native.recomp`: instalar uno reemplaza al otro.

## Límites

- Probado hasta la introducción y el menú principal en PC, y hasta el título
  en Android; falta una partida completa y la batalla final/créditos.
- El APK universal no es el mismo binario que el port manual: no incluye
  QuickState, el FBO HD 2x/3x ni el menú ImGui de `android/port`.
- Si el juego pide formatear la tarjeta, hay que dejar que la app copie su
  `ps2.ps2` vacía o formatearla desde el menú del juego.
- El repositorio no incluye código generado del juego (`generated/`), ISO,
  ELF ni APK. Cada usuario debe generarlo desde su propio disco.
