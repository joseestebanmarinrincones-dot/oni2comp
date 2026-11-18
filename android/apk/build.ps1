param(
    [string]$BuildDir = 'build\android-onimusha2',
    [string]$ApkName = 'ps2native.apk'
)
$ErrorActionPreference = 'Continue'
$sdk = 'C:\Users\Gamer\AppData\Local\Android\Sdk'
$jdk = 'C:\Users\Gamer\AppData\Local\AndroidTools\jdk17\jdk-17.0.21+9'
$env:JAVA_HOME = $jdk
$env:Path = "$jdk\bin;$sdk\build-tools\35.0.0;$sdk\platform-tools;" + [Environment]::GetEnvironmentVariable('Path','Machine') + ';' + [Environment]::GetEnvironmentVariable('Path','User')
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$apk = $PSScriptRoot
$bt = Join-Path $sdk 'build-tools\35.0.0'
$androidJar = Join-Path $sdk 'platforms\android-35\android.jar'
$ndk = Join-Path $sdk 'ndk\27.2.12479018'
$strip = Join-Path $ndk 'toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-strip.exe'
$build = Join-Path $root $BuildDir

& ninja -C $build main
if ($LASTEXITCODE -ne 0) { throw 'libmain build failed' }

$work = Join-Path $apk 'work'
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory -Force $work | Out-Null
$libs = Join-Path $work 'lib\arm64-v8a'
New-Item -ItemType Directory -Force $libs | Out-Null
Copy-Item (Join-Path $build 'libmain.so') (Join-Path $libs 'libmain.so') -Force
Copy-Item (Join-Path $root 'build\sdl2-android\libSDL2.so') (Join-Path $libs 'libSDL2.so') -Force
& $strip (Join-Path $libs 'libmain.so')
& $strip (Join-Path $libs 'libSDL2.so')

$resZip = Join-Path $work 'res.zip'
& (Join-Path $bt 'aapt2.exe') compile --dir (Join-Path $apk 'res') -o $resZip
if ($LASTEXITCODE -ne 0) { throw 'aapt2 compile failed' }
$base = Join-Path $work 'base.apk'
$assets = Join-Path $apk 'assets'
$aapt = @('link', '-o', $base, '-I', $androidJar, '--manifest', (Join-Path $apk 'AndroidManifest.xml'), '--java', (Join-Path $work 'gen'))
if (Test-Path $assets) { $aapt += @('-A', $assets) }
$aapt += $resZip
& (Join-Path $bt 'aapt2.exe') @aapt
if ($LASTEXITCODE -ne 0) { throw 'aapt2 link failed' }

$classes = Join-Path $work 'classes'
New-Item -ItemType Directory -Force $classes | Out-Null
$java = @(Get-ChildItem (Join-Path $root 'third_party\sdl2-src\SDL2-2.32.10\android-project\app\src\main\java') -Recurse -Filter '*.java' | ForEach-Object { $_.FullName })
$java += @(Get-ChildItem (Join-Path $work 'gen') -Recurse -Filter '*.java' | ForEach-Object { $_.FullName })
$java += @(Get-ChildItem (Join-Path $apk 'src') -Recurse -Filter '*.java' | ForEach-Object { $_.FullName })
& javac -encoding UTF-8 -source 8 -target 8 -nowarn -classpath $androidJar -d $classes @java
if ($LASTEXITCODE -ne 0) { throw 'javac failed' }

$dex = Join-Path $work 'dex'
New-Item -ItemType Directory -Force $dex | Out-Null
$classFiles = @(Get-ChildItem $classes -Recurse -Filter '*.class' | ForEach-Object { $_.FullName })
& (Join-Path $bt 'd8.bat') --release --lib $androidJar --output $dex @classFiles
if ($LASTEXITCODE -ne 0) { throw 'd8 failed' }

$unsigned = Join-Path $work 'unsigned.apk'
Copy-Item $base $unsigned -Force
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::Open($unsigned, 'Update')
foreach ($entry in @(@{f = (Join-Path $dex 'classes.dex'); n = 'classes.dex'},
                     @{f = (Join-Path $libs 'libmain.so'); n = 'lib/arm64-v8a/libmain.so'},
                     @{f = (Join-Path $libs 'libSDL2.so'); n = 'lib/arm64-v8a/libSDL2.so'})) {
    $existing = $zip.GetEntry($entry.n)
    if ($existing) { $existing.Delete() }
    [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $entry.f, $entry.n)
}
$zip.Dispose()

$aligned = Join-Path $work 'aligned.apk'
& (Join-Path $bt 'zipalign.exe') -f 4 $unsigned $aligned
if ($LASTEXITCODE -ne 0) { throw 'zipalign failed' }
$final = Join-Path $apk $ApkName
& (Join-Path $bt 'apksigner.bat') sign --ks (Join-Path $env:USERPROFILE '.android\debug.keystore') --ks-pass pass:android --key-pass pass:android --ks-key-alias androiddebugkey --out $final $aligned
if ($LASTEXITCODE -ne 0) { throw 'apksigner failed' }
Get-Item $final | Select-Object FullName, Length
