# Darp.Luau.Native

[![NuGet](https://img.shields.io/nuget/v/Darp.Luau.Native.svg)](https://www.nuget.org/packages/Darp.Luau.Native)
[![Downloads](https://img.shields.io/nuget/dt/Darp.Luau.Native)](https://www.nuget.org/packages/Darp.Luau.Native)

Managed .NET bindings for [Luau](https://github.com/luau-lang/luau) with prebuilt native runtimes per RID.

Git tags follow the format `v1.2.3+luau.0.708`, where `1.2.3` is the package
SemVer and `0.708` is the Luau version equal the `native/luau`
submodule.

The Luau bump automation records the latest release commit as `bootstrap-sha` in
the Release Please configuration. This bounds changelog generation when changing
Luau metadata makes the version in the manifest differ from the published tag.

## Included native runtimes

- `win-x64`
- `win-arm64`
- `linux-x64`
- `linux-arm64`
- `osx-x64`
- `osx-arm64`

## Develop locally

Build bindings:
```powershell
./scripts/generate_bindings.ps1
```

Build native libraries:
```powershell
./scripts/build_native.ps1 -RuntimeId 'win-x64' -Generator 'Visual Studio 18 2026'
./scripts/build_native.ps1 -RuntimeId 'linux-x64' -Generator 'Ninja'
./scripts/build_native.ps1 -RuntimeId 'osx-x64' -Generator 'Ninja'
```

Pack nuget package:
```shell
dotnet pack src/Darp.Luau.Native/Darp.Luau.Native.csproj -c Release -o artifacts/packages
```
