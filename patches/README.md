# Patch series

Applied in lexical order on top of the pinned Wine source (`inputs.json`). Each file starts with a
header stating what it fixes, how it was verified (game, machine, macOS, engine id, date), and the
upstream status (link to the merge request or issue). A patch that upstream ships is deleted here in
the same change that bumps the pinned source.

Current series:

- `0001-use-the-real-user-name.patch`: CrossOver Hack 12735 names the Windows user "crossover" and hardcodes `C:\users\crossover`; Highball bottles carry `C:\users\<macOS user>`, and a bottle moved to this engine would get a second profile directory. Restores upstream Wine's behaviour in advapi32 and shell32. Verified 2026-09-04 (M1 Pro, macOS 26.6.2).

Candidates, in order, from the 2026-09 investigation:

1. Rockstar Games Launcher installer: the service start that the CrossOver tree completes and the
   Sikarugir tree parks on (see Highball's tracking notes and Sikarugir-App/Sikarugir#258). Needed
   only if the diff between the two trees is not already in this base.
2. CS:GO Legacy: a wait-completion boost emulation for Source's thread pool hand-off (lost wake-up
   race measured 2026-09-02). Experimental.
