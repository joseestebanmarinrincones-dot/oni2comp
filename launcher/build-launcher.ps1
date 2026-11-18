# Compila el lanzador nativo Oni2comp.exe (sin ventana de consola).
# Usa el compilador de C# que viene con .NET Framework en Windows 10/11.
$ErrorActionPreference = 'Stop'
$csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if (-not (Test-Path $csc)) {
    $csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework\v4.0.30319\csc.exe'
}
if (-not (Test-Path $csc)) { throw 'No se encuentra csc.exe (.NET Framework 4.x).' }

& $csc /nologo /target:winexe /optimize+ /platform:anycpu `
    "/win32icon:$PSScriptRoot\Oni2comp.ico" `
    "/out:$PSScriptRoot\Oni2comp.exe" `
    "$PSScriptRoot\Oni2comp.cs"
if ($LASTEXITCODE -ne 0) { throw 'La compilación del lanzador falló.' }
Get-Item "$PSScriptRoot\Oni2comp.exe" | Select-Object FullName, Length, LastWriteTime
