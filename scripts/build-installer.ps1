[CmdletBinding()]
param(
    [string]$QtRoot = "",
    [string]$FfmpegPath = "",
    [string]$YtDlpPath = "",
    [string]$SpotDlPath = "",
    [string]$DenoPath = "",
    [string]$UpdateManifestUrl = "",
    [string]$Configuration = "Release",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $ProjectRoot "build"
$StageDir = Join-Path $BuildDir "stage"
$DistDir = Join-Path $ProjectRoot "dist"
$ProjectLine = Select-String -LiteralPath (Join-Path $ProjectRoot "CMakeLists.txt") `
    -Pattern '^project\(DannyDeeTrackManager VERSION ([0-9]+\.[0-9]+\.[0-9]+)' | Select-Object -First 1
if (-not $ProjectLine) { throw "Die Projektversion konnte nicht aus CMakeLists.txt gelesen werden." }
$AppVersion = $ProjectLine.Matches[0].Groups[1].Value

function Find-CommandPath([string]$Name, [string[]]$Candidates) {
    $found = Get-Command $Name -ErrorAction SilentlyContinue
    if ($found) { return $found.Source }
    foreach ($candidate in $Candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) { return $candidate }
    }
    throw "'$Name' wurde nicht gefunden. Siehe README unter 'Installer bauen'."
}

if ($Clean -and (Test-Path -LiteralPath $BuildDir)) {
    $resolvedBuild = [System.IO.Path]::GetFullPath($BuildDir)
    $resolvedRoot = [System.IO.Path]::GetFullPath($ProjectRoot)
    if (-not $resolvedBuild.StartsWith($resolvedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Unsicheres Build-Verzeichnis: $resolvedBuild"
    }
    Remove-Item -LiteralPath $BuildDir -Recurse -Force
}

$CMake = Find-CommandPath "cmake" @(
    "C:\Program Files\CMake\bin\cmake.exe"
)

if (-not $QtRoot) {
    $knownQt = Get-ChildItem -Path "C:\Qt\6.*\mingw*_64" -Directory -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending | Select-Object -First 1
    if ($knownQt) { $QtRoot = $knownQt.FullName }
}
if (-not $QtRoot -or -not (Test-Path -LiteralPath $QtRoot)) {
    throw "Qt 6 wurde nicht gefunden. Übergib -QtRoot 'C:\Qt\6.x.x\mingw_64'."
}

$WinDeployQt = Find-CommandPath "windeployqt" @(
    (Join-Path $QtRoot "bin\windeployqt.exe")
)
$Iscc = Find-CommandPath "iscc" @(
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
    "C:\Program Files\Inno Setup 6\ISCC.exe",
    (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe")
)

$QtInstallRoot = Split-Path -Parent (Split-Path -Parent $QtRoot)
$CompilerSearchRoots = @(
    (Join-Path $QtInstallRoot "Tools"),
    "C:\Qt\Tools"
) | Select-Object -Unique
$Compiler = $CompilerSearchRoots | ForEach-Object {
    Get-ChildItem -Path (Join-Path $_ "mingw*_64\bin\g++.exe") -File -ErrorAction SilentlyContinue
} | Sort-Object FullName -Descending | Select-Object -First 1
if (-not $Compiler) { throw "Der zum Qt-Kit passende MinGW-Compiler wurde nicht gefunden." }
$CompilerBin = Split-Path -Parent $Compiler.FullName
$env:PATH = "$CompilerBin;$env:PATH"

$NinjaCandidates = @(
    (Join-Path $QtRoot "bin\ninja.exe"),
    "C:\Program Files\CMake\bin\ninja.exe"
)
$Ninja = Find-CommandPath "ninja" $NinjaCandidates

New-Item -ItemType Directory -Force -Path $BuildDir, $StageDir, $DistDir | Out-Null

$CMakeArguments = @(
    "-S", $ProjectRoot,
    "-B", $BuildDir,
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=$Configuration",
    "-DCMAKE_PREFIX_PATH=$QtRoot",
    "-DCMAKE_MAKE_PROGRAM=$Ninja",
    "-DDANNYDEE_UPDATE_MANIFEST_URL=$UpdateManifestUrl"
)
& $CMake @CMakeArguments
if ($LASTEXITCODE -ne 0) { throw "CMake-Konfiguration fehlgeschlagen." }

& $CMake --build $BuildDir --config $Configuration
if ($LASTEXITCODE -ne 0) { throw "Build fehlgeschlagen." }

$Executable = Join-Path $BuildDir "DannyDeeTrackManager.exe"
if (-not (Test-Path -LiteralPath $Executable)) {
    $Executable = Join-Path $BuildDir "$Configuration\DannyDeeTrackManager.exe"
}
if (-not (Test-Path -LiteralPath $Executable)) { throw "Programmdatei wurde nicht erzeugt." }

$resolvedStage = [System.IO.Path]::GetFullPath($StageDir)
$resolvedRoot = [System.IO.Path]::GetFullPath($ProjectRoot)
if (-not $resolvedStage.StartsWith($resolvedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Unsicheres Stage-Verzeichnis: $resolvedStage"
}
Get-ChildItem -LiteralPath $StageDir -Force -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force
Copy-Item -LiteralPath $Executable -Destination $StageDir
& $WinDeployQt --release --compiler-runtime --no-translations --dir $StageDir (Join-Path $StageDir "DannyDeeTrackManager.exe")
if ($LASTEXITCODE -ne 0) { throw "Qt-Laufzeitbereitstellung fehlgeschlagen." }

if (-not $FfmpegPath) {
    $ffmpeg = Get-Command "ffmpeg" -ErrorAction SilentlyContinue
    if ($ffmpeg) { $FfmpegPath = $ffmpeg.Source }
}
if ($FfmpegPath -and (Test-Path -LiteralPath $FfmpegPath)) {
    $ToolsDir = Join-Path $StageDir "tools"
    New-Item -ItemType Directory -Force -Path $ToolsDir | Out-Null
    Copy-Item -LiteralPath $FfmpegPath -Destination (Join-Path $ToolsDir "ffmpeg.exe")
    $FfprobePath = Join-Path (Split-Path -Parent $FfmpegPath) "ffprobe.exe"
    if (Test-Path -LiteralPath $FfprobePath) {
        Copy-Item -LiteralPath $FfprobePath -Destination (Join-Path $ToolsDir "ffprobe.exe")
    }
} else {
    Write-Warning "FFmpeg wurde nicht eingebettet. Konvertierung benötigt FFmpeg im PATH."
}

if (-not $YtDlpPath) {
    $ytDlp = Get-Command "yt-dlp" -ErrorAction SilentlyContinue
    if ($ytDlp) { $YtDlpPath = $ytDlp.Source }
}
if ($YtDlpPath -and (Test-Path -LiteralPath $YtDlpPath)) {
    $ToolsDir = Join-Path $StageDir "tools"
    New-Item -ItemType Directory -Force -Path $ToolsDir | Out-Null
    Copy-Item -LiteralPath $YtDlpPath -Destination (Join-Path $ToolsDir "yt-dlp.exe")
} else {
    Write-Warning "yt-dlp wurde nicht eingebettet. Medien-Link-Downloads benötigen yt-dlp im PATH."
}

if (-not $SpotDlPath) {
    $spotDl = Get-Command "spotdl" -ErrorAction SilentlyContinue
    if ($spotDl) { $SpotDlPath = $spotDl.Source }
}
if ($SpotDlPath -and (Test-Path -LiteralPath $SpotDlPath)) {
    $ToolsDir = Join-Path $StageDir "tools"
    New-Item -ItemType Directory -Force -Path $ToolsDir | Out-Null
    Copy-Item -LiteralPath $SpotDlPath -Destination (Join-Path $ToolsDir "spotdl.exe")
} else {
    Write-Warning "spotDL wurde nicht eingebettet. Spotify-Links benötigen spotDL im PATH."
}

if (-not $DenoPath) {
    $deno = Get-Command "deno" -ErrorAction SilentlyContinue
    if ($deno) { $DenoPath = $deno.Source }
}
if ($DenoPath -and (Test-Path -LiteralPath $DenoPath)) {
    $ToolsDir = Join-Path $StageDir "tools"
    New-Item -ItemType Directory -Force -Path $ToolsDir | Out-Null
    Copy-Item -LiteralPath $DenoPath -Destination (Join-Path $ToolsDir "deno.exe")
} else {
    Write-Warning "Deno wurde nicht eingebettet. Einige YouTube-Links können ohne JavaScript-Laufzeit fehlschlagen."
}

Copy-Item -LiteralPath (Join-Path $ProjectRoot "README.md") -Destination $StageDir
Copy-Item -LiteralPath (Join-Path $ProjectRoot "THIRD_PARTY_NOTICES.txt") -Destination $StageDir

$IssFile = Join-Path $ProjectRoot "installer\DannyDeeTrackManager.iss"
& $Iscc "/DAppVersion=$AppVersion" "/DStageDir=$StageDir" "/DOutputDir=$DistDir" $IssFile
if ($LASTEXITCODE -ne 0) { throw "Installer-Erstellung fehlgeschlagen." }

$Setup = Get-ChildItem -LiteralPath $DistDir -Filter "*-Setup.exe" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $Setup) { throw "Installer wurde nicht gefunden." }
$ChecksumFile = Join-Path $DistDir "SHA256SUMS.txt"
$SetupHash = (Get-FileHash -LiteralPath $Setup.FullName -Algorithm SHA256).Hash.ToUpperInvariant()
$ExistingChecksums = @()
if (Test-Path -LiteralPath $ChecksumFile) {
    $EscapedSetupName = [regex]::Escape($Setup.Name)
    $ExistingChecksums = @(Get-Content -LiteralPath $ChecksumFile | Where-Object {
        $_ -notmatch "\s+$EscapedSetupName$"
    })
}
@($ExistingChecksums + "$SetupHash  $($Setup.Name)") | Set-Content -LiteralPath $ChecksumFile -Encoding ascii
Write-Host "Installer erstellt: $($Setup.FullName)"
Write-Host "SHA256: $SetupHash"
