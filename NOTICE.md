# Components and licences

| Component | Source | Licence | Bundled |
|---|---|---|---|
| Wine (CrossOver sources) | media.codeweavers.com/pub/crossover/source | LGPL-2.1-or-later | yes, as the engine |
| Highball patch series | patches/ | LGPL-2.1-or-later | yes |
| MoltenVK | github.com/KhronosGroup/MoltenVK | Apache-2.0 | yes |
| DXVK (macOS build) | github.com/doitsujin/dxvk, Gcenx/DXVK-macOS | zlib | as a renderer overlay |
| DXMT | github.com/3Shain/dxmt | see upstream LICENSE | as a renderer overlay |
| winetricks | github.com/Winetricks/winetricks | LGPL-2.1 | as a tool |
| wine-mono, wine-gecko | dl.winehq.org | MIT / MPL | as Wine expects |
| D3DMetal (Apple Game Porting Toolkit) | Apple | Apple licence | never; gated by the app |
| Wine (WineHQ upstream, arm64 line) | dl.winehq.org/wine/source | LGPL-2.1-or-later | yes, as the arm64 engine |
| FEX (Hangover's libarm64ecfex.dll and libwow64fex.dll) | github.com/FEX-Emu/FEX, github.com/AndreRH/hangover | MIT | yes, in the arm64 engine, as xtajit64.dll and xtajit.dll |
| FEX Unix helper, Darwin build | fex/fexunixlib_darwin.cpp (after FEX's Source/Windows/UnixLib) | MIT | yes, in the arm64 engine |
| llvm-mingw | github.com/mstorsjo/llvm-mingw | Apache-2.0 with LLVM exception | build tool only |

Copyright and licence texts of each component are kept unchanged inside the built engine.
