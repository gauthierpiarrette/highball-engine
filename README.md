# highball-engine

The Wine engine build for [Highball](https://github.com/gauthierpiarrette/highball), as a
reproducible recipe: pinned upstream sources, a small patch series, and a CI workflow that turns
them into an engine tarball the app can install.

## What ships

Two engine lines are built here. The Intel line is Highball's opt-in Wine 11 engine (the
`x64-crossover26.3-r*` revisions in the app's engine manifests), next to the default Wine 10 engine.
The launchers and games whose recipes need it name it, and the app never moves an environment to it
without the user asking. The arm64 line (below) is the native Apple silicon engine for the end of
Rosetta. It is offered as a preview engine to the games whose recipe asks for it (Microsoft Flight
Simulator 2024), and to nothing else.

## Patches at a glance

102 patches across five folders. Each folder's README starts with a table, one line per
patch: what it fixes, the game or program that needed it, what kind of change it is, and where it
stands upstream. The paragraphs under each table give the details and the measurements.

| Folder | Patches | On top of | Engine line |
|---|---|---|---|
| [`patches/`](patches/) | 24 | CodeWeavers' CrossOver 26.3 Wine source | Intel (Wine 11, `x64-crossover26.3-r*`) |
| [`patches-arm64/`](patches-arm64/) | 25 | WineHQ Wine 11.18 | arm64 |
| [`fex/patches/`](fex/patches/) | 23 | FEX 2610 | arm64 |
| [`moltenvk-arm64/`](moltenvk-arm64/) | 16 + 4 | MoltenVK 1.4.2 and the SPIRV-Cross it pins | arm64 |
| [`renderers-arm64/`](renderers-arm64/) | 7 + 3 | vkd3d-proton 3.0.1 and DXVK | arm64 |

The kinds used in the tables:

- **general**: a bug any user of that project can hit, on any system. These are the ones worth
  reporting upstream.
- **macOS**: a bug or gap that only shows on macOS, so it matters to any Wine on a Mac, CrossOver
  included.
- **CrossOver tree**: undoes or fixes code that only CrossOver's tree has (its hacks, msync).
- **arm64 port**: part of running Wine natively on Apple silicon. WineHQ and CodeWeavers have their
  own versions of this work in progress, and ours goes when theirs lands.
- **x86-64 fidelity**: makes translated x86-64 code see what real x86-64 Windows shows it. The FEX
  patches and their Wine half.
- **Apple GPU**: works within a limit or a behaviour of Metal and Apple GPUs.
- **performance**: makes something faster without changing what the program sees.
- **workaround**: avoids or hides a problem rather than fixing its cause.
- **Highball**: only means something with Highball's own setup (its renderer overlays,
  per-program switches, its signed loader). Nobody else needs these.
- **diagnostics**: messages or logs only.

"opt-in: VAR" after a kind means the change does nothing unless that variable is set (a game's
recipe sets it), "off-switch: VAR" that it is on unless the variable turns it off.

## What it builds from

