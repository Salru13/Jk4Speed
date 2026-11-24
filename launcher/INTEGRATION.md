# Jk4Speed launcher UI: integration guide

For the lead, who owns the title, Wine, DXVK and the package. The branch holds the launcher UI and its file logic only; nothing here changes the engine (engine 1.0 is locked: `docs/release/engine-1.0-manifest.md`). This page lists what the title must provide, what the launcher writes where, and what the package must ship.

Design: `docs/superpowers/specs/2026-11-17-jk4speed-launcher-design.md`, updated by the owner decisions of 2026-11-23 (`docs/launcher/2026-11-23-launcher-kickoff.md`, D1-D6). Release signing (not built yet): `docs/launcher/2026-11-23-release-signing-design.md`. Every screen: `launcher/golden/*.png`. Console test script: `docs/launcher/2026-11-23-launcher-console-test.md`.

## Status

Launcher r4. The folder layout, the mods engine, the Wine prefix seed and the config composer are in the launcher; the title passes the services below. Player pages: `docs/launcher/README-player.md`, `FAQ.md`, `MODS.md`, `save-games.md` (shipped in the package under `docs/`; `test_no_prospero.sh` keeps the runtime's internal name out of them).

| Part | Launcher side | Title / package side |
|---|---|---|
| PLAY / PATCH / SETTINGS (FRAME RATE, SHOW FPS / MS, MODS, CONTROLS, SAVE GAMES, LOGS & SUPPORT, ABOUT), Help (4 pages), "Patch was interrupted", "Mods can't be changed", Start failed, Restore and Import dialogs | Done, host-tested, golden images | **Wired** (`launcher/title/`): `jk_title_launcher()` replaces the bench start page |
| Folder layout (`jk_layout`) | `/data/Jk4Speed/{NFSMW,Saves,Mods,Logs,devtools}` and `System/{prefix,backup,modset,state,cache,profiles}`; game folder found case-insensitively, two spellings refused; the first open moves r3 state aside (`System/legacy-r3`) and deletes nothing | `jk_title_layout()` runs it right after the `/data` grant |
| Patcher | Check, install, verify, restore, backup; free-space check; cleanup list `System/state/cleanup.txt`; seeds the Wine prefix (`jk_seed`) | `launcher/title/make_pkg.sh` ships `jk4speed-files/` + manifest + `seed/` |
| Mods (X360 Stuff Pack, Upscaled HUD, HD Road Textures) | Drop folders `/data/Jk4Speed/Mods` and `<usb>/Jk4Speed/`; each mod is its own switch, any of the 8 combinations. The first PATCH keeps pristine copies (`System/backup/pristine/` + `pristine.txt`, accepted only at the vanilla hashes); every PATCH rebuilds every managed path from pristine + the ON layers in staging, verifies, swaps atomically and journals (`jk_modset`) | Only our deltas and Xenon Effects (MIT) in the package, never game, pack or mod-author data |
| Config composer (`jk_config`) | Writes `<game_dir>/jk4speed-dev.ini` and `System/profiles/nfsmw.profile` from the two shipped bases; FRAME RATE, SHOW FPS / MS and ENABLE LOGS change keys in them | Nothing: the client and the runtime read them |
| Saves | Redirected to `/data/Jk4Speed/Saves` by the seeded prefix; import / export / rename; r3 saves copied once | Pass `saves_dir`, `old_saves_dir`, `usb_dir` |
| Logs & Support | Enable logs, Clear logs, support bundle in `Logs/support/<stamp>` (+ USB) | Give `logs_dir`, `usb_dir`, `wall_time`, `local_user`; session logs in `Logs` |
| `free_bytes` | Before PATCH: pristine copies and the prefix | `jk_host_free_probe` (`_fstatfs`) |
| Release badge, About | Stub driven by `JkServices.release` (NULL: "Unofficial build") | Pass NULL until `jk_verify` exists |
| Title + PKG | | `launcher/title/README.md`; id `JKSP00001`, name JK4SPEED |

Not tested on hardware unless the console reports say so. The host tests cover the logic and every pixel; the console run follows `docs/launcher/2026-11-23-launcher-console-test.md`.

## Build

1. `launcher/tools/bake_assets.sh OUTDIR` (host C compiler + FreeType) writes `OUTDIR/jk_font.h` and `OUTDIR/jk_font_data.c` (about 520 KB of glyph data).
2. Compile `launcher/src/*.c` and `OUTDIR/jk_font_data.c` into the title, C11, with `-D_DEFAULT_SOURCE` (or the toolchain's equivalent for `fsync`).
3. Libc used: `malloc/realloc/free`, `memcpy/memcmp/memset/memchr`, `str*`, `snprintf`, `sscanf`, `strtoll`, `qsort`, `fopen/fread/fseek/ferror/fclose/fprintf`, `open/write/close/fsync/rename/unlink/mkdir/stat/lstat`, `opendir/readdir/closedir`, `errno`, `tolower`. No `memmem`, no `getenv`, no `statvfs` (free space comes from the host), no threads, no libm calls on the draw path except the compiler's float maths.

Memory: one 1920x1080 frame (8 MB, freed when `jk_frontend_run` returns) and the background cache (8 MB, kept for the loader; freed by `jk_loader_release`). Static: about 1 MB (font atlases, patcher tables).

## The seam

```c
#include "jk_frontend.h"
#include "jk_loader.h"

JkDecision d;
int rc = jk_frontend_run(&services, &d);   /* blocks until PLAY or close */
if (rc == 0 && d.action == JK_START_GAME) {
    /* log d.pace; start Wine with the nfsmw profile;
       each loading_frame(): jk_loader_draw(frame, ms_since_start) at most every 33 ms;
       at the game's first frame: jk_loader_release(); */
}
```

### `JkServices` (all in `launcher/src/jk_frontend.h`; the title's values in `launcher/title/README.md`)

| Field | Required | What the title gives |
|---|---|---|
| `ctx` | | passed back to every callback |
| `present(ctx, frame)` | yes | copy the 1920x1080 BGRX frame to video out; `<0` = display lost |
| `pad(ctx, &held, &pressed)` | | scePad bits (`JK_PAD_*`); `pressed` = newly pressed edges; left stick mapped onto the D-pad bits; `<0` = no controller yet |
| `should_quit(ctx)` | | 1 when the system asked the app to close |
| `now_ns`, `sleep_us` | yes | monotonic clock; sleep |
| `wall_time(ctx, out, cap)` | | `YYYY-MM-DD_HHMM`; names bundle, export and backup folders. NULL: numbered |
| `screen(ctx, name)`, `log(ctx, line)` | | the remote control's status; one line for the session log |
| `game_dir` | yes | `/data/Jk4Speed/NFSMW` in whatever letter case exists (`jk_game_dir_find`) |
| `data_root` | yes | `/data/Jk4Speed/System` |
| `product_root`, `mods_dir`, `logs_dir` | | `/data/Jk4Speed`, `/data/Jk4Speed/Mods`, `/data/Jk4Speed/Logs` |
| `files_dir` | yes | the package's `jk4speed-files/` (`/app0/jk4speed-files`) |
| `profiles_dir` | yes | `/data/Jk4Speed/System/profiles` |
| `saves_dir` | | `/data/Jk4Speed/Saves/NFS Most Wanted` |
| `old_saves_dir` | | the r3 save folder, read once for the copy |
| `usb_dir` | | the root of a mounted USB drive, or NULL |
| `free_bytes(ctx, path)` | | free bytes on the volume of `path`; `<0` unknown (PATCH proceeds and logs it) |
| `release`, `build_id` | | NULL / e.g. `JK4SPEED 1.0` |
| `start_failure`, `start_failure_log` | | set when the previous start died before the game's first frame |
| `local_user` | | the PS4 user name; masked in the bundle |

### `JkDecision`

| Field | Meaning |
|---|---|
| `action` | `JK_START_GAME`: start `speed.exe` with the `nfsmw` profile. `JK_LEAVE_HOME`: the system asked to close |
| `pace` | `JK_PACE_30`, `JK_PACE_60` or `JK_PACE_OTHER`, for the session log only. The ini is already on disk |

The launcher composes the ini and the profile at PATCH (see "What the launcher writes"); PLAY always means "the normal Jk4Speed start". There is no developer mode, no hidden key and no switch file. Bench tools (remote launch, autotest) stay in the bench title.

### Release badge and About (owner requirement 2026-11-23)

Top right of every launcher screen: "Official Build by XxXJoker13XxX · <build_id> · key <fingerprint>" only when `release->state == JK_REL_OFFICIAL`; "Unofficial build" in every other state. About shows the three states of the signing design (OFFICIAL RELEASE, UNOFFICIAL BUILD, CHANGED FILES) and the official link `github.com/Salru13/Jk4Speed`. No state changes what the launcher or the game does: PLAY, PATCH and every setting work the same in all of them (`release_state_never_blocks_play`). The value must later come from `jk_verify` and the signed `release.json`, never from an ini or another file a player can edit.

### Start failure (title side)

Suggested, as in JkFlow: before starting Wine write `<data_root>/state/starting`; delete it at the game's first frame and at a clean exit. If it exists when the app opens, delete it and pass `start_failure = "The game stopped before its first picture."` and `start_failure_log = "<this session's log name>"`. The launcher then opens on "The game didn't start" with □ Save support bundle.

## What the launcher writes

| Path | When | What |
|---|---|---|
| `/data/Jk4Speed/{Saves,Mods,Logs,Logs/support,devtools}`, `System/{prefix,backup,modset,state,cache,profiles}` | first open | the folders (`jk_layout_make`); never the game folder |
| `System/legacy-r3/<name>` | first open on an r3 console | r3's `logs, modset, state, backup, registry, support`, moved by rename; never read as an r4 install record; no bench file is touched |
| `<game_dir>/<our files>` | PATCH | allow-listed names only (`d3d9.dll`, `dinput8.dll`, `dsound.dll`, `Xidi.32.dll`, `Xidi.ini`, `dxvk.conf`, `jk4speed-dev.ini`, `scripts/*.asi|cfg|ini|tpk`) via `<name>.jk4new` + sha256 + rename |
| `<game_dir>/jk4speed-dev.ini` | PATCH, PLAY, and every settings change (FRAME RATE, SHOW FPS / MS, ENABLE LOGS), only once an install record for this game folder exists (before the first PATCH a settings change goes to `System/state/launcher.ini` only); its content depends on FRAME RATE, ENABLE LOGS and devtools | a `jk4speed-dev.ini` that is not ours (nor the hash last written) is copied first to `System/backup/jk4speed-dev.ini`, or, when that backup exists, to `System/backup/kept/jk4speed-dev.ini[.N]`; the hash written goes to `System/state/composed.txt`. Composed from `jk4speed.ini.base`: release keys written as `0` (`DXVK_PS4_AUTOTEST`, `FRAME_LOG`, `NFS_TIMING`, `NFS_GTPROF`, `CS_PROFILE`, `RACE_LOCK`, `NATIVE_PIN_STAMP`), `DXVK_PS4_NFS_PACE=30|60`; with Enable logs on, `DXVK_PS4_FRAME_LOG=1` |
| `System/profiles/nfsmw.profile` | PATCH, PLAY, and every settings change (FRAME RATE, SHOW FPS / MS, ENABLE LOGS), only once an install record for this game folder exists; its content depends on SHOW FPS / MS and ENABLE LOGS | hash written to `System/state/composed.txt`; composed from `nfsmw.profile.base`; `winedebug = -all`; env `DXVK_PS4_FPS_HUD=0|fps`, `DXVK_LOG_LEVEL=none`, `DXVK_STATE_CACHE_PATH=/data/Jk4Speed/System/cache`, `DXVK_PS4_NATIVE_SERVER_CPU=38`. With Enable logs on: `winedebug = +tid,err+all,+loaddll`, `DXVK_LOG_LEVEL=info`, `DXVK_PS4_LOG_DIR=/data/Jk4Speed/Logs` |
| `System/prefix` | first PATCH | the Wine prefix seed (about 330 MB), built in `System/prefix.staging`, marker `.jkseed-complete` last; `Personal` = `Z:\data\Jk4Speed\Saves` |
| `System/backup/pristine/<path>`, `pristine.txt` | first PATCH | unchanged copies of every managed game file (the road is 532 MB), accepted only at the catalog's vanilla hashes; refused before any write when the space is short |
| `<game_dir>/TRACKS/STREAML2RA.BUN`, `GLOBAL/{attributes.bin,GlobalB.lzc,InGameA.bun,HUDTEXRACE.BIN,HUDS_Custom_00.bin}`, `scripts/X360Stuff.{asi,ini}`, `scripts/*XenonEffects*`, `FX/COMPILED/*.fx.o` | every PATCH, Restore | rebuilt from pristine + the ON layers in `System/modset/` staging, verified, swapped atomically, journal `System/modset/journal.txt` (names its game folder and whether a PATCH or a Restore wrote it); rolled back or finished at the next open ("Patch was interrupted" once) only when it names this game folder and only one game folder is seen. A journal that could not be rolled back opens Patch FAILED and PLAY opens Patch until a PATCH (or Restore) undoes it first. A damaged journal: the next PATCH or Restore takes out only files it can prove are ours (catalog outputs, package files, the untouched file the store holds), puts the earlier file back, keeps everything else in `System/backup/kept/`, and moves the journal to `journal.damaged.txt`. Another existing folder's journal refuses the PATCH; a gone folder's moves to `journal.other.txt` (its files kept aside). Restore: a path with no unchanged copy stays as it is and is named on the result screen; then the first file kept aside at each path (`System/backup/kept/<path>`, never `.2+`) comes back where the path ended absent or untouched |
| `System/backup/kept/<path>[.N]` | PATCH, Restore | a file the launcher did not make that had to leave a managed path; never deleted, never overwritten |
| `System/state/cleanup.txt` | PATCH | `# game <dir>`, then sha + path of the sidecars the pristine store took over; cleanup removes only those, only while they still have that hash, only in that game folder |
| `System/state/installed.txt`, `System/state/starting` | PATCH, PLAY | install record (`# game <dir>`: Restore and the "installed" state hold only for that folder; an install over another existing folder's record is refused, a gone folder's record and backups move to `System/backup/kept/`); start marker |
| `System/state/composed.txt` | every compose | the sha of the ini and the profile last written; Restore deletes them only at that hash, then returns `System/backup/jk4speed-dev.ini` when the game folder has no ini |
| `System/backup/<path>` | before a file is replaced | the player's original; never overwritten once recorded; deleted after a restore |
| `/data/Jk4Speed/clean-test/System/...` | devtools clean-test runs only | the whole state above (backup, modset, state, cache, the launcher log) for a game folder under `clean-test/` (`jk_host_data_root`); the prefix and `System/profiles` stay shared. The override is refused when any part of the path or `clean-test/System` is a link (lstat) |
| `Logs/support/<stamp>/`, `<usb>/Jk4Speed-support/<stamp>/` | Save bundle | logs + `MANIFEST.txt` |
| `Logs/*` | ENABLE LOGS on | the game's logs; CLEAR LOGS deletes the logs and the bundles, nothing else |
| `/data/Jk4Speed/Saves/NFS Most Wanted/...` | Import saves, r3 migration | after `System/backup/saves/<stamp>/` |
| `<usb>/Jk4Speed/saves/<stamp>/NFS Most Wanted/` | Export saves | a copy (never over an older export) |
| `System/state/launcher.log` (+ `.1`) | always | launcher events, 256 KB, one rotation |

It never writes `speed.exe`. In the game folder it writes only its own allow-listed files, the composed `jk4speed-dev.ini` and the mod-managed paths in the table above (`GLOBAL/*`, `TRACKS/STREAML2RA.BUN`, `scripts/*`, `FX/COMPILED/*.fx.o`: rebuilt from the player's own untouched files and the player's mod downloads). It never deletes a file it did not make (those go to `System/backup/kept/`), never writes to the r3 runtime folders (it reads the old save folder once), never ships game files and never uses the network.

PATCH runs in steps, and each step checks the space it needs only for itself, right before it writes: the Jk4Speed files ("not enough free space: N MB needed, M MB free"), then the Wine prefix (about 330 MB, first PATCH; no space check of its own), then the pristine copies ("not enough free space to keep the original game files: N MB needed, M MB free"), then the mod swap ("not enough free space: N MB needed, M MB free"). A refused step writes nothing; the steps before it stay done (the Patch failure text says which). Unknown free space proceeds and logs it.

## Supported game

Players bring their own game. Only **NFS Most Wanted PC v1.3 Black Edition** is supported: the manifest's `exe` line is `speed.exe` `80774c2e5d619b4f120b48d4462896fd504c263399d203a238769cffde1d253c` (6,029,312 bytes, the release-lock console copy). Any other `speed.exe` shows **UNSUPPORTED GAME VERSION** with the first 8 hex of the hash found, and nothing is installed. Note: the hash proves "the v1.3 exe we tested"; whether every Black Edition v1.3 install has exactly this exe (and whether the regular edition's v1.3 exe differs) is not established in the repo; collect hashes from players' support bundles (`game exe sha256` in `MANIFEST.txt`).

## Save games

The seeded prefix redirects the game's "My Documents" (`Shell Folders\Personal` and `User Shell Folders\Personal`) to `Z:\data\Jk4Speed\Saves`, so the game writes `/data/Jk4Speed/Saves/NFS Most Wanted/<alias>/<alias>`. The folder is empty until the game's first save. r3 saves (the bench prefix's `Documents\NFS Most Wanted`) are copied once, never moved, when the new folder has no profile (`System/state/saves-migrated`). Player page: `docs/launcher/save-games.md`.

## Wine prefix seed (PATCH)

`jk_seed_import` builds `System/prefix` from the package's `jk4speed-files/seed/` (`index.txt`, `objects/`) and the `/app0/win` twins, every file checked by size and sha, marker `.jkseed-complete` last. A prefix folder with other content is never touched. PLAY without a complete prefix opens Patch. The old registry pre-seed (`nfsmw.reg`, `pending`) is gone.

## Package layout (r4)

```
/app0/jk4speed-files/
  manifest.txt                       # tools/make_manifest.py
  d3d9.dll  dinput8.dll  Xidi.32.dll  Xidi.ini  dxvk.conf
  jk4speed.ini.base  nfsmw.profile.base      # the launcher composes the ini and the profile from these
  deltas/*.jkd (10)  xenon/  about/CREDITS.txt  seed/
/app0/sce_module/dxvknative.prx      # enc17 86e62a08
/app0/docs/MODS.md save-games.md README-player.md FAQ.md
/app0/licenses/
```

- The manifest has no ini, profile or server lines. Build with `launcher/title/make_pkg.sh`; it ships `docs/launcher/README-player.md` and `FAQ.md` and fails if a player page holds the runtime's internal name.
- **Mods are never in the package.** The player brings the X360 Stuff Pack download, `NFSMWUHUD01022026b.7z` and the HD road ZIP as released; the launcher reads them on the console (zip deflate, 7z LZMA/LZMA2; RAR refused with a message). Player page: `docs/launcher/MODS.md`.
- Our builds come as JKD1 deltas (`X_*`, `H_*`/`H0_*`, `R_*`/`R0_*`; `launcher/title/author_deltas.py` also writes the catalog). Sources are the player's untouched game files (the pristine copies), archive entries by SHA-256 and the X road made by the pack's UPS; the tool refuses any delta whose literals hold a 64-byte block of a pack, HUD or road file. `HUDS_Custom_00.bin` is rebuilt with JDLZ ops and every compressed result passes the game-exact counter check.

Ship `launcher/assets/font/LICENCE.txt` with the package's licence texts.

## Open decisions (owner)

1. **Font licence.** Ubuntu Sans is under the Ubuntu Font Licence 1.0. If the owner wants a strict OFL face, Barlow Condensed is the drop-in candidate.
2. **Licence call (coordinator, 2026-11-23):** our re-encoded copies of the Upscaled HUD and N4SRazer textures are not shipped. The deltas now take the authors' DDS files as sources (`launcher/title/author_deltas.py`); their literal bytes are only our placement, records, sky merge and JDLZ container (126 B to 3.4 KB per file), checked against every source block.
4. **Title id** `JKSP00001` and content id `IV0000-JKSP00001_00-JK4SPEED00000001` are provisional (D4).
3. **Product name in the loader.** "Starting Most Wanted…" (nominative use, plain text). Change `JK_S_L_STARTING` if the owner prefers "Starting the game…".
