# Third-party components

JK4SPEED runs Need for Speed: Most Wanted (2005, PC, v1.3) on the PS4 through
the components below. Full licence texts are in `LICENSES/`. Where to get the
source for each component, with our changes, is listed in `SOURCES.txt`.

The JK4SPEED launcher and the other code in this repository are
LGPL-2.1-or-later (Copyright 2026 XxXJoker13XxX and contributors). Our native
DXVK (`dxvknative.prx` and `d3d9.dll`) is free to use with credit ("Native
DXVK by XxXJoker13XxX"). All are described in `LICENSE`.

The game itself is not part of JK4SPEED. The player supplies their own copy.
JK4SPEED contains no files from Electronic Arts and no mod files: the Xbox 360
Stuff Pack, the Upscaled HUD and the HD Road Textures are downloaded by the
player from their authors.

| Component | What it does here | Licence | Text |
|---|---|---|---|
| JK4SPEED launcher, patcher, save tools (ours) | The app: PLAY, PATCH, SETTINGS | LGPL-2.1-or-later | `LICENSES/LGPL-2.1.txt` |
| Wine PS4 runtime of JkFlow 1.00 (Wine 11.17 base) | Runs the game's Windows code (`sce_module/*.prx`, `win/`) | LGPL-2.1-or-later | `LICENSES/Wine-LICENSE.txt`, `LICENSES/LGPL-2.1.txt` |
| Mesa / RADV | The Vulkan driver for the console's GPU, inside the runtime (`libvulkan.prx`) | MIT (each file's SPDX line governs) | `LICENSES/Mesa-MIT.txt` |
| DXVK 1.10.3, the base of Native DXVK | Turns Direct3D 9 into Vulkan | zlib/libpng | `LICENSES/DXVK-zlib.txt` |
| Native DXVK (ours): `d3d9.dll` and `sce_module/dxvknative.prx` | Our changes to DXVK for the console | Free to use with credit "Native DXVK by XxXJoker13XxX"; source not published | `LICENSES/Native-DXVK.txt` |
| Ultimate ASI Loader 10.0.2 (ThirteenAG), as `dinput8.dll` | Loads the game's `.asi` mods | MIT | `LICENSES/UltimateASILoader-LICENSE.txt` |
| Xidi (Samuel Grossman; ThirteenAG fork build), as `Xidi.32.dll` | Maps the controller to DirectInput | BSD-3-Clause | `LICENSES/Xidi-LICENSE.txt` |
| Xenon Effects 1.02 (xan1242): `NFSMW_XenonEffects.asi`, `.ini`, `XenonEffects.tpk` | Xbox 360 style effects | MIT | `LICENSES/XenonEffects-LICENSE.txt` |
| Widescreen Fix (ThirteenAG, release `nfsmw`, source `a13a14a`): `NFSMostWanted.WidescreenFix.asi`, `.tpk`, original-look side only | 16:9 picture, HUD and movies | GPL-3.0 | `LICENSES/WidescreenFix-GPL-3.0.txt`, `LICENSES/WidescreenFix-SOURCE.txt` |
| Ubuntu Sans | The launcher's font | Ubuntu Font Licence 1.0 | `LICENSES/UbuntuFont-LICENCE.txt` |
| TweetNaCl | Checks the Official Build signature in the launcher | Public domain | in `launcher/third_party/tweetnacl` |
| The launcher's archive decoders (DEFLATE, LZMA, 7z, ZIP, UPS, MD5) | Read the mod archives on the console | Ours (LGPL-2.1-or-later); DEFLATE follows `puff.c` (zlib licence) | `LICENSES/Decoders-NOTICE.txt` |

## Thanks

Thanks to [plastikman](https://github.com/plastikman) for orbis-win and his
PS4 Wine port, and to [mpereiraesaa](https://github.com/mpereiraesaa) for the
original Wine console port. The Wine PS4 runtime of
[JkFlow](https://github.com/Salru13/JkFlow) is built from them.
