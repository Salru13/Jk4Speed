# JK4SPEED

**Need for Speed: Most Wanted (2005) on the PS4, with the Xbox 360 look.**

JK4SPEED is a homebrew package for jailbroken PS4 consoles. It runs your own copy of the PC version of
Need for Speed: Most Wanted (2005) on the console through a Wine-based runtime and a native Vulkan renderer,
and it builds the Xbox 360 look of the game from community mods that you download from their authors.

JK4SPEED ships **no game files and no mod files**. You bring the game; you download the mods; JK4SPEED's
launcher checks them and patches your copy on your own console.

Official source: **https://github.com/Salru13/Jk4Speed**

---

## Contents

- [Requirements](#requirements)
- [Install](#install)
- [The launcher: PLAY, PATCH, SETTINGS](#the-launcher-play-patch-settings)
- [Mods — where to get them](#mods--where-to-get-them)
- [Frame rate](#frame-rate)
- [Controls](#controls)
- [Save games](#save-games)
- [Official builds and verification](#official-builds-and-verification)
- [Support](#support)
- [Credits and licences](#credits-and-licences)
- [Disclaimer](#disclaimer)

---

## Requirements

| What | Details |
|---|---|
| Console | A **jailbroken PS4** running **GoldHEN** (to install homebrew PKGs). |
| Game | Your own copy of **Need for Speed: Most Wanted PC v1.3 Black Edition**. No other version is supported. `speed.exe` must be v1.3 (6,029,312 bytes). |
| Mods | The **Xbox 360 Stuff Pack (required)** and, if you like, the Upscaled HUD and HD Road Textures, downloaded from their authors' pages (below). |
| USB drive | To bring the mods and your PC saves to the console (FTP to `/data/Jk4Speed/` works too). |

We do not ship, sell or link to copies of the game. Please do not ask for them.

## Install

1. Download the JK4SPEED `.pkg` from the **Releases** page of https://github.com/Salru13/Jk4Speed (only there).
2. Optional: check the download on a PC (see [Official builds and verification](#official-builds-and-verification)).
3. Install the PKG on the PS4 with GoldHEN's package installer (or your usual homebrew PKG installer).
4. Copy your game folder (PC v1.3 Black Edition, untouched) to the console with FTP or a USB drive.
   `speed.exe` must sit directly in the game folder. The launcher shows the exact path on the **PATCH**
   screen if it cannot find the game.
5. Start **JK4SPEED** from the home screen. The first start shows **SETTING UP JK4SPEED** for about a minute or two
   (the Windows files the game needs are copied to `/data/Jk4Speed/System`). This happens once.

## The launcher: PLAY, PATCH, SETTINGS

The JK4SPEED launcher opens first. It has three items on its home screen:

| Item | What it does |
|---|---|
| **PLAY** | Starts the game. |
| **PATCH** | Checks your game folder, installs the JK4SPEED files and builds the mod files from the archives in `/data/Jk4Speed/Mods` (or `USB:/Jk4Speed/`). Anything it replaces is backed up first, and **Restore** puts it back. `SETTINGS > MODS` (or `PATCH > □ Mods`): the Xbox 360 Stuff Pack is **REQUIRED**; the Upscaled HUD and HD Road Textures each have an ON / OFF switch (ON by default). The first PATCH takes a few minutes (the pack is about 1 GB); the screen shows what it is reading, building and checking, in MB. |
| **SETTINGS** | **FRAME RATE**, **CONTROLS** (controller layout), **SAVE GAMES**, **SUPPORT** and **ABOUT**. |

On the game's title screen, wait until "Please press START" has fully faded in before you press ✕.

## Mods — where to get them

JK4SPEED looks like the Xbox 360 version of Most Wanted because of community mods. **JK4SPEED does not
include them.** Download each one from its author, the same way you bring the game. Please download from
these pages so the authors get the credit and the visits.

| Mod | Author | Official page | Archive file name | Size | SHA-256 |
|---|---|---|---|---|---|
| **Xbox 360 Stuff Pack v4.2 (required)** | elaymm4 and many others (see [CREDITS.md](CREDITS.md)) | https://nfsmods.xyz/mod/1200 | On the Google Drive page, download the **whole "Complex Installation" folder**. Google Drive zips it into one or more files such as `Complex Installation-20261124T090016Z-1-001.zip`. Put **all** parts in. | about 940 MB in total | no whole-archive hash: JK4SPEED checks every file it uses inside the pack by its own SHA-256 |
| NFSMW: Upscaled HUD (optional) | LunicAura106 | https://nfsmods.xyz/mod/1096 | **only** `NFSMWUHUD01022026b.7z` (2 January 2026). Older versions such as `NFSMWUHUD11302024a.7z` are not used. | 173,689,282 bytes | `61d793dea108eafff9a57484e86b2e2c6d6a888a537f9de8fac19c436f189660` |
| NFSMW HD Road Textures (optional) | N4SRazer | https://www.nfsaddons.com/downloads/nfsmw/tools/5410/nfsmw-hd-road-textures.html | the ZIP from that page (any file name) | 1,682,999 bytes | `7857e85120564227fe90ec0667cc9414c90136e1937431fe59bd321e0bbce4e9` |

### Step by step

1. Download the mods from the pages above. **Do not unpack them.**
2. Put each archive, **still zipped**, in `/data/Jk4Speed/Mods/` on the console (FTP), or in `USB:/Jk4Speed/` on a
   USB drive:
   ```
   /data/Jk4Speed/Mods/<all "Complex Installation" zip parts>
   /data/Jk4Speed/Mods/NFSMWUHUD01022026b.7z
   /data/Jk4Speed/Mods/<the HD Road Textures zip>
   ```
   Over FTP, use file names **without spaces** (rename `Complex Installation-...zip` to `Complex_Installation-...zip`):
   the console's FTP server refuses names with spaces. JK4SPEED accepts any name.
3. Plug the drive into the PS4, open JK4SPEED and press **PATCH**.

Your game folder only needs to be the untouched PC v1.3 Black Edition. Do **not** install the pack on the PC
first: JK4SPEED does that part on the console, from the pack's own files.

### Patch levels

| You dropped | You get |
|---|---|
| no X360 Stuff Pack | PATCH refuses and says which download is missing (without the pack the PC game is stretched on a 16:9 TV) |
| the X360 Stuff Pack | **X360 base**: the Xbox 360 look |
| the pack + Upscaled HUD | X360 base **+ HD HUD** |
| the pack + HD Road Textures | X360 base **+ HD road** |
| all three | X360 base **+ HD HUD + HD road** |

- **The X360 Stuff Pack is required.** The HUD and the road are optional extras on top of it.
- Every archive is checked first. The HUD 7z and the road ZIP must match the SHA-256 above byte for byte. A damaged
  or different archive is named on screen and skipped.
- **Add a mod later**: drop the archive in `USB:/Jk4Speed/` and press **PATCH** again; it is picked up. Remove one and
  press **PATCH**: its files go and your originals come back.
- PATCH is atomic: everything is built and checked first, then swapped in. If something fails or the console loses
  power, your game folder stays as it was.
- Your 30/60 FPS choice is kept across PATCH runs.

## Frame rate

**SETTINGS → FRAME RATE**: **30** or **60**.

| Setting | What you get |
|---|---|
| **30** (default) | An even 30 FPS. The smoothest option: in our tests about 97 % of frames land on time. |
| **60** | Up to 60 FPS in races, but less even than 30 (some frames arrive late, which you may feel as judder). |

Pick what feels best to you; you can switch at any time.

## Controls

DualShock 4 layout (also shown in **SETTINGS → CONTROLS**). No rebinding in this version.

| DS4 | Action |
|---|---|
| R2 | Gas |
| L2 | Brake / reverse |
| R1 | Shift up |
| L1 | Shift down |
| Square | Nitro (N2O) |
| Triangle | Speedbreaker |
| Cross | Handbrake / accept (menus) |
| Circle | Brake / back (menus) |
| R3 | Change camera view |
| Right stick pulled back | Look behind (hold) |
| L3 | Reset car |
| Options | Pause |
| Left stick | Steering |
| D-pad | Menu navigation |

## Save games

Your saves live in `/data/Jk4Speed/Saves/NFS Most Wanted/`, one folder per profile. **SETTINGS → SAVE GAMES**
shows where. Saves from **PC v1.3 Black Edition** are supported; saves made with other mods (extra cars, for
example) may not load.

### Bring your PC save

1. On the PC, copy `Documents\NFS Most Wanted` to **`USB:/Jk4Speed/saves/`** (the whole folder, just its profile
   folders, or a zip of them).
2. Plug the drive into the PS4 and open **SETTINGS → SAVE GAMES → Import**.
3. Pick what to import and press ✕. If a profile with the same name already exists, choose **Overwrite** or
   **Keep both**.

Your current console saves are backed up before anything changes.

### Export

**SETTINGS → SAVE GAMES → Export to USB**. The copy lands in `USB:/Jk4Speed/saves/<date>/NFS Most Wanted/`
and never overwrites an earlier export. On the PC, copy the profile folders into `Documents\NFS Most Wanted\`.

### Rename your profile

The game has no on-screen keyboard on the PS4, so a new profile keeps the default name **`NAME`**. To give it
your own name, close the game and open **SETTINGS → SAVE GAMES → Player name**. Type the name with the
launcher's on-screen keyboard (letters and digits, 1 to 15 characters). The launcher backs up the save, writes
the new name into it (with its checksums) and renames the folder and file.

## Official builds and verification

Official JK4SPEED releases come **only** from https://github.com/Salru13/Jk4Speed.

The badge in the top-right corner of the launcher (details in **SETTINGS → ABOUT**):

| Badge | Meaning |
|---|---|
| **Official Build by XxXJoker13XxX** | The files of this copy match the list signed with the JK4SPEED release key (Ed25519, key ID `AC6DB37B`, shown in ABOUT). |
| **Unofficial build** | This copy could not be verified. It may have been changed or repackaged by someone else. It still runs normally; nothing is turned off. |

If you got JK4SPEED anywhere else and it shows **Unofficial build**, get the official release from the GitHub page
above. Forks are welcome under the code licences, but they must not call themselves official.

## Support

Something went wrong? Open **SETTINGS → SUPPORT** and save a **support bundle**. It collects the logs of the last
runs into one folder (no saves and no personal data) and copies it to the USB drive if one is plugged in.
Attach the bundle when you open an issue on https://github.com/Salru13/Jk4Speed/issues and say what you did
and what happened.

## Credits and licences

- [CREDITS.md](CREDITS.md): everyone whose work JK4SPEED uses or builds on.
- [LICENSES.md](LICENSES.md): what ships in the PKG, under which licence, and what does not ship and why.

JK4SPEED by XxXJoker13XxX.

## Disclaimer

JK4SPEED is an unofficial fan project. It is **not affiliated with, made by or endorsed by Electronic Arts**.
Need for Speed and Need for Speed: Most Wanted are trademarks of Electronic Arts. JK4SPEED contains no EA game
files; you use your own copy of the game. PlayStation and PS4 are trademarks of Sony Interactive Entertainment;
JK4SPEED is not affiliated with Sony. Use it at your own risk.

## Repository layout

| Path | What |
|---|---|
| `launcher/` | The JK4SPEED launcher: PLAY / PATCH / SETTINGS, the mod patcher, save tools and the Official Build check. Host tests: `launcher/tests/run.sh` (Linux/WSL). |
| `title/` | How the PS4 title is built from the Wine PS4 runtime and the launcher, and how the package is made and signed. |
| `docs/` | Player pages: mods, save games, FAQ. |
| `probe/`, `templates/` | Pre-flight probe; runtime profile and registry templates. |
| `LICENSE`, `LICENSES.md`, `THIRD_PARTY.md`, `SOURCES.txt`, `CREDITS.md`, `LICENSES/` | Licences, sources and credits. |
