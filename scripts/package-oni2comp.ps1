# Arma la app portable de Windows: Oni2comp.exe + app + scripts + dependencias.
# No copia ISO, games\, generated\, build\, .gzf ni APK: el paquete se puede
# distribuir y cada usuario construye el juego desde su propio disco.
param(
    [string]$OutDir = (Join-Path (Split-Path -Parent $PSScriptRoot) 'dist'),
    [switch]$NoZip
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$name = 'Oni2comp'
$target = Join-Path $OutDir $name

if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Recurse -Force }
New-Item -ItemType Directory -Force -Path $target | Out-Null

Write-Host '== lanzador'
& (Join-Path $root 'launcher\build-launcher.ps1') | Out-Null

Write-Host '== archivos de la app'
foreach ($file in @('LICENSE', 'README.md', 'Start.cmd', 'CMakeLists.txt')) {
    Copy-Item -LiteralPath (Join-Path $root $file) -Destination (Join-Path $target $file) -Force
}
Copy-Item -LiteralPath (Join-Path $root 'launcher\Oni2comp.exe') -Destination $target -Force
Copy-Item -LiteralPath (Join-Path $root 'launcher\Oni2comp.ico') -Destination $target -Force

foreach ($dir in @('app', 'scripts', 'runtime', 'recomp', 'host', 'cmake', 'docs', 'tests')) {
    Copy-Item -LiteralPath (Join-Path $root $dir) -Destination (Join-Path $target $dir) -Recurse -Force
}

Write-Host '== dependencias nativas (sin sdl2-src ni android)'
$third = Join-Path $target 'third_party'
New-Item -ItemType Directory -Force -Path $third | Out-Null
foreach ($dir in @('imgui', 'sdl2', 'wgpu-native', 'dxc')) {
    $src = Join-Path $root "third_party\$dir"
    if (Test-Path -LiteralPath $src) {
        Copy-Item -LiteralPath $src -Destination (Join-Path $third $dir) -Recurse -Force
    }
}

$isos = Join-Path $target 'isos'
New-Item -ItemType Directory -Force -Path $isos | Out-Null
@'
Pon aqui tu ISO de Onimusha 2 (o el juego de PS2 que poseas).
La app abre esta carpeta al pulsar "Choose disc image...".
No se incluye ninguna imagen de disco en este paquete.
'@ | Set-Content -LiteralPath (Join-Path $isos 'LEEME.txt') -Encoding UTF8

@'
ONI2COMP PARA WINDOWS
=====================

1. Descomprime este ZIP en una carpeta corta, por ejemplo C:\Oni2comp.
   No lo ejecutes desde dentro del ZIP.

2. Copia tu ISO de Onimusha 2 a la carpeta "isos" (o a cualquier otra).
   No se incluye ningun ISO en el paquete.

3. Ejecuta Oni2comp.exe. Elige "Add a game", busca tu ISO y pulsa Build.
   La primera vez descarga las herramientas (unos 312 MB) y compila el juego.
   Puede tardar entre 5 y 30 minutos.

4. Cuando termine, pulsa Play. Tambien puedes crear un acceso directo.

Teclas: F1 menu, F2 estilo de imagen, F3 limite de FPS, F5/F8 guardar/cargar.
'@ | Set-Content -LiteralPath (Join-Path $target 'LEEME.txt') -Encoding UTF8

$size = (Get-ChildItem -LiteralPath $target -Recurse -File | Measure-Object Length -Sum).Sum
Write-Host ("carpeta: {0} ({1:N1} MB)" -f $target, ($size / 1MB))

if (-not $NoZip) {
    $zip = Join-Path $OutDir "$name-portable.zip"
    if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
    Write-Host '== comprimiendo'
    Compress-Archive -LiteralPath $target -DestinationPath $zip -CompressionLevel Optimal
    $zipSize = (Get-Item -LiteralPath $zip).Length
    Write-Host ("zip: {0} ({1:N1} MB)" -f $zip, ($zipSize / 1MB))
}
