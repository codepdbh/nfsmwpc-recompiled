# Android Mods Manager

The PC-version Android app exposes the manager in two ways:

- A separate **NFSMW Mods Manager** launcher icon (runs in the `:mods` process).
- A shortcut inside the normal game launcher.

Both entries are part of the same APK and use the existing `nfsmw` game folder.

## Install

Open Mods Manager and grant the app access to shared storage when prompted. The manager uses Android's document picker to import ZIP archives; it does not need broad access to the Downloads folder to read the selected archive.

## ZIP import

1. Select **IMPORT ZIP** and choose a mod archive.
2. The manager extracts into a temporary directory beneath `nfsmw/modloader`.
3. A single enclosing top-level folder is removed automatically, so archives packaged as `MyMod/CARS/...` install as `MyMod/CARS/...` under the mod's own directory.
4. The completed mod is moved into place only after extraction succeeds.

Import protections:

- Reject absolute paths, drive-prefixed paths and ZIP path traversal.
- Limit archives to 12,000 entries and 1 GiB expanded data.
- Never overwrite an existing mod with the same name.
- On extraction failure, remove the temporary/partial installation.
- The archive is read through the Storage Access Framework URI selected by the user.

## Enable / disable / delete

Enabled mods are ordinary directories. Disabled mods are prefixed with a dot, matching the native overlay scanner. The manager can toggle that prefix or delete a mod folder. Deletion affects only the selected mod folder, never the original game installation.

The native overlay scans mods when the game process starts. Exit and relaunch the game after importing, enabling, disabling or deleting mods.

## Supported content

The manager installs whole-file mirrored-folder replacements and additions. It does not yet parse `mod.json`, load ASI plugins, execute Binary scripts, apply VltEd database edits, or provide Unlimiter's add-on vehicle registration.
