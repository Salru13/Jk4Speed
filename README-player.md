# JK4SPEED player guide

JK4SPEED runs your own copy of Need for Speed Most Wanted (PC, v1.3 Black Edition) on a PS4. It is an unofficial fan project. The game is not included: you need your own copy. Official page: github.com/Salru13/Jk4Speed

## Where everything goes

```
/data/Jk4Speed/                          <- made by JK4SPEED at its first start (capital J and S)
├── NFSMW/                               <- your game: PC v1.3 Black Edition
│   ├── speed.exe                        <- directly in NFSMW, not in a folder inside it
│   └── (all the other game files and folders)
└── Mods/                                <- the mod downloads, STILL ZIPPED (do not unpack)
    ├── Complex_Installation-...-001.zip <- Xbox 360 Stuff Pack (required), every part
    ├── NFSMWUHUD01022026b.7z            <- Upscaled HUD (optional)
    └── nfsmw-hd-road-textures.zip       <- HD Road Textures (optional)
```

1. **Install the PKG** with GoldHEN's package installer (or your usual homebrew PKG installer).
2. **Open JK4SPEED once.** The first start shows **SETTING UP JK4SPEED** for a minute or two: it sets up the
   Windows files and makes `/data/Jk4Speed/` with `Mods`, `Saves` and `Logs`. When you see the Home screen, close
   JK4SPEED. Do not make these folders yourself: the names are case-sensitive (`Jk4Speed`, `Mods`).
3. **Copy your game** (PC v1.3 Black Edition, untouched) into `/data/Jk4Speed/NFSMW`, over FTP or from a USB drive.
   `speed.exe` must be directly in `NFSMW`.
4. **Copy the mod downloads, still zipped,** into `/data/Jk4Speed/Mods` (over FTP: file names without spaces).
5. **Open JK4SPEED, press PATCH, then PLAY.** The first PATCH takes a few minutes.

## Before you start

- A PS4 with GoldHEN (the homebrew enabler) running.
- A way to copy files to the PS4: FTP (GoldHEN has an FTP server), or a USB drive.
- Your own Most Wanted PC folder, version 1.3 Black Edition. Other versions are not supported.

## Install

1. **Install the PKG** with GoldHEN's package installer (or your usual homebrew PKG installer).
2. **Open JK4SPEED once.** The first start shows **SETTING UP JK4SPEED** for a minute or two: it sets up the
   Windows files and makes `/data/Jk4Speed/` with `Mods`, `Saves` and `Logs`. When you see the Home screen, close
   JK4SPEED. Do not make these folders yourself: the names are case-sensitive (`Jk4Speed`, `Mods`).
3. **Copy your game** (PC v1.3 Black Edition, untouched) into `/data/Jk4Speed/NFSMW`, over FTP or from a USB drive.
   `speed.exe` must be directly in `NFSMW`.
4. **Copy the mod downloads, still zipped,** into `/data/Jk4Speed/Mods` (over FTP: file names without spaces).
5. **Open JK4SPEED, press PATCH, then PLAY.** The first PATCH takes a few minutes.

## Patch

Patch installs the Jk4Speed files and the mods you turned on. The first Patch also:

- keeps an unchanged copy of the game files it manages, so a mod can always be turned off again;
- builds the Wine prefix the game runs in.

That needs free space. Patch checks the space before the Jk4Speed files, before the unchanged copies and before the mods; a step without enough room stops before it writes anything and says how much space it needs. Steps that already finished stay done. If the console loses power during a Patch, the next open says "Patch was interrupted" once, and every game file is back as it was. Press Patch again. If the files could not be put back right away, JK4SPEED opens on Patch with the reason and the game does not start until Patch has put them back.

Nothing is written into your game folder before the first Patch: settings you change earlier are saved and used from then on.

**Restore backup** (on the Patch screen, triangle button) removes the Jk4Speed files, turns every mod off and puts back the files Jk4Speed replaced or moved aside, your own `jk4speed-dev.ini` included. A game file that had no unchanged copy (the folder already had mods before Jk4Speed) stays as it is, and the screen names it. A `jk4speed-dev.ini` you changed by hand stays too. The game will not start from here until you install again.

## Frame rate

Choose it in **Settings > Frame rate**:

- **30 FPS** (default): races run at an even 30 frames per second. The smoothest.
- **60 FPS**: races run at up to 60 frames per second, less even.

## Mods

Mods are not part of Jk4Speed. Download each one from its author's page (Settings > Mods shows where, and `MODS.md` lists them) and put the downloads, unchanged and still zipped, in one of these folders:

```
/data/Jk4Speed/Mods
USB:/Jk4Speed          (the top of a USB drive)
```

Supported:

- Xbox 360 Stuff Pack v4.2
- Upscaled HUD (`NFSMWUHUD01022026b.7z`)
- HD Road Textures

Turn each one on or off in **Settings > Mods**, then press **Patch**. Any mix of the three works. Turning a mod off puts your original game files back; Patch never needs the download for that. A mod that is on but whose download is gone stays installed as it is.

## Saves

Your saves are in this folder, one folder per profile inside `NFS Most Wanted`:

```
/data/Jk4Speed/Saves
```

- **From a PC:** copy `Documents\NFS Most Wanted` to `USB:/Jk4Speed/saves/`, then open **Settings > Save games** and press the square button **Import**. Your current saves are backed up first.
- **To a PC:** **Settings > Save games**, cross button **Export to USB**. The copy goes to `USB:/Jk4Speed/saves/<date>/`.

Saves from v1.3 Black Edition work. Saves made with other mods (extra cars, for example) may not load. More: [docs/save-games.md](docs/save-games.md).

## Show FPS / ms

**Settings > Show FPS / ms** shows the frames per second and the frame time in ms in the top right corner while you race. Off is fastest.

## Logs and the support bundle

- **Settings > Logs & Support > Enable logs** writes game logs to `/data/Jk4Speed/Logs` while you play. It is off by default. Turn it on only when something goes wrong.
- **Clear logs** deletes the game logs and the support bundles in `/data/Jk4Speed/Logs`. Your saves and settings stay.
- **Save support bundle** collects the logs of the last runs and your settings into one folder in `/data/Jk4Speed/Logs`. It holds no saves and no personal data. With a USB drive plugged in, it is copied there too.

## Your data

Everything Jk4Speed keeps is in `/data/Jk4Speed`: your game (`NFSMW`), your saves (`Saves`), the mod downloads (`Mods`), the logs (`Logs`) and the app's own folder (`System`: leave it alone; Restore uses it).

- Uninstalling, reinstalling or updating the JK4SPEED package does not touch `/data/Jk4Speed`: your game, saves and settings stay.
- Deleting `/data/Jk4Speed` deletes your saves too. Export them first.

## Help and problems

- Help (the square button on Home) explains the game folder, mods, saves and what to do when something goes wrong.
- The most common problems are in [FAQ.md](docs/FAQ.md).

## Support

JK4SPEED has no contact address. If something goes wrong:

1. Read the [FAQ](docs/FAQ.md) and Help in JK4SPEED (Square on the Home screen).
2. Make a support bundle (**Settings > Logs & Support > Save support bundle**) and copy its folder to a PC over FTP.
3. Open an issue on [GitHub Issues](https://github.com/Salru13/Jk4Speed/issues): say what you did and what you saw, and attach the support bundle files.

Official page: github.com/Salru13/Jk4Speed
