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

### Windows ARM64 builds

Optimized MSVC ARM64 builds compile Luau's `VM/src/lstate.cpp` with `/O1`.
MSVC 19.51 with `/O2` miscompiles the caged GC page initialization introduced in
[Luau 0.738 (`c54f558b`)](https://github.com/luau-lang/luau/commit/c54f558b4d5748ab0658610b8ce0c432053e41eb):
it writes through an uninitialized stack pointer instead of clearing the new page-list fields.
This can cause an access violation in `lua_newstate`, or leave an invalid list that
later crashes `luaM_visitgco` during `lua_close`. The remaining translation units
use their normal optimization settings.

The [regression diagnostics](https://github.com/rosslight/Darp.Luau.Native/actions/runs/37587427985)
show the release-sync parent passing and the default 0.741 build crashing.
The [`/O1` diagnostics](https://github.com/rosslight/Darp.Luau.Native/actions/runs/37587949396/job/112682367797)
pass repeated state creation and closure on a worker thread and DLL unloading.
CI also runs the packaged `win-arm64` runtime on `windows-11-arm`.

Pack nuget package:
```shell
dotnet pack src/Darp.Luau.Native/Darp.Luau.Native.csproj -c Release -o artifacts/packages
```
