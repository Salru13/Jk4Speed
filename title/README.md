# JK4SPEED title integration

The JK4SPEED title is the bench's relocatable Wine PS4 app (tree `native298-package277`, the same base JkFlow 1.00 builds on) with the Jk4Speed launcher linked in. Built offline in WSL; nothing here talks to a console.

The title moves everything under `/data/Jk4Speed`, like JkFlow does for Blur under `/data/JkFlow`: the Wine library root is `/data/Jk4Speed/System`, the Wine prefix is seeded by the launcher into `System/prefix`, the session logs go to `/data/Jk4Speed/Logs`, and the native DXVK server ships in the package. The title writes nothing under `/data/prospero-win`.

| File | What |
|---|---|
| `title-jk4speed.patch` | Hunks on `prospero/native/wine64_main.c` (base sha256 `d4f56484…43ed`, checked by the build) and one on `pw_diagnostics.c`. See "Patch hunks" below. |
| `jk_title_host.inc` | The launcher's `JkServices` for the title (the JkFlow `BlurHost` pattern): the r4 folders (`jk_title_layout`), video out, pad, clock, wall time (UTC stamp), `free_bytes` (`jk_host_free_probe`), log and screen lines to ps5log, `release = NULL`. PLAY writes `System/state/starting` (`jk_session`) and restarts the title into the `nfsmw` profile with `Z:<game folder>\speed.exe`; when the game ends, Wine exits and the title opens the launcher again. A marker still there at the next open (the start died before its first frame) opens "The game didn't start". |
| `config/` | `jk4speed.ini.base`, `nfsmw.profile.base`: what the launcher composes `<game>/jk4speed-dev.ini` and `System/profiles/nfsmw.profile` from (`jk_config.c`). Shipped in `jk4speed-files/`. |
| `build_title.sh` | Bakes the font, runs the launcher tests, prepares `$JK_TITLE_OUT` (default `~/orbis-win-host-bootstrap/jk4speed-title-r4`; copies of the base `orbis/` and `prospero/`, links kept; only the folders it makes are replaced, `deltas/`, `seed*/` and `package/` stay), adds `launcher/src`, `launcher/devtools`, the font and the `.inc`, applies the patch, builds with `TITLE_ID=JKSP00001`. |
| `make_deltas.sh`, `author_deltas.py` | Build the 10 mod-set JKD1 deltas (`X_*` pack level, `H_*`/`H0_*` Upscaled HUD, `R_*`/`R0_*` HD road) and the launcher catalog `src/jk_modset_catalog.inc` from local, git-ignored copies, each pinned. A delta is refused if its literal bytes hold a 64-byte block (other than fill) of any pack, HUD or road file. |
| `make_pkg.sh` | Stages `jk4speed-files/` (each input checked against its pinned SHA-256: our files, the two config bases, the 10 deltas (taken from `$JK_TITLE_OUT/deltas`; `JK_REBUILD_DELTAS=1` rebuilds them), Xenon Effects in `xenon/`, the credits in `about/`, the prefix seed in `seed/`), adds `sce_module/dxvknative.prx` (enc17 `86e62a08…`), writes `manifest.txt` with `make_manifest.py`, renders `icon0.png`/`pic1.png` with our own art, writes `param.sfo`, builds and validates the PKG with the SDK's `PkgTool.Core`. |
| `make_gp4.py` | The GP4 project writer (tested by `tests/test_make_gp4.py`). |

## Patch hunks

