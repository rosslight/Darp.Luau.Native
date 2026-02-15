# Darp.Luau.Native

Managed .NET bindings for Luau with prebuilt native runtimes per RID.

## Included native runtimes

- `win-x64`
- `win-arm64`
- `linux-x64`
- `linux-arm64`
- `osx-x64`
- `osx-arm64`

## Generate bindings

```powershell
./bindgen/generate_bindings.ps1
```

## Build locally

- Windows

```powershell
./scripts/build-native.ps1 artifacts/native -Platform x64 -Configuration Release
```

- Linux/macOS

```bash
./scripts/build-native.sh artifacts/native x64
```

## Pack

```bash
dotnet pack src/Darp.Luau.Native/Darp.Luau.Native.csproj -c Release -o artifacts/packages
```
