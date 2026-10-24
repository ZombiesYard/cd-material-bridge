# CD Material Bridge 0.1.1 — Development and Integration

## Scope

CD Material Bridge is a Windows x64 ASI plugin that enables the texture registry's alpha flag for explicitly matched outfit DDS files. It supports alpha-test cutouts; the game material determines the threshold and actual shading.

The plugin does not generate alpha textures or change meshes, skinning, physics, color space, normals, metallic or roughness data. It does not turn an opaque shader into a blended-transparency shader. It accepts BC2 / BC3 (`DXT3` / `DXT5`) textures with a legacy 128-byte DDS header, not DX10 headers. DDS archive records must use a supported direct-storage encoding.

## Layout and build

```text
src/
  bridge.cpp                 Plugin implementation
  version.rc                 Windows version resource
scripts/
  build_windows.py           Standalone build entry point
  generate_rule.py           INI rule generator
output/
  CDMaterialBridge.asi        Compiled plugin
```

Requirements: Windows x64, Python 3.9 or later, Visual Studio 2022 or Build Tools with Desktop development with C++, and a Windows SDK. The build uses the static MSVC runtime (`/MT`) and the Windows system library `bcrypt.lib`. Game files, outfit assets and an ASI loader are not required to compile it.

From the repository directory:

```powershell
py scripts/build_windows.py
```

The script locates an x64 C++ installation with `vswhere.exe`, or uses a configured x64 Native Tools Command Prompt. For a nonstandard installation:

```powershell
py scripts/build_windows.py --vcvars64 "C:\Tools\VisualStudio\VC\Auxiliary\Build\vcvars64.bat"
```

Paths are resolved relative to the script, independently of the working directory. Compiler options include C++17, `/O2 /W4 /WX /MT` and `/DYNAMICBASE /NXCOMPAT /Brepro`. Temporary files and the log go to `build/`. A successful build updates `output/CDMaterialBridge.asi` and prints its SHA256. It does not package, install or load the plugin. Different compiler versions, options and version resources can produce different binary hashes.

## Initialization and file handling

`DllMain` only records the module handle and disables thread notifications. The loader calls the exported `InitializeASI` once. The plugin checks for the process filename `CrimsonDesert.exe` and locates Ultimate ASI Loader's `IsUltimateASILoader`, `AddVirtualPathForOverloadW` and `RemoveVirtualPathFromOverloadW` interfaces.

1. Read `CDMaterialBridge/rules/*.ini` relative to the ASI, with limits of 256 files and 1,024 texture rules.
2. Read `meta/0.papgt` in the game root and inspect each group's `0.pamt` in registry order. The first matching path wins; a failed identity check does not fall back to an older, lower-priority asset.
3. Read the DDS from its PAZ and verify the path, dimensions, mip count, FourCC and SHA256 of the complete compressed pixel payload.
4. Find the matching `meta/0.pathc` entry, check its DDS header and `BaseFlags`, and set only the lowest flag bit at header offset 124: `flags |= 1`.
5. If other entries share the header, clone it and redirect only the selected entry. Preserve other flags, records and mip-block information.
6. Re-read PATHC and PAPGT to detect mount changes during initialization. Write and verify a corrected registry cache, register a loader path redirect, then verify the redirected read.

The original PATHC, PAPGT, PAMT, PAZ and game executable are not overwritten. Cache files and logs are written to disk. Redirect priority is 0; a loader refusal is logged. A failed readback after registration removes that redirect.

## PATHC handling

The supported layout has a 28-byte table header, an array of 148-byte DDS headers, path checksums, 20-byte direct entries, 24-byte collision records and a name block. Lookup uses Jenkins lookup3 with the game's seed; collision entries also require a full-path match. Unrelated existing collision ranges are preserved. An invalid range for a requested texture causes that operation to be skipped. Version 0.1.1 accepts an empty root directory in PAMT directory tables; texture rules still require valid, nonempty relative paths.

## Creating texture rules

Prepare a compatible game material and a BC2 / BC3 alpha texture, then extract the final mounted DDS. If the manager resizes or re-encodes it, generate the rule from the resulting file.

```powershell
py scripts/generate_rule.py mounted-lace.dds --texture character/texture/example/lace.dds --name ExampleLace --base-flags 4 --output ExampleLace.ini
```

`4` is an example. Replace it with the actual PATHC flags with the lowest bit cleared.

| Field | Requirement |
| --- | --- |
| `[Texture:Name]` | Rule section name |
| `Enabled` | `0` disables a rule; the generator writes `1` |
| `Path` | Virtual `.dds` path under `character/texture/` |
| `Width` / `Height` | 1–16,384; must match the DDS and registry |
| `Mips` | 1–15; must match the DDS and registry |
| `FourCC` | `DXT3` or `DXT5` |
| `BaseFlags` | Expected flags with the lowest alpha bit cleared |
| `PixelSHA256` | SHA256 of every byte after the 128-byte DDS header, covering all mips |

Place the INI in `CDMaterialBridge/rules/`, retaining profiles needed by other outfits. Python is not a runtime dependency. Different pixel versions may have rules for the same path; only the mounted version can match. Regenerate rules after texture updates, resizing or re-encoding instead of disabling the hash check.

## Runtime files and compatibility

- Log: `bin64/CDMaterialBridge/CDMaterialBridge.log`.
- Cache: `bin64/CDMaterialBridge/cache/<SHA256>.pathc`, named from the complete corrected table.
- The usual ASI location is `bin64`; rule and cache directories are always relative to the actual ASI location.
- Rules load during initialization. Fully restart the game after changing rules, mounts or plugin enablement.
- Do not remount while the game is running. The initialization readback does not make arbitrary later mount changes safe.
- Another plugin redirecting the same PATHC may conflict. Format or loader API changes can require an implementation update.
