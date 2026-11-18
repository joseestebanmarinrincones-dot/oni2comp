param(
    [string]$OutDir = (Join-Path (Split-Path -Parent $PSScriptRoot) 'dist')
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$apk = Join-Path $root 'android\apk\Oni2comp.apk'
if (-not (Test-Path -LiteralPath $apk)) {
    throw 'Falta android\apk\Oni2comp.apk. Ejecuta antes android\apk\build.ps1 con -ApkName Oni2comp.apk.'
}

$target = Join-Path $OutDir 'Oni2comp-Android'
if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Recurse -Force }
New-Item -ItemType Directory -Force -Path $target | Out-Null

Copy-Item -LiteralPath $apk -Destination (Join-Path $target 'Oni2comp.apk') -Force
@'
ONI2COMP PARA ANDROID
=====================

1. Copia tu ISO de Onimusha 2 a la memoria interna, dentro de la carpeta:

       /sdcard/Oni2comp/

   Puede tener cualquier nombre; si hay varias .iso, se usa la primera.
   Tambien vale /sdcard/Onimusha2.iso.

2. Instala Oni2comp.apk en el telefono (Android 10 o superior, arm64).
   Si Android lo pide, permite "instalar apps desconocidas" para el explorador.

3. Abre Oni2comp y concede "acceso a todos los archivos" cuando lo pida.
   Es necesario para leer el ISO y crear la tarjeta de memoria.

4. El juego arranca en la seleccion de idioma. Elige Espanol y 50 Hz.
   Puedes usar los controles tactiles o un mando Bluetooth/USB.

No se incluye ningun ISO ni datos del juego en este paquete: usa tu propia
copia del disco. El APK contiene el codigo recompilado de la version PAL; no
lo distribuyas a quien no tenga el juego.
'@ | Set-Content -LiteralPath (Join-Path $target 'LEEME.txt') -Encoding UTF8

$zip = Join-Path $OutDir 'Oni2comp-Android.zip'
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
Compress-Archive -LiteralPath $target -DestinationPath $zip -CompressionLevel Optimal

Get-Item (Join-Path $target 'Oni2comp.apk'), $zip | Select-Object FullName, Length, LastWriteTime
