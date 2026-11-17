# Jk4Speed

Play your own PC copy of **Need for Speed: Most Wanted** (2005) on a PS4 with GoldHEN.

![status](https://img.shields.io/badge/status-in%20development-orange)
![platform](https://img.shields.io/badge/platform-PS4%20(GoldHEN)-0070d1)

Jk4Speed runs the PC game on the PS4 through a Wine runtime for the console and the native DXVK renderer of [JkFlow](https://github.com/Salru13/JkFlow). You bring the game; Jk4Speed brings what it needs to start it.

> **Status: in development.** There is no download yet. This repository holds the parts that are ready to be public.

## Requirements

- A PS4 with GoldHEN.
- A way to copy files to the PS4: FTP (GoldHEN has an FTP server), or a USB drive and a file manager app on the PS4.
- Your own PC copy of Need for Speed: Most Wanted (2005), patched to v1.3. It is not included.
- A controller.

## Getting started

Coming with the first release. The game folder on the PS4 will be `/data/Jk4Speed/NFSMW`.

## Repository layout

| Path | What |
|---|---|
| `probe/` | A small pre-flight probe (our own code, no game files) that checks how the console's Wine runtime handles memory and timers before the game runs. See [probe/README.md](probe/README.md). |
| `templates/` | The runtime profile and registry templates for the game. See [templates/README.md](templates/README.md). |
| `LICENSE`, `THIRD_PARTY.md`, `SOURCES.txt`, `LICENSES/` | Licence texts. |

## Licence

- Our code in this repository: not decided yet (see [LICENSE](LICENSE)).
- Native DXVK (our changes in `d3d9.c988.dll`) is free to use with the credit line **"Native DXVK by XxXJoker13XxX"** ([LICENSES/Native-DXVK.txt](LICENSES/Native-DXVK.txt)). The DXVK base stays zlib/libpng.
- Third-party components: [THIRD_PARTY.md](THIRD_PARTY.md).

## Credits

- Native DXVK by XxXJoker13XxX.
- The PS4 Wine runtime comes from [JkFlow](https://github.com/Salru13/JkFlow), built from [orbis-win](https://github.com/plastikman/orbis-win) and the PS4 Wine port by [plastikman](https://github.com/plastikman), and the original Wine console port by [mpereiraesaa](https://github.com/mpereiraesaa).
- [Xidi](https://github.com/samuelgr/Xidi) by Samuel Grossman (BSD-3-Clause): controller mapping. Not bundled; get it from its releases.
- [NFS Most Wanted Fusion Fix](https://github.com/ThirteenAG/WidescreenFixesPack) by [ThirteenAG](https://github.com/ThirteenAG) (GPL-3.0). Not bundled; get it from its releases.
- Wine, Mesa/RADV and [DXVK](https://github.com/doitsujin/dxvk) (Philip Rebohle, Joshua Ashton): the stack underneath.

## Disclaimer

Jk4Speed is an unofficial fan project. It is not affiliated with or endorsed by Electronic Arts or Sony Interactive Entertainment. Need for Speed is a trademark of Electronic Arts; PlayStation is a trademark of Sony Interactive Entertainment. The game is not included: you use your own copy.
