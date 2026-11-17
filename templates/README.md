# Templates

DRAFT: Jk4Speed is in development; these files may change before the first release.

| File | What |
|---|---|
| `nfsmw.profile` | The runtime profile for the game: the game folder `Z:\data\Jk4Speed\NFSMW`, `speed.exe`, Wine WoW64 (32-bit), DXVK, the `d3d9` and `dinput8` DLL overrides, pad mapping. |
| `nfsmw.reg` | The game's registry settings (install folder, language, graphics options) for the Wine prefix, both the 32-bit (Wow6432Node) and the native key. No CD key is written. |

`dinput8=n,b` is for [Xidi](https://github.com/samuelgr/Xidi), which you get from its own releases.
