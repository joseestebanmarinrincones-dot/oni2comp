# Builds tools\disas.exe and regenerates the Onimusha 2 EE listings in
# games\onimusha2\disasm. The listings are derived from the game the user
# provides; they are not part of the repository.
#
#   powershell -ExecutionPolicy Bypass -File scripts\disas.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\disas.ps1 -Force
#
# -Clang <path> picks a compiler explicitly.

[CmdletBinding()]
param(
    [switch] $Force,
    [string] $Clang = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$tool = Join-Path $root "tools\disas.exe"

function Find-Clang {
    if ($Clang) { return $Clang }
    $portable = Join-Path $root "tools\portable\llvm-mingw\bin\clang++.exe"
    if (Test-Path -LiteralPath $portable) { return $portable }
    $command = Get-Command clang++.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $winget = Get-ChildItem (Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Packages") -Directory -Filter "MartinStorsjo.LLVM-MinGW*" -ErrorAction SilentlyContinue |
        ForEach-Object { Get-ChildItem $_.FullName -Recurse -Filter "clang++.exe" -ErrorAction SilentlyContinue } |
        Select-Object -First 1
    if ($winget) { return $winget.FullName }
    throw "clang++ not found. Run scripts\portable.ps1 first, put clang++ on PATH, or pass -Clang <path>."
}

if ($Force -or -not (Test-Path -LiteralPath $tool)) {
    $compiler = Find-Clang
    $sources = @(
        (Join-Path $root "scripts\disas.cpp"),
        (Join-Path $root "recomp\r5900.cpp"),
        (Join-Path $root "recomp\elf.cpp"),
        (Join-Path $root "recomp\analysis.cpp")
    )
    Write-Host "building $tool"
    & $compiler -std=c++20 -O2 -Wall -Wextra `
        -I (Join-Path $root "recomp") `
        -I (Join-Path $root "runtime\include") `
        -o $tool @sources
    if ($LASTEXITCODE -ne 0) { throw "the compiler failed" }
}

$game = Join-Path $root "games\onimusha2"
$out = Join-Path $game "disasm"
$eeDir = Join-Path $out "ee"
$overlayDir = Join-Path $out "overlays"
New-Item -ItemType Directory -Force -Path $eeDir, $overlayDir | Out-Null

$entries = Join-Path $game "entries.txt"

function Disassemble([string] $file, [string] $base, [string] $name, [string] $dir) {
    $cmdArgs = @($file, "--entries", $entries, "--quiet",
        "--out", (Join-Path $dir "$name.s"),
        "--stats", (Join-Path $dir "$name.stats.txt"),
        "--funcs", (Join-Path $dir "$name.funcs.txt"))
    if ($base) { $cmdArgs = @($file, "--base", $base) + $cmdArgs[1..($cmdArgs.Count - 1)] }
    & $tool @cmdArgs
    if ($LASTEXITCODE -ne 0) { throw "disas failed for $file" }
}

Write-Host "listing the main executable"
Disassemble (Join-Path $game "disc\SLUS_203.93") "" "SLUS_203.93" $eeDir

# The 01F64000 and 01F70000 dumps overlap completely; one listing covers both.
# The 02390000..023FFFFF dumps are a data module (they decode to no code), so
# they are not listed.
$modules = @(
    @{ File = "01F64000_856903D1.bin"; Base = "01F64000" },
    @{ File = "01F90000_86124295.bin"; Base = "01F90000" },
    @{ File = "01F90000_D3325583.bin"; Base = "01F90000" }
)
foreach ($module in $modules) {
    $name = [IO.Path]::GetFileNameWithoutExtension($module.File)
    Write-Host "listing overlay $name"
    Disassemble (Join-Path $game "overlays\$($module.File)") $module.Base $name $overlayDir
}

$index = Join-Path $out "index.txt"
$lines = @(
    "Onimusha 2 (SLUS_203.93) - EE disassembly index",
    "generated $(Get-Date -Format 'yyyy-MM-dd HH:mm')",
    "",
    "Main executable:",
    "  ee\SLUS_203.93.s"
)
Get-Content (Join-Path $eeDir "SLUS_203.93.stats.txt") | ForEach-Object { $lines += "    $_" }
$lines += ""
$lines += "Overlays:"
foreach ($module in $modules) {
    $name = [IO.Path]::GetFileNameWithoutExtension($module.File)
    $lines += "  overlays\$name.s  (base 0x$($module.Base))"
    Get-Content (Join-Path $overlayDir "$name.stats.txt") | ForEach-Object { $lines += "    $_" }
    $lines += ""
}
Set-Content -Path $index -Value $lines -Encoding UTF8
Write-Host "wrote $index"
