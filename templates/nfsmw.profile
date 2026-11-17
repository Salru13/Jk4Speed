; Jk4Speed profile template for NFS Most Wanted (2005, PC v1.3).
; The game folder is Z:\data\Jk4Speed\NFSMW on the PS4. DRAFT: Jk4Speed is in development.
[application]
id = nfsmw
name = NFS Most Wanted
executable = Z:\data\Jk4Speed\NFSMW\speed.exe
working_directory = Z:\data\Jk4Speed\NFSMW
dll_overrides = d3d9=n;dinput8=n,b
prefix = nfsmw
runtime = wine-wow64
architecture = pe32
graphics = dxvk
[display]
desktop = 1280x960
scaling = fit
[input]
mode = xinput
cross = enter
circle = escape
up = up
down = down
left = left
right = right
[runtime]
fast_clock = true
cpu = native
