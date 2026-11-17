# Third-party components

> DRAFT - OWNER DECISION. Versions and the component list are to be checked
> against the package that actually ships; Jk4Speed is in development.

Jk4Speed runs Need for Speed: Most Wanted (2005, PC, v1.3) on the PS4
through the components below. Full licence texts are in `LICENSES/`. Where
to get the source for each component, with our changes, is listed in
`SOURCES.txt`.

The game itself is not part of Jk4Speed. The player supplies their own
copy. Jk4Speed contains no files from Electronic Arts.

| Component | What it does here | Licence | Text |
|---|---|---|---|
| Wine (the Wine PS4 runtime of JkFlow 1.00, Wine 11.17 base) | Runs the game's Windows code | LGPL-2.1-or-later | `LICENSES/Wine-LICENSE.txt`, `LICENSES/LGPL-2.1.txt` |
| DXVK 1.10.3, the base of Native DXVK (`d3d9.c988.dll`) | Turns Direct3D 9 into Vulkan | zlib/libpng | `LICENSES/DXVK-zlib.txt` |
| Mesa / RADV | The Vulkan driver for the console's GPU | MIT | `LICENSES/Mesa-MIT.txt` |

Not bundled, linked only (the player gets them from their authors):

| Component | Author | Licence | Where |
|---|---|---|---|
| Xidi (controller mapping) | Samuel Grossman | BSD-3-Clause | https://github.com/samuelgr/Xidi |
| NFS Most Wanted Fusion Fix | ThirteenAG | GPL-3.0 | https://github.com/ThirteenAG/WidescreenFixesPack |

If a later release ships Xidi, its BSD-3-Clause text goes into `LICENSES/`
and this table moves it into the first one.

## Our own components

| What | Licence |
|---|---|
| Native DXVK: our changes in `d3d9.c988.dll` | Free to use with credit "Native DXVK by XxXJoker13XxX" (`LICENSES/Native-DXVK.txt`); the DXVK base stays zlib/libpng |
| The probe, templates and scripts in this repository | OWNER DECISION (see `LICENSE`) |

## Thanks

Thanks to [plastikman](https://github.com/plastikman) for orbis-win and his
PS4 Wine port, and to [mpereiraesaa](https://github.com/mpereiraesaa) for the
original Wine console port. The runtime Jk4Speed uses is built from them.
