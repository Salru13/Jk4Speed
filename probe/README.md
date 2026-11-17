# mwprobe

`mwprobe.exe` is a small pre-flight probe for Jk4Speed: our own code, no game files. It answers one question before the game runs: can 32-bit code execute from heap and data memory in the console's Wine runtime? The game is not NX_COMPAT, so Wine allows code execution on readable pages; if the console refused that, code the game copies to the heap would fault.

The probe has the same relevant PE flags as the game: image base 0x400000, relocations stripped, no NX_COMPAT, no DYNAMIC_BASE, not large-address-aware. It imports only kernel32 and msvcrt.

It writes `mwprobe.log` in its working folder: one line per executed case (`exec <case> ... result=OK|FAULT`), an address-space summary, timer resolution and sleep statistics, and `done exit=<n>`. Exit code 0 = every counted case executed, 2 = at least one faulted, 1 = the log could not be opened.

## Build and test (Linux or WSL)

```bash
sudo apt install gcc-mingw-w64-i686 wine
bash probe/build.sh          # -> build/probe/mwprobe.exe
bash probe/test_host.sh      # PE flags, imports, and a run under host Wine
```

`CC`, `OBJDUMP`, `WINE` and `OUT` override the tools and the output folder.
