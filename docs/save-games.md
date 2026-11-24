# Save games (player guide)

JK4SPEED runs Most Wanted PC v1.3 Black Edition on the console. The game keeps its saves in `/data/Jk4Speed/Saves`, one folder per profile ("alias"), each holding one file of the same name:

```
/data/Jk4Speed/Saves/NFS Most Wanted/<alias>/<alias>
```

The game's "My Documents" is redirected to `/data/Jk4Speed/Saves`, so the saves sit next to the game, not inside a hidden system folder.

**Saves from the older launcher (r3):** on the first open the launcher copies them once into the folder above, when the new folder has no profile yet. They are copied, never moved: the old folder stays as it was. A second copy is never made.

The launcher shows this path in **SETTINGS > SAVE GAMES**. The folder exists but stays empty until the game saves for the first time; the screen then says "No saves yet". An import creates the folder when it is missing.

Saves from **v1.3 Black Edition** are supported. Saves made with other mods (extra cars, for example) may not load.

## Bring your save from the PC

**Copy your PC save from `Documents\NFS Most Wanted` to `USB:/Jk4Speed/saves/` → SETTINGS → Save games → Import.**

1. On the PC, open `Documents\NFS Most Wanted`. It holds one folder per profile, for example `Documents\NFS Most Wanted\GOATS\GOATS`.
2. On the USB drive, make the folder `Jk4Speed/saves/` and put one of these in it:
   - the whole `NFS Most Wanted` folder: `Jk4Speed/saves/NFS Most Wanted/GOATS/GOATS`;
   - or just the profile folders: `Jk4Speed/saves/GOATS/GOATS`;
   - or a `.zip` (or `.7z`) of either.
3. Plug the drive into the console. In JK4SPEED open **SETTINGS > SAVE GAMES** and press □ **Import from USB**.
4. The launcher lists what it found: the profile name, the date and where it comes from (a PC or a JK4SPEED export). Files that are not Most Wanted saves are named and skipped. Press ✕ to import.
5. If a profile with the same name is already on the console, you choose: ✕ **Overwrite** or □ **Keep both** (the imported copy gets a new name that differs in the last character, e.g. `GOATS` → `GOAT2`).

Before anything changes, your current console saves are backed up by the launcher. The import is built next to the real folder, every save in it is checked, and only then is it swapped in. If the console loses power in the middle, the next start of the launcher puts the folder right.

What "checked" means: the file has the save header (`20CM`), the right size (63,596 bytes for v1.3), and all four checksums hold (three CRCs in the header and the MD5 at the end; `docs/launcher/2026-11-23-save-format.md`).

## Player name (rename a profile)

The game asks for a name when you make a profile, but there is no on-screen keyboard in the game on the console, so keep the default `NAME`. Then, with the game closed:

1. **SETTINGS > SAVE GAMES**, press △ **Player name** (with more than one profile, pick it first).
2. Type the name with the launcher's keyboard (D-pad to move, ✕ to type, □ to delete, △ for capitals, ✕ on **OK** to finish). Letters and digits only, 1 to 15 characters.
3. The launcher backs up the saves, writes the new name into the save (and updates its checksums), and renames the folder and file. A name that another profile already has (in any case) is refused.

## Take your save to the PC

**SETTINGS > SAVE GAMES**, ✕ **Export to USB**. The copy lands in `USB:/Jk4Speed/saves/<date>/NFS Most Wanted/`. An export never overwrites an earlier one. On the PC, copy the profile folders into `Documents\NFS Most Wanted\`.

Without a USB drive: copy the folder by FTP into the path above (game closed).

## Status

- The path is confirmed on the console (pre-check 2026-11-23, `docs/launcher/2026-11-23-console-prechecks.md`): the folder is there and empty until the first save.
- The save layout and the rename recipe come from `speed.exe` and a real save (`docs/launcher/2026-11-23-save-format.md`); the launcher reproduces the doc's test vector (`NAME` → `Joker`, SHA-256 `8c3ccdcb…`). Loading a renamed save in the game on the console is still to be tried.