- **Wine:** CodeWeavers' CrossOver Wine sources, published under the LGPL with each CrossOver
  release. The exact tarball, its size and SHA-256 are pinned in [`inputs.json`](inputs.json).
  This is the tree that currently fixes things the upstream-derived builds cannot (the Rockstar
  installer's service start, for one), and it is the only public source of a current macOS-capable
  Wine.
- **Patches:** the series in [`patches/`](patches/), applied in order, each dropped the day
  upstream ships the same fix. Where each one stands upstream is in the folder's table. Most have
  not been sent.
- **Components** the engine bundles or expects: MoltenVK (Apache 2.0), DXVK (zlib), DXMT (see its
  licence), winetricks (LGPL). D3DMetal is never included; Highball gates it behind Apple's licence
  at install time.

## Provenance and credit

- CodeWeavers, for CrossOver and for publishing its sources.
- Gcenx, whose Sikarugir engine Highball has shipped on since its first release.
- frankea, whose [winecx-gptk](https://github.com/frankea/winecx-gptk) workflow showed that a
  clean-room CI build of the CrossOver tree on hosted macOS runners is practical; the workflow
  here is written independently but follows the same shape (pinned inputs, nix-free where
  possible, mingw-w64 for the PE half, ccache).
- The DXMT, DXVK and MoltenVK authors.
- The people whose code is in these series: athei (Alexander Theissen) for `patches/0012`, `0013`
  and `0020`, Tx for `patches/0022` and `0023`, Moritz Jost for `patches/0025`, Paul Gofman for
  `patches/0017` (upstream Wine) and `0024` (from Proton), Proton for `patches/0010`, the MacPorts
  game-porting-toolkit port for `0003` in both Wine series,
  Michael Liendo for the start of `renderers-arm64/dxvk/0003` and the Microsoft Flight Simulator
  2024 teardown (highball-db#6), and Mesa's AGX driver for the layout in `moltenvk-arm64` 0010.

## Licensing

Wine and the patches to it are LGPL-2.1-or-later; see [LICENSE](LICENSE). Every release of an
engine built here carries the corresponding source: the pinned tarball and the patch series at the
tagged commit. [NOTICE.md](NOTICE.md) lists the bundled components and their licences. "CrossOver"
is a trademark of CodeWeavers; this is not CrossOver, it is a build from CrossOver's published
LGPL sources.

## Bugs

A game that misbehaves on an engine built here is a bug for this repository or for Highball, not
for CodeWeavers, Sikarugir or WineHQ. Reports there about a patched build waste their time and
are unwelcome by their own policies.

## Build notes

- The Unix side is built as x86_64 on an Intel runner (`macos-15-intel`). The CrossOver tree guards its Metal layer class with `#if defined(__x86_64__)`, and the Mac driver uses it unconditionally, so an arm64 host compile fails in `cocoa_window.m`. CrossOver and Sikarugir ship x86_64 Unix libraries that run under Rosetta.
- `--with-opengl` must not be passed: configure makes the missing EGL headers a hard error when OpenGL is requested explicitly, and macOS has no EGL. Without the flag the Mac driver links OpenGL.framework.
- MoltenVK must be present at build time (`brew install molten-vk`): configure defines the Vulkan library name from `-lMoltenVK`, and the tree's win32u uses that name unguarded, so without it the compile fails in `dlls/win32u/vulkan.c`. The engine still ships its own libMoltenVK at runtime.
- Wine Mono and Gecko are unpacked into `share/wine/mono` and `share/wine/gecko` at the versions the tree names in `dlls/appwiz.cpl/addons.c`. Without them a bottle's first boot stops at Wine's "download Mono?" prompt, which no unattended run can answer.
- The tree's CrossOver hack that names the Windows user "crossover" is reverted by the first patch in the series, so bottles keep `C:\users\<macOS user>` like on the Sikarugir engines.
- DXMT's `winemetal.dll` is copied into the engine's built-in directories so a prefix carries its placeholder. Without it Wine refuses to load the overlay's copy for DXMT's dxgi.

## The arm64 line

Apple ends most of Rosetta 2 in macOS 28 (September 2027). The second workflow here,
[`build-arm64.yml`](.github/workflows/build-arm64.yml), builds the engine Highball will need for
that: upstream Wine ([`inputs-arm64.json`](inputs-arm64.json)) compiled natively for Apple silicon
with ARM64X builtins, FEX for x86 and x86-64 programs under Wine's emulator names (FEX 2610 built
from source with the series in [`fex/patches/`](fex/patches/) by default, or Hangover's DLLs when
dispatched with `fex_build: hangover`, either with a Darwin build of FEX's Unix helper from
[`fex/`](fex/)), and the series in [`patches-arm64/`](patches-arm64/). It runs only through Highball's signed Wine loader, the helper
bundle inside Highball.app that carries Apple's cross-architecture entitlement; the loader in the
tarball is for development. Outside the app (a test harness, a hand-made install) an engine
tarball needs three things beside it before an x86-64 game starts: `engine/bin/wine` pointed at the
signed loader (the tarball's own is ad-hoc signed and cannot run x86-64 code), the renderers
release unpacked or linked as `renderers/` (the tarball ships that folder empty; the manifest names
the release), and a copy of `engine/lib/libMoltenVK.dylib` wherever the harness expects one. Every
host library in `engine/lib` must load on its own: the build now follows `@rpath` dependencies and
fails if one does not (the first r3 release shipped a FreeType whose brotli dependency was missing). Measured on an M4 under macOS 27: a prefix boots in 2.4 s with both
halves populated, and 32-bit and 64-bit x86 programs run within 20 percent of Rosetta on a compute
loop. The build ships the host libraries Wine opens by name (MoltenVK, FreeType, GnuTLS, SDL) in
`lib/`, and games' videos play through Media Foundation with GStreamer, the official macOS framework
pruned to 46 MB in `frameworks/` ([`gstreamer/`](gstreamer/)). Direct3D 12, 11 and 10 go through
vkd3d-proton and DXVK, built with their patches by
[`build-renderers-arm64.yml`](.github/workflows/build-renderers-arm64.yml) as the overlays
Highball's vkd3d and DXVK modes load ([`renderers-arm64/`](renderers-arm64/)), on Vulkan through
MoltenVK with Highball's patches, built by
[`build-moltenvk-arm64.yml`](.github/workflows/build-moltenvk-arm64.yml)
([`moltenvk-arm64/`](moltenvk-arm64/)). DXMT is not on this line: it needs the Mac driver's Metal glue
that only CrossOver's tree has. The decision document and the measurements are in Highball's
`private/notes/rosetta-transition-plan.md`. The Intel line stays the default for every macOS up to 27.

What an x86-64 program can observe about the CPU and the Windows exception and memory model is
covered by [`tests/x64-fidelity/`](tests/x64-fidelity/): standalone programs whose expectations hold
on real x86-64 Windows (the `x64-fidelity` workflow runs them there, and on Windows 11 ARM64's own
emulation for comparison). Each engine fix in `patches-arm64/0022` onwards and in `fex/patches/`
comes from one of their checks; the READMEs beside the patches say which, and what is left open.

## Releases

A dispatch with `publish_release=true` creates a GitHub release tagged with the artifact's file name (for the Intel line `engine-wine-<version>-<date>-<note>`), with the tarball and its `.sha256`. The arm64 engine, its renderers and its MoltenVK are released by their own workflows the same way. Highball's engine manifests (`spike/engines/<id>.json` in the app repository) name the tarball's URL, size and checksum, and extract its `engine/` subtree; the app never builds anything itself. Nothing is published by a push or a merge.
