# DXVK (master 1cad6cd1) for the arm64 line

`0001`: the 3.0 series in `../dxvk` (unshared-texture fallback, dxgi.maxVideoMemoryBudget, DXVK_RELAX_FEATURES) on
master, plus:

- HB_DXVK_CONFIG, appended after DXVK_CONFIG so a recipe's environment wins over dxvk.conf (e.g. `dxgi.enableHDR=True`).
- The swap chain's (and its dispatcher's) Present, Present1, ResizeBuffers, ResizeBuffers1, SetFullscreenState and SetColorSpace1 are
  `hybrid_patchable` on ARM64EC: their vtable entries are x64 fast-forward sequences, so in-process x64 hookers (the
  Steam overlay, which logged "Unknown opcodes for AMD64 ... DXGISwapChain_Present") can detour them. Pairs with Wine
  0063 (WriteProcessMemory on the own process).
