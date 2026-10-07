# Darp.Luau.Native

[![NuGet](https://img.shields.io/nuget/v/Darp.Luau.Native.svg)](https://www.nuget.org/packages/Darp.Luau.Native)
[![Downloads](https://img.shields.io/nuget/dt/Darp.Luau.Native)](https://www.nuget.org/packages/Darp.Luau.Native)

.NET bindings for [Luau](https://github.com/luau-lang/luau), with native libraries
for Windows, Linux, and macOS.

## Supported runtimes

| OS | x64 | ARM64 |
| --- | --- | --- |
| Windows | `win-x64` | `win-arm64` |
| Linux | `linux-x64` | `linux-arm64` |
| macOS | `osx-x64` | `osx-arm64` |

CI tests the packed NuGet package on native runners for all six runtimes on every
push and pull request, and before publishing.

## Build locally

Windows builds use Visual Studio's `ClangCL` toolset. Install the C++ Clang tools
for Windows, the Windows SDK, and the C++ build tools for your target architecture.
Clang-cl avoids the MSVC ARM64 crash documented in [#18](https://github.com/rosslight/Darp.Luau.Native/pull/18).

Generate the bindings (requires Rust):

```powershell
./scripts/generate_bindings.ps1
```

Build a native library for your platform:

```powershell
./scripts/build_native.ps1 -RuntimeId 'win-x64' -Generator 'Visual Studio 18 2026'
./scripts/build_native.ps1 -RuntimeId 'linux-x64' -Generator 'Ninja'
./scripts/build_native.ps1 -RuntimeId 'osx-x64' -Generator 'Ninja'
```

Use the ARM64 runtime ID for ARM64 builds. Linux builds require a host with the
target architecture.

Pack the NuGet package:

```shell
dotnet pack src/Darp.Luau.Native/Darp.Luau.Native.csproj -c Release -o artifacts/packages
```

## Releases

Tags include the package and Luau versions, for example `v0.6.0+luau.0.741`.
The Luau version matches the `native/luau` submodule.

The Luau update workflow sets Release Please's `bootstrap-sha` to the latest
release commit so changelog generation starts from that release.
