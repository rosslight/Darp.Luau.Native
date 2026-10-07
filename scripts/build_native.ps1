param(
  [Parameter(Mandatory = $true)]
  [string]$RuntimeId,
  [Parameter(Mandatory = $true)]
  [string]$Generator,
  [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

# Parse RuntimeId into OS and arch (e.g. "win-x64" → os=win, arch=x64)
$parts = $RuntimeId -split '-', 2
if ($parts.Length -ne 2) {
  throw "Invalid RuntimeId '$RuntimeId'. Expected format: <os>-<arch> (e.g. win-x64, linux-arm64, osx-arm64)"
}
$os = $parts[0]
$arch = $parts[1]

# Everything relative to the script
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = [System.IO.Path]::GetFullPath((Join-Path $ScriptDir "../artifacts/build"))
$OutputDir = [System.IO.Path]::GetFullPath((Join-Path (Join-Path $ScriptDir "../artifacts/native") $RuntimeId))
$SourceDir = [System.IO.Path]::GetFullPath((Join-Path $ScriptDir "../native"))

Write-Host "Runtime ID:       $RuntimeId (os=$os, arch=$arch)"
Write-Host "Generator:        $Generator"
Write-Host "Configuration:    $Configuration"
Write-Host "Build directory:  $BuildDir"
Write-Host "Source directory:  $SourceDir"
Write-Host "Output directory: $OutputDir"
Write-Host ""

if (!(Test-Path $OutputDir)) {
  New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
}

if (Test-Path $BuildDir) {
  Remove-Item -Recurse -Force $BuildDir
  Write-Host "Cleaned BuildDir"
  mkdir $BuildDir | Out-Null
}

# Build CMake configure arguments based on RID
$cmakeArgs = @("-S", $SourceDir, "-B", $BuildDir, "-G", $Generator)

switch ($os) {
  "win" {
    $msvcArch = if ($arch -eq "arm64") { "ARM64" } else { $arch }
    $cmakeArgs += "-A", $msvcArch, "-T", "ClangCL"
  }
  "linux" {
    $cmakeArgs += "-DCMAKE_BUILD_TYPE=$Configuration"
  }
  "osx" {
    $cmakeArgs += "-DCMAKE_BUILD_TYPE=$Configuration"
    $osxArch = if ($arch -eq "x64") { "x86_64" } else { $arch }
    $cmakeArgs += "-DCMAKE_OSX_ARCHITECTURES=$osxArch"
  }
  default {
    throw "Unsupported OS prefix '$os' in RuntimeId '$RuntimeId'. Expected win, linux, or osx."
  }
}

& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }

cmake --build $BuildDir --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw "CMake build failed with exit code $LASTEXITCODE" }

# Resolve output library path based on RID
switch ($os) {
  "win"   { $LibPath = Join-Path $BuildDir "$Configuration/luau.dll" }
  "linux" { $LibPath = Join-Path $BuildDir "libluau.so" }
  "osx"   { $LibPath = Join-Path $BuildDir "libluau.dylib" }
}

if (!(Test-Path $LibPath)) {
  throw "Native library not found at $LibPath"
}

Copy-Item $LibPath $OutputDir -Force
Write-Host "Copied $LibPath -> $OutputDir"
