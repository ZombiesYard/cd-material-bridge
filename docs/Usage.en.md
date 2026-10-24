# CD Material Bridge 0.1.1 — User Guide

## Purpose

The plugin enables alpha-test registration for specific textures supplied with compatible outfits, allowing texture alpha to create lace and other cutouts. It only processes resources whose path, resolution, format, mip count and pixel hash match an installed rule.

It supplies no outfits, textures or physics and does not change metallic, roughness or mesh data. Alpha testing creates cutouts; it does not independently reproduce blended transparency, refraction or every shader from another game.

## Installation

Windows x64 and an Ultimate ASI Loader build supporting the plugin's file-redirect interfaces are required. The standalone ASI does not include a loader. Python and Visual Studio are not required to run the plugin.

1. Fully exit the game.
2. Install and enable the compatible outfit in DMM, then use Mount Mods as usual.
3. If the outfit supplies a DMM plugin archive, install it through ASI Mods, enable CD Material Bridge, and follow DMM's prompts to configure a compatible loader.
4. Confirm that the outfit's INI rules are installed too. The standalone ASI contains no outfit profiles and makes no registry correction without rules.
5. Start the game. Fully exit and restart after changing the plugin or its rules.

A standalone source build produces a raw ASI, not a DMM installation ZIP. A typical manual installation has the following layout, with the loader installed separately:

```text
Crimson Desert/
  bin64/
    CDMaterialBridge.asi
    CDMaterialBridge/
      rules/
        ExampleOutfit.ini
```

Multiple outfits share one `CDMaterialBridge.asi` with their respective rules. Do not rename the plugin to load duplicate copies. The plugin cannot merge two different textures competing for the same virtual path.

## Operation and local data

At startup, the plugin reads the current mount tables, texture registry and textures referenced by its rules. It enables only the lowest alpha flag for matched entries. A shared registry header is copied for the selected entry so other textures are unaffected.

The corrected table is saved under `bin64/CDMaterialBridge/cache/`. The loader redirects the current process's corresponding file reads to that copy. Original game archives and registry files are not overwritten. The plugin also writes `bin64/CDMaterialBridge/CDMaterialBridge.log` with matches, skipped operations and redirect results.

Keep supplied textures at their original resolution and encoding. Resizing, recompressing or replacing any mip can change the pixel hash and prevent a rule from matching. The outfit maintainer must update rules for the final files.

## Troubleshooting

| Log entry | Meaning and action |
| --- | --- |
| `ASSET_VERIFIED` | The active texture passed identity checks; this is not visual validation |
| `ALPHA_ENABLED` | The target alpha flag was enabled in the corrected copy |
| `REDIRECT_READY` | Redirect registration and readback succeeded; check the material in game |
| `ALREADY_ENABLED` | Alpha was already enabled; no duplicate correction is needed |
| `NO_REDIRECT` | No valid entry needed correction; check preceding messages for the reason |
| `ASSET_MISSING` / `ASSET_SKIPPED` | Missing or overridden resource, different dimensions or pixels, or unsupported storage |
| `RULE_FILE_SKIPPED` | Invalid INI format; that rule file was skipped |
| `REGISTRY_SKIPPED` | The target registry entry did not meet expected conditions |
| `INACTIVE` | Initialization failed; the following text explains why |

If the log only reports `RULES 0`, check the rules directory. If no log is created, check loader and ASI enablement, the plugin location and directory write permissions.

## Disabling and compatibility

Exit the game, disable the plugin and restart to stop its redirect. Cached files may be deleted while the game is closed. Disabling the plugin does not undo edits made to original files by other tools.

If other outfits still need the plugin, remove only the unused outfit's rules. Game or loader updates, other plugins redirecting the same registry, and mods replacing the same texture path can require compatibility checks. Use Mount Mods only while the game is closed.
