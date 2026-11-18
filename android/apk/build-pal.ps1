# Builds the universal Android app with the PAL/Spanish executable of
# Onimusha 2 (SLES_509.78). The generated translation stays in generated/,
# which is ignored by git: it is derived from the disc the user owns.
#
# usage:
#   powershell -ExecutionPolicy Bypass -File android\apk\build-pal.ps1 `
#     -Iso "C:\ruta\Onimusha 2 SD.iso"
#
# The APK is written to android\apk\ps2native-pal.apk. It replaces the NTSC
# build when installed because both use org.ps2native.recomp.
param(
    [string]$Iso = '',
    [switch]$Force,
    [switch]$SkipGenerate
)

$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$gen = Join-Path $root 'generated\onimusha2-pal'
$game = Join-Path $root 'games\onimusha2-pal'
$elf = Join-Path $game 'disc\SLES_509.78'
$table = Join-Path $gen 'table.cpp'

if (-not $SkipGenerate -and ($Force -or -not (Test-Path $table))) {
    if (-not $Iso) {
        throw 'Falta -Iso: pasa la ruta del ISO PAL (SYSTEM.CNF con SLES_509.78).'
    }
    if (-not (Test-Path $Iso)) { throw "No existe el ISO: $Iso" }

    New-Item -ItemType Directory -Force (Split-Path $elf) | Out-Null
    if ($Force -or -not (Test-Path $elf)) {
        Write-Host '== Extrayendo SYSTEM.CNF y SLES_509.78 del ISO'
        & python (Join-Path $root 'scripts\disc.py') extract $Iso (Split-Path $elf) 'SLES_509.78' 'SYSTEM.CNF'
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path $elf)) {
            throw 'No se pudo extraer SLES_509.78. Comprueba que el ISO es la versión PAL de Onimusha 2.'
        }
    }

    $eerecomp = Join-Path $root 'build\tools\eerecomp.exe'
    if (-not (Test-Path $eerecomp)) {
        throw 'Falta build\tools\eerecomp.exe. Ejecuta antes scripts\port.sh o compila el objetivo eerecomp.'
    }
    Write-Host "== Traduciendo $elf"
    & $eerecomp $elf --out $gen
    if ($LASTEXITCODE -ne 0) { throw 'eerecomp fallo.' }
}

if (-not (Test-Path $table)) { throw "No hay traduccion PAL en $gen." }

$sdk = if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { Join-Path $env:LOCALAPPDATA 'Android\Sdk' }
$ndk = if ($env:ANDROID_NDK_HOME) { $env:ANDROID_NDK_HOME } else { Join-Path $sdk 'ndk\27.2.12479018' }
$toolchain = Join-Path $ndk 'build\cmake\android.toolchain.cmake'
if (-not (Test-Path $toolchain)) { throw "No encuentro el NDK en $ndk" }

$build = Join-Path $root 'build\android-onimusha2-pal'
Write-Host '== Configurando CMake (arm64-v8a, API 29)'
& cmake -S $root -B $build -G Ninja `
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
    -DANDROID_ABI=arm64-v8a `
    -DANDROID_PLATFORM=android-29 `
    -DCMAKE_BUILD_TYPE=Release `
    "-DPS2_GENERATED=$gen"
if ($LASTEXITCODE -ne 0) { throw 'CMake fallo.' }

Write-Host '== Empaquetando el APK PAL'
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'build.ps1') `
    -BuildDir 'build\android-onimusha2-pal' `
    -ApkName 'ps2native-pal.apk'
if ($LASTEXITCODE -ne 0) { throw 'El empaquetado fallo.' }

Get-Item (Join-Path $PSScriptRoot 'ps2native-pal.apk') | Select-Object FullName, Length, LastWriteTime
