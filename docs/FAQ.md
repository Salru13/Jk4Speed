# JK4SPEED FAQ

## Where does my game go?

Copy the whole game folder of Most Wanted PC v1.3 Black Edition into `/data/Jk4Speed/NFSMW`, over FTP or with a USB drive. `speed.exe` must be directly in this folder, not in a folder inside it. Upper or lower case of `NFSMW` does not matter. Then open Patch and press the cross button to install. Only use a copy of Most Wanted you own.

Folder names on the PS4 are case-sensitive: use exactly `/data/Jk4Speed` (capital J and S). `/data/Jk4speed` or `/data/jk4speed` is a different folder that JK4SPEED does not read. The easiest way: open JK4SPEED once first; it makes `/data/Jk4Speed/` with `Mods`, `Saves` and `Logs` for you. Only the game folder `NFSMW` may be written in any case (`nfsmw` works too), and the mod zip names can be anything.

## Game not found

The folder must be `/data/Jk4Speed/NFSMW`, with `speed.exe` directly in it.

## Two game folders

The console sees `NFSMW` and `nfsmw` as two folders. Keep only one, then start Jk4Speed again. Jk4Speed does not guess which one is yours, and it changes nothing until there is only one.

## Unsupported game version

Only Most Wanted PC v1.3 Black Edition works. Copy the game folder of a v1.3 Black Edition install from your PC.

## 30 FPS or 60 FPS?

Choose it in **Settings > Frame rate**. **30 FPS**: races run at an even 30 frames per second, the smoothest. **60 FPS**: races run at up to 60 frames per second, less even.

## How do I use mods?

Mods are not part of Jk4Speed. Get each one from its author's page (Settings > Mods shows where) and put the download, unchanged and still zipped, in `/data/Jk4Speed/Mods` or `USB:/Jk4Speed`:

- **Xbox 360 Stuff Pack v4.2 (required)**: on its Google Drive page, download the whole **"Complex Installation"** folder. Google Drive gives you one or more zips; put all of them in.
- **Upscaled HUD (optional)**: only `NFSMWUHUD01022026b.7z` (2 January 2026). Older versions are not used.
- **HD Road Textures (optional)**: the zip from N4SRazer's page.

Over FTP, use file names without spaces (the console's FTP server refuses them); Jk4Speed accepts any name.

Then press **Patch**. The pack is always on; turn the HUD and the roads on or off in **Settings > Mods**.

Turning a mod off puts your original game files back: Patch keeps an unchanged copy of every game file a mod replaces, so it never needs the download for that. A mod that is on but whose download is gone stays installed as it is.

## Archive damaged or not found

Download it again from the author's page and put it back, still zipped, in `/data/Jk4Speed/Mods` or `USB:/Jk4Speed`.

## Mods can't be changed

Patch keeps an unchanged copy of each game file a mod replaces. When your game folder already had mods in it before Jk4Speed, there is none, and Patch says "Mods can't be changed". Your game still plays as it is now.

To fix it: copy the game folder again from a PC install without mods, then press Patch. Your saves stay.

## Patch was interrupted

The last Patch stopped before it finished: the console lost power or the app was closed. Every game file was put back as it was before that Patch. Nothing was lost. Press Patch again.

If the files could not be put back right away, JK4SPEED opens on the Patch screen with the reason, and Play opens Patch until it is fixed: the game never starts on half-changed files. Press the cross button: Patch first puts back the files it had swapped, then tries again. If the record of that Patch is damaged, Patch puts back the files it can check and keeps every other file aside. Nothing is deleted.

## Not enough free space

Free some space on the PS4, then press Patch again. Patch works in steps: the Jk4Speed files, then the game's Wine prefix (about 330 MB, the first time), then an unchanged copy of the game files it manages (the road file alone is 532 MB), then the mods. Before the Jk4Speed files, the unchanged copies and the mods it checks the free space; when there is not enough, that step stops before it writes anything and says how much space it needs. Steps that already finished stay done, and the next Patch carries on. If the console does not report its free space, Patch goes ahead.

## How do I put everything back?

On the Patch screen press the triangle button (**Restore backup**). It removes the Jk4Speed files, turns every mod off, and puts back the files Jk4Speed replaced or moved aside, your own `jk4speed-dev.ini` included. Two things stay as they are:

- a game file that had no unchanged copy (your game folder already had mods in it before Jk4Speed): the screen names it;
- a `jk4speed-dev.ini` you changed by hand after Jk4Speed wrote it.

The game will not start from here until you install again.

## Do settings change my game folder before the first Patch?

No. Settings you change before the first Patch are saved and used from the first Patch on. Nothing is written into your game folder until you press Patch.

## Where are my saves?

In `/data/Jk4Speed/Saves`, one folder per profile inside `NFS Most Wanted`. To bring a PC save: copy `Documents\NFS Most Wanted` to `USB:/Jk4Speed/saves/`, open Settings > Save games and press the square button Import. Your current saves are backed up first. To take them to a PC: Settings > Save games, cross button Export to USB; the copy goes to `USB:/Jk4Speed/saves/<date>/`. Saves from v1.3 Black Edition work. Saves made with other mods (extra cars, for example) may not load.

## What is Settings > Show FPS / ms?

It shows the frames per second and the frame time in ms in the top right corner while you race. Off is fastest.

## Logs, Clear logs and the support bundle

- **Enable logs** (Settings > Logs & Support) is off by default. It writes game logs to `/data/Jk4Speed/Logs` while you play. Turn it on only when something goes wrong.
- **Clear logs** deletes the game logs and the support bundles in `/data/Jk4Speed/Logs`. Your saves and settings stay.
- **Save support bundle** collects the logs of the last runs and your settings into one folder in `/data/Jk4Speed/Logs`. It holds no saves and no personal data. With a USB drive plugged in, it is copied there too. Get it with FTP.

## The game didn't start

In Settings > Logs & Support turn on Enable logs, play again, then save a support bundle.

## Official build or unofficial build?

The corner badge says "Official Build by XxXJoker13XxX" only for a copy that matches the signed list of the JK4SPEED project; every other copy says "Unofficial build". It still works as normal. Get the official release from github.com/Salru13/Jk4Speed.

## Support

JK4SPEED has no contact address. If something goes wrong:

1. Read this FAQ and Help in JK4SPEED (Square on the Home screen).
2. Make a support bundle (**Settings > Logs & Support > Save support bundle**) and copy its folder to a PC over FTP.
3. Open an issue on [GitHub Issues](https://github.com/Salru13/Jk4Speed/issues): say what you did and what you saw, and attach the support bundle files.
