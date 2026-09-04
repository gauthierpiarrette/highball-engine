# Patch series

Applied in lexical order on top of the pinned Wine source (`inputs.json`). Each file starts with a
header stating what it fixes, how it was verified (game, machine, macOS, engine id, date), and the
upstream status (link to the merge request or issue). A patch that upstream ships is deleted here in
the same change that bumps the pinned source.

Current series:

- `0001-use-the-real-user-name.patch`: CrossOver Hack 12735 names the Windows user "crossover" and hardcodes `C:\users\crossover`; Highball bottles carry `C:\users\<macOS user>`, and a bottle moved to this engine would get a second profile directory. Restores upstream Wine's behaviour in advapi32 and shell32. Verified 2026-09-04 (M1 Pro, macOS 26.6.2).

- `0002-wined3d-auto-renderer-opengl-first.patch`: CrossOver hack 18311 tries the Vulkan wined3d adapter first on macOS; with MoltenVK present it is created and then fails Direct3D 11 feature checks (Warframe's launcher, Rockstar's launcher). Upstream's OpenGL-first choice, which Sikarugir's engine shows in practice, restored; renderer=vulkan stays selectable.
- `0003-ntdll-winedllpath-prepend.patch`: WINEDLLPATH_PREPEND support (MacPorts GPTK patch 1005), without which none of Highball's renderer overlays applied on this engine.

Candidates, in order, from the 2026-09 investigation:

1. Rockstar Games Launcher installer: the service start that the CrossOver tree completes and the
   Sikarugir tree parks on (see Highball's tracking notes and Sikarugir-App/Sikarugir#258). Needed
   only if the diff between the two trees is not already in this base.
2. CS:GO Legacy: a wait-completion boost emulation for Source's thread pool hand-off (lost wake-up
   race measured 2026-09-02). Experimental.

## 0004-foreign-client-surface-overlay.patch

winemac.drv: a client surface for a window another process owns draws into a borderless,
mouse-transparent overlay window of ours, framed on the foreign window's client rect and kept
above it on every present. This is the driver half of cross-process presenting for CEF-based
launchers (Steam's browser, Rockstar, Ubisoft) under DXMT; the DXMT half is the
DXMT_ALLOW_CROSS_PROCESS_SWAPCHAIN opt-in in github.com/gauthierpiarrette/dxmt. Unverified until
build 12 and the Steam-on-r2 test.