| Where | What |
|---|---|
| top of `wine64_main.c` | `JK4SPEED_TITLE`, `JK_TITLE_PRODUCT_ROOT` `/data/Jk4Speed`, `JK_TITLE_DATA_ROOT` `/data/Jk4Speed/System`, `JK_TITLE_PROFILE_ID` `nfsmw`, and `PW_WINE64_ROOT_DATA` = the data root, so the Wine library root (profiles, `prefix/`, `cache/radv`, `control/`) is `System`. |
| `loading_done` | the game's first frame clears the start marker and frees the launcher's loader. |
| the loader frame | `jk_loader_draw` instead of the base spinner until the first frame. |
| late `dev.conf` | `/data/Jk4Speed/devtools/dev.conf` is read first (the bench path stays a read). |
| `open_library` | right after the `/data` grant: `jk_title_layout()` (r3 state moved aside, then the r4 folders made). The Blur seed import (task 245) is compiled out: the prefix is the launcher's (`jk_seed`, at PATCH). |
| `run_launcher` | hands over to `jk_title_launcher()`. |
| `main` | Blur direct mode off (always the profile library); `pw_diagnostics_open("/data/Jk4Speed")`. |
| `pw_diagnostics.c` | the session ring is `<root>/Logs`, i.e. `/data/Jk4Speed/Logs` (`session-N.log`, bounded as before). |

## Services the title passes

