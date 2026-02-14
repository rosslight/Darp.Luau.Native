# Generate C# bindings by running the Rust build (build.rs runs bindgen + csbindgen).
$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

# Point cargo directly at the Rust crate's Cargo.toml
$ManifestPath = Join-Path $ScriptDir "Cargo.toml"

Write-Host "Generating C# bindings..." -ForegroundColor Cyan
& cargo build --manifest-path $ManifestPath

$exitCode = $LASTEXITCODE
if ($exitCode -ne 0) {
    Write-Host "Bindings generation failed." -ForegroundColor Red
    exit $exitCode
}

Write-Host "C# bindings generated: src/Darp.Luau.Native/LuauNative.g.cs" -ForegroundColor Green
