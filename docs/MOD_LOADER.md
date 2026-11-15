# Android PC Mod Loader: file overlay

This document describes the first native mod-loader milestone for the Android
PC-version recompile.

## Folder layout

Place each mod in its own folder beneath the game installation:

```
nfsmw/
  Speed.exe
  CARS/
  GLOBAL/
  modloader/
    A_Mod_Name/
      mod.json                 # optional metadata; not interpreted yet
      CARS/
        ...
      GLOBAL/
        ...
    B_Mod_Name/
      GLOBAL/
        ...
    .Disabled_Mod/
      GLOBAL/
        ...
```

A mod mirrors the original game's directory structure. Only files that already
fit the guest file API's ordinary path-based reads are eligible for replacement.
The original installation is never rewritten by the overlay.

## Resolution and priority

For a read-only open of an existing guest path, the Android file bridge checks
enabled mod directories in case-insensitive alphabetical order, then falls back
to the original game directory. If multiple mods contain the same relative path,
the alphabetically last mod wins. A directory whose name starts with a dot is
disabled and ignored.

The resolver also merges directory listings from the original game tree and
enabled mod trees. This allows a mod to add a file under a directory which the
game enumerates before opening it.

The enabled-mod directory list is cached for the lifetime of the game process.
Install, disable, enable, reorder, or remove mods while the game is stopped,
then launch the game again.

## Safety boundaries

- Only read-only, open-existing file requests are redirected.
- Create, truncate, write, delete, rename, save and profile operations continue
  to target the original game/data paths.
- Mod directories must be real directories, not symlinks.
- Resolved files and enumerated entries are checked to remain inside their mod
  directory.
- `modloader/` and the bridge's `_outside/` sandbox cannot override themselves.
- This is a whole-file overlay. It does not yet implement texture-hash hooks,
  ASI loading, Binary script execution, VltEd editing, or Unlimiter features.

## Current milestone

This is the native file-system foundation only. The Android Mods Manager,
archive extraction, manifests, mod priorities, and the add-on-car/Unlimiter
engine work are separate milestones.
