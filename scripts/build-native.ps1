param(
  [Parameter(Mandatory = $true)]
  [string]$OutputDir,
  [string]$Configuration = "Release",
  [string]$Generator = "",  # empty = use CMake's default generator
  [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"

# Everything relative to the caller
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = [System.IO.Path]::GetFullPath((Join-Path $ScriptDir "../artifacts/build"))
$SourceDir = [System.IO.Path]::GetFullPath((Join-Path $ScriptDir "../native"))

Write-Host "Build directory:  $BuildDir"
Write-Host "Source directory:  $SourceDir"
Write-Host "Output directory: $OutputDir"
Write-Host ""

if (!(Test-Path $OutputDir)) {
  New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
}

if (Test-Path $BuildDir) {
  Remove-Item -Recurse -Force $BuildDir
  mkdir $BuildDir
}

$cmakeArgs = @("-S", $SourceDir, "-B", $BuildDir, "-A", $Platform)
if ($Generator) { $cmakeArgs += "-G", $Generator }
& cmake @cmakeArgs
cmake --build $BuildDir --config $Configuration --parallel

$DllPath = Join-Path $BuildDir "$Configuration\luau.dll"
if (!(Test-Path $DllPath)) {
  throw "luau.dll not found at $DllPath"
}

Copy-Item $DllPath $OutputDir -Force
Write-Host "Copied $DllPath -> $OutputDir"
