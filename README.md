# CD Material Bridge

A Windows x64 ASI plugin for opt-in alpha-test texture registration in **Crimson Desert**.

**Start here: [Development and Integration (English)](docs/Development-and-Integration.en.md)**

[中文开发与接入说明](docs/开发与接入说明.zh-CN.md) · [User Guide](docs/Usage.en.md) · [中文使用说明](docs/使用说明.zh-CN.md)

## Downloads

- [Latest release](https://github.com/ZombiesYard/cd-material-bridge/releases/latest)
- [Download CDMaterialBridge.asi](https://github.com/ZombiesYard/cd-material-bridge/releases/latest/download/CDMaterialBridge.asi)
- [SHA256 checksums](https://github.com/ZombiesYard/cd-material-bridge/releases/latest/download/SHA256SUMS.txt)

The standalone ASI requires a compatible **Ultimate ASI Loader** and outfit-specific INI rules. Neither is bundled with the download. This is a raw plugin binary, not a DMM installation archive. See the [User Guide](docs/Usage.en.md) for installation and troubleshooting.

## What it does

- Matches the active mounted texture by virtual path, dimensions, mip count, format and complete compressed-pixel SHA256.
- Enables only the matching texture's alpha registry flag, isolating shared registry headers when needed.
- Writes a corrected registry cache and redirects reads through the loader, without overwriting original game archives or registry files.

It supports BC2 / BC3 alpha-test cutouts. It does not provide physics, change mesh data or PBR textures, or reproduce blended transparency and other game-specific shader effects. Cache files and logs are written locally.

## Build

Requires Windows x64, Python 3.9 or later, Visual Studio 2022 or Build Tools with the C++ desktop workload, and a Windows SDK.

```powershell
py scripts/build_windows.py
```

The output is `output/CDMaterialBridge.asi`. Game files and an ASI loader are not needed to compile it. See [Development and Integration](docs/Development-and-Integration.en.md) for build options, runtime behavior and rule generation.

## License

[MIT License](LICENSE), copyright 2026 ZombiesYard. The repository does not include game assets or a third-party ASI loader.