| Field | Value |
|---|---|
| `game_dir` | `/data/Jk4Speed/NFSMW` in any letter case (`jk_host_game_dir` → `jk_game_dir_find`; two spellings: both logged, the first used). With `/data/Jk4Speed/devtools/enable`, the first line of `devtools/game_dir` when it names a folder under `/data/Jk4Speed/clean-test/` (logged). |
| `data_root` | `/data/Jk4Speed/System` |
| `product_root`, `mods_dir`, `logs_dir` | `/data/Jk4Speed`, `/data/Jk4Speed/Mods`, `/data/Jk4Speed/Logs` |
| `files_dir` | `/app0/jk4speed-files` (found through the title's runtime roots) |
| `profiles_dir` | `/data/Jk4Speed/System/profiles` (the composed `nfsmw.profile`; `prefix = default` is `System/prefix`) |
| `runtime_root`, `override_dir` | NULL (the native server is `sce_module/dxvknative.prx`) |
| `saves_dir` | `/data/Jk4Speed/Saves/NFS Most Wanted` (the seed's `Personal` shell folder is `Z:\data\Jk4Speed\Saves`) |
| `old_saves_dir` | `/data/prospero-win/prefixes/nfsmw/drive_c/users/prospero/Documents/NFS Most Wanted`, read once to copy old saves |
| `usb_dir` | the first of `/mnt/usb0`, `/mnt/usb1` that is a folder, else NULL |
| `free_bytes` | `jk_host_free_probe` (`_fstatfs`, libc `statfs`, the parent folder) |
| `release` | NULL: "Unofficial build" until the signed manifest exists |

## Wine prefix seed

`launcher/tools/make_seed.py` makes `jk4speed-files/seed/` from a copy of the bench prefix (`~/nfsmw-bench/kit/library/prefixes/nfsmw`, copied to `$JK_TITLE_OUT/seed-src`; the original is only read): `index.txt` (`JKSEED 1`, folders, the two `dosdevices` links, every file with its SHA-256) and `objects/` for the files only the seed has (about 100, 5 MB). The other ~1,690 files are byte-identical to the package's own `win/` runtime and are copied from there (`app:` entries). In the seed's `user.reg`, `Explorer\Shell Folders\Personal` and `Explorer\User Shell Folders\Personal` are `Z:\data\Jk4Speed\Saves`, so the game's saves land in `/data/Jk4Speed/Saves/NFS Most Wanted/<alias>/`. The Wine user inside the prefix stays the seed's own (internal, never shown).

PATCH imports it when `System/prefix/.jkseed-complete` does not hold the index's hash (`jk_seed_import`: built in `System/prefix.staging`, resumed after a cut, every file checked, marker last, then renamed; a prefix folder with other content is never touched). PLAY without a complete prefix opens Patch.

## Console test hook (devtools)

`jk_title_host.inc` passes the services through `jk_devhook_attach` (`launcher/devtools/jk_devhook.h`). Without `/data/Jk4Speed/devtools/enable` at launcher open nothing is attached (players). With it, the launcher publishes `devtools/status.json` (`"phase":"launcher"`), takes `PWCTRL1 <session> <seq> PAD|RELEASE|SNAP|STATE` lines from `devtools/command.txt`, merges PAD presses into the real pad, writes its last presented frame to `devtools/snap-<seq>.png` and screen/focus/last messages to `devtools/state.json`, and acknowledges in `devtools/ack.json`. PC side: `tools/console/launcher_step.py`. The hook folder is `<product_root>/devtools`, never `System/devtools`.

## Package contents

`eboot.bin` and `sce_module/relocated_wine_app.prx` from this build; `sce_module/dxvknative.prx` (enc17 `86e62a08`, our server; the runtime's loader still prefers a `/data/prospero-win/override/dxvknative.prx` when a console has one); every other `sce_module/*.prx` and all of `win/` from the bench package 282 payload unchanged (`~/orbis-win-host-bootstrap/mwbench-package002/base/uroot`); `sce_sys/` (JK4SPEED `param.sfo`, our icon and background, the payload's keystone); `jk4speed-files/` (d3d9.dll `28c101ff`, dinput8.dll `63b465db`, Xidi.32.dll `226624ef`, Xidi.ini `3c03a044`, dxvk.conf `b54c72f6`, `jk4speed.ini.base` `66180951`, `nfsmw.profile.base` `7b1096cc`, `deltas/*.jkd` (10), `xenon/` (Xenon Effects, MIT), `about/CREDITS.txt`, `seed/`); `licenses/`; `docs/` (MODS.md, save-games.md, and README-player.md / FAQ.md once they exist). The manifest has no ini, profile or server lines. Not in the package: game files, the X360 Stuff Pack, the Upscaled HUD, the HD road (players drop the archives), the Blur parts of the bench payload (`blur-files/`, `native287/`, `seed245/`).

## Build

```bash
# WSL, from a native copy of the repository
JK_TITLE_OUT=~/orbis-win-host-bootstrap/jk4speed-title-r4 bash launcher/title/build_title.sh   # JK_SKIP_TESTS=1 skips the tests
JK_TITLE_OUT=~/orbis-win-host-bootstrap/jk4speed-title-r4 bash launcher/title/make_pkg.sh
```

Inputs outside the repository (read only): the base tree `native298-package277`, the 282 payload, the SDK's `PkgTool.Core` (needs OpenSSL 1.1: `~/.cache/orbis-ports/openssl11/lib`), the release client `evidence/codex-re60/world-lod-queue-decl.dll` (git-ignored, in the main checkout), the loader/Xidi binaries in `~/nfsmw-bench/kit/fusionfix/`, the server in `~/nfsmw-bench/native/server-enc17/link/`, and the bench prefix for the seed.

## Known limits (for the console test)

- Title id `JKSP00001` is provisional (own homebrew PKG).
- The package 282 runtime modules (not built here) still write a few bench files on their own: `libvulkan.prx` sets `MESA_LOG_FILE=/data/prospero-win/radv.log` and reads `radv.env` there; `wowprospero` writes `/data/prospero-win/logs/wowprospero.log` on a translator memory fault. The title itself writes nothing there.
- A console that already has `/data/prospero-win/override/dxvknative.prx` runs that server, not the packaged one (the bench's is ours by digest `807b2334`).
- The first PATCH copies about 330 MB into `System/prefix` (mostly from `/app0/win`); a cut PATCH resumes.
- Whether Wine's prefix update (`.update-timestamp`) keeps the seeded `Personal` value, and whether `SHGetFolderPath(CSIDL_PERSONAL|CREATE)` makes `/data/Jk4Speed/Saves`, is to be checked on the console (the launcher makes `Saves/` itself).
- Two spellings of the game folder: the first is used and both are logged; the launcher shows no screen for it yet.
- The marker catches a start that dies before the game's first frame, not a crash later.
