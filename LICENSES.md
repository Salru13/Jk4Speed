# Licences

This page lists what ships inside the JK4SPEED PKG and under which licence, what JK4SPEED does **not** ship
and why, and the licence of JK4SPEED's own code. The licence texts also ship inside the PKG under `licenses/`.
Credits are in [CREDITS.md](CREDITS.md).

## 1. What ships in the PKG

| Component | File(s) in the PKG | Licence | Text |
|---|---|---|---|
| JK4SPEED launcher, patcher, archive readers, save tools (ours) | `eboot.bin` (launcher compiled in), `jk4speed-files/deltas/*.jkd`, configs | LGPL-2.1-or-later | [LGPL-2.1](https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html) |
| Native DXVK client changes (ours) | `jk4speed-files/d3d9.dll`, `dxvknative.prx` | Native DXVK licence (draft, credit licence) on top of DXVK's zlib/libpng | [below](#native-dxvk-licence-draft) |
| DXVK 1.10.3 (base of Native DXVK) | inside `d3d9.dll` | zlib/libpng | [below](#dxvk-zliblibpng) |
| Wine (the PS4 Wine runtime, Wine 11.17 base) | `sce_module/*.prx`, `win/` | LGPL-2.1-or-later | [Wine licence](https://gitlab.winehq.org/wine/wine/-/blob/master/LICENSE), [LGPL-2.1](https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html) |
| Mesa / RADV | `sce_module/libvulkan.prx` | MIT (most of Mesa; each source file's SPDX identifier governs) | [Mesa licence](https://docs.mesa3d.org/license.html) |
| Xenon Effects 1.02, xan1242 (Lovro Pleše) | `jk4speed-files/xenon/` (`NFSMW_XenonEffects.asi`, `.ini`, `XenonEffects.tpk`) | MIT | [below](#xenon-effects-mit) |
| Ultimate ASI Loader 10.0.2, ThirteenAG | `jk4speed-files/dinput8.dll` | MIT | [below](#ultimate-asi-loader-mit) |
| Xidi (ThirteenAG fork build `085d7c09`), Samuel Grossman | `jk4speed-files/Xidi.32.dll` | BSD 3-Clause | [below](#xidi-bsd-3-clause) |
| Launcher DEFLATE reader, modelled on `puff.c` by Mark Adler | inside `eboot.bin` | zlib (for the puff.c structure); our code LGPL-2.1-or-later | [zlib licence](https://zlib.net/zlib_license.html) |
| Ubuntu Sans font (launcher text) | inside `eboot.bin` | Ubuntu Font Licence 1.0 | [UFL 1.0](https://ubuntu.com/legal/font-licence) |

The PKG is built by JK4SPEED. GoldHEN is **not** part of the PKG; you install it yourself.

## 2. What is NOT shipped, and why

| Not shipped | Author | Why |
|---|---|---|
| Need for Speed: Most Wanted (2005) game files, original or modified (`speed.exe`, `GLOBAL/*`, `TRACKS/*`, ...) | Electronic Arts | **EA content.** You supply your own copy of PC v1.3 Black Edition. JK4SPEED ships only binary recipes (deltas) that rebuild the modified files on your console from your own game and the mod archives; the deltas contain no EA or mod bytes beyond JK4SPEED's own. |
| Xbox 360 Stuff Pack v4.2 (`X360Stuff.asi`, compiled shaders, textures, VLT edits, sky and road data) | elaymm4 and the pack's authors | **No licence for redistribution.** The pack's readme states none. The pack folder does contain a `LICENSE` file, but it is a BSD 3-Clause text with the notice "Copyright (c) 2019, Electronic Arts", which does not name the pack's authors and does not clearly cover their work; we do not rely on it. Redistributing would need the authors' permission. You download the pack from https://nfsmods.xyz/mod/1200. |
| NFSMW: Upscaled HUD textures | LunicAura106 | **No licence stated; author permission required** to redistribute. You download it from https://nfsmods.xyz/mod/1096. |
| NFSMW HD Road Textures | N4SRazer | **No licence stated; author permission required** to redistribute. You download it from https://www.nfsaddons.com/downloads/nfsmw/tools/5410/nfsmw-hd-road-textures.html. |
| NFS Most Wanted Fusion Fix | ThirteenAG (GPL-3.0) | Not used by this release. |

JK4SPEED reads these archives on your console, checks them by SHA-256 and applies them to your own copy of the
game. Nothing from them leaves your console.

## 3. Licence texts

### Xenon Effects (MIT)

```
MIT License

Copyright (c) 2023 Lovro Pleše

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

Source: https://github.com/xan1242/NFSMW_XenonEffects

### Ultimate ASI Loader (MIT)

```
MIT License

Copyright (c) ThirteenAG

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

Source: https://github.com/ThirteenAG/Ultimate-ASI-Loader

### Xidi (BSD 3-Clause)

```
BSD 3-Clause License

Copyright (c) 2016-2026, Samuel Grossman
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the copyright holder nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

Source: https://github.com/samuelgr/Xidi

### DXVK (zlib/libpng)

```
                  Copyright (c) 2017-2021 Philip Rebohle
                  Copyright (c) 2019-2021 Joshua Ashton

                          zlib/libpng license

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

– The origin of this software must not be misrepresented; you must not
  claim that you wrote the original software. If you use this software
  in a product, an acknowledgment in the product documentation would be
  appreciated but is not required.

– Altered source versions must be plainly marked as such, and must not
  be misrepresented as being the original software.

– This notice may not be removed or altered from any source distribution.
```

Source: https://github.com/doitsujin/dxvk

### Native DXVK licence (draft)

**Draft, pending owner confirmation.** Applies to our changes to DXVK in the Native DXVK client and the native
renderer it talks to. The DXVK 1.10.3 code they are built from stays under zlib/libpng.

```
Copyright (c) 2026 XxXJoker13XxX

You may use, copy, modify and distribute these files, and works based on
them, for any purpose, free of charge, if all of the following are met:

1. Credit. Any release or distribution of these files, or of a work based
   on them, names the author where its credits, readme or description are
   shown, with this line:

       Native DXVK by XxXJoker13XxX

2. Notice. This licence text and the DXVK zlib/libpng licence are included
   with every copy of the files or of a work based on them.

3. Changes. A modified version says that it is modified and must not be
   presented as the original.

THE FILES ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY ARISING FROM THE USE OF THE FILES.
```

### Wine (LGPL-2.1-or-later)

```
Copyright (c) 1993-2026 the Wine project authors (see the file AUTHORS
for a complete list)

Wine is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 2.1 of the License, or (at
your option) any later version.
```

Full LGPL-2.1 text: https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html (also in the PKG as
`licenses/LGPL-2.1.txt`). Wine: https://www.winehq.org. Source of the runtime with our changes: see `SOURCES.txt`
in the PKG.

### Mesa / RADV (MIT)

Most of Mesa is under the MIT licence; each source file's SPDX identifier gives its terms. The copyright lines of
every MIT-licensed file compiled into the shipped driver are in the PKG under `licenses/`. Licence page:
https://docs.mesa3d.org/license.html. Source: https://gitlab.freedesktop.org/mesa/mesa

### Ubuntu Font Licence 1.0

Full text: https://ubuntu.com/legal/font-licence (also in the PKG next to the font).

## 4. Our own code

| What | Licence |
|---|---|
| JK4SPEED launcher, patcher, archive readers (`jk_*`), delta tools and scripts | **LGPL-2.1-or-later**, per the release design (SPDX headers `LGPL-2.1-or-later`, "Copyright 2026 XxXJoker13XxX and contributors"). Confirmed by the owner on 2026-11-24. |
| Native DXVK changes | Native DXVK licence (draft above), pending owner confirmation |
| JK4SPEED name, wordmark and icon | Not covered by the code licences. Forks are welcome under the code licences, but must use their own name and must not present themselves as official JK4SPEED builds (brand terms pending owner decision). |

The LGPL lets you study, change and redistribute our code. If you redistribute a modified version, keep our
copyright notices, mark your changes, pass on the licence and offer the source.

---

Need for Speed and Need for Speed: Most Wanted are trademarks of Electronic Arts. JK4SPEED is an unofficial fan
project, not made or endorsed by EA.
