## Mods — where to get them

JK4SPEED looks like the Xbox 360 version of Most Wanted because of community mods. **JK4SPEED does not include them, nor any game file.** You download each mod from its author, the same way you bring the game, and drop the download **as it is, still zipped,** in a drop folder (`/data/Jk4Speed/Mods` on the console, or `USB:/Jk4Speed` on a USB drive). Every PATCH reads the archives, checks them, and builds the game files from your game and the authors' releases. Any combination of the three works.

Official JK4SPEED page: https://github.com/Salru13/Jk4Speed

| Mod | Author | Get it from | What you download | SHA-256 | Needs |
|---|---|---|---|---|---|
| Xbox 360 Stuff Pack v4.2 | elaymm4 and many others (see Credits) | https://nfsmods.xyz/mod/1200 | the pack's download (a Google Drive folder; your browser saves it as one or more `.zip` files) | none: a folder download has no fixed hash. JK4SPEED checks every file it uses inside it (list below) | — |
| NFSMW: Upscaled HUD (2026-01-02) | LunicAura106 | https://nfsmods.xyz/mod/1096 | `NFSMWUHUD01022026b.7z`, 173,689,282 bytes | `61d793dea108eafff9a57484e86b2e2c6d6a888a537f9de8fac19c436f189660` | — |
| NFSMW HD Road Textures | N4SRazer | https://www.nfsaddons.com/downloads/nfsmw/tools/5410/nfsmw-hd-road-textures.html | the ZIP from that page, 1,682,999 bytes (any file name) | `7857e85120564227fe90ec0667cc9414c90136e1937431fe59bd321e0bbce4e9` | — |

Please download from these pages, so the authors get the credit and the visits.

### Step by step

1. Download the mods you want from the pages above. Do not unpack them.
2. Put the downloads in a drop folder, either one:
   ```
   /data/Jk4Speed/Mods/                 (on the console, over FTP)
   USB:/Jk4Speed/                       (the top of a USB drive)
   ```
   For example:
   ```
   NFSMW X360 Stuff Pack v4.2-....zip   (one or more parts, as the browser saved them)
   NFSMWUHUD01022026b.7z
   <the HD road ZIP>
   ```
3. In JK4SPEED open **SETTINGS > MODS** (or **PATCH > □ Mods**), turn each mod ON or OFF (✕ or ← →) and press **PATCH**. The page shows for each mod: installed, installs at the next Patch, archive not found, removed at the next Patch, off, or archive damaged. □ Apply now runs the mod part of PATCH at once. The focused row says where to get it.

Your game folder only needs to be the untouched game (PC v1.3 Black Edition). Do **not** install the pack on the PC first: JK4SPEED does that part on the console, from the pack's own files.

### What happens at PATCH

- **Any combination.** Each mod is its own switch; all eight combinations work (none, any one, any two, all three). Every PATCH rebuilds every managed game file from the unchanged copy plus the mods that are ON, so the result does not depend on the order you turned things on in.
- **Off and missing downloads.** Turning a mod OFF puts your original files for it back at the next PATCH and does not need its download. A mod that is ON but whose download is gone stays installed as it is.
- **The unchanged copy.** The first PATCH keeps an unchanged copy of every game file a mod replaces (the road file alone is 532 MB). A copy is accepted only when it has the vanilla hash. If your game folder already had mods in it, there is none: PATCH says "Mods can't be changed" (Help > Troubleshooting). Copy the game folder again from a PC install without mods, then press PATCH. Saves stay.
- **Checks first.** The HUD 7z and the road ZIP must match the SHA-256 above, byte for byte. In the pack's download every file JK4SPEED uses must be there with its released SHA-256. A damaged or different archive is named on screen and skipped; RAR is not supported (none of these mods is a RAR).
- **Atomic.** Everything is built in a staging area and checked (SHA-256 of every result, and the game's own JDLZ counter on compressed data, so the old crashing HUD file can never come back). Only then are the files swapped in, under a journal. If something fails, or the console loses power, the game folder is exactly as it was; the next open says "Patch was interrupted" once.
- **Add or remove later.** Drop another archive in and press PATCH: it is added when its switch is ON. Switch one OFF and press PATCH: its files go and your originals come back. A PATCH with nothing to change changes nothing.
- **Your settings stay.** The 30/60 FPS choice in SETTINGS is kept.

### What JK4SPEED makes from your files

The package carries small **binary deltas** (`jk4speed-files/deltas/`, format JKD1). Each one copies byte ranges out of your game files and the mods' files and adds only JK4SPEED's own bytes; the launcher checks the result's SHA-256. The build tool (`launcher/title/author_deltas.py`) refuses any delta whose own bytes contain a 64-byte block of any file of the pack, the HUD or the road.

| Result | Level | Made from | JK4SPEED's own bytes |
|---|---|---|---:|
| `GLOBAL/attributes.bin` | pack | your `attributes.bin` | 1,004 (the pack's game-setting values, see note) |
| `GLOBAL/GlobalB.lzc` | pack | your `GLOBALB.BUN` + 38 pack textures | 5,623 (texture records, sky merge) |
| `GLOBAL/InGameA.bun` | pack | your `InGameA.bun` + 7 pack textures | 601 |
| `scripts/X360Stuff.ini` | pack | the pack's `X360Stuff.ini` | 192 (JK4SPEED's settings) |
| `TRACKS/STREAML2RA.BUN` | pack | your road + the pack's `Install-Part-3.ups` (applied on the console) | 0 |
| `scripts/X360Stuff.asi`, 18 shaders `FX/COMPILED/*.fx.o` | pack | copied from the pack (LowPerf shaders) | 0 |
| `GLOBAL/InGameA.bun` | HUD | your `InGameA.bun` + pack textures + Upscaled HUD textures | 4,062 |
| `GLOBAL/HUDTEXRACE.BIN` | HUD | your file + 22 Upscaled HUD textures | 841 |
| `GLOBAL/HUDS_Custom_00.bin` | HUD | your file + 8 Upscaled HUD gauges; compressed on the console with the game's JDLZ, crash-fixed (`a67a45eb…`) | 1,704 |
| `TRACKS/STREAML2RA.BUN` | road | the pack-level road + 15 of N4SRazer's textures | 126 |
| `scripts/NFSMW_XenonEffects.*`, `XenonEffects.tpk` | pack | Xenon Effects by xan1242 (MIT), included in JK4SPEED | — |

Note on `attributes.bin`: the pack changes game settings through a script for NFS-VltEd; JK4SPEED stores the resulting values (1,004 bytes of numbers) in its delta rather than running that editor on the console.

### The pack files JK4SPEED reads (by SHA-256)

The full list with sizes and hashes is in `launcher/src/jk_modset_catalog.inc` (generated by `launcher/title/author_deltas.py`): `Install-Part-3.ups`, `ModData/Files/scripts/X360Stuff.asi`, `ModData/Files/scripts/X360Stuff.ini`, the 18 `AlternativeShaders/LowPerf/*.fx.o` and the pack textures the deltas name.
