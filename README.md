# highball-engine

The Wine engine build for [Highball](https://github.com/gauthierpiarrette/highball), as a
reproducible recipe: pinned upstream sources, a small patch series, and a CI workflow that turns
them into an engine tarball the app can install.

## Status

Scaffold. Nothing here is used by the app yet. Highball's shipped engine is still the Sikarugir
build described in the app's engine manifest; this repository exists so that Wine-side fixes can
be built, tested and published in the open instead of waiting for someone else's binaries. It will
become a second engine in the app only after it passes the same verification as the current one,
and it will never replace a bottle's engine without the user asking.

## What it builds from

- **Wine:** CodeWeavers' CrossOver Wine sources, published under the LGPL with each CrossOver
  release. The exact tarball, its size and SHA-256 are pinned in [`inputs.json`](inputs.json).
  This is the tree that currently fixes things the upstream-derived builds cannot (the Rockstar
  installer's service start, for one), and it is the only public source of a current macOS-capable
  Wine.
- **Patches:** the series in [`patches/`](patches/), applied in order. Kept as small as possible,
  each one sent upstream the day it is verified, and dropped the day upstream ships it.
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
