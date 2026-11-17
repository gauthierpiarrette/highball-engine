# DXVK (master 1cad6cd1) for the arm64 line

`0001`: dxgi.maxVideoMemoryBudget, DXVK_RELAX_FEATURES, the unshared-texture fallback (the 3.0 series in `../dxvk`,
on master) and HB_DXVK_CONFIG, appended after DXVK_CONFIG so a recipe's environment wins over dxvk.conf
(Forza Horizon 6: `dxgi.maxDeviceMemory=49152;dxgi.maxSharedMemory=0;dxgi.maxVideoMemoryBudget=49152;dxgi.enableHDR=True`).
