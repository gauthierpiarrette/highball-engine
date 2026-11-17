# vkd3d-proton (master) for the arm64 line — Forza Horizon 6 tree

Patches from `git format-patch 5b1089c8..` (vkd3d-proton master, "Work around descriptor mismatches in Phantom Blade
Zero"), applied with `git am` in order. 0001-0016: the 3.0.1 series (`../vkd3d-proton`) ported to master plus
HB_VKD3D_TILER / HB_VKD3D_CONFIG / force_native_fp16. Then the MetalSharp MetalFX lane (ID3D12GraphicsCommandListMetalFX),
KosmicKrisp accepted as a MetalFX driver, and Highball's:

- `HB_VKD3D_METALFX_FSR=1`: the game's own FSR 3.1 upscaler passes (recognized by DXIL hash + PSV0 bindings from the
  game's amd_fidelityfx_upscaler_dx12.dll, read only) run on MetalFX's temporal scaler; the game's RCAS sharpens after
  it (`HB_FSR_MFX_SHARPEN=0` skips it). No game file is replaced (Forza Horizon 6's anti-cheat refuses that, FHE21).
  Switches: HB_FSR_MFX_AUTO_EXPOSURE, _REACTIVE, _DEPTH_REVERSED, _HIRES_MV, _MV_SIGN_X/Y, _JITTER_SIGN_X/Y.
- VK_EXT_swapchain_colorspace enabled: without it the surface offered sRGB only, SetColorSpace1(G2084) failed and HDR
  games wrote PQ into an sRGB swapchain (washed-out gray). Forza Horizon 6 HDR works with it (color space 12).

Every DLL needs the "Wine builtin DLL" marker at offset 0x40, or Wine 11.18 loads its own d3d12 (FH201, GPU spoof lost).
